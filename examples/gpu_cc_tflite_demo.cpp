/*
 * GPU Confidential Computing with TensorFlow Lite Integration Demo
 * 
 * This example demonstrates how the GPU CC framework integrates with
 * WASI-NN TensorFlow Lite to provide secure AI inference on GPUs.
 */

#include <iostream>
#include <vector>
#include <memory>
#include <cstring>

// Include the GPU CC framework headers
#include "../include/wamr_gpu_cc_framework.h"
#include "../include/wamr_wasi_nn_tflite_gpu_cc.h"

// WASI-NN headers
extern "C" {
#include "wasi_nn.h"
}

// Forward declaration of TensorFlow Lite integration
extern "C" {
    error tensorflowlite_load(void* tflite_ctx, graph_builder_array* builder, 
                             graph_encoding encoding, execution_target target, graph* g);
    error tensorflowlite_init_execution_context(void* tflite_ctx, graph g, 
                                               graph_execution_context* ctx);
    error tensorflowlite_set_input(void* tflite_ctx, graph_execution_context ctx,
                                  uint32_t index, tensor* input_tensor);
    error tensorflowlite_compute(void* tflite_ctx, graph_execution_context ctx);
    error tensorflowlite_get_output(void* tflite_ctx, graph_execution_context ctx,
                                   uint32_t index, tensor_data output_tensor,
                                   uint32_t* output_tensor_size);
    void tensorflowlite_initialize(void** tflite_ctx);
    void tensorflowlite_destroy(void* tflite_ctx);
}

using namespace mvvm;

class SecureMLInferenceDemo {
private:
    std::unique_ptr<gpu::GPUCCFramework> gpu_framework;
    std::unique_ptr<wasi_nn::TFLiteGPUCCDelegate> tflite_delegate;
    void* tflite_context = nullptr;
    
public:
    SecureMLInferenceDemo() {
        std::cout << "=== GPU Confidential Computing ML Inference Demo ===" << std::endl;
    }
    
    ~SecureMLInferenceDemo() {
        if (tflite_context) {
            tensorflowlite_destroy(tflite_context);
        }
    }
    
