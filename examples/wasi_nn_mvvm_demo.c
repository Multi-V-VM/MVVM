/*
 * The WebAssembly Live Migration Project
 * WASI-NN MVVM Demo Application
 *
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "wasi_nn.h"
#include "wasi_nn_migration.h"
#include "wasi_nn_security.h"
#include "wasi_nn_gpu_cc.h"

/* Model data (placeholder) */
static uint8_t mobilenet_model_data[] = {
    /* This would contain actual MobileNet TFLite model data */
    0x00, 0x01, 0x02, 0x03
};

/* Input image data (placeholder) */
static float input_image_data[224 * 224 * 3];

/* Helper function to measure time */
static double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

/* Demo 1: Basic inference with migration */
static void demo_inference_with_migration() {
    printf("\n=== Demo 1: Inference with Migration ===\n");
    
    error err;
    graph model_graph;
    graph_execution_context exec_ctx;
    
    /* Initialize migration support */
    err = wasi_nn_migration_init(WASI_NN_MIGRATION_INCREMENTAL, 
                                WASI_NN_SECURITY_BASIC);
    if (err != success) {
        printf("Failed to initialize migration: %d\n", err);
        return;
    }
    
    /* Load model */
    graph_builder_array builder = {
        .buf = (graph_builder*)&mobilenet_model_data,
        .size = sizeof(mobilenet_model_data)
    };
    
    err = load(&builder, tensorflowlite, cpu, &model_graph);
    if (err != success) {
        printf("Failed to load model: %d\n", err);
        return;
    }
    printf("Model loaded successfully\n");
    
    /* Create execution context */
    err = init_execution_context(model_graph, &exec_ctx);
    if (err != success) {
        printf("Failed to create execution context: %d\n", err);
        return;
    }
    
    /* Prepare input tensor */
    tensor input_tensor = {
        .dimensions = (uint32_t[]){1, 224, 224, 3},
        .type = fp32,
        .data = (uint8_t*)input_image_data,
        .size = sizeof(input_image_data)
    };
    
    /* Generate random input data */
    for (int i = 0; i < 224 * 224 * 3; i++) {
        input_image_data[i] = (float)rand() / RAND_MAX;
    }
    
    /* Set input */
    err = set_input(exec_ctx, 0, &input_tensor);
    if (err != success) {
        printf("Failed to set input: %d\n", err);
        return;
    }
    
    /* Run inference */
    double start_time = get_time_ms();
    err = compute(exec_ctx);
    double inference_time = get_time_ms() - start_time;
    
    if (err != success) {
        printf("Failed to compute: %d\n", err);
        return;
    }
    printf("Inference completed in %.2f ms\n", inference_time);
    
    /* Create checkpoint */
    wasi_nn_checkpoint *checkpoint;
    start_time = get_time_ms();
    err = wasi_nn_checkpoint_create(&checkpoint);
    double checkpoint_time = get_time_ms() - start_time;
    
    if (err != success) {
        printf("Failed to create checkpoint: %d\n", err);
        return;
    }
    printf("Checkpoint created in %.2f ms, size: %zu bytes\n", 
           checkpoint_time, checkpoint->state_size);
    
    /* Simulate migration by restoring checkpoint */
    start_time = get_time_ms();
    err = wasi_nn_checkpoint_restore(checkpoint);
    double restore_time = get_time_ms() - start_time;
    
    if (err != success) {
        printf("Failed to restore checkpoint: %d\n", err);
        wasi_nn_checkpoint_free(checkpoint);
        return;
    }
    printf("Checkpoint restored in %.2f ms\n", restore_time);
    
    /* Get migration statistics */
    wasi_nn_migration_stats stats;
    wasi_nn_get_migration_stats(&stats);
    printf("Migration stats:\n");
    printf("  Data transferred: %zu bytes\n", stats.data_transferred_bytes);
    printf("  Compression ratio: %.2f\n", stats.compression_ratio);
    printf("  Dirty tensors: %u\n", stats.dirty_tensors_count);
    
    /* Cleanup */
    wasi_nn_checkpoint_free(checkpoint);
}

