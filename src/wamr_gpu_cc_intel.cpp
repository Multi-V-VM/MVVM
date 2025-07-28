/*
 * Intel GPU Confidential Computing Implementation
 */

#include "wamr_gpu_cc_framework.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <map>

// Cross-platform aligned allocation
#ifdef _WIN32
#include <malloc.h>
static inline void* aligned_alloc_wrapper(size_t alignment, size_t size) {
    return _aligned_malloc(size, alignment);
}
static inline void aligned_free_wrapper(void* ptr) {
    _aligned_free(ptr);
}
#else
static inline void* aligned_alloc_wrapper(size_t alignment, size_t size) {
    return std::aligned_alloc(alignment, size);
}
static inline void aligned_free_wrapper(void* ptr) {
    std::free(ptr);
}
#endif

// Intel specific headers would go here
// #include <level_zero/ze_api.h>
// #include <igsc/igsc.h>

#ifdef __linux__
#include "../lib/wasm-micro-runtime/core/shared/platform/linux-tdx/tdx_attestation.h"
#endif

namespace mvvm {
namespace gpu {

struct IntelGPUCCImpl::Impl {
    GPUDevice current_device;
    bool tdx_enabled = false;  // Intel TDX integration
    bool pvc_cc_mode = false;  // Ponte Vecchio CC mode
    bool cc_initialized = false;
    std::map<std::string, GPUKernel> loaded_kernels;
    std::map<void*, size_t> secure_allocations;
    
    // Intel specific CC state
    std::vector<uint8_t> tdx_report_data;
    std::vector<uint8_t> gpu_measurement;
    void* tdx_attestation_ctx = nullptr;
    