    bool initialize() {
        // Initialize GPU CC Framework
        gpu_framework = std::make_unique<gpu::GPUCCFramework>();
        
        // Auto-detect GPU vendor
        if (!gpu_framework->initialize(gpu::GPUVendor::GENERIC)) {
            std::cerr << "Failed to initialize GPU CC framework" << std::endl;
            return false;
        }
        
        // Get available GPUs
        auto* gpu_interface = gpu_framework->getInterface();
        auto devices = gpu_interface->enumerateDevices();
        
        std::cout << "\nAvailable GPUs:" << std::endl;
        for (size_t i = 0; i < devices.size(); ++i) {
            std::cout << "  [" << i << "] " << devices[i].name 
                     << " (CC: " << (devices[i].capability.supports_cc ? "Yes" : "No") 
                     << ")" << std::endl;
        }
        
        // Select first CC-capable GPU
        size_t selected_device = 0;
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].capability.supports_cc) {
                selected_device = i;
                break;
            }
        }
        
        if (!gpu_interface->selectDevice(selected_device)) {
            std::cerr << "Failed to select GPU device" << std::endl;
            return false;
        }
        
        std::cout << "\nSelected GPU: " << devices[selected_device].name << std::endl;
        
        // Setup secure computation with required features
        std::vector<gpu::CCFeature> required_features = {
            gpu::CCFeature::MEMORY_ENCRYPTION,
            gpu::CCFeature::REMOTE_ATTESTATION,
            gpu::CCFeature::TRUSTED_EXECUTION
        };
        
        if (!gpu_framework->setupSecureComputation(required_features)) {
            std::cerr << "Failed to setup secure computation" << std::endl;
            return false;
        }
        
        // Verify secure environment
        if (!gpu_framework->verifySecureEnvironment()) {
            std::cerr << "Failed to verify secure environment" << std::endl;
            return false;
        }
        
        std::cout << "Secure GPU environment verified" << std::endl;
        
        // Get attestation report
        auto attestation_report = gpu_interface->getAttestationReport();
        std::cout << "Attestation report generated: " << attestation_report.size() 
                 << " bytes" << std::endl;
        
        // Initialize TensorFlow Lite with GPU CC
        tflite_delegate = std::make_unique<wasi_nn::TFLiteGPUCCDelegate>();
        
        wasi_nn::TFLiteGPUCCConfig tflite_config;
        tflite_config.vendor = static_cast<gpu::GPUVendor>(
            gpu_interface->getCurrentDevice().vendor);
        tflite_config.enable_secure_inference = true;
        tflite_config.encrypt_model_weights = true;
        tflite_config.secure_memory_pool_size = 256 * 1024 * 1024; // 256MB
        
        if (!tflite_delegate->initialize(tflite_config)) {
            std::cerr << "Failed to initialize TFLite GPU CC delegate" << std::endl;
            return false;
        }
        
        // Initialize WASI-NN TensorFlow Lite context
        tensorflowlite_initialize(&tflite_context);
        
        return true;
    }
    
    bool loadSecureModel(const std::vector<uint8_t>& model_data) {
        std::cout << "\nLoading secure ML model..." << std::endl;
        
        // Load model into secure memory
        void* secure_model_ptr = nullptr;
        size_t secure_model_size = 0;
        
        if (!tflite_delegate->loadSecureModel(model_data.data(), model_data.size(),
                                             &secure_model_ptr, &secure_model_size)) {
            std::cerr << "Failed to load model into secure memory" << std::endl;
            return false;
        }
        
        std::cout << "Model loaded into secure memory: " << secure_model_size 
                 << " bytes" << std::endl;
        
        // Create WASI-NN graph from secure model
        graph_builder_array builder;
        graph_builder builder_data;
        builder_data.buf = static_cast<uint8_t*>(secure_model_ptr);
        builder_data.size = secure_model_size;
        builder.buf = &builder_data;
        builder.size = 1;
        
        graph model_graph;
        error result = tensorflowlite_load(tflite_context, &builder, 
                                         tensorflowlite, ::gpu, &model_graph);
        if (result != success) {
            std::cerr << "Failed to load TFLite model" << std::endl;
            tflite_delegate->freeSecureModel(secure_model_ptr);
            return false;
        }
        
        // Initialize execution context
        graph_execution_context exec_ctx;
        result = tensorflowlite_init_execution_context(tflite_context, 
                                                      model_graph, &exec_ctx);
        if (result != success) {
            std::cerr << "Failed to initialize execution context" << std::endl;
            tflite_delegate->freeSecureModel(secure_model_ptr);
            return false;
        }
        
        std::cout << "Model loaded successfully with GPU CC enabled" << std::endl;
        
        // Clean up secure model memory
        tflite_delegate->freeSecureModel(secure_model_ptr);
        
        return true;
    }
    
    void demonstrateSecureInference() {
        std::cout << "\n=== Secure Inference Demo ===" << std::endl;
        
        // Create a dummy model (MobileNet-like structure)
        std::vector<uint8_t> dummy_model = createDummyModel();
        
        if (!loadSecureModel(dummy_model)) {
            std::cerr << "Failed to load secure model" << std::endl;
            return;
        }
        
        // Demonstrate secure memory operations
        demonstrateSecureMemory();
        
        // Show performance metrics
        showPerformanceMetrics();
        
        // Generate final attestation
        generateFinalAttestation();
    }
    
