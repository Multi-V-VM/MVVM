#include "mvvm/dwarf_gpu.hpp"
#include "mvvm/reflection_codec.hpp"
#include "wamr_gpu_cc_framework.h"
#include "wamr_stream.h"
#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <cuda.h>
#include <fcntl.h>
#include <format>
#include <fstream>
#include <map>
#include <nvml.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace mvvm::dwarf {
namespace {
using namespace mvvm::gpu;
using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
constexpr size_t limit = 128ULL << 20;
void need(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
double elapsed(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }
void cuda(CUresult r) {
    if (r != CUDA_SUCCESS) {
        const char *s{};
        cuGetErrorString(r, &s);
        throw std::runtime_error(s ? s : "CUDA failure");
    }
}
const char *setting(const char *name) {
    auto p = getenv(name);
    need(p && *p, name);
    return p;
}
struct Buffer : ReadStream, WriteStream {
    mutable Bytes bytes;
    mutable size_t pos{};
    bool write(const char *p, size_t n) const override {
        if (n > limit - bytes.size())
            return false;
        bytes.insert(bytes.end(), p, p + n);
        return true;
    }
    bool read(char *p, size_t n) const override {
        if (n > bytes.size() - pos)
            return false;
        memcpy(p, bytes.data() + pos, n);
        pos += n;
        return true;
    }
};
struct Binding {
    uint32_t handle{};
    uint64_t address{}, size{};
    Bytes plain;
};
struct GPUState {
    bool confidential{};
    std::vector<Binding> bindings;
    Bytes blob;
    GPUKernel kernel;
};
struct Combined {
    uint32_t version{1};
    std::vector<std::byte> cpu;
    GPUState gpu;
};
struct Envelope {
    Bytes nonce, ciphertext, tag;
};
// Authenticates CPU frames/memory, logical handles and GPU ciphertext together;
// neither half can be substituted independently. Key travels out of band.
Bytes crypt(bool encrypt, const Bytes &key, const Bytes &nonce, std::span<const char> input, Bytes &tag) {
    need(input.size() <= limit && nonce.size() == 12 && key.size() == 32, "invalid migration envelope");
    auto c = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    need(bool(c), "cipher allocation");
    need(EVP_CipherInit_ex(c.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data(), encrypt) == 1, "cipher init");
    constexpr unsigned char aad[] = "MVVM-DWARF-GPU-v1";
    int n{}, total{};
    need(EVP_CipherUpdate(c.get(), nullptr, &n, aad, sizeof(aad)) == 1, "AAD failed");
    Bytes out(input.size() + 16);
    need(EVP_CipherUpdate(c.get(), out.data(), &total, reinterpret_cast<const uint8_t *>(input.data()),
                          int(input.size())) == 1,
         "cipher update");
    if (!encrypt) {
        need(tag.size() == 16, "invalid tag");
        need(EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data()) == 1, "tag failed");
    }
    if (EVP_CipherFinal_ex(c.get(), out.data() + total, &n) != 1) {
        OPENSSL_cleanse(out.data(), out.size());
        throw std::runtime_error("CPU/GPU snapshot authentication failed");
    }
    out.resize(total + n);
    if (encrypt) {
        tag.resize(16);
        need(EVP_CIPHER_CTX_ctrl(c.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) == 1, "get tag failed");
    }
    return out;
}
// Nonblocking pipe + one deadline covers report delivery and verifier execution.
bool attest(const Bytes &report, const Bytes &binding) {
    std::string challenge;
    for (auto x : binding)
        challenge += std::format("{:02x}", x);
    const char *python = setting("MVVM_GPU_ATTEST_PYTHON"), *helper = setting("MVVM_GPU_ATTEST_HELPER");
    int fds[2];
    need(pipe2(fds, O_CLOEXEC) == 0, "attestation pipe failed");
    auto pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return false;
    }
    if (pid == 0) {
        dup2(fds[0], STDIN_FILENO);
        dup2(STDERR_FILENO, STDOUT_FILENO);
        close(fds[0]);
        close(fds[1]);
        execl(python, python, helper, challenge.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    close(fds[0]);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);
    size_t at = 0;
    int status{};
    bool done = false;
    auto deadline = Clock::now() + std::chrono::seconds(120);
    while (Clock::now() < deadline) {
        if (at < report.size()) {
            pollfd p{fds[1], POLLOUT, 0};
            poll(&p, 1, 20);
            auto n = ::write(fds[1], report.data() + at, report.size() - at);
            if (n > 0)
                at += size_t(n);
            else if (n < 0 && errno != EAGAIN && errno != EINTR)
                break;
        }
        if (at == report.size() && fds[1] >= 0) {
            close(fds[1]);
            fds[1] = -1;
        }
        auto r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            done = true;
            break;
        }
        if (r < 0 && errno != EINTR)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (fds[1] >= 0)
        close(fds[1]);
    if (!done) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    return done && at == report.size() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
} // namespace
struct GPUContinuation::Impl {
    GPUContinuation &owner;
    bool cc;
    Bytes key;
    NVIDIAGPUCCImpl secure;
    CUcontext context{};
    CUmodule module{};
    CUfunction function{};
    int sm{};
    std::map<uint32_t, Binding> bindings;
    uint32_t next = 1;
    GPUKernel kernel;
    GPUState pending;
    static thread_local Impl *active;
    explicit Impl(GPUContinuation &o) : owner(o), cc(std::string_view(setting("MVVM_GPU_MODE")) == "cc") {
        need(!active, "one GPU participant per execution thread");
        auto mode = std::string_view(setting("MVVM_GPU_MODE"));
        need(mode == "cc" || mode == "plain", "GPU mode must be cc or plain");
        int fd = ::open(setting("MVVM_GPU_KEY_FILE"), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        need(fd >= 0, "migration key open failed");
        struct stat st{};
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (st.st_mode & 0077) || st.st_size != 32) {
            close(fd);
            throw std::runtime_error("migration key must be a private 32-byte regular file");
        }
        key.resize(32);
        auto n = ::read(fd, key.data(), 32);
        close(fd);
        need(n == 32, "migration key read failed");
        auto t = Clock::now();
        if (cc) {
            need(secure.selectDevice(0), "GPU missing");
            secure.setMigrationAttestationVerifier([this](GPUVendor v, const Bytes &r, const Bytes &b) {
                auto t = Clock::now();
                bool ok = v == GPUVendor::NVIDIA && attest(r, b);
                owner.attest_ms += elapsed(t);
                ++owner.attestations;
                return ok;
            });
            need(secure.initializeCC({CCFeature::MEMORY_ENCRYPTION, CCFeature::REMOTE_ATTESTATION}),
                 "production GPU CC initialization failed");
            auto d = secure.getCurrentDevice();
            sm = d.capability.major_version * 10 + d.capability.minor_version;
        } else {
            // Explicit benchmark-only comparator: refuses a CC-enabled device.
            need(nvmlInit_v2() == NVML_SUCCESS, "NVML init failed");
            nvmlConfComputeSystemState_t state{};
            auto rc = nvmlSystemGetConfComputeState(&state);
            nvmlShutdown();
            need(rc == NVML_SUCCESS && state.ccFeature == NVML_CC_SYSTEM_FEATURE_DISABLED,
                 "plain comparator requires hardware CC-Off");
            cuda(cuInit(0));
            CUdevice d{};
            cuda(cuDeviceGet(&d, 0));
            int major{}, minor{};
            cuda(cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, d));
            cuda(cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, d));
            sm = major * 10 + minor;
#if CUDA_VERSION >= 13000
            CUctxCreateParams params{};
            cuda(cuCtxCreate(&context, &params, 0, d));
#else
            cuda(cuCtxCreate(&context, 0, d));
#endif
        }
        owner.init_ms = elapsed(t);
        active = this;
    }
    ~Impl() {
        active = nullptr;
        if (context)
            cuCtxDestroy(context);
        OPENSSL_cleanse(key.data(), key.size());
    }
    Binding &get(uint32_t h, size_t n) {
        auto it = bindings.find(h);
        need(it != bindings.end() && n <= it->second.size, "invalid GPU buffer handle/range");
        return it->second;
    }
    uint32_t allocate(uint32_t size) {
        size_t total = size;
        for (auto &[h, b] : bindings)
            total += b.size;
        need(size && bindings.size() < 4096 && total <= (64ULL << 20) && next != 0, "GPU allocation limit");
        void *p{};
        if (cc) {
            p = secure.allocateSecureMemory(size, MemoryType::SECURE);
            need(p, "GPU allocation failed");
        } else {
            CUdeviceptr d{};
            cuda(cuMemAlloc(&d, size));
            cuda(cuMemsetD8(d, 0, size));
            p = reinterpret_cast<void *>(d);
        }
        auto h = next++;
        bindings.emplace(h, Binding{h, reinterpret_cast<uint64_t>(p), size, {}});
        return h;
    }
    void upload(uint32_t h, const void *p, uint32_t n) {
        auto &b = get(h, n);
        if (cc)
            need(secure.copyToDevice(reinterpret_cast<void *>(b.address), p, n), "GPU upload failed");
        else
            cuda(cuMemcpyHtoD(b.address, p, n));
    }
    void download(uint32_t h, void *p, uint32_t n) {
        auto &b = get(h, n);
        if (cc)
            need(secure.copyFromDevice(p, reinterpret_cast<void *>(b.address), n), "GPU download failed");
        else
            cuda(cuMemcpyDtoH(p, b.address, n));
    }
    void load(GPUKernel k) {
        need(kernel.binary_code.empty(), "one checked kernel per continuation");
        need(k.valid(), "invalid checked kernel");
        if (cc)
            need(secure.loadSecureKernel(k), "secure kernel load failed");
        else {
            need(k.format == GPUKernelFormat::NVIDIA_PTX && k.num_parameters == 3 && !k.source_module.empty() &&
                     k.entry_name == "mvvm_simd" && k.required_shared_memory == 0,
                 "kernel requires checked SIMD ABI");
            need(std::string_view(reinterpret_cast<const char *>(k.binary_code.data()), k.binary_code.size())
                     .starts_with("// mvvm-checked-simd-v1;"),
                 "checked SIMD lowering required");
            WasmToGPUTranslator translator;
            need(translator.verifyTranslation(k, k.source_module.data(), k.source_module.size()),
                 "kernel verification failed");
            auto ptx = k.binary_code;
            ptx.push_back(0);
            cuda(cuModuleLoadData(&module, ptx.data()));
            cuda(cuModuleGetFunction(&function, module, k.entry_name.c_str()));
        }
        kernel = std::move(k);
    }
    void launch(uint32_t in, uint32_t out) {
        auto &a = get(in, 32);
        auto &b = get(out, 16);
        need(in != out && !kernel.binary_code.empty(), "invalid GPU launch");
        void *p = reinterpret_cast<void *>(a.address), *q = reinterpret_cast<void *>(b.address);
        uint32_t lanes = 4;
        void *args[]{&p, &q, &lanes};
        if (cc)
            need(secure.executeSecureKernel(kernel.name, args, 3, 1, 4), "secure GPU launch failed");
        else {
            cuda(cuLaunchKernel(function, 1, 1, 1, 4, 1, 1, 0, nullptr, args, nullptr));
            cuda(cuCtxSynchronize());
        }
        ++owner.launches;
    }
    template <class F> static uint32_t invoke(wasm_exec_env_t env, F f) {
        try {
            need(active, "GPU participant missing");
            return f(*active);
        } catch (const std::exception &e) {
            wasm_runtime_set_exception(wasm_runtime_get_module_inst(env), e.what());
            return 0;
        }
    }
    static void *app(wasm_exec_env_t e, uint32_t p, uint32_t n) {
        auto m = wasm_runtime_get_module_inst(e);
        need(wasm_runtime_validate_app_addr(m, p, n), "Wasm GPU buffer outside linear memory");
        return wasm_runtime_addr_app_to_native(m, p);
    }
    static uint32_t alloc(wasm_exec_env_t e, uint32_t n) {
        return invoke(e, [&](Impl &s) { return s.allocate(n); });
    }
    static void put(wasm_exec_env_t e, uint32_t h, uint32_t p, uint32_t n) {
        invoke(e, [&](Impl &s) {
            s.upload(h, app(e, p, n), n);
            return 0;
        });
    }
    static void take(wasm_exec_env_t e, uint32_t h, uint32_t p, uint32_t n) {
        invoke(e, [&](Impl &s) {
            s.download(h, app(e, p, n), n);
            return 0;
        });
    }
    static void compile(wasm_exec_env_t e, uint32_t p, uint32_t n) {
        invoke(e, [&](Impl &s) {
            need(n < 65536, "kernel too large");
            WasmToGPUTranslator t;
            WasmToGPUTranslator::TranslationOptions opts;
            opts.target_compute_capability = s.sm;
            auto k = t.translateFunction(static_cast<uint8_t *>(app(e, p, n)), n, "op", opts);
            k.name = "wasm_kernel";
            s.load(std::move(k));
            return 0;
        });
    }
    static void execute(wasm_exec_env_t e, uint32_t a, uint32_t b) {
        invoke(e, [&](Impl &s) {
            s.launch(a, b);
            return 0;
        });
    }
};
thread_local GPUContinuation::Impl *GPUContinuation::Impl::active = nullptr;
GPUContinuation::GPUContinuation() : impl(std::make_unique<Impl>(*this)) {}
GPUContinuation::~GPUContinuation() = default;
const char *GPUContinuation::mode() const { return impl->cc ? "cc" : "plain"; }
std::vector<NativeSymbol> GPUContinuation::symbols() {
    return {{const_cast<char *>("mvvm_gpu_alloc"), reinterpret_cast<void *>(Impl::alloc), "(i)i", nullptr},
            {const_cast<char *>("mvvm_gpu_upload"), reinterpret_cast<void *>(Impl::put), "(iii)", nullptr},
            {const_cast<char *>("mvvm_gpu_download"), reinterpret_cast<void *>(Impl::take), "(iii)", nullptr},
            {const_cast<char *>("mvvm_gpu_compile"), reinterpret_cast<void *>(Impl::compile), "(ii)", nullptr},
            {const_cast<char *>("mvvm_gpu_execute"), reinterpret_cast<void *>(Impl::execute), "(ii)", nullptr}};
}
std::vector<std::byte> GPUContinuation::seal(std::span<const std::byte> cpu) {
    auto t = Clock::now();
    Combined c;
    c.cpu.assign(cpu.begin(), cpu.end());
    c.gpu.confidential = impl->cc;
    c.gpu.kernel = impl->kernel;
    for (auto &[h, b] : impl->bindings) {
        auto copy = b;
        if (!impl->cc) {
            copy.plain.resize(b.size);
            impl->download(h, copy.plain.data(), b.size);
        }
        c.gpu.bindings.push_back(std::move(copy));
    }
    if (impl->cc) {
        Buffer b;
        need(impl->secure.setMigrationKey(impl->key) && impl->secure.checkpointGPUState(&b), "GPU checkpoint failed");
        c.gpu.blob = std::move(b.bytes);
    }
    auto plain = codec::encode(c);
    Envelope e;
    e.nonce.resize(12);
    need(RAND_bytes(e.nonce.data(), 12) == 1, "nonce failed");
    e.ciphertext = crypt(true, impl->key, e.nonce, {reinterpret_cast<const char *>(plain.data()), plain.size()}, e.tag);
    OPENSSL_cleanse(plain.data(), plain.size());
    for (auto &b : c.gpu.bindings)
        OPENSSL_cleanse(b.plain.data(), b.plain.size());
    auto result = codec::encode(e);
    capture_ms = elapsed(t);
    return result;
}
std::vector<char> GPUContinuation::open(std::span<const char> packet) {
    need(packet.size() <= limit, "GPU packet too large");
    auto e = codec::decode<Envelope>(std::as_bytes(packet));
    need(bool(e), "invalid GPU envelope");
    auto plain = crypt(false, impl->key, e->nonce,
                       {reinterpret_cast<const char *>(e->ciphertext.data()), e->ciphertext.size()}, e->tag);
    auto c = codec::decode<Combined>(std::as_bytes(std::span(plain)));
    OPENSSL_cleanse(plain.data(), plain.size());
    need(bool(c) && c->version == 1, "invalid CPU/GPU continuation");
    need(c->gpu.confidential == impl->cc, "GPU snapshot mode mismatch");
    impl->pending = std::move(c->gpu);
    return {reinterpret_cast<const char *>(c->cpu.data()),
            reinterpret_cast<const char *>(c->cpu.data()) + c->cpu.size()};
}
void GPUContinuation::restore() {
    auto t = Clock::now();
    auto &p = impl->pending;
    need(impl->bindings.empty(), "GPU restore requires empty context");
    need(p.bindings.size() <= 4096, "GPU handle limit");
    size_t total = 0;
    std::map<uint32_t, bool> seen;
    for (auto &b : p.bindings) {
        need(b.handle && b.handle != UINT32_MAX && b.size && b.size <= 64ULL << 20 && !seen[b.handle],
             "invalid GPU binding");
        seen[b.handle] = true;
        total += b.size;
        need(total <= 64ULL << 20, "GPU state too large");
    }
    if (impl->cc) {
        Buffer b;
        b.bytes = std::move(p.blob);
        need(impl->secure.setMigrationKey(impl->key) && impl->secure.restoreGPUState(&b) && b.pos == b.bytes.size(),
             "GPU restore failed");
    } else if (p.kernel.valid())
        impl->load(std::move(p.kernel));
    for (auto &b : p.bindings) {
        uint64_t address{};
        if (impl->cc) {
            address = reinterpret_cast<uint64_t>(impl->secure.remapRestoredPointer(b.address));
            need(address, "GPU pointer remap failed");
        } else {
            need(b.plain.size() == b.size, "GPU buffer size mismatch");
            CUdeviceptr d{};
            cuda(cuMemAlloc(&d, b.size));
            cuda(cuMemcpyHtoD(d, b.plain.data(), b.size));
            address = d;
            OPENSSL_cleanse(b.plain.data(), b.plain.size());
        }
        impl->bindings.emplace(b.handle, Binding{b.handle, address, b.size, {}});
        impl->next = std::max(impl->next, b.handle + 1);
    }
    // CC backend restored the kernel itself; retain its checked metadata.
    if (impl->cc && p.kernel.valid()) {
        need(p.kernel.name == "wasm_kernel", "GPU kernel binding mismatch");
        impl->kernel = std::move(p.kernel);
    }
    p = {};
    restore_ms = elapsed(t);
}
} // namespace mvvm::dwarf
