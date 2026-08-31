/*
 * The WebAssembly Live Migration Project
 * GPU Confidential Computing Framework Implementation
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 */

#include "wamr_gpu_cc_framework.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <openssl/evp.h>
#include <spdlog/spdlog.h>

// Cross-platform aligned allocation
#ifdef _WIN32
#include <malloc.h>
static inline void *aligned_alloc_wrapper(size_t alignment, size_t size) { return _aligned_malloc(size, alignment); }
static inline void aligned_free_wrapper(void *ptr) { _aligned_free(ptr); }
#else
static inline void *aligned_alloc_wrapper(size_t alignment, size_t size) { return std::aligned_alloc(alignment, size); }
static inline void aligned_free_wrapper(void *ptr) { std::free(ptr); }
#endif

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
    security::SecurityPolicy security_level = security::SecurityPolicy::POLICY_BALANCED;

    // Timing utilities
    std::chrono::high_resolution_clock::time_point start_time;

    void startTiming() { start_time = std::chrono::high_resolution_clock::now(); }

    double endTiming() {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
};

GPUCCFramework::GPUCCFramework() : pImpl(std::make_unique<Impl>()) { SPDLOG_INFO("Initializing GPU CC Framework"); }

GPUCCFramework::~GPUCCFramework() { shutdown(); }

bool GPUCCFramework::initialize(GPUVendor vendor) {
    if (pImpl->initialized) {
        SPDLOG_WARN("GPU CC Framework already initialized");
        return true;
    }

    // Auto-detect from the kernel's DRM inventory.  Do not shell out to lspci:
    // it is absent in many guests and its output is not an API.
    if (vendor == GPUVendor::GENERIC) {
        NVIDIAGPUCCImpl nvidia;
        AMDGPUCCImpl amd;
        IntelGPUCCImpl intel;
        if (!nvidia.enumerateDevices().empty())
            vendor = GPUVendor::NVIDIA;
        else if (!amd.enumerateDevices().empty())
            vendor = GPUVendor::AMD;
        else if (!intel.enumerateDevices().empty())
            vendor = GPUVendor::INTEL;
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
        SPDLOG_ERROR("No supported physical GPU was found");
        return false;
    }
    pImpl->current_vendor = vendor;
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

GPUCCInterface *GPUCCFramework::getInterface() { return pImpl->gpu_interface.get(); }

bool GPUCCFramework::setupSecureComputation(const std::vector<CCFeature> &features) {
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

    SPDLOG_INFO("GPU runtime initialized and nonce-bound local attestation evidence acquired; remote vendor "
                "collateral verification is still required");
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

    // A TDREPORT/SNP report is evidence, not a verification result.  It must
    // be checked against vendor collateral and an application policy by the
    // relying party.  Returning true here would turn an unverified blob into
    // a security decision.
    SPDLOG_WARN("Received {} bytes of attestation evidence; external collateral verification is required",
                report.size());
    return false;
}

bool GPUCCFramework::secureDataTransferToGPU(const void *host_data, void *gpu_data, size_t size, bool encrypt) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }

    pImpl->startTiming();

    if ((!host_data && size) || (!gpu_data && size))
        return false;
    if (encrypt) {
        const auto device = pImpl->gpu_interface->getCurrentDevice();
        if (std::find(device.cc_features.begin(), device.cc_features.end(), CCFeature::MEMORY_ENCRYPTION) ==
            device.cc_features.end()) {
            SPDLOG_ERROR("Refusing a confidential GPU transfer: this backend has no verified GPU-memory "
                         "encryption capability");
            return false;
        }
    }
    // This abstraction has host pointers, not CUDA/HIP/Level-Zero handles.
    // Copy only into a buffer registered by the platform implementation; no
    // caller-owned const buffer is ever encrypted in place.
    std::memcpy(gpu_data, host_data, size);
    if (encrypt && !pImpl->gpu_interface->encryptMemory(gpu_data, size))
        return false;

    pImpl->metrics.memory_transfer_time_ms += pImpl->endTiming();
    pImpl->metrics.memory_usage_bytes += size;

    return true;
}

bool GPUCCFramework::secureDataTransferFromGPU(const void *gpu_data, void *host_data, size_t size, bool decrypt) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }

    pImpl->startTiming();

    if ((!host_data && size) || (!gpu_data && size))
        return false;
    if (decrypt) {
        const auto device = pImpl->gpu_interface->getCurrentDevice();
        if (std::find(device.cc_features.begin(), device.cc_features.end(), CCFeature::MEMORY_ENCRYPTION) ==
            device.cc_features.end()) {
            SPDLOG_ERROR("Refusing a confidential GPU transfer: this backend has no verified GPU-memory "
                         "encryption capability");
            return false;
        }
    }
    if (decrypt && !pImpl->gpu_interface->decryptMemory(const_cast<void *>(gpu_data), size))
        return false;
    std::memcpy(host_data, gpu_data, size);

    pImpl->metrics.memory_transfer_time_ms += pImpl->endTiming();

    return true;
}

