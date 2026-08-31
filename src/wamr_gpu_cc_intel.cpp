/* Intel GPU workloads running in a TDX guest.  TDREPORTs come directly from
 * the Linux tdx-guest driver; quote generation/verification is intentionally
 * left to a DCAP relying party instead of manufacturing a quote. */
#include "wamr_gpu_cc_framework.h"
#include "wamr_gpu_cc_migration.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>
#ifdef __linux__
#include <fcntl.h>
#include <linux/tdx-guest.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#ifdef MVVM_ENABLE_LEVEL_ZERO
#include <level_zero/ze_api.h>
#endif
namespace mvvm::gpu {
namespace {
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}
std::string text(const std::filesystem::path &p) {
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    return trim(s);
}
uint64_t number(const std::filesystem::path &p) {
    try {
        auto s = text(p);
        return s.empty() ? 0 : std::stoull(s, nullptr, 0);
    } catch (...) {
        return 0;
    }
}
#ifdef __linux__
bool tdReport(const std::vector<uint8_t> &binding, std::vector<uint8_t> &report) {
    int fd = open("/dev/tdx_guest", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        fd = open("/dev/tdx-guest", O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return false;
    tdx_report_req request{};
    std::memcpy(request.reportdata, binding.data(), std::min(binding.size(), sizeof(request.reportdata)));
    const bool ok = ioctl(fd, TDX_CMD_GET_REPORT0, &request) == 0;
    close(fd);
    if (!ok)
        return false;
    report.assign(request.tdreport, request.tdreport + sizeof(request.tdreport));
    return true;
}

bool tdQuote(const std::vector<uint8_t> &binding, std::vector<uint8_t> &quote) {
    if (binding.size() > TDX_REPORTDATA_LEN)
        return false;
    static std::atomic<uint64_t> sequence{0};
    const std::filesystem::path root{"/sys/kernel/config/tsm/report"};
    const auto instance = root / ("mvvm-" + std::to_string(getpid()) + "-" +
                                  std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
    std::error_code ec;
    if (!std::filesystem::create_directory(instance, ec))
        return false;
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{instance};

    if (text(instance / "provider") != "tdx_guest")
        return false;
    std::array<uint8_t, TDX_REPORTDATA_LEN> report_data{};
    std::copy(binding.begin(), binding.end(), report_data.begin());
    {
        std::ofstream input(instance / "inblob", std::ios::binary);
        input.write(reinterpret_cast<const char *>(report_data.data()), report_data.size());
        if (!input)
            return false;
    }
    std::ifstream output(instance / "outblob", std::ios::binary);
    if (!output)
        return false;
    quote.clear();
    std::array<char, 4096> chunk{};
    while (output) {
        output.read(chunk.data(), chunk.size());
        const auto count = output.gcount();
        if (count <= 0)
            break;
        if (quote.size() > migration::max_report_size - static_cast<size_t>(count)) {
            quote.clear();
            return false;
        }
        quote.insert(quote.end(), chunk.begin(), chunk.begin() + count);
    }
    return output.eof() && !quote.empty();
}
#else
bool tdReport(const std::vector<uint8_t> &, std::vector<uint8_t> &) { return false; }
bool tdQuote(const std::vector<uint8_t> &, std::vector<uint8_t> &) { return false; }
#endif
bool feature(const GPUDevice &d, CCFeature f) {
    return std::find(d.cc_features.begin(), d.cc_features.end(), f) != d.cc_features.end();
}
bool reportBindsData(const std::vector<uint8_t> &report, const std::vector<uint8_t> &data) {
    constexpr size_t report_data_offset = 128;
    return data.size() <= 64 && report.size() >= report_data_offset + data.size() &&
           CRYPTO_memcmp(report.data() + report_data_offset, data.data(), data.size()) == 0;
}
bool quoteIsPresent(const std::vector<uint8_t> &quote,
                    const std::array<uint8_t, migration::digest_size> &) {
    // Quote layout varies (including DICE/CWT quotes). The mandatory external
    // verifier validates its signature and REPORTDATA binding to the digest.
    return !quote.empty();
}
} // namespace
struct IntelGPUCCImpl::Impl {
    GPUDevice device{};
    bool selected = false, initialized = false;
    std::vector<uint8_t> report;
    std::map<void *, size_t> allocations;
    std::map<std::string, GPUKernel> kernels;
    std::array<uint8_t, migration::key_size> migration_key{};
    bool has_migration_key = false;
    std::map<uint64_t, void *> restored_pointers;
    std::vector<uint8_t> source_report;
    AttestationVerifier migration_verifier;
#ifdef MVVM_ENABLE_LEVEL_ZERO
    ze_driver_handle_t driver = nullptr;
    ze_device_handle_t ze_device = nullptr;
    ze_context_handle_t context = nullptr;
    ze_command_list_handle_t commands = nullptr;
    std::map<std::string, ze_module_handle_t> modules;
    std::map<std::string, ze_kernel_handle_t> ze_kernels;
#endif
};
IntelGPUCCImpl::IntelGPUCCImpl() : pImpl(std::make_unique<Impl>()) {}
IntelGPUCCImpl::~IntelGPUCCImpl() {
    for (const auto &[p, n] : pImpl->allocations) {
        OPENSSL_cleanse(p, n);
#ifdef MVVM_ENABLE_LEVEL_ZERO
        if (pImpl->context)
            zeMemFree(pImpl->context, p);
        else
#endif
        {
#ifdef __linux__
            munlock(p, n);
#endif
            std::free(p);
        }
    }
#ifdef MVVM_ENABLE_LEVEL_ZERO
    for (const auto &[_, kernel] : pImpl->ze_kernels)
        zeKernelDestroy(kernel);
    for (const auto &[_, module] : pImpl->modules)
        zeModuleDestroy(module);
    if (pImpl->commands)
        zeCommandListDestroy(pImpl->commands);
    if (pImpl->context)
        zeContextDestroy(pImpl->context);
#endif
    OPENSSL_cleanse(pImpl->migration_key.data(), pImpl->migration_key.size());
}
std::vector<GPUDevice> IntelGPUCCImpl::enumerateDevices() {
    std::vector<GPUDevice> out;
#ifdef MVVM_ENABLE_LEVEL_ZERO
    if (zeInit(0) != ZE_RESULT_SUCCESS)
        return out;
    uint32_t driver_count = 0;
    if (zeDriverGet(&driver_count, nullptr) != ZE_RESULT_SUCCESS || !driver_count)
        return out;
    std::vector<ze_driver_handle_t> drivers(driver_count);
    if (zeDriverGet(&driver_count, drivers.data()) != ZE_RESULT_SUCCESS)
        return out;
    std::vector<uint8_t> probe(64), tdx_report;
    const bool in_tdx = tdReport(probe, tdx_report);
    for (auto driver : drivers) {
        uint32_t device_count = 0;
        if (zeDeviceGet(driver, &device_count, nullptr) != ZE_RESULT_SUCCESS)
            continue;
        std::vector<ze_device_handle_t> devices(device_count);
        if (zeDeviceGet(driver, &device_count, devices.data()) != ZE_RESULT_SUCCESS)
            continue;
        for (auto device : devices) {
            ze_device_properties_t properties{ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES};
            if (zeDeviceGetProperties(device, &properties) != ZE_RESULT_SUCCESS ||
                properties.type != ZE_DEVICE_TYPE_GPU)
                continue;
            GPUDevice gpu{};
            gpu.name = properties.name;
            gpu.vendor = GPUVendor::INTEL;
            gpu.device_id = out.size();
            uint32_t memory_count = 0;
            if (zeDeviceGetMemoryProperties(device, &memory_count, nullptr) == ZE_RESULT_SUCCESS && memory_count) {
                std::vector<ze_device_memory_properties_t> memory(memory_count);
                for (auto &item : memory)
                    item.stype = ZE_STRUCTURE_TYPE_DEVICE_MEMORY_PROPERTIES;
                if (zeDeviceGetMemoryProperties(device, &memory_count, memory.data()) == ZE_RESULT_SUCCESS)
                    for (const auto &item : memory)
                        gpu.total_memory += item.totalSize;
            }
            gpu.available_memory = gpu.total_memory;
            ze_device_compute_properties_t compute{ZE_STRUCTURE_TYPE_DEVICE_COMPUTE_PROPERTIES};
            if (zeDeviceGetComputeProperties(device, &compute) == ZE_RESULT_SUCCESS) {
                gpu.capability.max_threads_per_block = compute.maxTotalGroupSize;
                gpu.capability.max_blocks_per_grid = compute.maxGroupCountX;
                gpu.capability.shared_memory_per_block = compute.maxSharedLocalMemory;
            }
            gpu.capability.supports_cc = in_tdx;
            if (in_tdx)
                gpu.cc_features = {CCFeature::REMOTE_ATTESTATION};
            out.push_back(std::move(gpu));
        }
    }
    return out;
#else
#ifdef __linux__
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        auto n = e.path().filename().string();
        if (n.rfind("card", 0) != 0 || n.find('-') != std::string::npos)
            continue;
        auto d = e.path() / "device";
        if (text(d / "vendor") != "0x8086")
            continue;
        GPUDevice g{};
        auto id = text(d / "device");
        g.name = "Intel GPU " + (id.empty() ? std::string("unknown") : id);
        g.vendor = GPUVendor::INTEL;
        g.device_id = out.size();
        g.total_memory = number(d / "mem_info_vram_total");
        g.available_memory = g.total_memory;
        g.capability = {0, 0, 0, 0, 0, false};
        std::vector<uint8_t> probe(32), r;
        if (tdReport(probe, r)) {
            g.capability.supports_cc = true;
            g.cc_features = {CCFeature::REMOTE_ATTESTATION};
        }
        out.push_back(std::move(g));
    }
#endif
    return out;
#endif
}
bool IntelGPUCCImpl::selectDevice(size_t id) {
    auto d = enumerateDevices();
    if (id >= d.size())
        return false;
    pImpl->device = d[id];
#ifdef MVVM_ENABLE_LEVEL_ZERO
    uint32_t driver_count = 0;
    if (zeDriverGet(&driver_count, nullptr) != ZE_RESULT_SUCCESS)
        return false;
    std::vector<ze_driver_handle_t> drivers(driver_count);
    if (zeDriverGet(&driver_count, drivers.data()) != ZE_RESULT_SUCCESS)
        return false;
    size_t current = 0;
    for (auto driver : drivers) {
        uint32_t count = 0;
        if (zeDeviceGet(driver, &count, nullptr) != ZE_RESULT_SUCCESS)
            continue;
        std::vector<ze_device_handle_t> devices(count);
        if (zeDeviceGet(driver, &count, devices.data()) != ZE_RESULT_SUCCESS)
            continue;
        for (auto device : devices) {
            ze_device_properties_t properties{ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES};
            if (zeDeviceGetProperties(device, &properties) != ZE_RESULT_SUCCESS ||
                properties.type != ZE_DEVICE_TYPE_GPU)
                continue;
            if (current++ == id) {
                pImpl->driver = driver;
                pImpl->ze_device = device;
                break;
            }
        }
        if (pImpl->ze_device)
            break;
    }
    if (!pImpl->ze_device)
        return false;
    ze_context_desc_t context_desc{ZE_STRUCTURE_TYPE_CONTEXT_DESC};
    if (zeContextCreate(pImpl->driver, &context_desc, &pImpl->context) != ZE_RESULT_SUCCESS)
        return false;
    uint32_t queue_count = 0;
    if (zeDeviceGetCommandQueueGroupProperties(pImpl->ze_device, &queue_count, nullptr) != ZE_RESULT_SUCCESS)
        return false;
    std::vector<ze_command_queue_group_properties_t> queues(queue_count);
    for (auto &queue : queues)
        queue.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_GROUP_PROPERTIES;
    if (zeDeviceGetCommandQueueGroupProperties(pImpl->ze_device, &queue_count, queues.data()) != ZE_RESULT_SUCCESS)
        return false;
    const auto queue = std::find_if(queues.begin(), queues.end(), [](const auto &properties) {
        return (properties.flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) != 0;
    });
    if (queue == queues.end())
        return false;
    ze_command_queue_desc_t queue_desc{ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC};
    queue_desc.ordinal = static_cast<uint32_t>(std::distance(queues.begin(), queue));
    queue_desc.index = 0;
    queue_desc.mode = ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS;
    queue_desc.priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL;
    if (zeCommandListCreateImmediate(pImpl->context, pImpl->ze_device, &queue_desc, &pImpl->commands) !=
        ZE_RESULT_SUCCESS)
        return false;
#endif
    pImpl->selected = true;
    return true;
}
GPUDevice IntelGPUCCImpl::getCurrentDevice() { return pImpl->device; }
bool IntelGPUCCImpl::initializeCC(const std::vector<CCFeature> &required) {
    if (!pImpl->selected || !pImpl->device.capability.supports_cc)
        return false;
    for (auto f : required)
        if (!feature(pImpl->device, f)) {
            SPDLOG_ERROR("TDX guest does not expose requested GPU CC feature");
            return false;
        }
    std::vector<uint8_t> nonce(64);
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !tdReport(nonce, pImpl->report) ||
        !reportBindsData(pImpl->report, nonce)) {
        SPDLOG_ERROR("TDX TDREPORT request failed");
        return false;
    }
    pImpl->initialized = true;
    return true;
}
bool IntelGPUCCImpl::verifyDevice() {
    if (!pImpl->initialized)
        return false;
    std::vector<uint8_t> nonce(64), r;
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !tdReport(nonce, r) || !reportBindsData(r, nonce))
        return false;
    pImpl->report = std::move(r);
    return true;
}
std::vector<uint8_t> IntelGPUCCImpl::getAttestationReport() {
    return pImpl->initialized ? pImpl->report : std::vector<uint8_t>{};
}
void *IntelGPUCCImpl::allocateSecureMemory(size_t size, MemoryType type) {
#ifdef _WIN32
    (void)size;
    (void)type;
    return nullptr;
#else
    if (!pImpl->initialized || type != MemoryType::SECURE || !size)
        return nullptr;
    void *p = nullptr;
#ifdef MVVM_ENABLE_LEVEL_ZERO
    if (pImpl->context) {
        ze_device_mem_alloc_desc_t device_desc{ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC};
        ze_host_mem_alloc_desc_t host_desc{ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC};
        if (zeMemAllocShared(pImpl->context, &device_desc, &host_desc, size, 4096, pImpl->ze_device, &p) !=
            ZE_RESULT_SUCCESS)
            return nullptr;
        std::memset(p, 0, size);
        pImpl->allocations[p] = size;
        return p;
    }
#endif
    if (size > std::numeric_limits<size_t>::max() - 4095)
        return nullptr;
    size_t rounded = (size + 4095) & ~size_t(4095);
    if (posix_memalign(&p, 4096, rounded) != 0)
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
void IntelGPUCCImpl::freeSecureMemory(void *p) {
    auto i = pImpl->allocations.find(p);
    if (i == pImpl->allocations.end())
        return;
    OPENSSL_cleanse(p, i->second);
#ifdef MVVM_ENABLE_LEVEL_ZERO
    if (pImpl->context) {
        zeMemFree(pImpl->context, p);
        pImpl->allocations.erase(i);
        return;
    }
#endif
#ifdef __linux__
    munlock(p, i->second);
#endif
    std::free(p);
    pImpl->allocations.erase(i);
}
bool IntelGPUCCImpl::encryptMemory(void *p, size_t n) {
    return pImpl->initialized && feature(pImpl->device, CCFeature::MEMORY_ENCRYPTION) &&
           pImpl->allocations.contains(p) && n <= pImpl->allocations[p];
}
bool IntelGPUCCImpl::decryptMemory(void *p, size_t n) { return encryptMemory(p, n); }
bool IntelGPUCCImpl::loadSecureKernel(const GPUKernel &k) {
    if (!pImpl->initialized || k.name.empty() || k.binary_code.empty() || k.signature.empty() ||
        pImpl->kernels.contains(k.name))
        return false;
#ifdef MVVM_ENABLE_LEVEL_ZERO
    if (!pImpl->context || !pImpl->ze_device)
        return false;
    ze_module_desc_t module_desc{ZE_STRUCTURE_TYPE_MODULE_DESC};
    module_desc.format = ZE_MODULE_FORMAT_IL_SPIRV;
    module_desc.inputSize = k.binary_code.size();
    module_desc.pInputModule = k.binary_code.data();
    ze_module_handle_t module = nullptr;
    ze_module_build_log_handle_t build_log = nullptr;
    if (zeModuleCreate(pImpl->context, pImpl->ze_device, &module_desc, &module, &build_log) != ZE_RESULT_SUCCESS) {
        if (build_log) {
            size_t size = 0;
            zeModuleBuildLogGetString(build_log, &size, nullptr);
            std::string message(size, '\0');
            if (size)
                zeModuleBuildLogGetString(build_log, &size, message.data());
            SPDLOG_ERROR("Level Zero module build failed: {}", message);
            zeModuleBuildLogDestroy(build_log);
        }
        return false;
    }
    if (build_log)
        zeModuleBuildLogDestroy(build_log);
    ze_kernel_desc_t kernel_desc{ZE_STRUCTURE_TYPE_KERNEL_DESC};
    kernel_desc.pKernelName = k.name.c_str();
    ze_kernel_handle_t kernel = nullptr;
    if (zeKernelCreate(module, &kernel_desc, &kernel) != ZE_RESULT_SUCCESS) {
        zeModuleDestroy(module);
        return false;
    }
    pImpl->modules[k.name] = module;
    pImpl->ze_kernels[k.name] = kernel;
#else
    SPDLOG_ERROR("Cannot load Intel GPU kernel: Level Zero backend was not built");
    return false;
#endif
    pImpl->kernels[k.name] = k;
    return true;
}
bool IntelGPUCCImpl::executeSecureKernel(const std::string &name, void **args, size_t num_args, size_t grid_size,
                                        size_t block_size) {
#ifdef MVVM_ENABLE_LEVEL_ZERO
    const auto found = pImpl->ze_kernels.find(name);
    const auto metadata = pImpl->kernels.find(name);
    if (!pImpl->initialized || found == pImpl->ze_kernels.end() || metadata == pImpl->kernels.end() ||
        (!args && num_args) || !grid_size || !block_size || block_size > UINT32_MAX ||
        num_args > UINT32_MAX || metadata->second.required_shared_memory > UINT32_MAX ||
        (metadata->second.num_parameters && metadata->second.num_parameters != num_args))
        return false;
    if (zeKernelSetGroupSize(found->second, static_cast<uint32_t>(block_size), 1, 1) != ZE_RESULT_SUCCESS)
        return false;
    for (uint32_t i = 0; i < num_args; ++i)
        if (zeKernelSetArgumentValue(found->second, i, sizeof(void *), &args[i]) != ZE_RESULT_SUCCESS)
            return false;
    const size_t group_count = grid_size / block_size + (grid_size % block_size != 0);
    if (group_count > UINT32_MAX)
        return false;
    ze_group_count_t groups{static_cast<uint32_t>(group_count), 1, 1};
    return zeCommandListAppendLaunchKernel(pImpl->commands, found->second, &groups, nullptr, 0, nullptr) ==
               ZE_RESULT_SUCCESS &&
           zeCommandListHostSynchronize(pImpl->commands, UINT64_MAX) == ZE_RESULT_SUCCESS;
#else
    (void)name;
    (void)args;
    (void)num_args;
    (void)grid_size;
    (void)block_size;
    SPDLOG_ERROR("Intel Level Zero backend was not built; install libze-dev and reconfigure");
    return false;
#endif
}
bool IntelGPUCCImpl::checkpointGPUState(WriteStream *writer) {
    if (!pImpl->initialized || !pImpl->has_migration_key || !writer)
        return false;
#ifdef MVVM_ENABLE_LEVEL_ZERO
    if (pImpl->commands && zeCommandListHostSynchronize(pImpl->commands, UINT64_MAX) != ZE_RESULT_SUCCESS)
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
    const bool written = migration::write(writer, GPUVendor::INTEL, state, pImpl->migration_key, tdQuote);
    if (!written)
        SPDLOG_ERROR("TDX migration checkpoint requires the Linux configfs-tsm tdx_guest quote provider");
    if (written) {
        OPENSSL_cleanse(pImpl->migration_key.data(), pImpl->migration_key.size());
        pImpl->has_migration_key = false;
    }
    return written;
}
bool IntelGPUCCImpl::restoreGPUState(ReadStream *reader) {
    if (!pImpl->has_migration_key || !reader || !pImpl->allocations.empty() || !pImpl->kernels.empty())
        return false;
    migration::State state;
    std::array<uint8_t, migration::digest_size> digest{};
    if (!migration::read(reader, GPUVendor::INTEL, pImpl->migration_key, quoteIsPresent, state,
                         pImpl->source_report, digest))
        return false;
    if (!pImpl->migration_verifier)
        return false;
    try {
        const std::vector<uint8_t> digest_bytes(digest.begin(), digest.end());
        if (!pImpl->migration_verifier(GPUVendor::INTEL, pImpl->source_report, digest_bytes))
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
    if (!tdReport(binding, pImpl->report) || !reportBindsData(pImpl->report, binding))
        return false;
    std::vector<std::string> loaded_kernels;
    std::vector<void *> restored_allocations;
    const auto rollback = [&] {
        for (auto it = restored_allocations.rbegin(); it != restored_allocations.rend(); ++it)
            freeSecureMemory(*it);
        pImpl->restored_pointers.clear();
        for (auto it = loaded_kernels.rbegin(); it != loaded_kernels.rend(); ++it) {
#ifdef MVVM_ENABLE_LEVEL_ZERO
            if (const auto kernel = pImpl->ze_kernels.find(*it); kernel != pImpl->ze_kernels.end()) {
                if (zeKernelDestroy(kernel->second) != ZE_RESULT_SUCCESS)
                    SPDLOG_WARN("zeKernelDestroy failed during restore rollback");
                pImpl->ze_kernels.erase(kernel);
            }
            if (const auto module = pImpl->modules.find(*it); module != pImpl->modules.end()) {
                if (zeModuleDestroy(module->second) != ZE_RESULT_SUCCESS)
                    SPDLOG_WARN("zeModuleDestroy failed during restore rollback");
                pImpl->modules.erase(module);
            }
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
bool IntelGPUCCImpl::setMigrationKey(const std::vector<uint8_t> &key) {
    if (key.size() != pImpl->migration_key.size())
        return false;
    OPENSSL_cleanse(pImpl->migration_key.data(), pImpl->migration_key.size());
    std::copy(key.begin(), key.end(), pImpl->migration_key.begin());
    pImpl->has_migration_key = true;
    return true;
}
bool IntelGPUCCImpl::setMigrationAttestationVerifier(AttestationVerifier verifier) {
    pImpl->migration_verifier = std::move(verifier);
    return static_cast<bool>(pImpl->migration_verifier);
}
void *IntelGPUCCImpl::remapRestoredPointer(uint64_t old_address) const {
    const auto it = pImpl->restored_pointers.find(old_address);
    return it == pImpl->restored_pointers.end() ? nullptr : it->second;
}
} // namespace mvvm::gpu
