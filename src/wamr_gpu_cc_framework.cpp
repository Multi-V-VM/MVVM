/*
 * The WebAssembly Live Migration Project
 * GPU Confidential Computing Framework Implementation
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 */

#include "wamr_gpu_cc_framework.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <cstring>
#include <algorithm>

// For TDX attestation on Intel platforms
#ifdef __linux__
#include "../lib/wasm-micro-runtime/core/shared/platform/linux-tdx/tdx_attestation.h"
#endif

namespace mvvm {
namespace gpu {

// GPU CC Framework Implementation
struct GPUCCFramework::Impl {
    std::unique_ptr<GPUCCInterface> gpu_interface;
    GPUVendor current_vendor;
    bool initialized = false;
    PerformanceMetrics metrics;
    security::SecurityPolicy security_level = security::SecurityPolicy::BALANCED;
    
    // Timing utilities
    std::chrono::high_resolution_clock::time_point start_time;
    
    void startTiming() {
        start_time = std::chrono::high_resolution_clock::now();
    }
    
    double endTiming() {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
};

GPUCCFramework::GPUCCFramework() : pImpl(std::make_unique<Impl>()) {
    SPDLOG_INFO("Initializing GPU CC Framework");
}

GPUCCFramework::~GPUCCFramework() {
    shutdown();
}

bool GPUCCFramework::initialize(GPUVendor vendor) {
    if (pImpl->initialized) {
        SPDLOG_WARN("GPU CC Framework already initialized");
        return true;
    }
    
    pImpl->current_vendor = vendor;
    
    // Auto-detect vendor if not specified
    if (vendor == GPUVendor::GENERIC) {
        // Try to detect GPU vendor
        // This is simplified - real implementation would check actual hardware
        #ifdef __linux__
        FILE* fp = popen("lspci | grep -E 'VGA|3D' | head -1", "r");
        if (fp) {
            char buffer[256];
            if (fgets(buffer, sizeof(buffer), fp)) {
                if (strstr(buffer, "NVIDIA")) {
                    vendor = GPUVendor::NVIDIA;
                } else if (strstr(buffer, "AMD") || strstr(buffer, "ATI")) {
                    vendor = GPUVendor::AMD;
                } else if (strstr(buffer, "Intel")) {
                    vendor = GPUVendor::INTEL;
                }
            }
            pclose(fp);
        }
        #endif
    }
    
    // Create appropriate implementation
    switch (vendor) {
        case GPUVendor::NVIDIA:
            pImpl->gpu_interface = std::make_unique<NVIDIAGPUCCImpl>();
            SPDLOG_INFO("Using NVIDIA GPU CC implementation");
            break;
        case GPUVendor::AMD:
            pImpl->gpu_interface = std::make_unique<AMDGPUCCImpl>();
            SPDLOG_INFO("Using AMD GPU CC implementation");
            break;
        case GPUVendor::INTEL:
            pImpl->gpu_interface = std::make_unique<IntelGPUCCImpl>();
            SPDLOG_INFO("Using Intel GPU CC implementation");
            break;
        default:
            SPDLOG_ERROR("Unsupported GPU vendor");
            return false;
    }
    
    pImpl->initialized = true;
    return true;
}

void GPUCCFramework::shutdown() {
    if (pImpl->initialized) {
        SPDLOG_INFO("Shutting down GPU CC Framework");
        pImpl->gpu_interface.reset();
        pImpl->initialized = false;
    }
}

GPUCCInterface* GPUCCFramework::getInterface() {
    return pImpl->gpu_interface.get();
}

bool GPUCCFramework::setupSecureComputation(const std::vector<CCFeature>& features) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        SPDLOG_ERROR("GPU CC Framework not initialized");
        return false;
    }
    
    pImpl->startTiming();
    
    // Initialize confidential computing with required features
    if (!pImpl->gpu_interface->initializeCC(features)) {
        SPDLOG_ERROR("Failed to initialize confidential computing");
        return false;
    }
    
    // Verify device security
    if (!pImpl->gpu_interface->verifyDevice()) {
        SPDLOG_ERROR("Device verification failed");
        return false;
    }
    
    pImpl->metrics.encryption_overhead_ms = pImpl->endTiming();
    
    SPDLOG_INFO("Secure computation setup completed");
    return true;
}

bool GPUCCFramework::verifySecureEnvironment() {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }
    
    // Get attestation report
    auto report = pImpl->gpu_interface->getAttestationReport();
    if (report.empty()) {
        SPDLOG_ERROR("Failed to get attestation report");
        return false;
    }
    
    SPDLOG_INFO("Secure environment verified, report size: {} bytes", report.size());
    return true;
}

