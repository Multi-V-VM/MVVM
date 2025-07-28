/*
 * The WebAssembly Live Migration Project
 * WASI-NN MVVM Demo Application
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

/*
 * NOTE: This is a WASI-NN demo application that should be compiled to WebAssembly
 * and run inside the WAMR runtime, not as a native application.
 * 
 * To use this demo:
 * 1. Compile this file to WASM using wasi-sdk or emscripten
 * 2. Run the resulting WASM module with WAMR with WASI-NN support enabled
 * 
 * Example compilation:
 * /path/to/wasi-sdk/bin/clang --sysroot=/path/to/wasi-sdk/share/wasi-sysroot \
 *   -O3 -o wasi_nn_mvvm_demo.wasm wasi_nn_mvvm_demo.c
 * 
 * Example execution:
 * iwasm --dir=. wasi_nn_mvvm_demo.wasm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

// Include WASI-NN headers from the runtime
#include "wasi_nn.h"

/* Model data (placeholder) */
static uint8_t mobilenet_model_data[] = {
    /* This would contain actual MobileNet TFLite model data */
    0x54, 0x46, 0x4C, 0x33,  // TFL3 magic
    0x00, 0x00, 0x00, 0x00,  // Version
    /* ... rest of model data ... */
};

/* Simulated input data (normally would be real image data) */
static float input_image_data[1 * 224 * 224 * 3];  // NHWC format

/* Demonstrate MVVM features with WASI-NN */
void demo_inference() {
    printf("\n=== WASI-NN MVVM Demo: Edge AI Inference ===\n");
    
    error err;
    graph model_graph;
    graph_execution_context exec_ctx;
    
    // Prepare model for loading
    graph_builder model_builder = {.buf = mobilenet_model_data, .size = sizeof(mobilenet_model_data)};
    graph_builder_array builder = {.buf = &model_builder, .size = 1};
    
    err = load(&builder, tensorflowlite, cpu, &model_graph);
    if (err != success) {
        printf("Failed to load model: %d\n", err);
        return;
    }
    printf("Model loaded successfully\n");
    
    // Initialize execution context
    err = init_execution_context(model_graph, &exec_ctx);
    if (err != success) {
        printf("Failed to initialize execution context: %d\n", err);
        return;
    }
    printf("Execution context initialized\n");
    
    // Simulate preprocessing an image
    for (int i = 0; i < 224 * 224 * 3; i++) {
        input_image_data[i] = (float)(rand() % 256) / 255.0f;
    }
    
    // Set input tensor
    uint32_t input_dims[] = {1, 224, 224, 3};
    tensor_dimensions input_dimensions = {.buf = input_dims, .size = 4};
    tensor input_tensor = {
        .dimensions = &input_dimensions,
        .type = fp32,
        .data = (uint8_t *)input_image_data
    };
    
    // Set the input
    err = set_input(exec_ctx, 0, &input_tensor);
    if (err != success) {
        printf("Failed to set input: %d\n", err);
        return;
    }
    
    // Run inference
    printf("Running inference...\n");
    err = compute(exec_ctx);
    if (err != success) {
        printf("Failed to compute: %d\n", err);
        return;
    }
    printf("Inference completed\n");
    
    // Get output
    uint8_t output_data[1001 * sizeof(float)];  // 1001 classes for ImageNet
    uint32_t output_size = sizeof(output_data);
    err = get_output(exec_ctx, 0, output_data, &output_size);
    if (err != success) {
        printf("Failed to get output: %d\n", err);
        return;
    }
    
    // Find top prediction
    float *predictions = (float *)output_data;
    int top_class = 0;
    float top_score = predictions[0];
    for (int i = 1; i < 1001; i++) {
        if (predictions[i] > top_score) {
            top_score = predictions[i];
            top_class = i;
        }
    }
    
    printf("Top prediction: class %d with score %.4f\n", top_class, top_score);
}

/* Simulate migration scenarios */
void demo_migration_scenarios() {
    printf("\n=== MVVM Migration Scenarios ===\n");
    
    // These scenarios would be handled by the MVVM runtime
    printf("1. Device-to-Edge Migration:\n");
    printf("   - Low battery detected on IoT device\n");
    printf("   - MVVM migrates inference workload to edge server\n");
    printf("   - Inference continues without interruption\n");
    
    printf("\n2. Edge-to-Cloud Migration:\n");
    printf("   - Complex model requires more compute\n");
    printf("   - MVVM migrates to cloud GPU instance\n");
    printf("   - Leverages GPU acceleration transparently\n");
    
    printf("\n3. Privacy-Aware Migration:\n");
    printf("   - Sensitive data detected in input\n");
    printf("   - MVVM restricts migration to trusted edge nodes\n");
    printf("   - Maintains data privacy requirements\n");
}

int main() {
    printf("WebAssembly Live Migration (MVVM) Demo\n");
    printf("======================================\n");
    
    // Initialize random seed
    srand(time(NULL));
    
    // Run demonstrations
    demo_inference();
    demo_migration_scenarios();
    
    printf("\n=== Demo Complete ===\n");
    return 0;
}