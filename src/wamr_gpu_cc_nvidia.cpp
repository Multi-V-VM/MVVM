/* NVIDIA CC requires NVIDIA's attestation SDK and a CC-enabled GPU/driver.
 * This build has neither a statically linked SDK nor a safe substitute, so it
 * enumerates real devices but never invents an H100 report or mode bit. */
#include "wamr_gpu_cc_framework.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <spdlog/spdlog.h>
namespace mvvm::gpu {
namespace {
std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
        s.pop_back();
    return s;
}
std::string read(const std::filesystem::path &p) {
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    return trim(s);
}
uint64_t num(const std::filesystem::path &p) {
    try {
        auto s = read(p);
        return s.empty() ? 0 : std::stoull(s, nullptr, 0);
    } catch (...) {
        return 0;
    }
}
} // namespace
struct NVIDIAGPUCCImpl::Impl {
    GPUDevice device{};
    bool selected = false;
    std::map<std::string, GPUKernel> kernels;
};
NVIDIAGPUCCImpl::NVIDIAGPUCCImpl() : pImpl(std::make_unique<Impl>()) {}
NVIDIAGPUCCImpl::~NVIDIAGPUCCImpl() = default;
std::vector<GPUDevice> NVIDIAGPUCCImpl::enumerateDevices() {
    std::vector<GPUDevice> out;
#ifdef __linux__
    std::error_code ec;
    for (const auto &e : std::filesystem::directory_iterator("/sys/class/drm", ec)) {
        auto n = e.path().filename().string();
        if (n.rfind("card", 0) != 0 || n.find('-') != std::string::npos)
            continue;
        auto d = e.path() / "device";
        if (read(d / "vendor") != "0x10de")
            continue;
        GPUDevice g{};
        auto id = read(d / "device");
        g.name = "NVIDIA GPU " + (id.empty() ? std::string("unknown") : id);
        g.vendor = GPUVendor::NVIDIA;
        g.device_id = out.size();
        g.total_memory = num(d / "mem_info_vram_total");
        g.available_memory = g.total_memory;
        g.capability = {0, 0, 0, 0, 0, false};
        out.push_back(std::move(g));
    }
#endif
    return out;
}
bool NVIDIAGPUCCImpl::selectDevice(size_t id) {
    auto d = enumerateDevices();
    if (id >= d.size())
        return false;
    pImpl->device = d[id];
    pImpl->selected = true;
    return true;
}
GPUDevice NVIDIAGPUCCImpl::getCurrentDevice() { return pImpl->device; }
bool NVIDIAGPUCCImpl::initializeCC(const std::vector<CCFeature> &) {
    SPDLOG_ERROR("NVIDIA CC is unavailable: build with the NVIDIA attestation SDK and a CC-enabled driver");
    return false;
}
bool NVIDIAGPUCCImpl::verifyDevice() { return false; }
std::vector<uint8_t> NVIDIAGPUCCImpl::getAttestationReport() { return {}; }
void *NVIDIAGPUCCImpl::allocateSecureMemory(size_t, MemoryType) { return nullptr; }
void NVIDIAGPUCCImpl::freeSecureMemory(void *) {}
bool NVIDIAGPUCCImpl::encryptMemory(void *, size_t) { return false; }
bool NVIDIAGPUCCImpl::decryptMemory(void *, size_t) { return false; }
bool NVIDIAGPUCCImpl::loadSecureKernel(const GPUKernel &) { return false; }
bool NVIDIAGPUCCImpl::executeSecureKernel(const std::string &, void **, size_t, size_t, size_t) { return false; }
bool NVIDIAGPUCCImpl::checkpointGPUState(WriteStream *) { return false; }
bool NVIDIAGPUCCImpl::restoreGPUState(ReadStream *) { return false; }
} // namespace mvvm::gpu