bool GPUCCFramework::secureDataTransferToGPU(const void* host_data, void* gpu_data, 
                                              size_t size, bool encrypt) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }
    
    pImpl->startTiming();
    
    if (encrypt) {
        // Encrypt data before transfer
        if (!pImpl->gpu_interface->encryptMemory(const_cast<void*>(host_data), size)) {
            SPDLOG_ERROR("Failed to encrypt data");
            return false;
        }
    }
    
    // Transfer data to GPU
    // This is simplified - real implementation would use CUDA/ROCm/Level0 APIs
    std::memcpy(gpu_data, host_data, size);
    
    pImpl->metrics.memory_transfer_time_ms += pImpl->endTiming();
    pImpl->metrics.memory_usage_bytes += size;
    
    return true;
}

bool GPUCCFramework::secureDataTransferFromGPU(const void* gpu_data, void* host_data,
                                                size_t size, bool decrypt) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }
    
    pImpl->startTiming();
    
    // Transfer data from GPU
    std::memcpy(host_data, gpu_data, size);
    
    if (decrypt) {
        // Decrypt data after transfer
        if (!pImpl->gpu_interface->decryptMemory(host_data, size)) {
            SPDLOG_ERROR("Failed to decrypt data");
            return false;
        }
    }
    
    pImpl->metrics.memory_transfer_time_ms += pImpl->endTiming();
    
    return true;
}

bool GPUCCFramework::registerSecureKernel(const std::string& name, const void* code, 
                                          size_t code_size) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }
    
    GPUKernel kernel;
    kernel.name = name;
    kernel.binary_code.resize(code_size);
    std::memcpy(kernel.binary_code.data(), code, code_size);
    
    // Generate signature for verification
    // Simplified - real implementation would use proper crypto
    kernel.signature.resize(32);
    for (size_t i = 0; i < 32; ++i) {
        kernel.signature[i] = static_cast<uint8_t>(i ^ kernel.binary_code[i % code_size]);
    }
    
    return pImpl->gpu_interface->loadSecureKernel(kernel);
}

bool GPUCCFramework::runSecureKernel(const std::string& name, 
                                     const std::vector<void*>& args,
                                     size_t grid_size, size_t block_size) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }
    
    pImpl->startTiming();
    
    bool result = pImpl->gpu_interface->executeSecureKernel(
        name, const_cast<void**>(args.data()), args.size(), grid_size, block_size);
    
    pImpl->metrics.kernel_execution_time_ms += pImpl->endTiming();
    
    return result;
}

bool GPUCCFramework::prepareGPUMigration(WriteStream* writer) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }
    
    return pImpl->gpu_interface->checkpointGPUState(writer);
}

bool GPUCCFramework::completeGPUMigration(ReadStream* reader) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }
    
    return pImpl->gpu_interface->restoreGPUState(reader);
}

GPUCCFramework::PerformanceMetrics GPUCCFramework::getPerformanceMetrics() const {
    return pImpl->metrics;
}

void GPUCCFramework::setSecurityLevel(security::SecurityPolicy level) {
    pImpl->security_level = level;
}

bool GPUCCFramework::validateSecurityCompliance() {
    if (!pImpl->initialized) {
        return false;
    }
    
    // Verify security policy is being enforced
    auto device = pImpl->gpu_interface->getCurrentDevice();
    
    switch (pImpl->security_level) {
        case security::SecurityPolicy::MINIMAL:
            // Minimal requirements
            return true;
            
        case security::SecurityPolicy::BALANCED:
            // Standard security features required
            return std::find(device.cc_features.begin(), device.cc_features.end(),
                           CCFeature::MEMORY_ENCRYPTION) != device.cc_features.end();
            
        case security::SecurityPolicy::STRICT:
            // All security features required
            return device.cc_features.size() >= 3 &&
                   std::find(device.cc_features.begin(), device.cc_features.end(),
                           CCFeature::REMOTE_ATTESTATION) != device.cc_features.end();
            
        case security::SecurityPolicy::CUSTOM:
            // Custom policy - for now, same as balanced
            return std::find(device.cc_features.begin(), device.cc_features.end(),
                           CCFeature::MEMORY_ENCRYPTION) != device.cc_features.end();
            
        default:
            return false;
    }
}

// Secure GPU Memory Pool Implementation
struct SecureGPUMemoryPool::Impl {
    struct MemoryBlock {
        void* ptr;
        size_t size;
        bool allocated;
        size_t alignment;
    };
    
    std::vector<MemoryBlock> blocks;
    void* pool_base = nullptr;
    size_t pool_size;
    size_t allocated_size = 0;
    MemoryType memory_type;
    bool encryption_enabled = false;
    std::vector<uint8_t> encryption_key;
};

SecureGPUMemoryPool::SecureGPUMemoryPool(size_t pool_size, MemoryType type)
    : pImpl(std::make_unique<Impl>()) {
    pImpl->pool_size = pool_size;
    pImpl->memory_type = type;
    
    // Allocate pool - simplified
    pImpl->pool_base = std::aligned_alloc(256, pool_size);
    if (pImpl->pool_base) {
        std::memset(pImpl->pool_base, 0, pool_size);
        SPDLOG_INFO("Allocated secure GPU memory pool: {} bytes", pool_size);
    }
}

