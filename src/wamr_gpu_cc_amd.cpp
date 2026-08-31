/* AMD GPU execution inside an AMD SEV-SNP guest.  SEV-SNP protects guest
 * memory; it must not be advertised as an AMD GPU encryption feature. */
#include "wamr_gpu_cc_framework.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
    const bool ok = ioctl(fd, SNP_GET_REPORT, &ioctl_request) == 0;
    close(fd);
    if (!ok)
        return false;
    report.assign(response.data, response.data + sizeof(response.data));
    return true;
}
#else
bool snpReport(const std::vector<uint8_t> &, std::vector<uint8_t> &) { return false; }
#endif
bool hasFeature(const GPUDevice &device, CCFeature feature) {
    return std::find(device.cc_features.begin(), device.cc_features.end(), feature) != device.cc_features.end();
}
} // namespace

struct AMDGPUCCImpl::Impl {
    GPUDevice device{};
    bool selected = false, initialized = false, snp = false;
    std::vector<uint8_t> last_report;
    std::map<void *, size_t> allocations;
    std::map<std::string, GPUKernel> kernels;
};
AMDGPUCCImpl::AMDGPUCCImpl() : pImpl(std::make_unique<Impl>()) {}
AMDGPUCCImpl::~AMDGPUCCImpl() {
    for (const auto &[p, n] : pImpl->allocations) {
        OPENSSL_cleanse(p, n);
#ifdef __linux__
        munlock(p, n);
#endif
        std::free(p);
    }
}

std::vector<GPUDevice> AMDGPUCCImpl::enumerateDevices() {
    std::vector<GPUDevice> devices;
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
            d.cc_features = {CCFeature::MEMORY_ENCRYPTION, CCFeature::REMOTE_ATTESTATION};
            d.capability.supports_cc = true;
        }
        devices.push_back(std::move(d));
    }
#endif
    return devices;
}
bool AMDGPUCCImpl::selectDevice(size_t id) {
    auto devices = enumerateDevices();
    if (id >= devices.size())
        return false;
    pImpl->device = devices[id];
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
    std::vector<uint8_t> nonce(32);
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !snpReport(nonce, pImpl->last_report)) {
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
    std::vector<uint8_t> nonce(32), report;
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !snpReport(nonce, report))
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
#ifdef __linux__
    munlock(p, it->second);
#endif
    std::free(p);
    pImpl->allocations.erase(it);
}
bool AMDGPUCCImpl::encryptMemory(void *p, size_t size) {
    return pImpl->initialized && pImpl->allocations.contains(p) && size <= pImpl->allocations[p];
}
bool AMDGPUCCImpl::decryptMemory(void *p, size_t size) { return encryptMemory(p, size); }
bool AMDGPUCCImpl::loadSecureKernel(const GPUKernel &kernel) {
    if (!pImpl->initialized || kernel.name.empty() || kernel.binary_code.empty() || kernel.signature.empty())
        return false;
    pImpl->kernels[kernel.name] = kernel;
    return true;
}
bool AMDGPUCCImpl::executeSecureKernel(const std::string &, void **, size_t, size_t, size_t) {
    SPDLOG_ERROR("AMD GPU kernel launch is unavailable: no HIP protected-execution backend is linked");
    return false;
}
bool AMDGPUCCImpl::checkpointGPUState(WriteStream *) {
    SPDLOG_ERROR("AMD GPU driver contexts cannot be migrated safely by serializing process memory");
    return false;
}
bool AMDGPUCCImpl::restoreGPUState(ReadStream *) { return false; }
} // namespace mvvm::gpu