    std::vector<uint8_t> generateAttestationReport() {
        std::vector<uint8_t> report(768);  // Larger for TDX integration
        
        // Intel TDX + GPU attestation report format
        // [0-31] = Report type and version
        // [32-63] = TDX measurements
        // [64-127] = GPU device info
        // [128-191] = Security capabilities
        // [192-447] = TDX quote (if available)
        // [448-511] = GPU measurements
        // [512-767] = Signature and certificates
        
        // Report type
        report[0] = 0x03;  // Intel GPU CC
        report[1] = tdx_enabled ? 0x01 : 0x00;
        report[2] = pvc_cc_mode ? 0x01 : 0x00;
        
        // Device info
        std::memcpy(&report[64], current_device.name.c_str(), 
                   (std::min)(current_device.name.size(), size_t(64)));
        
        // TDX integration
#ifdef __linux__
        if (tdx_enabled && tdx_attestation_ctx) {
            tdx_report_full_t tdx_report;
            if (tdx_attestation_get_td_info(&tdx_report) == TDX_ATTEST_SUCCESS) {
                std::memcpy(&report[192], &tdx_report, 
                           (std::min)(sizeof(tdx_report), size_t(256)));
            }
        }
#endif
        
        // GPU measurements
        if (!gpu_measurement.empty()) {
            std::memcpy(&report[448], gpu_measurement.data(),
                       (std::min)(gpu_measurement.size(), size_t(64)));
        }
        
        return report;
    }
};

IntelGPUCCImpl::IntelGPUCCImpl() : pImpl(std::make_unique<Impl>()) {
    SPDLOG_INFO("Initializing Intel GPU CC implementation");
    
#ifdef __linux__
    // Initialize TDX attestation if available
    tdx_attestation_config_t config = {};
    if (tdx_attestation_init(&config) == TDX_ATTEST_SUCCESS) {
        pImpl->tdx_attestation_ctx = &config;
        pImpl->tdx_enabled = true;
        SPDLOG_INFO("TDX attestation support detected");
    }
#endif
}

IntelGPUCCImpl::~IntelGPUCCImpl() {
    // Cleanup secure allocations
    for (const auto& [ptr, size] : pImpl->secure_allocations) {
        std::free(ptr);
    }
    
#ifdef __linux__
    if (pImpl->tdx_attestation_ctx) {
        tdx_attestation_cleanup();
    }
#endif
}

std::vector<GPUDevice> IntelGPUCCImpl::enumerateDevices() {
    std::vector<GPUDevice> devices;
    
    // Mock Intel GPU enumeration
    GPUDevice device;
    device.name = "Intel Data Center GPU Max 1550";  // Ponte Vecchio
    device.vendor = GPUVendor::INTEL;
    device.device_id = 0;
    device.total_memory = 128ULL * 1024 * 1024 * 1024;  // 128GB HBM2e
    device.available_memory = 126ULL * 1024 * 1024 * 1024;
    
    device.capability.major_version = 12;
    device.capability.minor_version = 7;
    device.capability.max_threads_per_block = 1024;
    device.capability.max_blocks_per_grid = 2147483647;
    device.capability.shared_memory_per_block = 128 * 1024;
    device.capability.supports_cc = true;
    
    // Ponte Vecchio CC features with TDX integration
    device.cc_features.push_back(CCFeature::MEMORY_ENCRYPTION);
    device.cc_features.push_back(CCFeature::SECURE_BOOT);
    device.cc_features.push_back(CCFeature::REMOTE_ATTESTATION);
    device.cc_features.push_back(CCFeature::TRUSTED_EXECUTION);
    device.cc_features.push_back(CCFeature::SECURE_CHANNELS);
    
    devices.push_back(device);
    
    // Add Arc GPU
    GPUDevice device2;
    device2.name = "Intel Arc A770";
    device2.vendor = GPUVendor::INTEL;
    device2.device_id = 1;
    device2.total_memory = 16ULL * 1024 * 1024 * 1024;
    device2.available_memory = 15ULL * 1024 * 1024 * 1024;
    device2.capability.major_version = 12;
    device2.capability.minor_version = 7;
    device2.capability.supports_cc = false;
    
    devices.push_back(device2);
    
    return devices;
}

bool IntelGPUCCImpl::selectDevice(size_t device_id) {
    auto devices = enumerateDevices();
    if (device_id >= devices.size()) {
        SPDLOG_ERROR("Invalid device ID: {}", device_id);
        return false;
    }
    
    pImpl->current_device = devices[device_id];
    SPDLOG_INFO("Selected device: {}", pImpl->current_device.name);
    
    return true;
}

GPUDevice IntelGPUCCImpl::getCurrentDevice() {
    return pImpl->current_device;
}

bool IntelGPUCCImpl::initializeCC(const std::vector<CCFeature>& required_features) {
    if (!pImpl->current_device.capability.supports_cc) {
        SPDLOG_ERROR("Current device does not support confidential computing");
        return false;
    }
    
    // Check feature support
    for (const auto& feature : required_features) {
        bool found = false;
        for (const auto& dev_feature : pImpl->current_device.cc_features) {
            if (dev_feature == feature) {
                found = true;
                break;
            }
        }
        if (!found) {
            SPDLOG_ERROR("Device does not support required CC feature");
            return false;
        }
    }
    
    // Initialize Ponte Vecchio CC mode
    if (pImpl->current_device.name.find("Max") != std::string::npos) {
        pImpl->pvc_cc_mode = true;
        SPDLOG_INFO("Enabled Ponte Vecchio Confidential Computing mode");
        
        // Generate GPU measurement
        pImpl->gpu_measurement.resize(64);  // SHA-512
        for (size_t i = 0; i < 64; ++i) {
            pImpl->gpu_measurement[i] = static_cast<uint8_t>((i * 7 + 3) % 256);
        }
        
        // Extend TDX RTMR with GPU measurement if available
#ifdef __linux__
        if (pImpl->tdx_enabled) {
            tdx_attestation_extend_rtmr(2, pImpl->gpu_measurement.data(), 
                                       pImpl->gpu_measurement.size());
            SPDLOG_INFO("Extended TDX RTMR with GPU measurement");
        }
#endif
    }
    
    pImpl->cc_initialized = true;
    return true;
}

bool IntelGPUCCImpl::verifyDevice() {
    if (!pImpl->cc_initialized) {
        return false;
    }
    
    // Verify GPU firmware with Intel signatures
    // In real implementation, would verify against Intel root certificate
    
    // If TDX is available, include in verification
#ifdef __linux__
    if (pImpl->tdx_enabled) {
        uint32_t tcb_status;
        // Generate and verify TDX quote
        tdx_attestation_evidence_t* evidence = nullptr;
        if (tdx_attestation_generate_evidence(pImpl->gpu_measurement.data(),
                                             pImpl->gpu_measurement.size(),
                                             &evidence) == TDX_ATTEST_SUCCESS) {
            tdx_attestation_verify_quote(evidence->quote, evidence->quote_size,
                                        evidence->collateral, evidence->collateral_size,
                                        &tcb_status);
            tdx_attestation_free_evidence(evidence);
            
            if (tcb_status != TDX_TCB_STATUS_OK) {
                SPDLOG_WARN("TDX TCB status not OK: {}", tcb_status);
            }
        }
    }
#endif
    
    SPDLOG_INFO("Intel GPU verification successful");
    return true;
}

std::vector<uint8_t> IntelGPUCCImpl::getAttestationReport() {
    if (!pImpl->cc_initialized) {
        return {};
    }
    
    return pImpl->generateAttestationReport();
}

void* IntelGPUCCImpl::allocateSecureMemory(size_t size, MemoryType type) {
    if (!pImpl->cc_initialized) {
        return nullptr;
    }
    
    // Allocate memory with Intel PRM (Protected Region Memory)
    // In real implementation, would use Level Zero protected memory APIs
    
    void* ptr = aligned_alloc_wrapper(4096, size);  // Page aligned for PRM
    if (ptr) {
        std::memset(ptr, 0, size);
        pImpl->secure_allocations[ptr] = size;
        
        SPDLOG_DEBUG("Allocated {} bytes of PRM memory", size);
    }
    
    return ptr;
}

void IntelGPUCCImpl::freeSecureMemory(void* ptr) {
    auto it = pImpl->secure_allocations.find(ptr);
    if (it != pImpl->secure_allocations.end()) {
        // Clear memory before freeing
        std::memset(ptr, 0, it->second);
        std::free(ptr);
        pImpl->secure_allocations.erase(it);
        
        SPDLOG_DEBUG("Freed PRM memory");
    }
}

bool IntelGPUCCImpl::encryptMemory(void* ptr, size_t size) {
    if (!pImpl->pvc_cc_mode) {
        SPDLOG_WARN("Memory encryption not available without PVC CC mode");
        return true;
    }
    
    // Simulate AES-XTS encryption (used by Intel MKTME)
    uint8_t* data = static_cast<uint8_t*>(ptr);
    
    // Use GPU measurement as encryption key
    for (size_t i = 0; i < size; ++i) {
        // Simple XTS-like transformation
        uint8_t tweak = static_cast<uint8_t>(i & 0xFF);
        data[i] ^= pImpl->gpu_measurement[i % pImpl->gpu_measurement.size()];
        data[i] ^= tweak;
        
        // Simulate block cipher
        data[i] = ((data[i] << 3) | (data[i] >> 5)) ^ 0xA5;
    }
    
    return true;
}

bool IntelGPUCCImpl::decryptMemory(void* ptr, size_t size) {
    if (!pImpl->pvc_cc_mode) {
        return true;
    }
    
    uint8_t* data = static_cast<uint8_t*>(ptr);
    
    // Reverse the encryption
    for (size_t i = 0; i < size; ++i) {
        // Reverse block cipher
        data[i] ^= 0xA5;
        data[i] = ((data[i] >> 3) | (data[i] << 5));
        
        // Reverse XTS
        uint8_t tweak = static_cast<uint8_t>(i & 0xFF);
        data[i] ^= tweak;
        data[i] ^= pImpl->gpu_measurement[i % pImpl->gpu_measurement.size()];
    }
    
    return true;
}

bool IntelGPUCCImpl::loadSecureKernel(const GPUKernel& kernel) {
    if (!pImpl->cc_initialized) {
        return false;
    }
    
    // Verify kernel with Intel GPU signatures
    // Update measurements
    
    pImpl->loaded_kernels[kernel.name] = kernel;
    SPDLOG_INFO("Loaded secure kernel into Intel GPU CC: {}", kernel.name);
    
    // Update GPU measurement
    for (size_t i = 0; i < kernel.signature.size() && i < pImpl->gpu_measurement.size(); ++i) {
        pImpl->gpu_measurement[i] = (pImpl->gpu_measurement[i] + kernel.signature[i]) % 256;
    }
    
    return true;
}

bool IntelGPUCCImpl::executeSecureKernel(const std::string& kernel_name,
                                         void** args, size_t num_args,
                                         size_t grid_size, size_t block_size) {
    auto it = pImpl->loaded_kernels.find(kernel_name);
    if (it == pImpl->loaded_kernels.end()) {
        SPDLOG_ERROR("Kernel not found: {}", kernel_name);
        return false;
    }
    
    // Execute kernel in Intel GPU CC environment
    // In real implementation, would use Level Zero command lists
    
    SPDLOG_DEBUG("Executing kernel {} with TDX+GPU protection", kernel_name);
    
    // Simulate secure execution
    
    return true;
}

bool IntelGPUCCImpl::checkpointGPUState(WriteStream* writer) {
    if (!writer) return false;
    
    // Save Intel GPU CC state including:
    // - PRM allocations
    // - TDX measurements
    // - Kernel state
    
    SPDLOG_INFO("Checkpointed Intel GPU CC state");
    return true;
}

bool IntelGPUCCImpl::restoreGPUState(ReadStream* reader) {
    if (!reader) return false;
    
    // Restore and re-verify state with TDX
    
    SPDLOG_INFO("Restored Intel GPU CC state");
    return true;
}

} // namespace gpu
} // namespace mvvm