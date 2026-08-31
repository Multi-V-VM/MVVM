/*
 * The WebAssembly Live Migration Project
 * GPU Confidential Computing Abstraction Framework
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#ifndef WAMR_GPU_CC_FRAMEWORK_H
#define WAMR_GPU_CC_FRAMEWORK_H

#include "wamr_security_framework.h"
#include "wamr_type.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declarations
struct WriteStream;
struct ReadStream;

namespace mvvm {
namespace gpu {

// GPU vendor types
enum class GPUVendor { NVIDIA, AMD, INTEL, GENERIC };

using AttestationVerifier =
    std::function<bool(GPUVendor, const std::vector<uint8_t> &, const std::vector<uint8_t> &)>;

// Confidential computing features
enum class CCFeature {
    MEMORY_ENCRYPTION, // Encrypted GPU memory
    SECURE_BOOT, // Secure boot attestation
    REMOTE_ATTESTATION, // Remote attestation support
    SEALED_STORAGE, // Sealed persistent storage
    SECURE_CHANNELS, // Encrypted communication channels
    TRUSTED_EXECUTION // Trusted execution environment
};

// GPU memory types
enum class MemoryType {
    DEVICE_LOCAL, // GPU device memory
    HOST_VISIBLE, // CPU-accessible GPU memory
    UNIFIED, // Unified memory (CPU+GPU)
    SECURE // Encrypted/secure memory
};

// GPU compute capability
struct ComputeCapability {
    int major_version;
    int minor_version;
    size_t max_threads_per_block;
    size_t max_blocks_per_grid;
    size_t shared_memory_per_block;
    bool supports_cc; // Confidential computing support
};

// GPU device information
struct GPUDevice {
    std::string name;
    GPUVendor vendor;
    size_t device_id;
    size_t total_memory;
    size_t available_memory;
    ComputeCapability capability;
    std::vector<CCFeature> cc_features;
};

// Secure GPU context
struct SecureGPUContext {
    GPUDevice device;
    void *secure_context_handle;
    std::vector<uint8_t> attestation_report;
    std::unordered_map<std::string, void *> secure_buffers;
    bool is_verified;
};

// GPU kernel metadata
struct GPUKernel {
    std::string name;
    std::vector<uint8_t> binary_code;
    size_t num_parameters;
    size_t required_shared_memory;
    std::vector<uint8_t> signature; // Code signature for verification
};

// Abstract GPU Confidential Computing Interface
class GPUCCInterface {
public:
    virtual ~GPUCCInterface() = default;

    // Device management
    virtual std::vector<GPUDevice> enumerateDevices() = 0;
    virtual bool selectDevice(size_t device_id) = 0;
    virtual GPUDevice getCurrentDevice() = 0;

    // Confidential computing initialization
    virtual bool initializeCC(const std::vector<CCFeature> &required_features) = 0;
    virtual bool verifyDevice() = 0;
    virtual std::vector<uint8_t> getAttestationReport() = 0;

    // Secure memory management
    virtual void *allocateSecureMemory(size_t size, MemoryType type) = 0;
    virtual void freeSecureMemory(void *ptr) = 0;
    virtual bool encryptMemory(void *ptr, size_t size) = 0;
    virtual bool decryptMemory(void *ptr, size_t size) = 0;

    // Secure kernel execution
    virtual bool loadSecureKernel(const GPUKernel &kernel) = 0;
    virtual bool executeSecureKernel(const std::string &kernel_name, void **args, size_t num_args, size_t grid_size,
                                     size_t block_size) = 0;

    // Migration support
    virtual bool checkpointGPUState(WriteStream *writer) = 0;
    virtual bool restoreGPUState(ReadStream *reader) = 0;
    // Set a 32-byte, per-migration key established with the destination.
    // Implementations consume it after one successful checkpoint or restore.
    virtual bool setMigrationKey(const std::vector<uint8_t> &key) = 0;
    // The verifier must validate vendor signatures/collateral, freshness and policy.
    // The final vector is the SHA-512 snapshot digest bound into report data.
    virtual bool setMigrationAttestationVerifier(AttestationVerifier verifier) = 0;
    // Driver and allocation addresses are recreated, never serialized as live handles.
    virtual void *remapRestoredPointer(uint64_t old_address) const = 0;
};

// NVIDIA Confidential Computing implementation
class NVIDIAGPUCCImpl : public GPUCCInterface {
public:
    NVIDIAGPUCCImpl();
    ~NVIDIAGPUCCImpl() override;

    std::vector<GPUDevice> enumerateDevices() override;
    bool selectDevice(size_t device_id) override;
    GPUDevice getCurrentDevice() override;

    bool initializeCC(const std::vector<CCFeature> &required_features) override;
    bool verifyDevice() override;
    std::vector<uint8_t> getAttestationReport() override;

    void *allocateSecureMemory(size_t size, MemoryType type) override;
    void freeSecureMemory(void *ptr) override;
    bool encryptMemory(void *ptr, size_t size) override;
    bool decryptMemory(void *ptr, size_t size) override;

    bool loadSecureKernel(const GPUKernel &kernel) override;
    bool executeSecureKernel(const std::string &kernel_name, void **args, size_t num_args, size_t grid_size,
                             size_t block_size) override;

    bool checkpointGPUState(WriteStream *writer) override;
    bool restoreGPUState(ReadStream *reader) override;
    bool setMigrationKey(const std::vector<uint8_t> &key) override;
    bool setMigrationAttestationVerifier(AttestationVerifier verifier) override;
    void *remapRestoredPointer(uint64_t old_address) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// AMD Confidential Computing implementation
class AMDGPUCCImpl : public GPUCCInterface {
public:
    AMDGPUCCImpl();
    ~AMDGPUCCImpl() override;

    // Device management
    std::vector<GPUDevice> enumerateDevices() override;
    bool selectDevice(size_t device_id) override;
    GPUDevice getCurrentDevice() override;

    // Confidential computing setup
    bool initializeCC(const std::vector<CCFeature> &required_features) override;
    bool verifyDevice() override;
    std::vector<uint8_t> getAttestationReport() override;

    // Memory management
    void *allocateSecureMemory(size_t size, MemoryType type) override;
    void freeSecureMemory(void *ptr) override;
    bool encryptMemory(void *ptr, size_t size) override;
    bool decryptMemory(void *ptr, size_t size) override;

    // Kernel management
    bool loadSecureKernel(const GPUKernel &kernel) override;
    bool executeSecureKernel(const std::string &kernel_name, void **args, size_t num_args, size_t grid_size,
                             size_t block_size) override;

    // State management
    bool checkpointGPUState(WriteStream *writer) override;
    bool restoreGPUState(ReadStream *reader) override;
    bool setMigrationKey(const std::vector<uint8_t> &key) override;
    bool setMigrationAttestationVerifier(AttestationVerifier verifier) override;
    void *remapRestoredPointer(uint64_t old_address) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Intel Confidential Computing implementation
class IntelGPUCCImpl : public GPUCCInterface {
public:
    IntelGPUCCImpl();
    ~IntelGPUCCImpl() override;

    // Device management
    std::vector<GPUDevice> enumerateDevices() override;
    bool selectDevice(size_t device_id) override;
    GPUDevice getCurrentDevice() override;

    // Confidential computing setup
    bool initializeCC(const std::vector<CCFeature> &required_features) override;
    bool verifyDevice() override;
    std::vector<uint8_t> getAttestationReport() override;

    // Memory management
    void *allocateSecureMemory(size_t size, MemoryType type) override;
    void freeSecureMemory(void *ptr) override;
    bool encryptMemory(void *ptr, size_t size) override;
    bool decryptMemory(void *ptr, size_t size) override;

    // Kernel management
    bool loadSecureKernel(const GPUKernel &kernel) override;
    bool executeSecureKernel(const std::string &kernel_name, void **args, size_t num_args, size_t grid_size,
                             size_t block_size) override;

    // State management
    bool checkpointGPUState(WriteStream *writer) override;
    bool restoreGPUState(ReadStream *reader) override;
    bool setMigrationKey(const std::vector<uint8_t> &key) override;
    bool setMigrationAttestationVerifier(AttestationVerifier verifier) override;
    void *remapRestoredPointer(uint64_t old_address) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// GPU CC Framework main class
class GPUCCFramework {
public:
    GPUCCFramework();
    ~GPUCCFramework();

    // Initialize framework with specific vendor
    bool initialize(GPUVendor vendor = GPUVendor::GENERIC);
    void shutdown();

    // Get GPU interface
    GPUCCInterface *getInterface();

    // High-level secure computation
    bool setupSecureComputation(const std::vector<CCFeature> &features);
    bool verifySecureEnvironment();

    // Secure data transfer
    bool secureDataTransferToGPU(const void *host_data, void *gpu_data, size_t size, bool encrypt = true);
    bool secureDataTransferFromGPU(const void *gpu_data, void *host_data, size_t size, bool decrypt = true);

    // Kernel management
    bool registerSecureKernel(const std::string &name, const void *code, size_t code_size);
    bool runSecureKernel(const std::string &name, const std::vector<void *> &args, size_t grid_size, size_t block_size);

    // Migration support
    bool prepareGPUMigration(WriteStream *writer);
    bool completeGPUMigration(ReadStream *reader);
    bool setMigrationKey(const std::vector<uint8_t> &key);
    bool setMigrationAttestationVerifier(AttestationVerifier verifier);
    void *remapRestoredPointer(uint64_t old_address) const;

    // Performance monitoring
    struct PerformanceMetrics {
        double kernel_execution_time_ms;
        double memory_transfer_time_ms;
        double encryption_overhead_ms;
        size_t memory_usage_bytes;
    };

    PerformanceMetrics getPerformanceMetrics() const;

    // Security policy management
    void setSecurityLevel(security::SecurityPolicy level);
    bool validateSecurityCompliance();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// WebAssembly to GPU kernel translator
class WasmToGPUTranslator {
public:
    WasmToGPUTranslator();
    ~WasmToGPUTranslator();

    // Translation options
    struct TranslationOptions {
        bool optimize_for_cc; // Optimize for confidential computing
        bool enable_vectorization;
        bool enable_shared_memory;
        size_t target_compute_capability;
    };

    // Translate WASM module to GPU kernel
    GPUKernel translateModule(const void *wasm_module, size_t module_size, const TranslationOptions &options);

    // Translate specific function
    GPUKernel translateFunction(const void *wasm_module, size_t module_size, const std::string &function_name,
                                const TranslationOptions &options);

    // Verify translation correctness
    bool verifyTranslation(const GPUKernel &kernel, const void *wasm_module, size_t module_size);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// GPU memory pool for efficient allocation
class SecureGPUMemoryPool {
public:
    SecureGPUMemoryPool(size_t pool_size, MemoryType type);
    ~SecureGPUMemoryPool();

    // Memory allocation from pool
    void *allocate(size_t size, size_t alignment = 256);
    void deallocate(void *ptr);

    // Pool management
    size_t getAvailableMemory() const;
    size_t getTotalMemory() const;
    void defragment();

    // Security features
    bool enableEncryption(const std::vector<uint8_t> &key);
    bool rotateEncryptionKey(const std::vector<uint8_t> &new_key);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// GPU task scheduler for CC workloads
class GPUCCScheduler {
public:
    GPUCCScheduler();
    ~GPUCCScheduler();

    // Task definition
    struct Task {
        std::string kernel_name;
        std::vector<void *> arguments;
        size_t grid_size;
        size_t block_size;
        std::function<void(bool)> completion_callback;
    };

    // Submit task for execution
    void submitTask(const Task &task);

    // Batch execution
    void submitBatch(const std::vector<Task> &tasks);

    // Wait for completion
    void waitForCompletion();
    void waitForTask(const std::string &task_id);

    // Priority scheduling
    void setTaskPriority(const std::string &task_id, int priority);

    // Resource management
    void setMaxConcurrentTasks(size_t max_tasks);
    void setMemoryLimit(size_t memory_bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

} // namespace gpu
} // namespace mvvm

#endif // WAMR_GPU_CC_FRAMEWORK_H
