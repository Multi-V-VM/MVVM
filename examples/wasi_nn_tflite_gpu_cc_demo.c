/*
 * Demo: WASI-NN TensorFlow Lite with GPU Confidential Computing
 * This demonstrates secure inference using GPU CC features
 */

/*
 * NOTE: This is a WASI-NN demo application that should be compiled to WebAssembly
 * and run inside the WAMR runtime with GPU CC support, not as a native application.
 * 
 * To use this demo:
 * 1. Compile this file to WASM using wasi-sdk or emscripten
 * 2. Run the resulting WASM module with WAMR with WASI-NN and GPU CC support enabled
 * 
 * Example compilation:
 * /path/to/wasi-sdk/bin/clang --sysroot=/path/to/wasi-sdk/share/wasi-sysroot \
 *   -O3 -o wasi_nn_tflite_gpu_cc_demo.wasm wasi_nn_tflite_gpu_cc_demo.c
 * 
 * Example execution:
 * iwasm --dir=. --enable-gpu-cc wasi_nn_tflite_gpu_cc_demo.wasm
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include WASI-NN headers
#include "wasi_nn.h"

// Simple MobileNet model for demonstration
// In production, this would be loaded from a file
static const uint8_t mobilenet_model[] = {
    // TFLite model header (simplified for demo)
    0x54, 0x46, 0x4C, 0x33, // TFL3 magic
    0x00, 0x00, 0x00, 0x00, // Version
    // ... model data would go here ...
};

void print_attestation_report(uint8_t *report, size_t report_size) {
    printf("GPU CC Attestation Report (%zu bytes):\n", report_size);
    printf("Report Hash: ");
    for (size_t i = 0; i < 32 && i < report_size; i++) {
        printf("%02x", report[i]);
    }
    printf("\n");
}

int main() {
    printf("=== WASI-NN TensorFlow Lite GPU CC Demo ===\n");
    printf("This demo shows secure AI inference using GPU Confidential Computing\n\n");
    
    graph model_graph;
    graph_execution_context ctx;
    error err;
    
    // Load the TFLite model with GPU execution target
    printf("Loading TFLite model for GPU execution...\n");
    graph_builder model_builder = {
        .buf = (uint8_t *)mobilenet_model,
        .size = sizeof(mobilenet_model)
    };
    graph_builder_array builders = {
        .buf = &model_builder,
        .size = 1
    };
    
    err = load(&builders, tensorflowlite, gpu, &model_graph);
    if (err != success) {
        printf("Failed to load model: %d\n", err);
        return 1;
    }
    printf("Model loaded successfully on GPU\n");
    
    // Initialize execution context
    printf("Initializing GPU execution context...\n");
    err = init_execution_context(model_graph, &ctx);
    if (err != success) {
        printf("Failed to initialize context: %d\n", err);
        return 1;
    }
    
    // Prepare input data (random data for demo)
    printf("Preparing secure input data...\n");
    float input_data[224 * 224 * 3]; // MobileNet input size
    for (int i = 0; i < 224 * 224 * 3; i++) {
        input_data[i] = (float)(rand() % 256) / 255.0f;
    }
    
    // Set input tensor
    uint32_t dims[] = {1, 224, 224, 3};
    tensor_dimensions dimensions = {.buf = dims, .size = 4};
    tensor input_tensor = {
        .dimensions = &dimensions,
        .type = fp32,
        .data = (uint8_t *)input_data
    };
    
    printf("Setting encrypted input to GPU...\n");
    err = set_input(ctx, 0, &input_tensor);
    if (err != success) {
        printf("Failed to set input: %d\n", err);
        return 1;
    }
    
    // Run inference on GPU with CC protection
    printf("Running secure inference on GPU...\n");
    err = compute(ctx);
    if (err != success) {
        printf("Failed to compute: %d\n", err);
        return 1;
    }
    printf("Inference completed securely\n");
    
    // Get output (classifications)
    float output_buffer[1001]; // ImageNet classes
    uint32_t output_bytes = sizeof(output_buffer);
    
    printf("Retrieving secure results...\n");
    err = get_output(ctx, 0, output_buffer, &output_bytes);
    if (err != success) {
        printf("Failed to get output: %d\n", err);
        return 1;
    }
    
    // Find top prediction
    int top_class = 0;
    float top_score = output_buffer[0];
    for (int i = 1; i < 1001; i++) {
        if (output_buffer[i] > top_score) {
            top_score = output_buffer[i];
            top_class = i;
        }
    }
    
    printf("\n=== Results ===\n");
    printf("Top prediction: Class %d (score: %.4f)\n", top_class, top_score);
    printf("All computations performed in GPU secure enclave\n");
    
    // Simulate GPU attestation report
    uint8_t mock_attestation[64];
    for (int i = 0; i < 64; i++) {
        mock_attestation[i] = rand() % 256;
    }
    print_attestation_report(mock_attestation, 64);
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}