/* Demo 2: Secure inference */
static void demo_secure_inference() {
    printf("\n=== Demo 2: Secure Inference ===\n");
    
    error err;
    
    /* Initialize security framework */
    err = wasi_nn_security_init(WASI_NN_AUTH_PSK, WASI_NN_CRYPTO_AES_256_GCM);
    if (err != success) {
        printf("Failed to initialize security: %d\n", err);
        return;
    }
    
    /* Authenticate peer */
    uint8_t psk[] = "my_pre_shared_key";
    err = wasi_nn_authenticate_peer("demo_peer", psk, sizeof(psk));
    if (err != success) {
        printf("Failed to authenticate peer: %d\n", err);
        return;
    }
    printf("Peer authenticated successfully\n");
    
    /* Create security context */
    wasi_nn_security_context *sec_ctx;
    err = wasi_nn_create_security_context("demo_peer", &sec_ctx);
    if (err != success) {
        printf("Failed to create security context: %d\n", err);
        return;
    }
    printf("Security context created: %s\n", sec_ctx->session_id);
    
    /* Create test tensor */
    float test_data[10] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
    tensor test_tensor = {
        .dimensions = (uint32_t[]){1, 10},
        .type = fp32,
        .data = (uint8_t*)test_data,
        .size = sizeof(test_data)
    };
    
    /* Encrypt tensor */
    uint8_t *encrypted_data;
    size_t encrypted_size;
    double start_time = get_time_ms();
    err = wasi_nn_encrypt_tensor(sec_ctx, &test_tensor, 
                                &encrypted_data, &encrypted_size);
    double encrypt_time = get_time_ms() - start_time;
    
    if (err != success) {
        printf("Failed to encrypt tensor: %d\n", err);
        return;
    }
    printf("Tensor encrypted in %.2f ms, size: %zu bytes\n", 
           encrypt_time, encrypted_size);
    
    /* Decrypt tensor */
    float decrypted_data[10];
    tensor decrypted_tensor = {
        .dimensions = (uint32_t[]){1, 10},
        .type = fp32,
        .data = (uint8_t*)decrypted_data,
        .size = sizeof(decrypted_data)
    };
    
    start_time = get_time_ms();
    err = wasi_nn_decrypt_tensor(sec_ctx, encrypted_data, encrypted_size,
                                &decrypted_tensor);
    double decrypt_time = get_time_ms() - start_time;
    
    if (err != success) {
        printf("Failed to decrypt tensor: %d\n", err);
        free(encrypted_data);
        return;
    }
    printf("Tensor decrypted in %.2f ms\n", decrypt_time);
    
    /* Verify decryption */
    int match = 1;
    for (int i = 0; i < 10; i++) {
        if (test_data[i] != decrypted_data[i]) {
            match = 0;
            break;
        }
    }
    printf("Decryption verification: %s\n", match ? "PASSED" : "FAILED");
    
    /* Generate attestation report */
    wasi_nn_attestation_report *report;
    err = wasi_nn_generate_attestation(0, &report);
    if (err != success) {
        printf("Failed to generate attestation: %d\n", err);
        free(encrypted_data);
        return;
    }
    printf("Attestation report generated\n");
    
    /* Verify attestation */
    err = wasi_nn_verify_attestation(report, NULL);
    if (err != success) {
        printf("Attestation verification failed: %d\n", err);
    } else {
        printf("Attestation verified successfully\n");
    }
    
    /* Cleanup */
    free(encrypted_data);
    free(report);
}