bool GPUCCFramework::registerSecureKernel(const std::string &name, const void *code, size_t code_size) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }

    if (name.empty() || !code || code_size == 0)
        return false;
    GPUKernel kernel;
    kernel.name = name;
    kernel.binary_code.resize(code_size);
    std::memcpy(kernel.binary_code.data(), code, code_size);

    // The API has no signing key/certificate parameter.  Store an actual
    // SHA-256 measurement rather than labelling fabricated bytes a signature.
    kernel.signature.resize(32);
    EVP_MD_CTX *digest = EVP_MD_CTX_new();
    unsigned int digest_size = 0;
    const bool measured = digest && EVP_DigestInit_ex(digest, EVP_sha256(), nullptr) == 1 &&
                          EVP_DigestUpdate(digest, code, code_size) == 1 &&
                          EVP_DigestFinal_ex(digest, kernel.signature.data(), &digest_size) == 1 &&
                          digest_size == kernel.signature.size();
    EVP_MD_CTX_free(digest);
    if (!measured)
        return false;

    return pImpl->gpu_interface->loadSecureKernel(kernel);
}

bool GPUCCFramework::runSecureKernel(const std::string &name, const std::vector<void *> &args, size_t grid_size,
                                     size_t block_size) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }

    pImpl->startTiming();

    bool result = pImpl->gpu_interface->executeSecureKernel(name, const_cast<void **>(args.data()), args.size(),
                                                            grid_size, block_size);

    pImpl->metrics.kernel_execution_time_ms += pImpl->endTiming();

    return result;
}

bool GPUCCFramework::prepareGPUMigration(WriteStream *writer) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }

    return pImpl->gpu_interface->checkpointGPUState(writer);
}

bool GPUCCFramework::completeGPUMigration(ReadStream *reader) {
    if (!pImpl->initialized || !pImpl->gpu_interface) {
        return false;
    }

    return pImpl->gpu_interface->restoreGPUState(reader);
}

bool GPUCCFramework::setMigrationKey(const std::vector<uint8_t> &key) {
    return pImpl->initialized && pImpl->gpu_interface && pImpl->gpu_interface->setMigrationKey(key);
}

bool GPUCCFramework::setMigrationAttestationVerifier(AttestationVerifier verifier) {
    return pImpl->initialized && pImpl->gpu_interface &&
           pImpl->gpu_interface->setMigrationAttestationVerifier(std::move(verifier));
}

void *GPUCCFramework::remapRestoredPointer(uint64_t old_address) const {
    return pImpl->initialized && pImpl->gpu_interface
               ? pImpl->gpu_interface->remapRestoredPointer(old_address)
               : nullptr;
}

GPUCCFramework::PerformanceMetrics GPUCCFramework::getPerformanceMetrics() const { return pImpl->metrics; }

void GPUCCFramework::setSecurityLevel(security::SecurityPolicy level) { pImpl->security_level = level; }

bool GPUCCFramework::validateSecurityCompliance() {
    if (!pImpl->initialized) {
        return false;
    }

    // Verify security policy is being enforced
    auto device = pImpl->gpu_interface->getCurrentDevice();

    switch (pImpl->security_level) {
    case security::SecurityPolicy::POLICY_MINIMAL:
        // Minimal requirements
        return true;

    case security::SecurityPolicy::POLICY_BALANCED:
        // Standard security features required
        return std::find(device.cc_features.begin(), device.cc_features.end(), CCFeature::MEMORY_ENCRYPTION) !=
               device.cc_features.end();

    case security::SecurityPolicy::POLICY_STRICT:
        // All security features required
        return device.cc_features.size() >= 3 && std::find(device.cc_features.begin(), device.cc_features.end(),
                                                           CCFeature::REMOTE_ATTESTATION) != device.cc_features.end();

    case security::SecurityPolicy::POLICY_CUSTOM:
        // Custom policy - for now, same as balanced
        return std::find(device.cc_features.begin(), device.cc_features.end(), CCFeature::MEMORY_ENCRYPTION) !=
               device.cc_features.end();

    default:
        return false;
    }
}

// Secure GPU Memory Pool Implementation
struct SecureGPUMemoryPool::Impl {
    struct MemoryBlock {
        void *ptr;
        size_t size;
        bool allocated;
        size_t alignment;
    };

