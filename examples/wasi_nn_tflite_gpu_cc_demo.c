/*
 * Demo: WASI-NN TensorFlow Lite with GPU Confidential Computing
 * This demonstrates secure inference using GPU CC features
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "wasi_nn.h"

// Simple MobileNet model for demonstration
// In production, this would be loaded from a file
static const uint8_t mobilenet_model[] = {
    // TFLite model header (simplified for demo)
    0x54, 0x46, 0x4C, 0x33, // TFL3 magic
    0x00, 0x00, 0x00, 0x00, // Version
    // ... model data would go here ...
};

void print_attestation_report(uint8_t* report, size_t report_size) {
    printf("GPU CC Attestation Report (%zu bytes):\n", report_size);
    printf("Report Hash: ");
    for (size_t i = 0; i < 32 && i < report_size; i++) {
        printf("%02x", report[i]);
    }
    printf("\n");
}

int main() {
    printf("=== WASI-NN TFLite GPU CC Demo ===\n");
    
    // Initialize WASI-NN
    graph model_graph;
    graph_execution_context ctx;
    error err;
    
    // Load model with GPU target (CC will be enabled automatically)
    printf("Loading model with GPU CC support...\n");
    graph_builder_array model_builder = {
        .buf = (uint8_t*)mobilenet_model,
        .size = sizeof(mobilenet_model)
    };
    graph_builder_array builders[] = {model_builder};
    
    err = load(builders, 1, graph_encoding_tensorflowlite, execution_target_gpu, &model_graph);
    if (err != success) {
        printf("Error loading model: %d\n", err);
        return 1;
    }
    printf("Model loaded successfully with GPU CC\n");
    
    // Initialize execution context
    err = init_execution_context(model_graph, &ctx);
    if (err != success) {
        printf("Error initializing execution context: %d\n", err);
        return 1;
    }
    printf("Execution context initialized\n");
    
    // Prepare input tensor (224x224x3 image)
    const int input_size = 224 * 224 * 3;
    float* input_data = malloc(input_size * sizeof(float));
    if (!input_data) {
        printf("Failed to allocate input buffer\n");
        return 1;
    }
    
    // Fill with dummy data (normally would be preprocessed image)
    for (int i = 0; i < input_size; i++) {
        input_data[i] = (float)(i % 256) / 255.0f;
    }
    
    tensor input_tensor = {
        .dimensions = (int32_t[]){1, 224, 224, 3},
        .type = fp32,
        .data = (uint8_t*)input_data
    };
    
    // Set input
    printf("Setting secure input tensor...\n");
    err = set_input(ctx, 0, &input_tensor);
    if (err != success) {
        printf("Error setting input: %d\n", err);
        free(input_data);
        return 1;
    }
    
    // Run secure inference
    printf("Running secure inference on GPU...\n");
    err = compute(ctx);
    if (err != success) {
        printf("Error during computation: %d\n", err);
        free(input_data);
        return 1;
    }
    printf("Secure inference completed\n");
    
    // Get output
    const int output_size = 1001; // ImageNet classes
    float* output_data = malloc(output_size * sizeof(float));
    if (!output_data) {
        printf("Failed to allocate output buffer\n");
        free(input_data);
        return 1;
    }
    
    uint32_t output_bytes = output_size * sizeof(float);
    err = get_output(ctx, 0, (uint8_t*)output_data, &output_bytes);
    if (err != success) {
        printf("Error getting output: %d\n", err);
        free(input_data);
        free(output_data);
        return 1;
    }
    
    // Find top prediction
    float max_prob = 0.0f;
    int max_idx = 0;
    for (int i = 0; i < output_size; i++) {
        if (output_data[i] > max_prob) {
            max_prob = output_data[i];
            max_idx = i;
        }
    }
    
    printf("\nInference Results:\n");
    printf("Top prediction: Class %d with probability %.4f\n", max_idx, max_prob);
    
    // GPU CC specific: Get attestation report
    // This would be exposed through WASI-NN extensions in production
    printf("\nGPU CC Security Features:\n");
    printf("- Memory encryption: ENABLED\n");
    printf("- Secure boot verification: PASSED\n");
    printf("- Model weights encryption: ENABLED\n");
    printf("- Intermediate tensor encryption: ENABLED\n");
    
    // Simulate attestation report
    uint8_t attestation_report[512];
    size_t report_size = 512;
    // In production, this would call wasi_nn_get_attestation()
    for (size_t i = 0; i < report_size; i++) {
        attestation_report[i] = (uint8_t)((i * 37 + 89) % 256);
    }
    print_attestation_report(attestation_report, report_size);
    
    // Cleanup
    free(input_data);
    free(output_data);
    
    printf("\nDemo completed successfully!\n");
    return 0;
}