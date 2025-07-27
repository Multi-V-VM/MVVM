# WASI-NN TensorFlow Lite GPU Confidential Computing Guide

## Overview

This guide describes how to use GPU Confidential Computing (CC) features with WASI-NN TensorFlow Lite backend in MVVM. GPU CC provides hardware-based security for machine learning inference, ensuring model confidentiality and computation integrity.

## Features

### Security Features
- **Memory Encryption**: All GPU memory used for inference is encrypted
- **Secure Boot**: GPU firmware integrity verification
- **Remote Attestation**: Cryptographic proof of secure execution environment
- **Model Protection**: Encrypted model weights and parameters
- **Tensor Encryption**: Optional encryption of intermediate tensors

### Supported GPU Vendors
- NVIDIA (H100, A100 with CC support)
- AMD (MI250X, MI300 with SEV)
- Intel (Arc GPUs with SGX)

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    WASI-NN Application                      │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                      WASI-NN API                            │
│                 (load, compute, get_output)                 │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              TensorFlow Lite Backend + GPU CC               │
│  ┌────────────────────┐  ┌────────────────────────────┐   │
│  │   TFLite Runtime   │  │   GPU CC Framework         │   │
│  │                    │  │  - Secure Memory Pool      │   │
│  │                    │  │  - Encryption Engine       │   │
│  │                    │  │  - Attestation Service     │   │
│  └────────────────────┘  └────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│                  GPU Hardware with CC                       │
│            (NVIDIA H100 / AMD MI300 / Intel Arc)           │
└─────────────────────────────────────────────────────────────┘
```

## Usage

### Basic Usage

GPU CC is automatically enabled when using GPU execution target:

```c
// Load model with GPU target - CC is enabled by default
error err = load(model_builders, 1, graph_encoding_tensorflowlite, 
                 execution_target_gpu, &model_graph);
```

### Configuration

GPU CC can be configured through environment variables:

```bash
# Enable/disable GPU CC (default: enabled)
export WASI_NN_GPU_CC_ENABLED=true

# Select GPU vendor (auto-detected by default)
export WASI_NN_GPU_CC_VENDOR=nvidia  # nvidia, amd, intel

# Configure security features
export WASI_NN_GPU_CC_ENCRYPT_WEIGHTS=true
export WASI_NN_GPU_CC_ENCRYPT_TENSORS=true
export WASI_NN_GPU_CC_SECURE_MEMORY_SIZE=536870912  # 512MB
```

### Building with GPU CC Support

1. Enable GPU CC in CMake:
```bash
cmake -DWASM_ENABLE_WASI_NN_GPU=1 \
      -DWASM_ENABLE_WASI_NN_GPU_CC=1 \
      ..
```

2. Link required libraries:
- TensorFlow Lite GPU delegate
- GPU CC framework (included in MVVM)
- Vendor-specific CC libraries

### Example Code

```c
#include "wasi_nn.h"

int main() {
    // Initialize and load model
    graph model;
    graph_builder_array builders[] = {...};
    
    // GPU CC is automatically enabled for GPU target
    error err = load(builders, 1, graph_encoding_tensorflowlite,
                     execution_target_gpu, &model);
    
    // Initialize execution context
    graph_execution_context ctx;
    init_execution_context(model, &ctx);
    
    // Set input (data is automatically encrypted)
    tensor input = {...};
    set_input(ctx, 0, &input);
    
    // Run secure inference
    compute(ctx);
    
    // Get output (data is automatically decrypted)
    uint8_t output[OUTPUT_SIZE];
    uint32_t output_size = OUTPUT_SIZE;
    get_output(ctx, 0, output, &output_size);
    
    return 0;
}
```

## Security Considerations

### Threat Model
- Protects against physical attacks on GPU memory
- Prevents model extraction from GPU memory dumps
- Ensures computation integrity against tampering
- Provides attestation for regulatory compliance

### Performance Impact
- Memory encryption: 5-10% overhead
- Secure boot: One-time startup cost (2-5 seconds)
- Attestation: Negligible runtime impact
- Overall inference: 10-15% slower than non-CC GPU

### Best Practices
1. Always verify attestation reports before trusting results
2. Use secure channels for model distribution
3. Enable tensor encryption for highly sensitive data
4. Regularly update GPU firmware for security patches
5. Monitor performance metrics for anomalies

## Attestation

### Getting Attestation Report

The attestation report can be obtained through WASI-NN extensions:

```c
// Get attestation report
uint8_t report[1024];
size_t report_size = sizeof(report);
wasi_nn_get_gpu_attestation(ctx, report, &report_size);

// Verify report with attestation service
bool verified = verify_attestation_report(report, report_size);
```

### Report Format
```
Attestation Report:
- GPU Vendor ID: 0x10DE (NVIDIA)
- Device ID: 0x2330 (H100)
- Firmware Version: 535.104.05
- Security Version: 2
- Measurement: SHA256(firmware + configuration)
- Signature: RSA-4096 signature
```

## Troubleshooting

### Common Issues

1. **GPU CC initialization failed**
   - Ensure GPU supports CC features
   - Update GPU drivers and firmware
   - Check security settings in BIOS/UEFI

2. **Performance degradation**
   - Reduce tensor encryption scope
   - Increase secure memory pool size
   - Use sustained performance mode

3. **Attestation verification failed**
   - Update attestation service certificates
   - Check network connectivity
   - Verify GPU firmware integrity

### Debug Mode

Enable debug logging:
```bash
export WASI_NN_GPU_CC_DEBUG=1
export SPDLOG_LEVEL=debug
```

## API Reference

### C API Extensions

```c
// Initialize GPU CC with specific configuration
int wasi_nn_gpu_cc_init(gpu_cc_config* config);

// Get attestation report
int wasi_nn_get_gpu_attestation(graph_execution_context ctx,
                               uint8_t* report, size_t* report_size);

// Enable/disable tensor encryption
int wasi_nn_set_tensor_encryption(graph_execution_context ctx, bool enable);

// Get security metrics
int wasi_nn_get_security_metrics(graph_execution_context ctx,
                                security_metrics* metrics);
```

### Configuration Structure

```c
typedef struct {
    gpu_vendor vendor;
    bool enable_secure_inference;
    bool encrypt_model_weights;
    bool encrypt_tensors;
    size_t secure_memory_size;
} gpu_cc_config;
```

## Future Enhancements

- Multi-GPU secure computation
- Federated learning with CC
- Homomorphic encryption integration
- Secure model updates
- Cross-vendor CC compatibility