/* Demo 3: GPU Confidential Computing */
static void demo_gpu_cc() {
    printf("\n=== Demo 3: GPU Confidential Computing ===\n");
    
    error err;
    wasi_nn_gpu_context *gpu_ctx;
    
    /* Initialize GPU CC */
    uint32_t required_features = WASI_NN_CC_MEMORY_ENCRYPTION | 
                                WASI_NN_CC_TRUSTED_EXECUTION;
    err = wasi_nn_gpu_cc_init(WASI_NN_GPU_VENDOR_NVIDIA, 
                             required_features, &gpu_ctx);
    if (err != success) {
        printf("Failed to initialize GPU CC: %d\n", err);
        return;
    }
    
    printf("GPU CC initialized on device: %s\n", gpu_ctx->device.name);
    printf("  Vendor: %d\n", gpu_ctx->device.vendor);
    printf("  Total memory: %lu MB\n", gpu_ctx->device.total_memory / (1024*1024));
    printf("  CC features: 0x%x\n", gpu_ctx->device.cc_features);
    
    /* Verify device */
    err = wasi_nn_gpu_verify_device(gpu_ctx);
    if (err != success) {
        printf("GPU device verification failed: %d\n", err);
        wasi_nn_gpu_cleanup(gpu_ctx);
        return;
    }
    printf("GPU device verified\n");
    
    /* Allocate secure GPU memory */
    size_t tensor_size = 1024 * 1024; /* 1MB */
    void *gpu_mem;
    err = wasi_nn_gpu_alloc_secure_memory(gpu_ctx, tensor_size,
                                         WASI_NN_GPU_MEM_SECURE, &gpu_mem);
    if (err != success) {
        printf("Failed to allocate GPU memory: %d\n", err);
        wasi_nn_gpu_cleanup(gpu_ctx);
        return;
    }
    printf("Allocated %zu bytes of secure GPU memory\n", tensor_size);
    
    /* Create test tensor */
    float *test_data = malloc(tensor_size);
    for (size_t i = 0; i < tensor_size/sizeof(float); i++) {
        test_data[i] = (float)i;
    }
    
    tensor test_tensor = {
        .dimensions = (uint32_t[]){1024, 256},
        .type = fp32,
        .data = (uint8_t*)test_data,
        .size = tensor_size
    };
    
    /* Transfer to GPU with encryption */
    double start_time = get_time_ms();
    err = wasi_nn_gpu_secure_transfer_to(gpu_ctx, &test_tensor, gpu_mem, true);
    double transfer_time = get_time_ms() - start_time;
    
    if (err != success) {
        printf("Failed to transfer to GPU: %d\n", err);
        free(test_data);
        wasi_nn_gpu_free_secure_memory(gpu_ctx, gpu_mem);
        wasi_nn_gpu_cleanup(gpu_ctx);
        return;
    }
    printf("Secure transfer to GPU completed in %.2f ms\n", transfer_time);
    
    /* Simulate GPU computation */
    start_time = get_time_ms();
    err = wasi_nn_gpu_compute_secure(gpu_ctx, 0);
    double compute_time = get_time_ms() - start_time;
    
    if (err != success) {
        printf("GPU computation failed: %d\n", err);
    } else {
        printf("GPU computation completed in %.2f ms\n", compute_time);
    }
    
    /* Get attestation report */
    uint8_t *attestation;
    size_t attestation_size;
    err = wasi_nn_gpu_get_attestation(gpu_ctx, &attestation, &attestation_size);
    if (err != success) {
        printf("Failed to get GPU attestation: %d\n", err);
    } else {
        printf("GPU attestation report: %zu bytes\n", attestation_size);
    }
    
    /* Cleanup */
    free(test_data);
    wasi_nn_gpu_free_secure_memory(gpu_ctx, gpu_mem);
    wasi_nn_gpu_cleanup(gpu_ctx);
}

int main(int argc, char *argv[]) {
    printf("WASI-NN MVVM Demo Application\n");
    printf("=============================\n");
    
    /* Seed random number generator */
    srand(time(NULL));
    
    /* Run demos */
    demo_inference_with_migration();
    demo_secure_inference();
    demo_gpu_cc();
    
    printf("\nAll demos completed successfully!\n");
    return 0;
}