private:
    std::vector<uint8_t> createDummyModel() {
        // Create a minimal valid TFLite model
        // In real use, this would be an actual model file
        std::vector<uint8_t> model(1024 * 1024); // 1MB dummy model
        
        // TFLite file identifier
        const char* identifier = "TFL3";
        std::memcpy(model.data(), identifier, 4);
        
        // Fill with dummy data
        for (size_t i = 4; i < model.size(); ++i) {
            model[i] = static_cast<uint8_t>(i % 256);
        }
        
        return model;
    }
    
    void demonstrateSecureMemory() {
        std::cout << "\n--- Secure Memory Operations ---" << std::endl;
        
        // Create secure memory pool
        gpu::SecureGPUMemoryPool secure_pool(64 * 1024 * 1024, gpu::MemoryType::SECURE);
        
        // Enable encryption
        std::vector<uint8_t> encryption_key(32);
        for (size_t i = 0; i < 32; ++i) {
            encryption_key[i] = static_cast<uint8_t>(i ^ 0xAA);
        }
        secure_pool.enableEncryption(encryption_key);
        
        // Allocate secure memory
        void* secure_buffer = secure_pool.allocate(1024 * 1024, 256);
        if (secure_buffer) {
            std::cout << "Allocated 1MB of encrypted GPU memory" << std::endl;
            
            // Simulate secure data transfer
            std::vector<uint8_t> test_data(1024);
            for (size_t i = 0; i < test_data.size(); ++i) {
                test_data[i] = static_cast<uint8_t>(i);
            }
            
            if (gpu_framework->secureDataTransferToGPU(test_data.data(), 
                                                       secure_buffer, 
                                                       test_data.size(), true)) {
                std::cout << "Secure data transfer to GPU completed" << std::endl;
            }
            
            // Clean up
            secure_pool.deallocate(secure_buffer);
        }
        
        std::cout << "Available secure memory: " << secure_pool.getAvailableMemory() 
                 << " bytes" << std::endl;
    }
    
    void showPerformanceMetrics() {
        std::cout << "\n--- Performance Metrics ---" << std::endl;
        
        auto metrics = gpu_framework->getPerformanceMetrics();
        std::cout << "Kernel execution time: " << metrics.kernel_execution_time_ms << " ms" << std::endl;
        std::cout << "Memory transfer time: " << metrics.memory_transfer_time_ms << " ms" << std::endl;
        std::cout << "Encryption overhead: " << metrics.encryption_overhead_ms << " ms" << std::endl;
        std::cout << "Memory usage: " << metrics.memory_usage_bytes << " bytes" << std::endl;
        
        auto tflite_metrics = tflite_delegate->getMetrics();
        std::cout << "TFLite CC initialization: " << tflite_metrics.cc_initialization_ms << " ms" << std::endl;
        std::cout << "Encrypted memory usage: " << tflite_metrics.encrypted_memory_usage << " bytes" << std::endl;
    }
    
    void generateFinalAttestation() {
        std::cout << "\n--- Final Attestation ---" << std::endl;
        
        // Get comprehensive attestation report
        auto gpu_report = gpu_framework->getInterface()->getAttestationReport();
        auto tflite_report = tflite_delegate->getAttestationReport();
        
        std::cout << "GPU attestation report: " << gpu_report.size() << " bytes" << std::endl;
        std::cout << "TFLite attestation report: " << tflite_report.size() << " bytes" << std::endl;
        
        // Combine reports
        std::vector<uint8_t> combined_report;
        combined_report.insert(combined_report.end(), gpu_report.begin(), gpu_report.end());
        combined_report.insert(combined_report.end(), tflite_report.begin(), tflite_report.end());
        
        std::cout << "Combined attestation report: " << combined_report.size() << " bytes" << std::endl;
        std::cout << "\nSecure ML inference environment fully attested!" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    SecureMLInferenceDemo demo;
    
    if (!demo.initialize()) {
        std::cerr << "Failed to initialize demo" << std::endl;
        return 1;
    }
    
    demo.demonstrateSecureInference();
    
    std::cout << "\n=== Demo completed successfully ===" << std::endl;
    return 0;
}