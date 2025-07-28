/*
 * WASI-NN TensorFlow Lite GPU Confidential Computing Implementation
 */

#include "wamr_wasi_nn_tflite_gpu_cc.h"
#include <chrono>
#include <cstring>
#include <spdlog/spdlog.h>

namespace mvvm {
namespace wasi_nn {

struct TFLiteGPUCCDelegate::Impl {
    std::unique_ptr<gpu::GPUCCFramework> gpu_cc_framework;
    std::unique_ptr<gpu::SecureGPUMemoryPool> secure_memory_pool;
    TFLiteGPUCCConfig config;
    TfLiteGpuDelegateOptionsV2 tflite_gpu_options;
    TfLiteDelegate *delegate = nullptr;

    // Timing utilities
    std::chrono::high_resolution_clock::time_point start_time;

    void startTiming() { start_time = std::chrono::high_resolution_clock::now(); }

    double endTiming() {
        auto end_time = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end_time - start_time).count();
    }
};

TFLiteGPUCCDelegate::TFLiteGPUCCDelegate() : pImpl(std::make_unique<Impl>()) {
    pImpl->gpu_cc_framework = std::make_unique<gpu::GPUCCFramework>();
    // Initialize TFLite GPU options with defaults
    pImpl->tflite_gpu_options = TfLiteGpuDelegateOptionsV2Default();
}

TFLiteGPUCCDelegate::~TFLiteGPUCCDelegate() {
    if (pImpl->delegate) {
        TfLiteGpuDelegateV2Delete(pImpl->delegate);
    }
}

bool TFLiteGPUCCDelegate::initialize(const TFLiteGPUCCConfig &config) {
    if (initialized_) {
        SPDLOG_WARN("GPU CC already initialized");
        return true;
    }

    pImpl->startTiming();
    pImpl->config = config;

    // Initialize GPU CC framework
    if (!pImpl->gpu_cc_framework->initialize(config.vendor)) {
        SPDLOG_ERROR("Failed to initialize GPU CC framework");
        return false;
    }

    // Setup secure computation with required features
    if (!pImpl->gpu_cc_framework->setupSecureComputation(config.required_features)) {
        SPDLOG_ERROR("Failed to setup secure computation");
        return false;
    }

    // Verify secure environment
    if (!pImpl->gpu_cc_framework->verifySecureEnvironment()) {
        SPDLOG_ERROR("Failed to verify secure environment");
        return false;
    }

    // Initialize secure memory pool
    pImpl->secure_memory_pool =
        std::make_unique<gpu::SecureGPUMemoryPool>(config.secure_memory_pool_size, gpu::MemoryType::SECURE);

    // Configure TFLite GPU options for secure execution
    if (config.enable_secure_inference) {
        // Use sustained speed for consistent secure execution
        pImpl->tflite_gpu_options.inference_preference = TFLITE_GPU_INFERENCE_PREFERENCE_SUSTAINED_SPEED;

        // Set priority for secure execution
        pImpl->tflite_gpu_options.inference_priority1 = TFLITE_GPU_INFERENCE_PRIORITY_MIN_MEMORY_USAGE;
        pImpl->tflite_gpu_options.inference_priority2 = TFLITE_GPU_INFERENCE_PRIORITY_MIN_LATENCY;
    }

    metrics_.cc_initialization_ms = pImpl->endTiming();
    initialized_ = true;

    SPDLOG_INFO("TFLite GPU CC initialized successfully");
    return true;
}

TfLiteDelegate *TFLiteGPUCCDelegate::createDelegate() {
    if (!initialized_) {
        SPDLOG_ERROR("GPU CC not initialized");
        return nullptr;
    }

    // Create GPU delegate with CC-enabled options
    pImpl->delegate = TfLiteGpuDelegateV2Create(&pImpl->tflite_gpu_options);
    if (!pImpl->delegate) {
        SPDLOG_ERROR("Failed to create GPU delegate");
        return nullptr;
    }

    SPDLOG_INFO("Created TFLite GPU delegate with CC support");
    return pImpl->delegate;
}

bool TFLiteGPUCCDelegate::loadSecureModel(const uint8_t *model_data, size_t model_size, void **secure_model_ptr,
                                          size_t *secure_model_size) {
    if (!initialized_) {
        SPDLOG_ERROR("GPU CC not initialized");
        return false;
    }

    pImpl->startTiming();

    // Allocate secure memory for model
    void *secure_buffer = pImpl->secure_memory_pool->allocate(model_size, 256);
    if (!secure_buffer) {
        SPDLOG_ERROR("Failed to allocate secure memory for model");
        return false;
    }

    // Copy model to secure memory
    std::memcpy(secure_buffer, model_data, model_size);

    // Encrypt model weights if enabled
    if (pImpl->config.encrypt_model_weights) {
        if (!pImpl->gpu_cc_framework->getInterface()->encryptMemory(secure_buffer, model_size)) {
            SPDLOG_ERROR("Failed to encrypt model weights");
            pImpl->secure_memory_pool->deallocate(secure_buffer);
            return false;
        }

        metrics_.encrypted_memory_usage += model_size;
    }

    *secure_model_ptr = secure_buffer;
    *secure_model_size = model_size;

    metrics_.encryption_overhead_ms += pImpl->endTiming();

    SPDLOG_INFO("Loaded secure model: {} bytes", model_size);
    return true;
}

void TFLiteGPUCCDelegate::freeSecureModel(void *secure_model_ptr) {
    if (secure_model_ptr) {
        pImpl->secure_memory_pool->deallocate(secure_model_ptr);
        SPDLOG_INFO("Freed secure model memory");
    }
}

std::vector<uint8_t> TFLiteGPUCCDelegate::getAttestationReport() {
    if (!initialized_) {
        SPDLOG_ERROR("GPU CC not initialized");
        return {};
    }

    auto report = pImpl->gpu_cc_framework->getInterface()->getAttestationReport();
    SPDLOG_INFO("Generated attestation report: {} bytes", report.size());
    return report;
}

// C API Implementation

extern "C" {

void *wasi_nn_tflite_gpu_cc_create() { return new TFLiteGPUCCDelegate(); }

void wasi_nn_tflite_gpu_cc_destroy(void *gpu_cc_ctx) {
    if (gpu_cc_ctx) {
        delete static_cast<TFLiteGPUCCDelegate *>(gpu_cc_ctx);
    }
}

int wasi_nn_tflite_gpu_cc_initialize(void *gpu_cc_ctx, int vendor, bool enable_secure_inference) {
    if (!gpu_cc_ctx) {
        return -1;
    }

    auto *delegate = static_cast<TFLiteGPUCCDelegate *>(gpu_cc_ctx);

    TFLiteGPUCCConfig config;
    config.vendor = static_cast<gpu::GPUVendor>(vendor);
    config.enable_secure_inference = enable_secure_inference;

    return delegate->initialize(config) ? 0 : -1;
}

void *wasi_nn_tflite_gpu_cc_create_delegate(void *gpu_cc_ctx) {
    if (!gpu_cc_ctx) {
        return nullptr;
    }

    auto *delegate = static_cast<TFLiteGPUCCDelegate *>(gpu_cc_ctx);
    return delegate->createDelegate();
}

int wasi_nn_tflite_gpu_cc_load_secure_model(void *gpu_cc_ctx, const uint8_t *model_data, size_t model_size,
                                            void **secure_model_ptr, size_t *secure_model_size) {
    if (!gpu_cc_ctx || !model_data || !secure_model_ptr || !secure_model_size) {
        return -1;
    }

    auto *delegate = static_cast<TFLiteGPUCCDelegate *>(gpu_cc_ctx);
    return delegate->loadSecureModel(model_data, model_size, secure_model_ptr, secure_model_size) ? 0 : -1;
}

void wasi_nn_tflite_gpu_cc_free_secure_model(void *gpu_cc_ctx, void *secure_model_ptr) {
    if (!gpu_cc_ctx) {
        return;
    }

    auto *delegate = static_cast<TFLiteGPUCCDelegate *>(gpu_cc_ctx);
    delegate->freeSecureModel(secure_model_ptr);
}

int wasi_nn_tflite_gpu_cc_get_attestation(void *gpu_cc_ctx, uint8_t *report_buffer, size_t *report_size) {
    if (!gpu_cc_ctx || !report_buffer || !report_size) {
        return -1;
    }

    auto *delegate = static_cast<TFLiteGPUCCDelegate *>(gpu_cc_ctx);
    auto report = delegate->getAttestationReport();

    if (report.empty()) {
        return -1;
    }

    if (*report_size < report.size()) {
        *report_size = report.size();
        return -2; // Buffer too small
    }

    std::memcpy(report_buffer, report.data(), report.size());
    *report_size = report.size();

    return 0;
}

} // extern "C"

} // namespace wasi_nn
} // namespace mvvm