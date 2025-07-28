/*
 * WASI-NN TensorFlow Lite GPU Confidential Computing Integration
 * This header provides GPU CC support for WASI-NN TFLite backend
 */

#ifndef WASI_NN_TFLITE_GPU_CC_H
#define WASI_NN_TFLITE_GPU_CC_H

#include "wamr_gpu_cc_framework.h"
#include <memory>
#include <tensorflow/lite/delegates/gpu/delegate.h>

namespace mvvm {
namespace wasi_nn {

// GPU CC configuration for TFLite
struct TFLiteGPUCCConfig {
    gpu::GPUVendor vendor = gpu::GPUVendor::GENERIC;
    std::vector<gpu::CCFeature> required_features = {gpu::CCFeature::MEMORY_ENCRYPTION, gpu::CCFeature::SECURE_BOOT,
                                                     gpu::CCFeature::REMOTE_ATTESTATION};
    bool enable_secure_inference = true;
    bool encrypt_model_weights = true;
    bool encrypt_intermediate_tensors = true;
    size_t secure_memory_pool_size = 512 * 1024 * 1024; // 512MB default
};

// Extended TFLite GPU delegate with CC support
class TFLiteGPUCCDelegate {
public:
    TFLiteGPUCCDelegate();
    ~TFLiteGPUCCDelegate();

    // Initialize GPU CC for TFLite
    bool initialize(const TFLiteGPUCCConfig &config);

    // Create TFLite delegate with CC enabled
    TfLiteDelegate *createDelegate();

    // Secure model loading with encryption
    bool loadSecureModel(const uint8_t *model_data, size_t model_size, void **secure_model_ptr,
                         size_t *secure_model_size);

    // Free secure model memory
    void freeSecureModel(void *secure_model_ptr);

    // Get attestation report for secure inference
    std::vector<uint8_t> getAttestationReport();

    // Enable/disable secure tensor encryption
    void setTensorEncryption(bool enable) { encrypt_tensors_ = enable; }

    // Get performance metrics
    struct PerformanceMetrics {
        double encryption_overhead_ms;
        double cc_initialization_ms;
        double secure_inference_ms;
        size_t encrypted_memory_usage;
    };
    PerformanceMetrics getMetrics() const { return metrics_; }

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;

    bool initialized_ = false;
    bool encrypt_tensors_ = true;
    PerformanceMetrics metrics_ = {};
};

// C API for integration with existing WASI-NN code
extern "C" {

// Initialize GPU CC for TFLite backend
void *wasi_nn_tflite_gpu_cc_create();

// Destroy GPU CC context
void wasi_nn_tflite_gpu_cc_destroy(void *gpu_cc_ctx);

// Initialize GPU CC with vendor and features
int wasi_nn_tflite_gpu_cc_initialize(void *gpu_cc_ctx, int vendor, bool enable_secure_inference);

// Create GPU delegate with CC support
void *wasi_nn_tflite_gpu_cc_create_delegate(void *gpu_cc_ctx);

// Load model securely with encryption
int wasi_nn_tflite_gpu_cc_load_secure_model(void *gpu_cc_ctx, const uint8_t *model_data, size_t model_size,
                                            void **secure_model_ptr, size_t *secure_model_size);

// Free secure model
void wasi_nn_tflite_gpu_cc_free_secure_model(void *gpu_cc_ctx, void *secure_model_ptr);

// Get attestation report
int wasi_nn_tflite_gpu_cc_get_attestation(void *gpu_cc_ctx, uint8_t *report_buffer, size_t *report_size);

} // extern "C"

} // namespace wasi_nn
} // namespace mvvm

#endif // WASI_NN_TFLITE_GPU_CC_H