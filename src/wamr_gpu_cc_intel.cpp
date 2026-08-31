/* Intel GPU workloads running in a TDX guest.  TDREPORTs come directly from
 * the Linux tdx-guest driver; quote generation/verification is intentionally
 * left to a DCAP relying party instead of manufacturing a quote. */
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
#ifdef __linux__
#include <fcntl.h>
#include <linux/tdx-guest.h>
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
#else
bool tdReport(const std::vector<uint8_t> &, std::vector<uint8_t> &) { return false; }
#endif
bool feature(const GPUDevice &d, CCFeature f) {
    return std::find(d.cc_features.begin(), d.cc_features.end(), f) != d.cc_features.end();
}
} // namespace
struct IntelGPUCCImpl::Impl {
    GPUDevice device{};
    bool selected = false, initialized = false;
    std::vector<uint8_t> report;
    std::map<void *, size_t> allocations;
    std::map<std::string, GPUKernel> kernels;
};
IntelGPUCCImpl::IntelGPUCCImpl() : pImpl(std::make_unique<Impl>()) {}
IntelGPUCCImpl::~IntelGPUCCImpl() {
    for (const auto &[p, n] : pImpl->allocations) {
        OPENSSL_cleanse(p, n);
#ifdef __linux__
        munlock(p, n);
#endif
        std::free(p);
    }
}
std::vector<GPUDevice> IntelGPUCCImpl::enumerateDevices() {
    std::vector<GPUDevice> out;
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
            g.cc_features = {CCFeature::MEMORY_ENCRYPTION, CCFeature::REMOTE_ATTESTATION};
        }
        out.push_back(std::move(g));
    }
#endif
    return out;
}
bool IntelGPUCCImpl::selectDevice(size_t id) {
    auto d = enumerateDevices();
    if (id >= d.size())
        return false;
    pImpl->device = d[id];
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
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !tdReport(nonce, pImpl->report)) {
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
    if (RAND_bytes(nonce.data(), nonce.size()) != 1 || !tdReport(nonce, r))
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
#ifdef __linux__
    munlock(p, i->second);
#endif
    std::free(p);
    pImpl->allocations.erase(i);
}
bool IntelGPUCCImpl::encryptMemory(void *p, size_t n) {
    return pImpl->initialized && pImpl->allocations.contains(p) && n <= pImpl->allocations[p];
}
bool IntelGPUCCImpl::decryptMemory(void *p, size_t n) { return encryptMemory(p, n); }
bool IntelGPUCCImpl::loadSecureKernel(const GPUKernel &k) {
    if (!pImpl->initialized || k.name.empty() || k.binary_code.empty() || k.signature.empty())
        return false;
    pImpl->kernels[k.name] = k;
    return true;
}
bool IntelGPUCCImpl::executeSecureKernel(const std::string &, void **, size_t, size_t, size_t) {
    SPDLOG_ERROR("Intel secure GPU dispatch is unavailable: no Level Zero protected-execution backend is linked");
    return false;
}
bool IntelGPUCCImpl::checkpointGPUState(WriteStream *) {
    SPDLOG_ERROR("Intel GPU driver contexts cannot be safely checkpointed by this interface");
    return false;
}
bool IntelGPUCCImpl::restoreGPUState(ReadStream *) { return false; }
} // namespace mvvm::gpu
