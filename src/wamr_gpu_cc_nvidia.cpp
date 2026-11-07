#ifdef MVVM_ENABLE_NVIDIA_CC
// NVIDIA production CC backend. CUDA execution is gated by fresh attestation.
#include "mvvm/reflection_codec.hpp"
#include "wamr_gpu_cc_framework.h"
#include "wamr_stream.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cuda.h>
#include <format>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <nvml.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>
namespace mvvm::gpu {
namespace {
using Bytes = std::vector<uint8_t>;
void need(bool b, const char *m) {
    if (!b)
        throw std::runtime_error(m);
}
void cuda(CUresult r) {
    if (r != CUDA_SUCCESS) {
        const char *s{};
        cuGetErrorString(r, &s);
        throw std::runtime_error(s ? s : "CUDA failure");
    }
}
void nv(nvmlReturn_t r) {
    if (r != NVML_SUCCESS)
        throw std::runtime_error(nvmlErrorString(r));
}
void log(const std::exception &e) { std::cerr << std::format("NVIDIA CC: {}\n", e.what()); }
Bytes random_bytes(size_t n) {
    Bytes b(n);
    need(RAND_bytes(b.data(), int(n)) == 1, "RAND failed");
    return b;
}
Bytes digest(const void *p, size_t n, const EVP_MD *md) {
    Bytes b(EVP_MD_get_size(md));
    unsigned len{};
    need(EVP_Digest(p, n, b.data(), &len, md, nullptr) == 1, "digest failed");
    return b;
}
std::string hex(const Bytes &b) {
    std::string s;
    for (auto c : b)
        s += std::format("{:02x}", c);
    return s;
}
std::string base64(const void *p, size_t n) {
    std::string s(4 * ((n + 2) / 3), '\0');
    if (n)
        EVP_EncodeBlock(reinterpret_cast<unsigned char *>(s.data()), static_cast<const unsigned char *>(p), int(n));
    return s;
}
struct Current {
    CUcontext popped{};
    explicit Current(CUcontext c) { cuda(cuCtxPushCurrent(c)); }
    ~Current() { cuCtxPopCurrent(&popped); }
};
struct Allocation {
    uint64_t old_address{};
    Bytes data;
};
struct Snapshot {
    uint32_t version{1};
    std::vector<GPUKernel> kernels;
    std::vector<Allocation> allocations;
};
struct SnapshotWipe {
    Snapshot &state;
    ~SnapshotWipe() {
        for (auto &a : state.allocations)
            if (!a.data.empty())
                OPENSSL_cleanse(a.data.data(), a.data.size());
    }
};
struct Envelope {
    Bytes nonce, ciphertext, tag, report;
};
template <class Buffer> struct Wipe {
    Buffer &b;
    ~Wipe() {
        if (!b.empty())
            OPENSSL_cleanse(b.data(), b.size());
    }
};
Bytes crypt(bool encrypt, const Bytes &key, const Bytes &nonce, const Bytes &in, Bytes &tag) {
    need(key.size() == 32 && nonce.size() == 12 && in.size() <= 256 * 1024 * 1024,
         "invalid encrypted snapshot size/key");
    auto ctx =
        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    need(bool(ctx), "cipher allocation");
    need(EVP_CipherInit_ex(ctx.get(), EVP_aes_256_gcm(), nullptr, key.data(), nonce.data(), encrypt) == 1,
         "cipher init");
    const unsigned char aad[] = "MVVM-NVIDIA-CC-SNAPSHOT-v1";
    int len{};
    need(EVP_CipherUpdate(ctx.get(), nullptr, &len, aad, sizeof(aad)) == 1, "cipher AAD");
    Bytes out(in.size() + 16);
    int count{};
    need(EVP_CipherUpdate(ctx.get(), out.data(), &count, in.data(), int(in.size())) == 1, "cipher update");
    if (!encrypt) {
        need(tag.size() == 16, "invalid authentication tag");
        need(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, 16, tag.data()) == 1, "set tag");
    }
    if (EVP_CipherFinal_ex(ctx.get(), out.data() + count, &len) != 1) {
        OPENSSL_cleanse(out.data(), out.size());
        throw std::runtime_error("snapshot authentication failed");
    }
    out.resize(count + len);
    if (encrypt) {
        tag.resize(16);
        need(EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, 16, tag.data()) == 1, "get tag");
    }
    return out;
}
} // namespace
struct NVIDIAGPUCCImpl::Impl {
    std::recursive_mutex mutex;
    bool nvml{}, selected{}, verified{}, initialized{};
    nvmlDevice_t handle{};
    CUdevice gpu{};
    CUcontext ctx{};
    GPUDevice device{};
    AttestationVerifier verifier;
    Bytes report, key;
    std::map<CUdeviceptr, size_t> allocations;
    std::map<uint64_t, void *> remap;
    struct Kernel {
        GPUKernel source;
        CUmodule module{};
        CUfunction function{};
    };
    std::map<std::string, Kernel> kernels;
    void strict_mode() {
        nvmlConfComputeSystemState_t state{};
        nv(nvmlSystemGetConfComputeState(&state));
        need(state.ccFeature == NVML_CC_SYSTEM_FEATURE_ENABLED &&
                 state.devToolsMode == NVML_CC_SYSTEM_DEVTOOLS_MODE_OFF &&
                 state.environment == NVML_CC_SYSTEM_ENVIRONMENT_PROD,
             "production CC-On required; OFF/DevTools rejected");
        nvmlConfComputeSystemCaps_t caps{};
        nv(nvmlSystemGetConfComputeCapabilities(&caps));
        need(caps.cpuCaps != NVML_CC_SYSTEM_CPU_CAPS_NONE && caps.gpusCaps == NVML_CC_SYSTEM_GPUS_CC_CAPABLE,
             "confidential CPU guest and CC-capable GPU required");
    }
    Bytes evidence(const Bytes &binding) {
        strict_mode();
        auto nonce = digest(binding.data(), binding.size(), EVP_sha256());
        auto r = std::make_unique<nvmlConfComputeGpuAttestationReport_t>();
        memcpy(r->nonce, nonce.data(), 32);
        nv(nvmlDeviceGetConfComputeGpuAttestationReport(handle, r.get()));
        auto c = std::make_unique<nvmlConfComputeGpuCertificate_t>();
        nv(nvmlDeviceGetConfComputeGpuCertificate(handle, c.get()));
        need(r->attestationReportSize > 0 && r->attestationReportSize <= sizeof(r->attestationReport) &&
                 c->attestationCertChainSize > 0 && c->attestationCertChainSize <= sizeof(c->attestationCertChain),
             "invalid NVML evidence lengths");
        std::string cert(reinterpret_cast<char *>(c->attestationCertChain), c->attestationCertChainSize);
        auto last = cert.rfind("-----BEGIN CERTIFICATE-----");
        need(last != std::string::npos && last > 0, "invalid attestation certificate chain");
        cert.resize(last); // SDK excludes device root from evidence chain.
        nvmlDeviceArchitecture_t arch{};
        nv(nvmlDeviceGetArchitecture(handle, &arch));
        need(arch == NVML_DEVICE_ARCH_HOPPER || arch == NVML_DEVICE_ARCH_BLACKWELL,
             "unsupported attestation architecture");
        auto json =
            std::format("{{\"nonce\":\"{}\",\"arch\":\"{}\",\"evidence\":\"{}\",\"certificate\":\"{}\"}}", hex(nonce),
                        arch == NVML_DEVICE_ARCH_HOPPER ? "HOPPER" : "BLACKWELL",
                        base64(r->attestationReport, r->attestationReportSize), base64(cert.data(), cert.size()));
        return Bytes(json.begin(), json.end());
    }
    bool owns(const void *p, size_t n) const {
        auto addr = reinterpret_cast<CUdeviceptr>(p);
        auto it = allocations.upper_bound(addr);
        if (it == allocations.begin())
            return false;
        --it;
        auto offset = addr - it->first;
        return offset <= it->second && n <= it->second - offset;
    }
    void ready() {
        need(initialized && verified && ctx, "CC context not initialized/verified");
        strict_mode();
    }
    void clear() {
        if (ctx) {
            Current guard(ctx);
            for (auto &[n, k] : kernels)
                cuModuleUnload(k.module);
            kernels.clear();
            for (auto [p, n] : allocations)
                cuMemFree(p);
            allocations.clear();
            remap.clear();
        }
        initialized = false;
        verified = false;
    }
    ~Impl() {
        try {
            clear();
        } catch (...) {
        }
        if (ctx)
            cuCtxDestroy(ctx);
        if (nvml)
            nvmlShutdown();
        if (!key.empty())
            OPENSSL_cleanse(key.data(), key.size());
    }
};
NVIDIAGPUCCImpl::NVIDIAGPUCCImpl() : pImpl(std::make_unique<Impl>()) { pImpl->nvml = nvmlInit_v2() == NVML_SUCCESS; }
NVIDIAGPUCCImpl::~NVIDIAGPUCCImpl() = default;
std::vector<GPUDevice> NVIDIAGPUCCImpl::enumerateDevices() {
    std::lock_guard lock(pImpl->mutex);
    std::vector<GPUDevice> out;
    try {
        need(pImpl->nvml, "NVML initialization failed");
        unsigned count{};
        nv(nvmlDeviceGetCount_v2(&count));
        nvmlConfComputeSystemCaps_t caps{};
        nvmlSystemGetConfComputeCapabilities(&caps);
        for (unsigned i = 0; i < count; ++i) {
            nvmlDevice_t h{};
            nv(nvmlDeviceGetHandleByIndex_v2(i, &h));
            char name[96]{};
            nv(nvmlDeviceGetName(h, name, sizeof(name)));
            nvmlMemory_t m{};
            nvmlDeviceGetMemoryInfo(h, &m);
            GPUDevice d{};
            d.vendor = GPUVendor::NVIDIA;
            d.device_id = i;
            d.name = name;
            d.total_memory = m.total;
            d.available_memory = m.free;
            d.capability.supports_cc = caps.gpusCaps == NVML_CC_SYSTEM_GPUS_CC_CAPABLE;
            out.push_back(std::move(d));
        }
    } catch (const std::exception &e) {
        log(e);
    }
    return out;
}
bool NVIDIAGPUCCImpl::selectDevice(size_t id) {
    std::lock_guard lock(pImpl->mutex);
    try {
        need(!pImpl->ctx, "cannot switch an active context");
        auto devices = enumerateDevices();
        need(id < devices.size(), "invalid device");
        pImpl->device = devices[id];
        nv(nvmlDeviceGetHandleByIndex_v2(id, &pImpl->handle));
        pImpl->selected = true;
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
GPUDevice NVIDIAGPUCCImpl::getCurrentDevice() {
    std::lock_guard lock(pImpl->mutex);
    return pImpl->device;
}
bool NVIDIAGPUCCImpl::verifyDevice() {
    std::lock_guard lock(pImpl->mutex);
    pImpl->verified = false;
    pImpl->report.clear();
    try {
        need(pImpl->selected && bool(pImpl->verifier), "select a device and configure an attestation verifier first");
        auto challenge = random_bytes(64);
        auto evidence = pImpl->evidence(challenge);
        need(pImpl->verifier(GPUVendor::NVIDIA, evidence, challenge), "vendor attestation rejected");
        pImpl->report = std::move(evidence);
        pImpl->verified = true;
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
Bytes NVIDIAGPUCCImpl::getAttestationReport() {
    std::lock_guard lock(pImpl->mutex);
    return pImpl->report;
}
bool NVIDIAGPUCCImpl::initializeCC(const std::vector<CCFeature> &features) {
    std::lock_guard lock(pImpl->mutex);
    try {
        for (auto f : features)
            need(f != CCFeature::SEALED_STORAGE, "sealed storage not supported");
        need(verifyDevice(), "GPU verification failed");
        nv(nvmlSystemSetConfComputeGpusReadyState(NVML_CC_ACCEPTING_CLIENT_REQUESTS_TRUE));
        cuda(cuInit(0));
        nvmlPciInfo_t pci{};
        nv(nvmlDeviceGetPciInfo_v3(pImpl->handle, &pci));
        cuda(cuDeviceGetByPCIBusId(&pImpl->gpu, pci.busId));
        if (!pImpl->ctx) {
#if CUDA_VERSION >= 13000
            cuda(cuCtxCreate(&pImpl->ctx, nullptr, 0, pImpl->gpu));
#else
            cuda(cuCtxCreate_v2(&pImpl->ctx, 0, pImpl->gpu));
#endif
            CUcontext popped{};
            cuda(cuCtxPopCurrent(&popped));
        }
        auto attr = [&](CUdevice_attribute a) {
            int v{};
            cuda(cuDeviceGetAttribute(&v, a, pImpl->gpu));
            return v;
        };
        auto &d = pImpl->device;
        d.capability = {attr(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR),
                        attr(CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR),
                        size_t(attr(CU_DEVICE_ATTRIBUTE_MAX_THREADS_PER_BLOCK)),
                        size_t(attr(CU_DEVICE_ATTRIBUTE_MAX_GRID_DIM_X)),
                        size_t(attr(CU_DEVICE_ATTRIBUTE_MAX_SHARED_MEMORY_PER_BLOCK)),
                        true};
        d.cc_features = {CCFeature::MEMORY_ENCRYPTION, CCFeature::SECURE_BOOT, CCFeature::REMOTE_ATTESTATION,
                         CCFeature::SECURE_CHANNELS, CCFeature::TRUSTED_EXECUTION};
        pImpl->initialized = true;
        return true;
    } catch (const std::exception &e) {
        pImpl->initialized = false;
        log(e);
        return false;
    }
}
void *NVIDIAGPUCCImpl::allocateSecureMemory(size_t n, MemoryType type) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        need(n > 0 && (type == MemoryType::DEVICE_LOCAL || type == MemoryType::SECURE),
             "only protected device allocations are supported");
        Current guard(pImpl->ctx);
        CUdeviceptr p{};
        cuda(cuMemAlloc(&p, n));
        try {
            cuda(cuMemsetD8(p, 0, n));
            pImpl->allocations.emplace(p, n);
        } catch (...) {
            cuMemFree(p);
            throw;
        }
        return reinterpret_cast<void *>(p);
    } catch (const std::exception &e) {
        log(e);
        return nullptr;
    }
}
void NVIDIAGPUCCImpl::freeSecureMemory(void *p) {
    std::lock_guard lock(pImpl->mutex);
    try {
        auto it = pImpl->allocations.find(reinterpret_cast<CUdeviceptr>(p));
        need(it != pImpl->allocations.end(), "unknown allocation");
        Current guard(pImpl->ctx);
        cuda(cuMemFree(it->first));
        pImpl->allocations.erase(it);
    } catch (const std::exception &e) {
        log(e);
    }
}
bool NVIDIAGPUCCImpl::encryptMemory(void *p, size_t n) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        return pImpl->owns(p, n);
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
bool NVIDIAGPUCCImpl::decryptMemory(void *p, size_t n) {
    return encryptMemory(p, n);
} // Hardware CC protection stays enabled.
bool NVIDIAGPUCCImpl::copyToDevice(void *d, const void *h, size_t n) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        need(h && pImpl->owns(d, n), "invalid copy range");
        Current guard(pImpl->ctx);
        cuda(cuMemcpyHtoD(reinterpret_cast<CUdeviceptr>(d), h, n));
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
bool NVIDIAGPUCCImpl::copyFromDevice(void *h, const void *d, size_t n) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        need(h && pImpl->owns(d, n), "invalid copy range");
        Current guard(pImpl->ctx);
        cuda(cuMemcpyDtoH(h, reinterpret_cast<CUdeviceptr>(d), n));
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
bool NVIDIAGPUCCImpl::loadSecureKernel(const GPUKernel &k) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        need(k.valid() && k.format == GPUKernelFormat::NVIDIA_PTX && k.num_parameters == 3 &&
                 !k.source_module.empty() && k.entry_name == "mvvm_simd" && k.required_shared_memory == 0,
             "kernel requires verified tensor-wasm-jit source and ABI");
        need(std::string_view(reinterpret_cast<const char *>(k.binary_code.data()), k.binary_code.size())
                 .starts_with("// mvvm-checked-simd-v1;"),
             "NVIDIA execution requires checked SIMD lowering");
        WasmToGPUTranslator translator;
        need(translator.verifyTranslation(k, k.source_module.data(), k.source_module.size()),
             "kernel translation verification failed");
        need(!pImpl->kernels.contains(k.name), "duplicate kernel name");
        Current guard(pImpl->ctx);
        auto ptx = k.binary_code;
        ptx.push_back(0);
        CUmodule mod{};
        cuda(cuModuleLoadData(&mod, ptx.data()));
        try {
            CUfunction f{};
            cuda(cuModuleGetFunction(&f, mod, k.entry_name.c_str()));
            pImpl->kernels.emplace(k.name, Impl::Kernel{k, mod, f});
        } catch (...) {
            cuModuleUnload(mod);
            throw;
        }
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
bool NVIDIAGPUCCImpl::executeSecureKernel(const std::string &name, void **args, size_t count, size_t grid,
                                          size_t block) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        auto it = pImpl->kernels.find(name);
        need(it != pImpl->kernels.end() && args && count == it->second.source.num_parameters,
             "unknown kernel/ABI mismatch");
        need(grid > 0 && grid <= pImpl->device.capability.max_blocks_per_grid && block > 0 &&
                 block <= pImpl->device.capability.max_threads_per_block,
             "invalid launch geometry");
        for (size_t i = 0; i < count; ++i)
            need(args[i], "null kernel argument");
        CUdeviceptr in{}, out{};
        memcpy(&in, args[0], sizeof(in));
        memcpy(&out, args[1], sizeof(out));
        need(pImpl->owns(reinterpret_cast<void *>(in), 32) && pImpl->owns(reinterpret_cast<void *>(out), 16),
             "unregistered kernel buffer");
        need(out >= in + 32 || in >= out + 16, "overlapping SIMD buffers are unsupported");
        uint32_t lanes{};
        memcpy(&lanes, args[2], 4);
        need(lanes == 4 && grid == 1 && block == 4, "checked SIMD ABI requires four lanes in one block");
        Current guard(pImpl->ctx);
        cuda(cuLaunchKernel(it->second.function, grid, 1, 1, block, 1, 1, it->second.source.required_shared_memory,
                            nullptr, args, nullptr));
        cuda(cuCtxSynchronize());
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
bool NVIDIAGPUCCImpl::setMigrationKey(const Bytes &key) {
    std::lock_guard lock(pImpl->mutex);
    if (key.size() != 32)
        return false;
    if (!pImpl->key.empty())
        OPENSSL_cleanse(pImpl->key.data(), pImpl->key.size());
    pImpl->key = key;
    return true;
}
bool NVIDIAGPUCCImpl::setMigrationAttestationVerifier(AttestationVerifier v) {
    std::lock_guard lock(pImpl->mutex);
    pImpl->verified = false;
    pImpl->verifier = std::move(v);
    return bool(pImpl->verifier);
}
void *NVIDIAGPUCCImpl::remapRestoredPointer(uint64_t old) const {
    std::lock_guard lock(pImpl->mutex);
    auto it = pImpl->remap.find(old);
    return it == pImpl->remap.end() ? nullptr : it->second;
}
bool NVIDIAGPUCCImpl::checkpointGPUState(WriteStream *writer) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        need(writer && pImpl->key.size() == 32, "writer/migration key missing");
        Current guard(pImpl->ctx);
        cuda(cuCtxSynchronize());
        Snapshot state;
        SnapshotWipe wipe_state{state};
        for (auto &[name, k] : pImpl->kernels)
            state.kernels.push_back(k.source);
        size_t total{};
        for (auto [p, n] : pImpl->allocations) {
            need(n <= 256 * 1024 * 1024 - total, "snapshot memory limit");
            total += n;
            state.allocations.push_back(Allocation{p, Bytes(n)});
            auto &a = state.allocations.back();
            cuda(cuMemcpyDtoH(a.data.data(), p, n));
        }
        struct CountWriter {
            size_t size{};
            bool write(const char *, size_t n) {
                if (n > 256 * 1024 * 1024 - size)
                    return false;
                size += n;
                return true;
            }
        } counter;
        mvvm::codec::serialize_to(counter, state);
        Bytes plain(counter.size);
        Wipe wipe{plain};
        struct PlainWriter {
            Bytes &buffer;
            size_t offset{};
            bool write(const char *p, size_t n) {
                if (n > buffer.size() - offset)
                    return false;
                memcpy(buffer.data() + offset, p, n);
                offset += n;
                return true;
            }
        } plain_writer{plain};
        mvvm::codec::serialize_to(plain_writer, state);
        for (auto &a : state.allocations)
            OPENSSL_cleanse(a.data.data(), a.data.size());
        auto binding = digest(plain.data(), plain.size(), EVP_sha512());
        Envelope e;
        e.report = pImpl->evidence(binding);
        need(pImpl->verifier(GPUVendor::NVIDIA, e.report, binding), "snapshot attestation rejected");
        e.nonce = random_bytes(12);
        e.ciphertext = crypt(true, pImpl->key, e.nonce, plain, e.tag);
        auto packet = mvvm::codec::encode(e);
        need(packet.size() <= 300 * 1024 * 1024, "encoded snapshot limit");
        std::array<char, 8> length{};
        for (unsigned i = 0; i < 8; ++i)
            length[i] = (uint64_t(packet.size()) >> (i * 8)) & 255;
        need(writer->write(length.data(), length.size()) &&
                 writer->write(reinterpret_cast<const char *>(packet.data()), packet.size()),
             "snapshot write failed");
        OPENSSL_cleanse(pImpl->key.data(), pImpl->key.size());
        pImpl->key.clear();
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
bool NVIDIAGPUCCImpl::restoreGPUState(ReadStream *reader) {
    std::lock_guard lock(pImpl->mutex);
    try {
        pImpl->ready();
        need(reader && pImpl->key.size() == 32 && pImpl->allocations.empty() && pImpl->kernels.empty(),
             "restore requires key and empty context");
        std::array<unsigned char, 8> length{};
        need(reader->read(reinterpret_cast<char *>(length.data()), 8), "snapshot length missing");
        uint64_t n{};
        for (unsigned i = 0; i < 8; ++i)
            n |= uint64_t(length[i]) << (i * 8);
        need(n > 0 && n <= 300 * 1024 * 1024, "snapshot length limit");
        std::vector<std::byte> packet(n);
        need(reader->read(reinterpret_cast<char *>(packet.data()), n), "snapshot truncated");
        auto envelope = mvvm::codec::decode<Envelope>(packet);
        need(bool(envelope), "invalid envelope");
        auto plain = crypt(false, pImpl->key, envelope->nonce, envelope->ciphertext, envelope->tag);
        Wipe wipe{plain};
        auto binding = digest(plain.data(), plain.size(), EVP_sha512());
        need(pImpl->verifier(GPUVendor::NVIDIA, envelope->report, binding), "source snapshot attestation rejected");
        auto state = mvvm::codec::decode<Snapshot>(std::as_bytes(std::span(plain)));
        need(state && state->version == 1, "invalid snapshot schema");
        SnapshotWipe wipe_state{*state};
        try {
            for (auto &k : state->kernels)
                need(loadSecureKernel(k), "restore kernel failed");
            for (auto &a : state->allocations) {
                need(!pImpl->remap.contains(a.old_address), "duplicate restored address");
                void *p = allocateSecureMemory(a.data.size(), MemoryType::SECURE);
                need(p && copyToDevice(p, a.data.data(), a.data.size()), "restore allocation failed");
                pImpl->remap.emplace(a.old_address, p);
                OPENSSL_cleanse(a.data.data(), a.data.size());
            }
        } catch (...) {
            pImpl->clear();
            throw;
        }
        OPENSSL_cleanse(pImpl->key.data(), pImpl->key.size());
        pImpl->key.clear();
        return true;
    } catch (const std::exception &e) {
        log(e);
        return false;
    }
}
} // namespace mvvm::gpu

#else
#include "wamr_gpu_cc_nvidia_unavailable.inc"
#endif
