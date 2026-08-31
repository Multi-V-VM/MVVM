/* AMD GPU execution inside an AMD SEV-SNP guest.  SEV-SNP protects guest
 * memory; it must not be advertised as an AMD GPU encryption feature. */
#include "wamr_gpu_cc_framework.h"
#include "wamr_gpu_cc_migration.h"
#include <array>
#include <climits>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>
#include <string>
#ifdef __linux__
#include <fcntl.h>
#include <linux/sev-guest.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#endif
#ifdef MVVM_ENABLE_HIP
#include <hip/hip_runtime_api.h>
#endif

namespace mvvm::gpu {
namespace {
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}
std::string readFile(const std::filesystem::path &p) {
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    return trim(s);
}
uint64_t readNumber(const std::filesystem::path &p) {
    const auto s = readFile(p);
    try {
        return s.empty() ? 0 : std::stoull(s, nullptr, 0);
    } catch (...) {
        return 0;
    }
}
#ifdef __linux__
bool snpReport(const std::vector<uint8_t> &binding, std::vector<uint8_t> &report) {
    int fd = open("/dev/sev-guest", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return false;
    snp_report_req request{};
    std::memcpy(request.user_data, binding.data(), std::min(binding.size(), sizeof(request.user_data)));
    snp_report_resp response{};
    snp_guest_request_ioctl ioctl_request{};
    ioctl_request.msg_version = 1;
    ioctl_request.req_data = reinterpret_cast<uint64_t>(&request);
    ioctl_request.resp_data = reinterpret_cast<uint64_t>(&response);
    int result;
    do {
        result = ioctl(fd, SNP_GET_REPORT, &ioctl_request);
    } while (result < 0 && errno == EINTR);
    close(fd);
    if (result != 0 || ioctl_request.fw_error != 0 || ioctl_request.vmm_error != 0)
        return false;
    struct ResponseHeader {
        uint32_t status;
        uint32_t report_size;
        uint8_t reserved[24];
    } header{};
    static_assert(sizeof(header) == 32);
    std::memcpy(&header, response.data, sizeof(header));
    if (header.status != 0 || header.report_size == 0 ||
        header.report_size > sizeof(response.data) - sizeof(header))
        return false;
    report.assign(response.data + sizeof(header), response.data + sizeof(header) + header.report_size);
    return true;
}
#else
bool snpReport(const std::vector<uint8_t> &, std::vector<uint8_t> &) { return false; }
#endif
bool hasFeature(const GPUDevice &device, CCFeature feature) {
    return std::find(device.cc_features.begin(), device.cc_features.end(), feature) != device.cc_features.end();
}
bool reportBindsDigest(const std::vector<uint8_t> &report,
                       const std::array<uint8_t, migration::digest_size> &digest) {
    constexpr size_t report_data_offset = 0x50;
    return report.size() >= report_data_offset + digest.size() &&
           CRYPTO_memcmp(report.data() + report_data_offset, digest.data(), digest.size()) == 0;
}
bool reportBindsData(const std::vector<uint8_t> &report, const std::vector<uint8_t> &data) {
    constexpr size_t report_data_offset = 0x50;
    return data.size() <= 64 && report.size() >= report_data_offset + data.size() &&
           CRYPTO_memcmp(report.data() + report_data_offset, data.data(), data.size()) == 0;
}
} // namespace

struct AMDGPUCCImpl::Impl {
    GPUDevice device{};
    bool selected = false, initialized = false, snp = false;
    std::vector<uint8_t> last_report;
    std::map<void *, size_t> allocations;
    std::map<std::string, GPUKernel> kernels;
    std::array<uint8_t, migration::key_size> migration_key{};
    bool has_migration_key = false;
    std::map<uint64_t, void *> restored_pointers;
    std::vector<uint8_t> source_report;
    AttestationVerifier migration_verifier;
#ifdef MVVM_ENABLE_HIP
    int hip_device = -1;
    std::map<void *, void *> device_aliases;
    std::map<std::string, hipModule_t> modules;
    std::map<std::string, hipFunction_t> functions;
#endif
};
AMDGPUCCImpl::AMDGPUCCImpl() : pImpl(std::make_unique<Impl>()) {}
AMDGPUCCImpl::~AMDGPUCCImpl() {
    for (const auto &[p, n] : pImpl->allocations) {
        OPENSSL_cleanse(p, n);
#ifdef MVVM_ENABLE_HIP
        if (pImpl->device_aliases.contains(p)) {
            const auto result = hipHostFree(p);
            if (result != hipSuccess)
                SPDLOG_WARN("hipHostFree failed while destroying the AMD GPU CC context: {}",
                            hipGetErrorString(result));
        } else
#endif
        {
#ifdef __linux__
            munlock(p, n);
#endif
            std::free(p);
        }
    }
#ifdef MVVM_ENABLE_HIP
    for (const auto &[_, module] : pImpl->modules) {
        const auto result = hipModuleUnload(module);
        if (result != hipSuccess)
            SPDLOG_WARN("hipModuleUnload failed while destroying the AMD GPU CC context: {}",
                        hipGetErrorString(result));
    }
#endif
    OPENSSL_cleanse(pImpl->migration_key.data(), pImpl->migration_key.size());
}

std::vector<GPUDevice> AMDGPUCCImpl::enumerateDevices() {
    std::vector<GPUDevice> devices;
#ifdef MVVM_ENABLE_HIP
    int count = 0;
    if (hipInit(0) != hipSuccess || hipGetDeviceCount(&count) != hipSuccess)
        return devices;
    std::vector<uint8_t> probe(64), report;
    const bool in_snp = snpReport(probe, report);
    for (int index = 0; index < count; ++index) {
        hipDeviceProp_t properties{};
        if (hipGetDeviceProperties(&properties, index) != hipSuccess)
            continue;
        GPUDevice device{};
        device.name = properties.name;
        device.vendor = GPUVendor::AMD;
        device.device_id = devices.size();
        device.total_memory = properties.totalGlobalMem;
        device.available_memory = device.total_memory;
        device.capability = {properties.major, properties.minor,
                             static_cast<size_t>(properties.maxThreadsPerBlock),
                             static_cast<size_t>(properties.maxGridSize[0]),
                             static_cast<size_t>(properties.sharedMemPerBlock), in_snp};
        if (in_snp)
            device.cc_features = {CCFeature::REMOTE_ATTESTATION};
        devices.push_back(std::move(device));
    }
    return devices;
#else
#ifdef __linux__
    const std::filesystem::path root{"/sys/class/drm"};
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(root, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("card", 0) != 0 || name.find('-') != std::string::npos)
            continue;
        const auto dev = entry.path() / "device";
        if (readFile(dev / "vendor") != "0x1002")
            continue;
        GPUDevice d{};
        const auto id = readFile(dev / "device");
        d.name = "AMD GPU " + (id.empty() ? std::string("unknown") : id);
        d.vendor = GPUVendor::AMD;
        d.device_id = devices.size();
        d.total_memory = readNumber(dev / "mem_info_vram_total");
        d.available_memory = readNumber(dev / "mem_info_vram_used");
        if (d.available_memory <= d.total_memory)
            d.available_memory = d.total_memory - d.available_memory;
        d.capability = {0, 0, 0, 0, 0, false};
        /* An SNP guest gives an AMD GPU a hardware-attested encrypted host-memory path.
         * It does not prove protected VRAM or a secure kernel launch interface. */
        std::vector<uint8_t> probe(32, 0), report;
        if (snpReport(probe, report)) {
            d.cc_features = {CCFeature::REMOTE_ATTESTATION};
            d.capability.supports_cc = true;
        }
        devices.push_back(std::move(d));
    }
#endif
    return devices;
#endif
}
bool AMDGPUCCImpl::selectDevice(size_t id) {
    auto devices = enumerateDevices();
    if (id >= devices.size())
        return false;
    pImpl->device = devices[id];
#ifdef MVVM_ENABLE_HIP
    if (id > static_cast<size_t>(INT_MAX) || hipSetDevice(static_cast<int>(id)) != hipSuccess)
        return false;
    pImpl->hip_device = static_cast<int>(id);
#endif
    pImpl->selected = true;
    return true;
}
GPUDevice AMDGPUCCImpl::getCurrentDevice() { return pImpl->device; }
bool AMDGPUCCImpl::initializeCC(const std::vector<CCFeature> &features) {
    if (!pImpl->selected || !pImpl->device.capability.supports_cc)
        return false;
    for (auto f : features)
        if (!hasFeature(pImpl->device, f)) {
            SPDLOG_ERROR("AMD SEV-SNP guest does not provide requested GPU CC feature");
            return false;
        }
    std::vector<uint8_t> nonce(64);
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !snpReport(nonce, pImpl->last_report) ||
        !reportBindsData(pImpl->last_report, nonce)) {
        SPDLOG_ERROR("AMD SEV-SNP report request failed");
        return false;
    }
    pImpl->snp = true;
    pImpl->initialized = true;
    return true;
}
bool AMDGPUCCImpl::verifyDevice() {
    if (!pImpl->initialized)
        return false;
    std::vector<uint8_t> nonce(64), report;
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !snpReport(nonce, report) ||
        !reportBindsData(report, nonce))
        return false; /* Firmware signature validation belongs to the relying party with AMD VCEK collateral. */
    pImpl->last_report = std::move(report);
    return true;
}
std::vector<uint8_t> AMDGPUCCImpl::getAttestationReport() {
    return pImpl->initialized ? pImpl->last_report : std::vector<uint8_t>{};
}
void *AMDGPUCCImpl::allocateSecureMemory(size_t size, MemoryType type) {
#ifdef _WIN32
    (void)size;
    (void)type;
    return nullptr;
#else
    if (!pImpl->initialized || type != MemoryType::SECURE || !size)
        return nullptr;
    void *p = nullptr;
#ifdef MVVM_ENABLE_HIP
    if (pImpl->hip_device >= 0) {
        if (hipHostMalloc(&p, size, hipHostMallocMapped | hipHostMallocCoherent) != hipSuccess)
            return nullptr;
        void *device_alias = nullptr;
        if (hipHostGetDevicePointer(&device_alias, p, 0) != hipSuccess) {
            const auto result = hipHostFree(p);
            if (result != hipSuccess)
                SPDLOG_WARN("hipHostFree failed after mapped pointer setup failed: {}", hipGetErrorString(result));
            return nullptr;
        }
        std::memset(p, 0, size);
        pImpl->allocations[p] = size;
        pImpl->device_aliases[p] = device_alias;
        return p;
    }
#endif
    if (size > std::numeric_limits<size_t>::max() - 4095)
        return nullptr;
    if (posix_memalign(&p, 4096, ((size + 4095) / 4096) * 4096) != 0)
        return nullptr;
    std::memset(p, 0, size);
#ifdef __linux__
    if (mlock(p, size) != 0) {
        OPENSSL_cleanse(p, size);
        std::free(p);
        return nullptr;
    }
#endif
    pImpl->allocations[p] = size;
    return p;
#endif
}
void AMDGPUCCImpl::freeSecureMemory(void *p) {
    auto it = pImpl->allocations.find(p);
    if (it == pImpl->allocations.end())
        return;
    OPENSSL_cleanse(p, it->second);
#ifdef MVVM_ENABLE_HIP
    if (pImpl->device_aliases.contains(p)) {
        const auto result = hipHostFree(p);
        if (result != hipSuccess) {
            SPDLOG_ERROR("hipHostFree failed: {}", hipGetErrorString(result));
            return;
        }
        pImpl->device_aliases.erase(p);
        pImpl->allocations.erase(it);
        return;
    }
#endif
#ifdef __linux__
    munlock(p, it->second);
#endif
    std::free(p);
    pImpl->allocations.erase(it);
}
bool AMDGPUCCImpl::encryptMemory(void *p, size_t size) {
    return pImpl->initialized && hasFeature(pImpl->device, CCFeature::MEMORY_ENCRYPTION) &&
           pImpl->allocations.contains(p) && size <= pImpl->allocations[p];
}
bool AMDGPUCCImpl::decryptMemory(void *p, size_t size) { return encryptMemory(p, size); }
bool AMDGPUCCImpl::loadSecureKernel(const GPUKernel &kernel) {
    if (!pImpl->initialized || kernel.name.empty() || kernel.binary_code.empty() || kernel.signature.empty() ||
        pImpl->kernels.contains(kernel.name))
        return false;
#ifdef MVVM_ENABLE_HIP
    hipModule_t module = nullptr;
    hipFunction_t function = nullptr;
    if (pImpl->hip_device < 0 || hipModuleLoadData(&module, kernel.binary_code.data()) != hipSuccess ||
        hipModuleGetFunction(&function, module, kernel.name.c_str()) != hipSuccess) {
        if (module) {
            const auto result = hipModuleUnload(module);
            if (result != hipSuccess)
                SPDLOG_WARN("hipModuleUnload failed after kernel loading failed: {}", hipGetErrorString(result));
        }
        return false;
    }
    pImpl->modules[kernel.name] = module;
    pImpl->functions[kernel.name] = function;
#else
    SPDLOG_ERROR("Cannot load AMD GPU kernel: HIP backend was not built");
    return false;
#endif
    pImpl->kernels[kernel.name] = kernel;
    return true;
}
bool AMDGPUCCImpl::executeSecureKernel(const std::string &name, void **args, size_t num_args, size_t grid_size,
                                      size_t block_size) {
#ifdef MVVM_ENABLE_HIP
    const auto function = pImpl->functions.find(name);
    const auto metadata = pImpl->kernels.find(name);
    if (!pImpl->initialized || function == pImpl->functions.end() || metadata == pImpl->kernels.end() ||
        (!args && num_args) || !grid_size || !block_size || grid_size > UINT_MAX || block_size > UINT_MAX ||
        metadata->second.required_shared_memory > UINT_MAX ||
        (metadata->second.num_parameters && metadata->second.num_parameters != num_args))
        return false;
    std::vector<void *> values(args, args + num_args);
    for (auto &value : values) {
        const auto alias = pImpl->device_aliases.find(value);
        if (alias != pImpl->device_aliases.end())
            value = alias->second;
    }
    std::vector<void *> parameters;
    parameters.reserve(values.size());
    for (auto &value : values)
        parameters.push_back(&value);
    return hipModuleLaunchKernel(function->second, static_cast<unsigned>(grid_size), 1, 1,
                                 static_cast<unsigned>(block_size), 1, 1,
                                 static_cast<unsigned>(metadata->second.required_shared_memory), nullptr,
                                 parameters.data(), nullptr) == hipSuccess &&
           hipDeviceSynchronize() == hipSuccess;
#else
    (void)name;
    (void)args;
    (void)num_args;
    (void)grid_size;
    (void)block_size;
    SPDLOG_ERROR("AMD HIP backend was not built; install libamdhip64-dev/ROCm and reconfigure");
    return false;
#endif
}
bool AMDGPUCCImpl::checkpointGPUState(WriteStream *writer) {
    if (!pImpl->initialized || !pImpl->has_migration_key || !writer)
        return false;
#ifdef MVVM_ENABLE_HIP
    if (pImpl->hip_device >= 0 && hipDeviceSynchronize() != hipSuccess)
        return false;
#endif
    migration::State state;
    state.device_name = pImpl->device.name;
    for (const auto &[_, kernel] : pImpl->kernels)
        state.kernels.push_back(kernel);
    for (const auto &[address, size] : pImpl->allocations) {
        migration::Allocation allocation;
        allocation.old_address = reinterpret_cast<uint64_t>(address);
        allocation.contents.assign(static_cast<const uint8_t *>(address),
                                   static_cast<const uint8_t *>(address) + size);
        state.allocations.push_back(std::move(allocation));
    }
    const bool written = migration::write(writer, GPUVendor::AMD, state, pImpl->migration_key, snpReport);
    if (written) {
        OPENSSL_cleanse(pImpl->migration_key.data(), pImpl->migration_key.size());
        pImpl->has_migration_key = false;
    }
    return written;
}
bool AMDGPUCCImpl::restoreGPUState(ReadStream *reader) {
    if (!pImpl->has_migration_key || !reader || !pImpl->allocations.empty() || !pImpl->kernels.empty())
        return false;
    migration::State state;
    std::array<uint8_t, migration::digest_size> digest{};
    if (!migration::read(reader, GPUVendor::AMD, pImpl->migration_key, reportBindsDigest, state,
                         pImpl->source_report, digest))
        return false;
    if (!pImpl->migration_verifier)
        return false;
    try {
        const std::vector<uint8_t> digest_bytes(digest.begin(), digest.end());
        if (!pImpl->migration_verifier(GPUVendor::AMD, pImpl->source_report, digest_bytes))
            return false;
    } catch (...) {
        return false;
    }
    if (!pImpl->selected) {
        const auto devices = enumerateDevices();
        const auto match = std::find_if(devices.begin(), devices.end(),
                                        [&](const GPUDevice &device) { return device.name == state.device_name; });
        if (match == devices.end() || !selectDevice(match->device_id))
            return false;
    } else if (pImpl->device.name != state.device_name) {
        return false;
    }
    if (!pImpl->initialized && !initializeCC({CCFeature::REMOTE_ATTESTATION}))
        return false;
    std::vector<uint8_t> binding(digest.begin(), digest.end());
    if (!snpReport(binding, pImpl->last_report) || !reportBindsData(pImpl->last_report, binding))
        return false;
    std::vector<std::string> loaded_kernels;
    std::vector<void *> restored_allocations;
    const auto rollback = [&] {
        for (auto it = restored_allocations.rbegin(); it != restored_allocations.rend(); ++it)
            freeSecureMemory(*it);
        pImpl->restored_pointers.clear();
        for (auto it = loaded_kernels.rbegin(); it != loaded_kernels.rend(); ++it) {
#ifdef MVVM_ENABLE_HIP
            if (const auto module = pImpl->modules.find(*it); module != pImpl->modules.end()) {
                const auto result = hipModuleUnload(module->second);
                if (result != hipSuccess)
                    SPDLOG_WARN("hipModuleUnload failed during restore rollback: {}", hipGetErrorString(result));
                pImpl->modules.erase(module);
            }
            pImpl->functions.erase(*it);
#endif
            pImpl->kernels.erase(*it);
        }
    };
    for (const auto &kernel : state.kernels) {
        if (!loadSecureKernel(kernel)) {
            rollback();
            return false;
        }
        loaded_kernels.push_back(kernel.name);
    }
    for (const auto &saved : state.allocations) {
        if (!saved.old_address || saved.contents.empty() || pImpl->restored_pointers.contains(saved.old_address)) {
            rollback();
            return false;
        }
        void *address = allocateSecureMemory(saved.contents.size(), MemoryType::SECURE);
        if (!address) {
            rollback();
            return false;
        }
        restored_allocations.push_back(address);
        std::memcpy(address, saved.contents.data(), saved.contents.size());
        pImpl->restored_pointers[saved.old_address] = address;
    }
    OPENSSL_cleanse(pImpl->migration_key.data(), pImpl->migration_key.size());
    pImpl->has_migration_key = false;
    return true;
}
bool AMDGPUCCImpl::setMigrationKey(const std::vector<uint8_t> &key) {
    if (key.size() != pImpl->migration_key.size())
        return false;
    OPENSSL_cleanse(pImpl->migration_key.data(), pImpl->migration_key.size());
    std::copy(key.begin(), key.end(), pImpl->migration_key.begin());
    pImpl->has_migration_key = true;
    return true;
}
bool AMDGPUCCImpl::setMigrationAttestationVerifier(AttestationVerifier verifier) {
    pImpl->migration_verifier = std::move(verifier);
    return static_cast<bool>(pImpl->migration_verifier);
}
void *AMDGPUCCImpl::remapRestoredPointer(uint64_t old_address) const {
    const auto it = pImpl->restored_pointers.find(old_address);
    return it == pImpl->restored_pointers.end() ? nullptr : it->second;
}
} // namespace mvvm::gpu