    std::vector<MemoryBlock> blocks;
    void *pool_base = nullptr;
    size_t pool_size;
    size_t allocated_size = 0;
    MemoryType memory_type;
};

SecureGPUMemoryPool::SecureGPUMemoryPool(size_t pool_size, MemoryType type) : pImpl(std::make_unique<Impl>()) {
    pImpl->pool_size = pool_size;
    pImpl->memory_type = type;

    // A CPU-addressable allocation cannot become protected GPU memory.  That
    // mode requires one of the vendor backends, so fail closed here.
    if (type == MemoryType::SECURE) {
        SPDLOG_ERROR("SecureGPUMemoryPool requires a vendor protected-memory backend");
        return;
    }
    const size_t allocation_size = (pool_size + 255) & ~size_t(255);
    pImpl->pool_size = allocation_size;
    pImpl->pool_base = aligned_alloc_wrapper(256, allocation_size);
    if (pImpl->pool_base) {
        std::memset(pImpl->pool_base, 0, allocation_size);
        SPDLOG_INFO("Allocated GPU memory pool: {} bytes", allocation_size);
    }
}

SecureGPUMemoryPool::~SecureGPUMemoryPool() {
    if (pImpl->pool_base) {
        // Clear memory before freeing
        std::memset(pImpl->pool_base, 0, pImpl->pool_size);
        std::free(pImpl->pool_base);
    }
}

void *SecureGPUMemoryPool::allocate(size_t size, size_t alignment) {
    if (!pImpl->pool_base || size == 0) {
        return nullptr;
    }

    // Align size
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

    // Find free block or allocate new one
    size_t offset = 0;
    for (const auto &block : pImpl->blocks) {
        if (!block.allocated) {
            if (block.size >= aligned_size) {
                // Reuse existing block
                const_cast<Impl::MemoryBlock &>(block).allocated = true;
                pImpl->allocated_size += aligned_size;
                return block.ptr;
            }
        }
        offset = (std::max)(offset, reinterpret_cast<uintptr_t>(block.ptr) -
                                        reinterpret_cast<uintptr_t>(pImpl->pool_base) + block.size);
    }

    // Allocate new block
    offset = (offset + alignment - 1) & ~(alignment - 1);
    if (offset + aligned_size > pImpl->pool_size) {
        SPDLOG_ERROR("Secure memory pool exhausted");
        return nullptr;
    }

    void *ptr = reinterpret_cast<uint8_t *>(pImpl->pool_base) + offset;
    pImpl->blocks.push_back({ptr, aligned_size, true, alignment});
    pImpl->allocated_size += aligned_size;

    return ptr;
}

void SecureGPUMemoryPool::deallocate(void *ptr) {
    if (!ptr)
        return;

    for (auto &block : pImpl->blocks) {
        if (block.ptr == ptr && block.allocated) {
            // Clear memory before deallocation
            std::memset(ptr, 0, block.size);
            block.allocated = false;
            pImpl->allocated_size -= block.size;
            return;
        }
    }
}

size_t SecureGPUMemoryPool::getAvailableMemory() const { return pImpl->pool_size - pImpl->allocated_size; }

size_t SecureGPUMemoryPool::getTotalMemory() const { return pImpl->pool_size; }

void SecureGPUMemoryPool::defragment() {
    // Simplified defragmentation
    SPDLOG_INFO("Defragmenting secure memory pool");

    // Sort blocks by address
    std::sort(pImpl->blocks.begin(), pImpl->blocks.end(),
              [](const Impl::MemoryBlock &a, const Impl::MemoryBlock &b) { return a.ptr < b.ptr; });

    // Merge adjacent free blocks
    for (size_t i = 0; i + 1 < pImpl->blocks.size();) {
        if (!pImpl->blocks[i].allocated && !pImpl->blocks[i + 1].allocated) {
            // Merge blocks
            pImpl->blocks[i].size += pImpl->blocks[i + 1].size;
            pImpl->blocks.erase(pImpl->blocks.begin() + i + 1);
        } else {
            ++i;
        }
    }
}

bool SecureGPUMemoryPool::enableEncryption(const std::vector<uint8_t> &key) {
    (void)key;
    SPDLOG_ERROR(
        "Software pool encryption is unsupported: raw pointers cannot provide authenticated encrypted-at-rest storage");
    return false;
}

bool SecureGPUMemoryPool::rotateEncryptionKey(const std::vector<uint8_t> &new_key) {
    (void)new_key;
    return false;
}

} // namespace gpu
} // namespace mvvm