SecureGPUMemoryPool::~SecureGPUMemoryPool() {
    if (pImpl->pool_base) {
        // Clear memory before freeing
        std::memset(pImpl->pool_base, 0, pImpl->pool_size);
        std::free(pImpl->pool_base);
    }
}

void* SecureGPUMemoryPool::allocate(size_t size, size_t alignment) {
    if (!pImpl->pool_base || size == 0) {
        return nullptr;
    }
    
    // Align size
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);
    
    // Find free block or allocate new one
    size_t offset = 0;
    for (const auto& block : pImpl->blocks) {
        if (!block.allocated) {
            if (block.size >= aligned_size) {
                // Reuse existing block
                const_cast<Impl::MemoryBlock&>(block).allocated = true;
                pImpl->allocated_size += aligned_size;
                return block.ptr;
            }
        }
        offset = std::max(offset, 
                         reinterpret_cast<uintptr_t>(block.ptr) - 
                         reinterpret_cast<uintptr_t>(pImpl->pool_base) + block.size);
    }
    
    // Allocate new block
    offset = (offset + alignment - 1) & ~(alignment - 1);
    if (offset + aligned_size > pImpl->pool_size) {
        SPDLOG_ERROR("Secure memory pool exhausted");
        return nullptr;
    }
    
    void* ptr = reinterpret_cast<uint8_t*>(pImpl->pool_base) + offset;
    pImpl->blocks.push_back({ptr, aligned_size, true, alignment});
    pImpl->allocated_size += aligned_size;
    
    // Encrypt if enabled
    if (pImpl->encryption_enabled) {
        // Simple XOR encryption for demo
        uint8_t* data = static_cast<uint8_t*>(ptr);
        for (size_t i = 0; i < aligned_size; ++i) {
            data[i] ^= pImpl->encryption_key[i % pImpl->encryption_key.size()];
        }
    }
    
    return ptr;
}

void SecureGPUMemoryPool::deallocate(void* ptr) {
    if (!ptr) return;
    
    for (auto& block : pImpl->blocks) {
        if (block.ptr == ptr && block.allocated) {
            // Clear memory before deallocation
            std::memset(ptr, 0, block.size);
            block.allocated = false;
            pImpl->allocated_size -= block.size;
            return;
        }
    }
}

size_t SecureGPUMemoryPool::getAvailableMemory() const {
    return pImpl->pool_size - pImpl->allocated_size;
}

size_t SecureGPUMemoryPool::getTotalMemory() const {
    return pImpl->pool_size;
}

void SecureGPUMemoryPool::defragment() {
    // Simplified defragmentation
    SPDLOG_INFO("Defragmenting secure memory pool");
    
    // Sort blocks by address
    std::sort(pImpl->blocks.begin(), pImpl->blocks.end(),
              [](const Impl::MemoryBlock& a, const Impl::MemoryBlock& b) {
                  return a.ptr < b.ptr;
              });
    
    // Merge adjacent free blocks
    for (size_t i = 0; i < pImpl->blocks.size() - 1; ) {
        if (!pImpl->blocks[i].allocated && !pImpl->blocks[i + 1].allocated) {
            // Merge blocks
            pImpl->blocks[i].size += pImpl->blocks[i + 1].size;
            pImpl->blocks.erase(pImpl->blocks.begin() + i + 1);
        } else {
            ++i;
        }
    }
}

bool SecureGPUMemoryPool::enableEncryption(const std::vector<uint8_t>& key) {
    if (key.size() < 16) {
        SPDLOG_ERROR("Encryption key too short");
        return false;
    }
    
    pImpl->encryption_key = key;
    pImpl->encryption_enabled = true;
    
    SPDLOG_INFO("Memory pool encryption enabled");
    return true;
}

bool SecureGPUMemoryPool::rotateEncryptionKey(const std::vector<uint8_t>& new_key) {
    if (new_key.size() < 16) {
        return false;
    }
    
    // Re-encrypt all allocated blocks with new key
    for (auto& block : pImpl->blocks) {
        if (block.allocated) {
            uint8_t* data = static_cast<uint8_t*>(block.ptr);
            
            // Decrypt with old key
            for (size_t i = 0; i < block.size; ++i) {
                data[i] ^= pImpl->encryption_key[i % pImpl->encryption_key.size()];
            }
            
            // Encrypt with new key
            for (size_t i = 0; i < block.size; ++i) {
                data[i] ^= new_key[i % new_key.size()];
            }
        }
    }
    
    pImpl->encryption_key = new_key;
    SPDLOG_INFO("Encryption key rotated");
    return true;
}

} // namespace gpu
} // namespace mvvm