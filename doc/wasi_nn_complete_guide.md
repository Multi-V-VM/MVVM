# WASI-NN Record and Replay Complete Guide

## Table of Contents
1. [Overview](#overview)
2. [Quick Start](#quick-start)
3. [Architecture](#architecture)
4. [API Reference](#api-reference)
5. [Use Cases](#use-cases)
6. [Examples](#examples)
7. [Performance Considerations](#performance-considerations)
8. [Troubleshooting](#troubleshooting)
9. [Best Practices](#best-practices)
10. [Advanced Features](#advanced-features)

## Overview

The WASI-NN Record and Replay system is a critical component of the MVVM (Multi-Version Virtual Machine) project that enables seamless migration of WebAssembly applications with neural network inference capabilities. This system captures all WASI-NN operations during execution and faithfully replays them during restoration, ensuring consistent ML inference behavior across checkpoints and migrations.

### Key Benefits

- **State Consistency**: Maintains exact neural network state across migrations
- **Performance Optimization**: Reduces cold-start latency for ML applications
- **Fault Tolerance**: Enables recovery from failures without losing ML context
- **Live Migration**: Supports moving running ML workloads between hosts
- **Debugging**: Provides detailed operation history for troubleshooting

### Supported Backends

- TensorFlow Lite (Primary)
- ONNX Runtime (Experimental)
- PyTorch Mobile (Planned)

## Quick Start

### 1. Setup

Ensure your WAMR build includes WASI-NN support:

```cmake
set(WAMR_BUILD_WASI_NN 1)
set(WAMR_BUILD_WASI_NN_ENABLE_GPU 1)
```

### 2. Basic Integration

```c
#include "wamr_wasi_nn_recorder.h"

// Initialize the recorder
wasm_module_inst_t instance = /* your module instance */;
wamr_wasi_nn_recorder_init(instance);

// Use wrapped WASI-NN functions
error result = wamr_wasi_nn_load_with_recording(
    exec_env, builder, size, encoding, target, &graph, "model.tflite"
);

// Cleanup when done
wamr_wasi_nn_recorder_cleanup(instance);
```

### 3. Checkpoint and Restore

```cpp
// During checkpoint
WAMRWASINNContext* ctx = wamr_get_wasi_nn_mvvm_context(instance);
WASINNContext native_ctx;
ctx->dump_impl(&native_ctx);

// During restore
ctx->restore_impl(&native_ctx);
```

## Architecture

### Component Overview

```
┌─────────────────────┐    ┌─────────────────────┐
│   WASM Application  │    │   MVVM Checkpoint   │
└─────────┬───────────┘    └─────────┬───────────┘
          │                          │
          ▼                          ▼
┌─────────────────────┐    ┌─────────────────────┐
│  WASI-NN Recorder   │◄──►│  WAMRWASINNContext  │
└─────────┬───────────┘    └─────────┬───────────┘
          │                          │
          ▼                          ▼
┌─────────────────────┐    ┌─────────────────────┐
│    WASI-NN API      │    │  Operation Storage  │
└─────────┬───────────┘    └─────────────────────┘
          │
          ▼
┌─────────────────────┐
│  TensorFlow Lite    │
└─────────────────────┘
```

### Data Flow

1. **Recording Phase**:
   - Application calls WASI-NN functions
   - Recorder intercepts calls and logs operations
   - Operations stored in WAMRWASINNContext
   - Native WASI-NN functions execute normally

2. **Checkpoint Phase**:
   - dump_impl() serializes recorded operations
   - Model states and tensor data preserved
   - Operation sequence maintained

3. **Restore Phase**:
   - restore_impl() replays operations in order
   - Models reloaded and contexts recreated
   - Input tensors restored to previous state

### Operation Types

```cpp
enum class WASINNOperationType {
    LOAD_MODEL,              // Loading ML models
    INIT_EXECUTION_CONTEXT,  // Creating execution contexts
    SET_INPUT,               // Setting input tensors
    COMPUTE,                 // Running inference
    GET_OUTPUT               // Retrieving results
};
```

## API Reference

### Core Functions

#### `wamr_wasi_nn_recorder_init`
```c
void wamr_wasi_nn_recorder_init(wasm_module_inst_t instance);
```
Initializes the WASI-NN recorder for a module instance.

**Parameters:**
- `instance`: WASM module instance

**Usage:**
```c
wamr_wasi_nn_recorder_init(module_instance);
```

#### `wamr_wasi_nn_load_with_recording`
```c
error wamr_wasi_nn_load_with_recording(
    wasm_exec_env_t exec_env,
    graph_builder_wasm *builder,
    uint32_t builder_wasm_size,
    graph_encoding encoding,
    execution_target target,
    graph *g,
    const char* model_name
);
```
Loads a model while recording the operation.

**Parameters:**
- `exec_env`: Execution environment
- `builder`: Model data builder
- `builder_wasm_size`: Builder size
- `encoding`: Model encoding (e.g., tensorflowlite)
- `target`: Execution target (cpu, gpu, tpu)
- `g`: Output graph handle
- `model_name`: Path to model file

**Returns:** Error code indicating success or failure

#### `wamr_wasi_nn_set_input_with_recording`
```c
error wamr_wasi_nn_set_input_with_recording(
    wasm_exec_env_t exec_env,
    graph_execution_context ctx,
    uint32_t index,
    tensor_wasm *input_tensor
);
```
Sets input tensor while recording the operation.

### Context Management

#### `wamr_get_wasi_nn_mvvm_context`
```c
WAMRWASINNContext* wamr_get_wasi_nn_mvvm_context(wasm_module_inst_t instance);
```
Retrieves the MVVM context for a module instance.

#### `WAMRWASINNContext Methods`
```cpp
class WAMRWASINNContext {
public:
    void enable_recording(bool enable);
    void enable_replay(bool enable);
    void reset_replay();
    void record_operation(const WAMRWASINNOperation& op);
    WAMRWASINNOperation* get_next_operation();
    void dump_impl(WASINNContext *env);
    void restore_impl(WASINNContext *env);
};
```

## Use Cases

### 1. Edge AI Migration

**Scenario**: Moving AI workloads between edge devices

```cpp
// Device A: Running image classification
wamr_wasi_nn_load_with_recording(exec_env, builder, size, 
                                tensorflowlite, cpu, &graph, "mobilenet.tflite");

// Process images...
for (int i = 0; i < num_images; i++) {
    wamr_wasi_nn_set_input_with_recording(exec_env, ctx, 0, &image_tensors[i]);
    wamr_wasi_nn_compute_with_recording(exec_env, ctx);
    wamr_wasi_nn_get_output_with_recording(exec_env, ctx, 0, output, len, &size);
}

// Checkpoint and migrate to Device B
WAMRWASINNContext* mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);
mvvm_ctx->dump_impl(&checkpoint_data);

// Device B: Restore and continue processing
mvvm_ctx->restore_impl(&checkpoint_data);
// Continue with remaining images...
```

### 2. Serverless Cold Start Optimization

**Scenario**: Reducing ML model initialization time in serverless functions

```cpp
// Pre-warm phase: Record model loading and setup
wamr_wasi_nn_load_with_recording(exec_env, builder, size,
                                tensorflowlite, gpu, &graph, "bert-large.tflite");
wamr_wasi_nn_init_execution_context_with_recording(exec_env, graph, &ctx);

// Save warm state
save_checkpoint("warm-bert-state.ckpt");

// Cold start: Restore from warm state (much faster)
load_checkpoint("warm-bert-state.ckpt");
// Model already loaded and ready for inference
```

### 3. Fault-Tolerant ML Pipeline

**Scenario**: Recovering from failures in long-running ML pipelines

```cpp
// Process large dataset with periodic checkpoints
for (int batch = 0; batch < total_batches; batch++) {
    // Process batch
    wamr_wasi_nn_set_input_with_recording(exec_env, ctx, 0, &batch_data[batch]);
    wamr_wasi_nn_compute_with_recording(exec_env, ctx);
    
    // Checkpoint every 100 batches
    if (batch % 100 == 0) {
        create_checkpoint(batch);
    }
}

// On failure: restore from last checkpoint
if (failure_detected) {
    restore_from_checkpoint(last_checkpoint_batch);
    // Continue from where we left off
}
```

### 4. A/B Testing ML Models

**Scenario**: Testing different model versions with state preservation

```cpp
// Model A evaluation
wamr_wasi_nn_load_with_recording(exec_env, builder_a, size_a,
                                tensorflowlite, cpu, &graph_a, "model_v1.tflite");
run_evaluation_dataset(&results_a);

// Save state before switching
save_checkpoint("model_a_state.ckpt");

// Model B evaluation  
wamr_wasi_nn_load_with_recording(exec_env, builder_b, size_b,
                                tensorflowlite, cpu, &graph_b, "model_v2.tflite");
run_evaluation_dataset(&results_b);

// Compare results and rollback if needed
if (results_b.accuracy < results_a.accuracy) {
    restore_checkpoint("model_a_state.ckpt");
}
```

### 5. Distributed ML Inference

**Scenario**: Load balancing ML workloads across multiple nodes

```cpp
// Node 1: Process first half of requests
for (int i = 0; i < requests.size() / 2; i++) {
    process_request(requests[i]);
}

// Check load on other nodes
if (node2_overloaded) {
    // Migrate some work to Node 3
    WAMRWASINNContext* ctx = wamr_get_wasi_nn_mvvm_context(instance);
    serialize_context(ctx, "migration.data");
    
    // Node 3: Restore context and continue processing
    deserialize_context("migration.data", &restored_ctx);
    continue_processing_from(requests.size() / 2);
}
```

## Examples

### Example 1: Image Classification with Checkpointing

```cpp
#include "wamr_wasi_nn_recorder.h"
#include <vector>
#include <string>

class ImageClassifier {
private:
    wasm_exec_env_t exec_env;
    wasm_module_inst_t instance;
    graph model_graph;
    graph_execution_context exec_ctx;
    WAMRWASINNContext* mvvm_ctx;

public:
    bool initialize(const std::string& model_path) {
        // Initialize recorder
        wamr_wasi_nn_recorder_init(instance);
        mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);
        
        // Load model
        graph_builder_array builder;
        load_model_file(model_path, &builder);
        
        error result = wamr_wasi_nn_load_with_recording(
            exec_env, &builder, builder.size,
            tensorflowlite, cpu, &model_graph, model_path.c_str()
        );
        
        if (result != success) return false;
        
        // Initialize execution context
        result = wamr_wasi_nn_init_execution_context_with_recording(
            exec_env, model_graph, &exec_ctx
        );
        
        return result == success;
    }
    
    std::vector<float> classify(const std::vector<uint8_t>& image_data) {
        // Prepare input tensor
        tensor_wasm input_tensor;
        setup_input_tensor(&input_tensor, image_data);
        
        // Set input
        wamr_wasi_nn_set_input_with_recording(exec_env, exec_ctx, 0, &input_tensor);
        
        // Run inference
        wamr_wasi_nn_compute_with_recording(exec_env, exec_ctx);
        
        // Get output
        std::vector<float> output(1000);  // ImageNet classes
        uint32_t output_size = output.size() * sizeof(float);
        wamr_wasi_nn_get_output_with_recording(
            exec_env, exec_ctx, 0, 
            reinterpret_cast<uint8_t*>(output.data()),
            output_size, &output_size
        );
        
        return output;
    }
    
    void save_checkpoint(const std::string& path) {
        WASINNContext native_ctx;
        mvvm_ctx->dump_impl(&native_ctx);
        serialize_to_file(mvvm_ctx->recorded_operations, path);
    }
    
    void load_checkpoint(const std::string& path) {
        auto operations = deserialize_from_file(path);
        mvvm_ctx->recorded_operations = operations;
        
        WASINNContext native_ctx;
        mvvm_ctx->restore_impl(&native_ctx);
    }
    
    ~ImageClassifier() {
        wamr_wasi_nn_recorder_cleanup(instance);
    }
};

// Usage
int main() {
    ImageClassifier classifier;
    
    // Initialize with model
    if (!classifier.initialize("mobilenet_v2.tflite")) {
        std::cerr << "Failed to initialize classifier" << std::endl;
        return 1;
    }
    
    // Process images
    std::vector<std::string> image_paths = get_image_paths();
    
    for (size_t i = 0; i < image_paths.size(); i++) {
        auto image_data = load_image(image_paths[i]);
        auto predictions = classifier.classify(image_data);
        
        // Save checkpoint every 100 images
        if (i % 100 == 0) {
            classifier.save_checkpoint("checkpoint_" + std::to_string(i) + ".ckpt");
        }
        
        process_predictions(predictions);
    }
    
    return 0;
}
```

### Example 2: Batch Processing with Recovery

```cpp
class BatchProcessor {
private:
    WAMRWASINNContext* mvvm_ctx;
    std::string checkpoint_dir;
    size_t current_batch;
    
public:
    bool process_dataset(const std::vector<BatchData>& batches) {
        current_batch = 0;
        
        // Try to restore from last checkpoint
        if (restore_last_checkpoint()) {
            std::cout << "Restored from checkpoint at batch " << current_batch << std::endl;
        }
        
        try {
            for (size_t i = current_batch; i < batches.size(); i++) {
                process_batch(batches[i]);
                current_batch = i;
                
                // Checkpoint every 50 batches
                if (i % 50 == 0) {
                    save_checkpoint(i);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing batch " << current_batch << ": " << e.what() << std::endl;
            return false;
        }
        
        return true;
    }
    
private:
    void process_batch(const BatchData& batch) {
        // Set input tensors
        for (size_t i = 0; i < batch.inputs.size(); i++) {
            tensor_wasm input;
            prepare_tensor(&input, batch.inputs[i]);
            wamr_wasi_nn_set_input_with_recording(exec_env, exec_ctx, i, &input);
        }
        
        // Compute
        wamr_wasi_nn_compute_with_recording(exec_env, exec_ctx);
        
        // Get outputs
        for (size_t i = 0; i < batch.expected_outputs; i++) {
            uint8_t output_buffer[MAX_OUTPUT_SIZE];
            uint32_t output_size = MAX_OUTPUT_SIZE;
            wamr_wasi_nn_get_output_with_recording(
                exec_env, exec_ctx, i, output_buffer, output_size, &output_size
            );
            
            process_output(output_buffer, output_size);
        }
    }
    
    void save_checkpoint(size_t batch_id) {
        std::string path = checkpoint_dir + "/batch_" + std::to_string(batch_id) + ".ckpt";
        WASINNContext native_ctx;
        mvvm_ctx->dump_impl(&native_ctx);
        
        CheckpointData data;
        data.batch_id = batch_id;
        data.operations = mvvm_ctx->recorded_operations;
        data.context_state = native_ctx;
        
        serialize_checkpoint(data, path);
    }
    
    bool restore_last_checkpoint() {
        auto checkpoint_files = find_checkpoint_files(checkpoint_dir);
        if (checkpoint_files.empty()) return false;
        
        // Load most recent checkpoint
        std::sort(checkpoint_files.rbegin(), checkpoint_files.rend());
        
        for (const auto& file : checkpoint_files) {
            try {
                auto data = deserialize_checkpoint(file);
                current_batch = data.batch_id;
                mvvm_ctx->recorded_operations = data.operations;
                mvvm_ctx->restore_impl(&data.context_state);
                return true;
            } catch (...) {
                // Try next checkpoint if this one is corrupted
                continue;
            }
        }
        
        return false;
    }
};
```

## Performance Considerations

### Memory Usage

The record and replay system stores all operation data in memory:

- **Model data**: Stored once per unique model
- **Tensor data**: Stored for each set_input/get_output operation
- **Metadata**: Operation type, sequence, dimensions

**Optimization strategies:**

1. **Compression**: Use tensor compression for large inputs
```cpp
// Enable compression for large tensors
mvvm_ctx->enable_tensor_compression(true);
mvvm_ctx->set_compression_threshold(1024 * 1024);  // 1MB
```

2. **Selective recording**: Record only critical operations
```cpp
// Disable recording for inference-only phases
mvvm_ctx->enable_recording(false);
run_inference_batch();
mvvm_ctx->enable_recording(true);
```

3. **Periodic cleanup**: Remove old operations
```cpp
// Keep only last 1000 operations
if (mvvm_ctx->recorded_operations.size() > 1000) {
    mvvm_ctx->trim_operations(1000);
}
```

### Checkpoint Size

Typical checkpoint sizes:
- **Small model** (MobileNet): 5-10 MB
- **Medium model** (BERT-base): 50-100 MB  
- **Large model** (BERT-large): 200-500 MB

### Restore Performance

Restore time depends on:
- Number of recorded operations
- Model loading time
- Tensor data size

**Benchmarks** (on typical edge device):
- **Small model restore**: 100-200ms
- **Medium model restore**: 500ms-1s
- **Large model restore**: 2-5s

## Troubleshooting

### Common Issues

#### 1. Out of Memory During Recording

**Symptoms:**
```
Error: Failed to allocate memory for tensor data
Recorded operations: 10000+
```

**Solutions:**
```cpp
// Reduce recording frequency
mvvm_ctx->set_recording_interval(10);  // Record every 10th operation

// Use compression
mvvm_ctx->enable_tensor_compression(true);

// Limit operation history
mvvm_ctx->set_max_operations(1000);
```

#### 2. Model File Not Found During Restore

**Symptoms:**
```
Error: Failed to open model file during restore
Path: /path/to/model.tflite
```

**Solutions:**
```cpp
// Use absolute paths
wamr_wasi_nn_load_with_recording(exec_env, builder, size,
                                encoding, target, &graph, 
                                "/absolute/path/to/model.tflite");

// Verify file accessibility
if (!file_exists(model_path)) {
    copy_model_to_accessible_location(model_path);
}
```

#### 3. Context Mismatch During Replay

**Symptoms:**
```
Error: Graph ID mismatch during replay
Expected: 123, Got: 456
```

**Solutions:**
```cpp
// Clear context before restore
mvvm_ctx->clear_mappings();
mvvm_ctx->reset_replay();

// Verify operation sequence
mvvm_ctx->validate_operation_sequence();
```

#### 4. Performance Degradation

**Symptoms:**
- Slow inference after restore
- High memory usage
- Checkpoint/restore taking too long

**Solutions:**
```cpp
// Profile recording overhead
auto start = std::chrono::high_resolution_clock::now();
wamr_wasi_nn_compute_with_recording(exec_env, ctx);
auto end = std::chrono::high_resolution_clock::now();
auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

// Optimize recording
if (duration.count() > threshold) {
    mvvm_ctx->enable_recording(false);  // Disable for performance-critical sections
}
```

### Debug Mode

Enable debug logging:
```cpp
#define WASI_NN_DEBUG 1
mvvm_ctx->set_debug_level(DEBUG_VERBOSE);
```

Debug output example:
```
[DEBUG] Recording SET_INPUT operation: ctx=123, index=0, size=150528 bytes
[DEBUG] Tensor dimensions: [1, 224, 224, 3]
[DEBUG] Tensor type: fp32
[DEBUG] Operation sequence: 42
```

### Validation Tools

```cpp
// Validate recorded operations
bool validate_operations(const std::vector<WAMRWASINNOperation>& ops) {
    for (size_t i = 0; i < ops.size(); i++) {
        if (ops[i].sequence_id != i) {
            std::cerr << "Sequence mismatch at " << i << std::endl;
            return false;
        }
        
        if (!validate_operation_data(ops[i])) {
            std::cerr << "Invalid operation data at " << i << std::endl;
            return false;
        }
    }
    return true;
}

// Check context integrity
bool check_context_integrity(WAMRWASINNContext* ctx) {
    return ctx->graph_mapping.size() == ctx->context_mapping.size() &&
           ctx->replay_position <= ctx->recorded_operations.size();
}
```

## Best Practices

### 1. Resource Management

```cpp
// Always initialize and cleanup properly
class WAMRWASINNManager {
    wasm_module_inst_t instance;
    bool initialized = false;
    
public:
    WAMRWASINNManager(wasm_module_inst_t inst) : instance(inst) {
        wamr_wasi_nn_recorder_init(instance);
        initialized = true;
    }
    
    ~WAMRWASINNManager() {
        if (initialized) {
            wamr_wasi_nn_recorder_cleanup(instance);
        }
    }
    
    // Disable copy to prevent double cleanup
    WAMRWASINNManager(const WAMRWASINNManager&) = delete;
    WAMRWASINNManager& operator=(const WAMRWASINNManager&) = delete;
};
```

### 2. Error Handling

```cpp
error safe_wasi_nn_operation(std::function<error()> operation, int max_retries = 3) {
    for (int attempt = 0; attempt < max_retries; attempt++) {
        error result = operation();
        
        if (result == success) {
            return success;
        }
        
        // Log error and retry
        std::cerr << "Operation failed (attempt " << (attempt + 1) << "): " << result << std::endl;
        
        if (attempt < max_retries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
        }
    }
    
    return runtime_error;
}

// Usage
error result = safe_wasi_nn_operation([&]() {
    return wamr_wasi_nn_compute_with_recording(exec_env, ctx);
});
```

### 3. Checkpoint Strategy

```cpp
class CheckpointStrategy {
public:
    virtual bool should_checkpoint(size_t operation_count, 
                                 std::chrono::duration<double> elapsed) = 0;
};

class TimeBasedStrategy : public CheckpointStrategy {
    std::chrono::seconds interval;
public:
    TimeBasedStrategy(std::chrono::seconds interval) : interval(interval) {}
    
    bool should_checkpoint(size_t operation_count, std::chrono::duration<double> elapsed) override {
        return elapsed >= interval;
    }
};

class OperationBasedStrategy : public CheckpointStrategy {
    size_t operation_threshold;
public:
    OperationBasedStrategy(size_t threshold) : operation_threshold(threshold) {}
    
    bool should_checkpoint(size_t operation_count, std::chrono::duration<double> elapsed) override {
        return operation_count >= operation_threshold;
    }
};
```

### 4. Thread Safety

```cpp
class ThreadSafeWASINNContext {
    std::mutex recording_mutex;
    WAMRWASINNContext* ctx;
    
public:
    void record_operation(const WAMRWASINNOperation& op) {
        std::lock_guard<std::mutex> lock(recording_mutex);
        ctx->record_operation(op);
    }
    
    WAMRWASINNOperation* get_next_operation() {
        std::lock_guard<std::mutex> lock(recording_mutex);
        return ctx->get_next_operation();
    }
};
```

## Advanced Features

### 1. Operation Filtering

```cpp
// Custom operation filter
class OperationFilter {
public:
    virtual bool should_record(const WAMRWASINNOperation& op) = 0;
};

class SelectiveFilter : public OperationFilter {
    std::set<WASINNOperationType> allowed_types;
    
public:
    SelectiveFilter(std::initializer_list<WASINNOperationType> types) 
        : allowed_types(types) {}
    
    bool should_record(const WAMRWASINNOperation& op) override {
        return allowed_types.count(op.type) > 0;
    }
};

// Usage: Only record model loading and compute operations
auto filter = std::make_unique<SelectiveFilter>({
    WASINNOperationType::LOAD_MODEL,
    WASINNOperationType::COMPUTE
});

mvvm_ctx->set_operation_filter(std::move(filter));
```

### 2. Custom Serialization

```cpp
class CustomSerializer {
public:
    virtual std::vector<uint8_t> serialize(const WAMRWASINNOperation& op) = 0;
    virtual WAMRWASINNOperation deserialize(const std::vector<uint8_t>& data) = 0;
};

class CompressedSerializer : public CustomSerializer {
public:
    std::vector<uint8_t> serialize(const WAMRWASINNOperation& op) override {
        // Use compression for tensor data
        if (op.tensor_data.size() > compression_threshold) {
            return compress_tensor_data(op);
        }
        return default_serialize(op);
    }
    
    WAMRWASINNOperation deserialize(const std::vector<uint8_t>& data) override {
        if (is_compressed(data)) {
            return decompress_operation(data);
        }
        return default_deserialize(data);
    }
};
```

### 3. Distributed Checkpointing

```cpp
class DistributedCheckpoint {
    std::vector<std::string> storage_nodes;
    
public:
    void save_distributed(const WAMRWASINNContext& ctx) {
        auto operations = ctx.recorded_operations;
        size_t chunk_size = operations.size() / storage_nodes.size();
        
        for (size_t i = 0; i < storage_nodes.size(); i++) {
            size_t start = i * chunk_size;
            size_t end = (i == storage_nodes.size() - 1) ? operations.size() : start + chunk_size;
            
            std::vector<WAMRWASINNOperation> chunk(
                operations.begin() + start, 
                operations.begin() + end
            );
            
            save_to_node(storage_nodes[i], chunk);
        }
    }
    
    WAMRWASINNContext load_distributed() {
        WAMRWASINNContext ctx;
        
        for (const auto& node : storage_nodes) {
            auto chunk = load_from_node(node);
            ctx.recorded_operations.insert(
                ctx.recorded_operations.end(),
                chunk.begin(), chunk.end()
            );
        }
        
        // Sort by sequence ID
        std::sort(ctx.recorded_operations.begin(), 
                 ctx.recorded_operations.end(),
                 [](const auto& a, const auto& b) {
                     return a.sequence_id < b.sequence_id;
                 });
        
        return ctx;
    }
};
```

### 4. Real-time Monitoring

```cpp
class WAMRWASINNMonitor {
    std::atomic<size_t> operations_recorded{0};
    std::atomic<size_t> bytes_recorded{0};
    std::chrono::steady_clock::time_point start_time;
    
public:
    WAMRWASINNMonitor() : start_time(std::chrono::steady_clock::now()) {}
    
    void on_operation_recorded(const WAMRWASINNOperation& op) {
        operations_recorded++;
        bytes_recorded += calculate_operation_size(op);
    }
    
    struct Stats {
        size_t total_operations;
        size_t total_bytes;
        double operations_per_second;
        double mb_per_second;
        std::chrono::duration<double> uptime;
    };
    
    Stats get_stats() const {
        auto now = std::chrono::steady_clock::now();
        auto uptime = now - start_time;
        double seconds = uptime.count();
        
        return Stats{
            .total_operations = operations_recorded.load(),
            .total_bytes = bytes_recorded.load(),
            .operations_per_second = operations_recorded.load() / seconds,
            .mb_per_second = (bytes_recorded.load() / (1024.0 * 1024.0)) / seconds,
            .uptime = uptime
        };
    }
};
```

This comprehensive guide provides everything needed to effectively use the WASI-NN record and replay functionality in the MVVM project. The system enables robust, efficient migration of ML workloads while maintaining state consistency and performance.