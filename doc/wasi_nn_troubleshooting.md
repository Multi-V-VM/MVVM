# WASI-NN Record and Replay Troubleshooting Guide

## Table of Contents
1. [Common Issues and Solutions](#common-issues-and-solutions)
2. [Debug Mode and Logging](#debug-mode-and-logging)
3. [Performance Issues](#performance-issues)
4. [Memory Problems](#memory-problems)
5. [Checkpoint and Restore Failures](#checkpoint-and-restore-failures)
6. [Model Loading Issues](#model-loading-issues)
7. [Integration Problems](#integration-problems)
8. [Advanced Debugging Techniques](#advanced-debugging-techniques)

## Common Issues and Solutions

### 1. Recorder Not Initialized

**Symptoms:**
```
Segmentation fault when calling wamr_wasi_nn_*_with_recording functions
ctx pointer is NULL
```

**Cause:**
The recorder was not properly initialized for the module instance.

**Solution:**
```c
// Always initialize before use
wasm_module_inst_t instance = /* your module instance */;
wamr_wasi_nn_recorder_init(instance);

// Verify initialization
WAMRWASINNContext* ctx = wamr_get_wasi_nn_mvvm_context(instance);
if (ctx == NULL) {
    fprintf(stderr, "Failed to initialize WASI-NN recorder\n");
    return -1;
}

// Now safe to use recording functions
error result = wamr_wasi_nn_load_with_recording(/* ... */);
```

**Prevention:**
Always use RAII wrapper in C++:
```cpp
WAMRWASINNRecorder recorder(instance);
if (!recorder.is_initialized()) {
    throw std::runtime_error("Failed to initialize recorder");
}
```

### 2. Operation Sequence Corruption

**Symptoms:**
```
Error: Operation sequence mismatch during replay
Expected sequence: 42, Got: 45
Replay failed with inconsistent state
```

**Cause:**
Operations were recorded out of order or some operations were lost.

**Solution:**
```cpp
// Validate operation sequence before replay
bool validate_sequence(const std::vector<WAMRWASINNOperation>& ops) {
    for (size_t i = 0; i < ops.size(); ++i) {
        if (ops[i].sequence_id != i) {
            std::cerr << "Sequence gap at position " << i 
                     << ", expected " << i << ", got " << ops[i].sequence_id << std::endl;
            return false;
        }
    }
    return true;
}

// Fix sequence if possible
void fix_operation_sequence(std::vector<WAMRWASINNOperation>& ops) {
    // Sort by sequence ID
    std::sort(ops.begin(), ops.end(), 
              [](const auto& a, const auto& b) {
                  return a.sequence_id < b.sequence_id;
              });
    
    // Renumber sequence IDs
    for (size_t i = 0; i < ops.size(); ++i) {
        ops[i].sequence_id = i;
    }
}
```

**Prevention:**
Enable sequence validation in debug builds:
```cpp
#ifdef DEBUG
void record_operation_with_validation(WAMRWASINNContext* ctx, const WAMRWASINNOperation& op) {
    if (!ctx->recorded_operations.empty()) {
        auto last_seq = ctx->recorded_operations.back().sequence_id;
        if (op.sequence_id != last_seq + 1) {
            std::cerr << "WARNING: Sequence gap detected" << std::endl;
        }
    }
    ctx->record_operation(op);
}
#endif
```

### 3. Model File Not Found During Restore

**Symptoms:**
```
Error: Failed to open model file during restore
Path: /path/to/model.tflite
No such file or directory
```

**Cause:**
Model file path is relative or the file was moved/deleted between checkpoint and restore.

**Solution:**
```cpp
// Use absolute paths for models
std::string get_absolute_model_path(const std::string& relative_path) {
    std::filesystem::path abs_path = std::filesystem::absolute(relative_path);
    return abs_path.string();
}

// Verify model file exists before recording
bool verify_model_file(const std::string& path) {
    return std::filesystem::exists(path) && std::filesystem::is_regular_file(path);
}

// Example usage
std::string model_path = get_absolute_model_path("models/mobilenet.tflite");
if (!verify_model_file(model_path)) {
    throw std::runtime_error("Model file not found: " + model_path);
}

error result = wamr_wasi_nn_load_with_recording(
    exec_env, builder, size, encoding, target, &graph, model_path.c_str()
);
```

**Alternative Solution - Model Embedding:**
```cpp
// Embed model data in checkpoint
struct EmbeddedModelCheckpoint {
    std::string model_name;
    std::vector<uint8_t> model_data;
    std::vector<WAMRWASINNOperation> operations;
};

void create_embedded_checkpoint(const std::string& model_path, WAMRWASINNContext* ctx) {
    EmbeddedModelCheckpoint checkpoint;
    checkpoint.model_name = model_path;
    
    // Read model file into memory
    std::ifstream file(model_path, std::ios::binary);
    if (file) {
        checkpoint.model_data = std::vector<uint8_t>(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()
        );
    }
    
    checkpoint.operations = ctx->recorded_operations;
    save_embedded_checkpoint(checkpoint);
}
```

### 4. Out of Memory During Recording

**Symptoms:**
```
Error: Failed to allocate memory for tensor data
std::bad_alloc exception
Recording stopped due to memory pressure
```

**Cause:**
Large models or frequent operations cause excessive memory usage.

**Solution:**
```cpp
class MemoryAwareRecorder {
private:
    size_t max_memory_mb;
    size_t compression_threshold;
    
public:
    MemoryAwareRecorder(size_t max_mem_mb = 1024) 
        : max_memory_mb(max_mem_mb), compression_threshold(1024 * 1024) {}
    
    void record_operation_with_memory_management(WAMRWASINNContext* ctx, 
                                               const WAMRWASINNOperation& op) {
        // Check memory usage
        size_t current_usage = calculate_memory_usage(*ctx);
        size_t op_size = calculate_operation_size(op);
        
        if (current_usage + op_size > max_memory_mb * 1024 * 1024) {
            // Apply memory optimization strategies
            compress_old_operations(ctx);
            remove_redundant_operations(ctx);
            
            // If still too large, use selective recording
            if (calculate_memory_usage(*ctx) + op_size > max_memory_mb * 1024 * 1024) {
                if (is_critical_operation(op)) {
                    // Force record critical operations
                    ctx->record_operation(op);
                } else {
                    // Skip non-critical operations
                    std::cout << "Skipping operation due to memory constraints" << std::endl;
                    return;
                }
            }
        }
        
        ctx->record_operation(op);
    }
    
private:
    void compress_old_operations(WAMRWASINNContext* ctx) {
        for (auto& op : ctx->recorded_operations) {
            if (op.tensor_data.size() > compression_threshold) {
                op.tensor_data = compress_tensor_data(op.tensor_data);
            }
        }
    }
    
    void remove_redundant_operations(WAMRWASINNContext* ctx) {
        // Remove duplicate SET_INPUT operations with same data
        std::unordered_set<std::string> seen_inputs;
        
        ctx->recorded_operations.erase(
            std::remove_if(ctx->recorded_operations.begin(), 
                          ctx->recorded_operations.end(),
                          [&](const WAMRWASINNOperation& op) {
                              if (op.type == WASINNOperationType::SET_INPUT) {
                                  std::string hash = calculate_tensor_hash(op.tensor_data);
                                  if (seen_inputs.count(hash)) {
                                      return true;  // Remove duplicate
                                  }
                                  seen_inputs.insert(hash);
                              }
                              return false;
                          }),
            ctx->recorded_operations.end()
        );
    }
};
```

### 5. Context Mapping Errors During Replay

**Symptoms:**
```
Error: Graph ID not found in mapping
Graph ID: 123, Available mappings: [456, 789]
Context restoration failed
```

**Cause:**
Graph or context IDs from recording don't match those created during replay.

**Solution:**
```cpp
class RobustContextMapper {
private:
    std::unordered_map<graph, graph> graph_mapping;
    std::unordered_map<graph_execution_context, graph_execution_context> context_mapping;
    
public:
    void safe_add_graph_mapping(graph original, graph new_graph) {
        if (graph_mapping.find(original) != graph_mapping.end()) {
            std::cerr << "Warning: Overwriting existing graph mapping" << std::endl;
        }
        graph_mapping[original] = new_graph;
    }
    
    graph get_mapped_graph(graph original) {
        auto it = graph_mapping.find(original);
        if (it == graph_mapping.end()) {
            std::cerr << "Graph mapping not found for ID: " << original << std::endl;
            std::cerr << "Available mappings: ";
            for (const auto& pair : graph_mapping) {
                std::cerr << pair.first << " ";
            }
            std::cerr << std::endl;
            throw std::runtime_error("Graph mapping not found");
        }
        return it->second;
    }
    
    void debug_print_mappings() {
        std::cout << "Graph mappings:" << std::endl;
        for (const auto& pair : graph_mapping) {
            std::cout << "  " << pair.first << " -> " << pair.second << std::endl;
        }
        
        std::cout << "Context mappings:" << std::endl;
        for (const auto& pair : context_mapping) {
            std::cout << "  " << pair.first << " -> " << pair.second << std::endl;
        }
    }
};
```

## Debug Mode and Logging

### Enable Debug Logging

```cpp
// Enable comprehensive debug logging
#define WASI_NN_DEBUG 1

class DebugLogger {
private:
    std::ofstream log_file;
    bool console_output;
    
public:
    DebugLogger(const std::string& filename, bool console = true) 
        : log_file(filename), console_output(console) {}
    
    void log_operation(const WAMRWASINNOperation& op, const std::string& phase) {
        std::string msg = format_operation_log(op, phase);
        
        if (console_output) {
            std::cout << msg << std::endl;
        }
        
        if (log_file.is_open()) {
            log_file << msg << std::endl;
            log_file.flush();
        }
    }
    
    void log_context_state(const WAMRWASINNContext& ctx) {
        std::string msg = "Context State:\n";
        msg += "  Recording enabled: " + std::to_string(ctx.recording_enabled) + "\n";
        msg += "  Replaying enabled: " + std::to_string(ctx.replaying_enabled) + "\n";
        msg += "  Operations count: " + std::to_string(ctx.recorded_operations.size()) + "\n";
        msg += "  Replay position: " + std::to_string(ctx.replay_position) + "\n";
        
        if (console_output) std::cout << msg;
        if (log_file.is_open()) log_file << msg << std::flush;
    }
    
private:
    std::string format_operation_log(const WAMRWASINNOperation& op, const std::string& phase) {
        std::ostringstream oss;
        oss << "[" << phase << "] Operation " << op.sequence_id << ": ";
        
        switch (op.type) {
            case WASINNOperationType::LOAD_MODEL:
                oss << "LOAD_MODEL(" << op.model_name << ")";
                break;
            case WASINNOperationType::INIT_EXECUTION_CONTEXT:
                oss << "INIT_EXECUTION_CONTEXT(graph=" << op.graph_id << ")";
                break;
            case WASINNOperationType::SET_INPUT:
                oss << "SET_INPUT(index=" << op.input_index 
                    << ", dims=[";
                for (size_t i = 0; i < op.tensor_dims.size(); ++i) {
                    oss << op.tensor_dims[i];
                    if (i < op.tensor_dims.size() - 1) oss << ",";
                }
                oss << "], size=" << op.tensor_data.size() << ")";
                break;
            case WASINNOperationType::COMPUTE:
                oss << "COMPUTE(ctx=" << op.ctx_id << ")";
                break;
            case WASINNOperationType::GET_OUTPUT:
                oss << "GET_OUTPUT(index=" << op.output_index 
                    << ", size=" << op.output_size << ")";
                break;
        }
        
        return oss.str();
    }
};

// Usage
DebugLogger logger("wasi_nn_debug.log");

// In recording functions
logger.log_operation(operation, "RECORD");

// In replay functions  
logger.log_operation(operation, "REPLAY");
```

### Memory Usage Monitoring

```cpp
class MemoryMonitor {
private:
    std::chrono::steady_clock::time_point start_time;
    size_t peak_memory;
    
public:
    MemoryMonitor() : start_time(std::chrono::steady_clock::now()), peak_memory(0) {}
    
    void check_memory_usage(const WAMRWASINNContext& ctx) {
        size_t current = calculate_memory_usage(ctx);
        peak_memory = std::max(peak_memory, current);
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
        
        std::cout << "Memory usage at " << elapsed.count() << "s: " 
                  << (current / 1024 / 1024) << " MB (peak: " 
                  << (peak_memory / 1024 / 1024) << " MB)" << std::endl;
    }
    
    void print_memory_breakdown(const WAMRWASINNContext& ctx) {
        size_t total = sizeof(WAMRWASINNContext);
        size_t operations_meta = ctx.recorded_operations.size() * sizeof(WAMRWASINNOperation);
        size_t tensor_data = 0;
        size_t strings = 0;
        
        for (const auto& op : ctx.recorded_operations) {
            tensor_data += op.tensor_data.size() + op.output_data.size();
            strings += op.model_name.size();
        }
        
        std::cout << "Memory breakdown:" << std::endl;
        std::cout << "  Context: " << (total / 1024) << " KB" << std::endl;
        std::cout << "  Operations metadata: " << (operations_meta / 1024) << " KB" << std::endl;
        std::cout << "  Tensor data: " << (tensor_data / 1024 / 1024) << " MB" << std::endl;
        std::cout << "  Strings: " << (strings / 1024) << " KB" << std::endl;
    }
};
```

## Performance Issues

### Slow Recording Performance

**Symptoms:**
```
Recording operations taking too long
High CPU usage during tensor recording
Inference performance degraded
```

**Diagnosis:**
```cpp
class PerformanceProfiler {
private:
    std::unordered_map<std::string, std::chrono::nanoseconds> operation_times;
    
public:
    template<typename Func>
    auto time_operation(const std::string& name, Func&& func) {
        auto start = std::chrono::high_resolution_clock::now();
        auto result = func();
        auto end = std::chrono::high_resolution_clock::now();
        
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
        operation_times[name] += duration;
        
        return result;
    }
    
    void print_performance_report() {
        std::cout << "Performance Report:" << std::endl;
        for (const auto& pair : operation_times) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(pair.second);
            std::cout << "  " << pair.first << ": " << ms.count() << " ms" << std::endl;
        }
    }
};

// Usage
PerformanceProfiler profiler;

error result = profiler.time_operation("load_model", [&]() {
    return wamr_wasi_nn_load_with_recording(/* ... */);
});

error result2 = profiler.time_operation("set_input", [&]() {
    return wamr_wasi_nn_set_input_with_recording(/* ... */);
});

profiler.print_performance_report();
```

**Solutions:**
```cpp
// Optimize tensor data copying
class OptimizedRecorder {
public:
    static WAMRWASINNOperation create_set_input_operation_optimized(
        graph_execution_context ctx, uint32_t index, const tensor_wasm* tensor) {
        
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::SET_INPUT;
        op.ctx_id = ctx;
        op.input_index = index;
        
        // Copy dimensions (small data)
        if (tensor->dimensions) {
            op.tensor_dims.reserve(tensor->dimensions->size);
            for (uint32_t i = 0; i < tensor->dimensions->size; ++i) {
                op.tensor_dims.push_back(tensor->dimensions->buf[i]);
            }
        }
        
        op.data_type = tensor->type;
        
        // For large tensors, use memory mapping or compression
        size_t tensor_size = calculate_tensor_size(tensor);
        if (tensor_size > LARGE_TENSOR_THRESHOLD) {
            // Option 1: Compress data
            op.tensor_data = compress_tensor_data(tensor->data, tensor_size);
            
            // Option 2: Use reference instead of copy (advanced)
            // op.tensor_data_ref = create_tensor_reference(tensor->data, tensor_size);
        } else {
            // Normal copy for small tensors
            op.tensor_data.resize(tensor_size);
            std::memcpy(op.tensor_data.data(), tensor->data, tensor_size);
        }
        
        return op;
    }
};
```

### Slow Restore Performance

**Symptoms:**
```
Restore taking minutes instead of seconds
High disk I/O during restore
Model loading timeouts
```

**Solutions:**
```cpp
// Parallel restore for independent operations
class ParallelRestore {
public:
    void restore_operations_parallel(WAMRWASINNContext* ctx, WASINNContext* env) {
        auto operations = ctx->recorded_operations;
        
        // Group operations by dependencies
        auto groups = group_operations_by_dependencies(operations);
        
        for (const auto& group : groups) {
            // Execute operations in group in parallel
            std::vector<std::future<error>> futures;
            
            for (const auto& op : group) {
                futures.push_back(std::async(std::launch::async, [&op, env]() {
                    return execute_single_operation(op, env);
                }));
            }
            
            // Wait for all operations in group to complete
            for (auto& future : futures) {
                error result = future.get();
                if (result != success) {
                    throw std::runtime_error("Parallel restore failed");
                }
            }
        }
    }
    
private:
    std::vector<std::vector<WAMRWASINNOperation>> 
    group_operations_by_dependencies(const std::vector<WAMRWASINNOperation>& ops) {
        std::vector<std::vector<WAMRWASINNOperation>> groups;
        
        // Simple grouping: each LOAD_MODEL can be parallel, 
        // other operations depend on previous operations
        std::vector<WAMRWASINNOperation> current_group;
        
        for (const auto& op : ops) {
            if (op.type == WASINNOperationType::LOAD_MODEL) {
                if (!current_group.empty()) {
                    groups.push_back(current_group);
                    current_group.clear();
                }
                current_group.push_back(op);
            } else {
                current_group.push_back(op);
            }
        }
        
        if (!current_group.empty()) {
            groups.push_back(current_group);
        }
        
        return groups;
    }
};
```

## Memory Problems

### Memory Leaks

**Detection:**
```cpp
class MemoryLeakDetector {
private:
    std::unordered_map<void*, size_t> allocations;
    std::mutex alloc_mutex;
    size_t total_allocated = 0;
    
public:
    void* tracked_malloc(size_t size) {
        void* ptr = malloc(size);
        if (ptr) {
            std::lock_guard<std::mutex> lock(alloc_mutex);
            allocations[ptr] = size;
            total_allocated += size;
        }
        return ptr;
    }
    
    void tracked_free(void* ptr) {
        if (ptr) {
            std::lock_guard<std::mutex> lock(alloc_mutex);
            auto it = allocations.find(ptr);
            if (it != allocations.end()) {
                total_allocated -= it->second;
                allocations.erase(it);
            }
            free(ptr);
        }
    }
    
    void print_leak_report() {
        std::lock_guard<std::mutex> lock(alloc_mutex);
        if (!allocations.empty()) {
            std::cout << "Memory leaks detected:" << std::endl;
            std::cout << "  Leaked blocks: " << allocations.size() << std::endl;
            std::cout << "  Total leaked: " << total_allocated << " bytes" << std::endl;
        } else {
            std::cout << "No memory leaks detected" << std::endl;
        }
    }
};
```

### Stack Overflow

**Symptoms:**
```
Segmentation fault during replay
Stack overflow in recursive function calls
Deep call stack during restore
```

**Solution:**
```cpp
// Iterative instead of recursive replay
class IterativeRestore {
public:
    void restore_iteratively(WAMRWASINNContext* ctx, WASINNContext* env) {
        struct RestoreState {
            size_t operation_index;
            std::unordered_map<graph, graph> local_graph_mapping;
            std::unordered_map<graph_execution_context, graph_execution_context> local_context_mapping;
        };
        
        std::stack<RestoreState> state_stack;
        state_stack.push({0, {}, {}});
        
        while (!state_stack.empty()) {
            auto current_state = state_stack.top();
            state_stack.pop();
            
            if (current_state.operation_index >= ctx->recorded_operations.size()) {
                continue;  // Completed this branch
            }
            
            auto& op = ctx->recorded_operations[current_state.operation_index];
            
            // Execute current operation
            error result = execute_operation_with_state(op, current_state, env);
            
            if (result == success) {
                // Push next operation state
                current_state.operation_index++;
                state_stack.push(current_state);
            } else {
                // Handle error without recursion
                handle_restore_error(op, result);
            }
        }
    }
    
private:
    error execute_operation_with_state(const WAMRWASINNOperation& op, 
                                     RestoreState& state, WASINNContext* env) {
        // Implementation that uses state.local_*_mapping instead of global mappings
        // This prevents deep recursion and stack overflow
        
        switch (op.type) {
            case WASINNOperationType::LOAD_MODEL:
                return restore_load_model(op, state, env);
            case WASINNOperationType::INIT_EXECUTION_CONTEXT:
                return restore_init_context(op, state, env);
            // ... other operations
        }
        
        return success;
    }
};
```

## Checkpoint and Restore Failures

### Corrupted Checkpoint Data

**Detection:**
```cpp
class CheckpointValidator {
public:
    struct ValidationResult {
        bool is_valid;
        std::vector<std::string> errors;
        std::vector<std::string> warnings;
    };
    
    ValidationResult validate_checkpoint(const std::vector<WAMRWASINNOperation>& operations) {
        ValidationResult result{true, {}, {}};
        
        // Check operation sequence
        if (!validate_operation_sequence(operations)) {
            result.is_valid = false;
            result.errors.push_back("Invalid operation sequence");
        }
        
        // Check tensor data integrity
        for (const auto& op : operations) {
            if (!validate_tensor_data(op)) {
                result.is_valid = false;
                result.errors.push_back("Invalid tensor data in operation " + 
                                      std::to_string(op.sequence_id));
            }
        }
        
        // Check model file references
        for (const auto& op : operations) {
            if (op.type == WASINNOperationType::LOAD_MODEL) {
                if (!std::filesystem::exists(op.model_name)) {
                    result.warnings.push_back("Model file not found: " + op.model_name);
                }
            }
        }
        
        return result;
    }
    
    bool repair_checkpoint(std::vector<WAMRWASINNOperation>& operations) {
        bool repaired = false;
        
        // Fix operation sequence
        std::sort(operations.begin(), operations.end(),
                  [](const auto& a, const auto& b) {
                      return a.sequence_id < b.sequence_id;
                  });
        
        // Remove duplicate operations
        operations.erase(
            std::unique(operations.begin(), operations.end(),
                       [](const auto& a, const auto& b) {
                           return a.sequence_id == b.sequence_id;
                       }),
            operations.end()
        );
        
        // Renumber sequence IDs
        for (size_t i = 0; i < operations.size(); ++i) {
            if (operations[i].sequence_id != i) {
                operations[i].sequence_id = i;
                repaired = true;
            }
        }
        
        return repaired;
    }
};
```

### Version Compatibility Issues

**Symptoms:**
```
Error: Unsupported checkpoint version
Checkpoint created with newer version
Cannot deserialize operation data
```

**Solution:**
```cpp
class VersionCompatibilityManager {
private:
    static constexpr uint32_t CURRENT_VERSION = 2;
    
public:
    struct CheckpointHeader {
        uint32_t version;
        uint32_t operation_count;
        uint64_t checksum;
        std::chrono::system_clock::time_point timestamp;
    };
    
    std::vector<uint8_t> serialize_with_version(const std::vector<WAMRWASINNOperation>& operations) {
        CheckpointHeader header;
        header.version = CURRENT_VERSION;
        header.operation_count = operations.size();
        header.timestamp = std::chrono::system_clock::now();
        
        // Serialize operations
        auto operation_data = serialize_operations(operations);
        
        // Calculate checksum
        header.checksum = calculate_checksum(operation_data);
        
        // Combine header and data
        std::vector<uint8_t> result;
        result.resize(sizeof(CheckpointHeader) + operation_data.size());
        
        std::memcpy(result.data(), &header, sizeof(CheckpointHeader));
        std::memcpy(result.data() + sizeof(CheckpointHeader), 
                   operation_data.data(), operation_data.size());
        
        return result;
    }
    
    std::vector<WAMRWASINNOperation> deserialize_with_compatibility(const std::vector<uint8_t>& data) {
        if (data.size() < sizeof(CheckpointHeader)) {
            throw std::runtime_error("Invalid checkpoint: too small");
        }
        
        CheckpointHeader header;
        std::memcpy(&header, data.data(), sizeof(CheckpointHeader));
        
        // Verify checksum
        auto operation_data = std::vector<uint8_t>(
            data.begin() + sizeof(CheckpointHeader), data.end()
        );
        
        if (header.checksum != calculate_checksum(operation_data)) {
            throw std::runtime_error("Checkpoint corruption detected");
        }
        
        // Handle version compatibility
        switch (header.version) {
            case 1:
                return deserialize_v1_operations(operation_data);
            case 2:
                return deserialize_v2_operations(operation_data);
            default:
                if (header.version > CURRENT_VERSION) {
                    throw std::runtime_error("Checkpoint created with newer version");
                } else {
                    throw std::runtime_error("Unsupported checkpoint version");
                }
        }
    }
    
private:
    std::vector<WAMRWASINNOperation> deserialize_v1_operations(const std::vector<uint8_t>& data) {
        // Legacy format deserialization with conversion to current format
        auto v1_operations = deserialize_v1_format(data);
        return convert_v1_to_v2(v1_operations);
    }
};
```

## Model Loading Issues

### TensorFlow Lite Model Problems

**Common Issues:**
```cpp
class TensorFlowLiteDebugger {
public:
    void diagnose_model_loading_failure(const std::string& model_path, error result) {
        std::cout << "Diagnosing model loading failure for: " << model_path << std::endl;
        std::cout << "Error code: " << result << std::endl;
        
        // Check file existence and permissions
        if (!std::filesystem::exists(model_path)) {
            std::cout << "ERROR: Model file does not exist" << std::endl;
            return;
        }
        
        if (!std::filesystem::is_regular_file(model_path)) {
            std::cout << "ERROR: Path is not a regular file" << std::endl;
            return;
        }
        
        // Check file size
        auto file_size = std::filesystem::file_size(model_path);
        std::cout << "Model file size: " << file_size << " bytes" << std::endl;
        
        if (file_size == 0) {
            std::cout << "ERROR: Model file is empty" << std::endl;
            return;
        }
        
        // Check file format
        if (!is_valid_tflite_model(model_path)) {
            std::cout << "ERROR: Invalid TensorFlow Lite model format" << std::endl;
            return;
        }
        
        // Check model compatibility
        auto model_info = analyze_model_info(model_path);
        print_model_info(model_info);
        
        // Check available resources
        check_system_resources();
    }
    
private:
    bool is_valid_tflite_model(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        
        // TensorFlow Lite models start with specific magic bytes
        char magic[8];
        file.read(magic, 8);
        
        // Check for TFLite magic number (simplified check)
        return file.gcount() == 8 && magic[0] == 'T' && magic[1] == 'F' && magic[2] == 'L';
    }
    
    struct ModelInfo {
        size_t input_count;
        size_t output_count;
        std::vector<std::vector<int32_t>> input_shapes;
        std::vector<std::vector<int32_t>> output_shapes;
        size_t parameter_count;
    };
    
    ModelInfo analyze_model_info(const std::string& path) {
        // Simplified model analysis - in practice, use TensorFlow Lite C++ API
        ModelInfo info{};
        
        // Load model and extract metadata
        // This would require linking against TensorFlow Lite
        
        return info;
    }
    
    void check_system_resources() {
        // Check available memory
        auto available_memory = get_available_memory();
        std::cout << "Available memory: " << (available_memory / 1024 / 1024) << " MB" << std::endl;
        
        // Check for GPU availability if using GPU target
        if (is_gpu_target_requested()) {
            bool gpu_available = check_gpu_availability();
            std::cout << "GPU available: " << (gpu_available ? "Yes" : "No") << std::endl;
        }
    }
};
```

## Integration Problems

### WAMR Integration Issues

**Symptoms:**
```
Undefined symbol errors
Linking failures with WAMR
WASI-NN functions not found
```

**Solution:**
```cpp
// CMakeLists.txt configuration check
/*
# Ensure WASI-NN is enabled in WAMR build
set(WAMR_BUILD_WASI_NN 1)
set(WAMR_BUILD_WASI_NN_ENABLE_GPU 1)  # If using GPU

# Include WASI-NN in the build
include_directories(${WAMR_ROOT}/core/iwasm/libraries/wasi-nn/include)

# Link against required libraries
target_link_libraries(your_target vmlib tensorflow-lite)
*/

// Runtime verification
class WASINNIntegrationChecker {
public:
    bool verify_wasi_nn_support() {
        std::cout << "Checking WASI-NN integration..." << std::endl;
        
        // Check if WASI-NN symbols are available
        bool symbols_available = check_wasi_nn_symbols();
        std::cout << "WASI-NN symbols available: " << (symbols_available ? "Yes" : "No") << std::endl;
        
        // Check TensorFlow Lite backend
        bool tflite_available = check_tensorflow_lite_backend();
        std::cout << "TensorFlow Lite backend: " << (tflite_available ? "Yes" : "No") << std::endl;
        
        // Check GPU support if enabled
        if (is_gpu_enabled()) {
            bool gpu_support = check_gpu_support();
            std::cout << "GPU support: " << (gpu_support ? "Yes" : "No") << std::endl;
        }
        
        return symbols_available && tflite_available;
    }
    
private:
    bool check_wasi_nn_symbols() {
        // Try to get the WASI-NN export APIs
        NativeSymbol* symbols = nullptr;
        uint32_t count = get_wasi_nn_export_apis(&symbols);
        return count > 0 && symbols != nullptr;
    }
    
    bool check_tensorflow_lite_backend() {
        // Create a minimal TensorFlow Lite model to test backend
        try {
            // This would require actual TensorFlow Lite API calls
            return true;  // Simplified
        } catch (...) {
            return false;
        }
    }
};
```

## Advanced Debugging Techniques

### Operation Diff Analysis

```cpp
class OperationDiffAnalyzer {
public:
    struct OperationDiff {
        size_t operation_index;
        std::string field_name;
        std::string expected_value;
        std::string actual_value;
    };
    
    std::vector<OperationDiff> compare_operation_sequences(
        const std::vector<WAMRWASINNOperation>& expected,
        const std::vector<WAMRWASINNOperation>& actual) {
        
        std::vector<OperationDiff> diffs;
        
        size_t min_size = std::min(expected.size(), actual.size());
        
        for (size_t i = 0; i < min_size; ++i) {
            auto operation_diffs = compare_single_operation(expected[i], actual[i]);
            for (auto& diff : operation_diffs) {
                diff.operation_index = i;
                diffs.push_back(diff);
            }
        }
        
        // Check for size differences
        if (expected.size() != actual.size()) {
            OperationDiff size_diff;
            size_diff.operation_index = min_size;
            size_diff.field_name = "sequence_length";
            size_diff.expected_value = std::to_string(expected.size());
            size_diff.actual_value = std::to_string(actual.size());
            diffs.push_back(size_diff);
        }
        
        return diffs;
    }
    
    void print_diff_report(const std::vector<OperationDiff>& diffs) {
        if (diffs.empty()) {
            std::cout << "No differences found in operation sequences" << std::endl;
            return;
        }
        
        std::cout << "Found " << diffs.size() << " differences:" << std::endl;
        
        for (const auto& diff : diffs) {
            std::cout << "  Operation " << diff.operation_index 
                     << ", field '" << diff.field_name << "':" << std::endl;
            std::cout << "    Expected: " << diff.expected_value << std::endl;
            std::cout << "    Actual:   " << diff.actual_value << std::endl;
        }
    }
    
private:
    std::vector<OperationDiff> compare_single_operation(
        const WAMRWASINNOperation& expected, 
        const WAMRWASINNOperation& actual) {
        
        std::vector<OperationDiff> diffs;
        
        if (expected.type != actual.type) {
            diffs.push_back({"", "type", 
                           std::to_string(static_cast<int>(expected.type)),
                           std::to_string(static_cast<int>(actual.type))});
        }
        
        if (expected.sequence_id != actual.sequence_id) {
            diffs.push_back({"", "sequence_id", 
                           std::to_string(expected.sequence_id),
                           std::to_string(actual.sequence_id)});
        }
        
        // Compare type-specific fields
        switch (expected.type) {
            case WASINNOperationType::LOAD_MODEL:
                if (expected.model_name != actual.model_name) {
                    diffs.push_back({"", "model_name", expected.model_name, actual.model_name});
                }
                break;
                
            case WASINNOperationType::SET_INPUT:
                if (expected.tensor_dims != actual.tensor_dims) {
                    diffs.push_back({"", "tensor_dims", 
                                   format_vector(expected.tensor_dims),
                                   format_vector(actual.tensor_dims)});
                }
                if (expected.tensor_data != actual.tensor_data) {
                    diffs.push_back({"", "tensor_data", 
                                   "size=" + std::to_string(expected.tensor_data.size()),
                                   "size=" + std::to_string(actual.tensor_data.size())});
                }
                break;
        }
        
        return diffs;
    }
    
    std::string format_vector(const std::vector<uint32_t>& vec) {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < vec.size(); ++i) {
            oss << vec[i];
            if (i < vec.size() - 1) oss << ",";
        }
        oss << "]";
        return oss.str();
    }
};
```

### Comprehensive Test Suite

```cpp
class WAMRWASINNTestSuite {
public:
    void run_all_tests() {
        std::cout << "Running WAMR WASI-NN Test Suite..." << std::endl;
        
        int passed = 0, failed = 0;
        
        auto tests = {
            std::make_pair("Basic Recording", &WAMRWASINNTestSuite::test_basic_recording),
            std::make_pair("Replay Functionality", &WAMRWASINNTestSuite::test_replay_functionality),
            std::make_pair("Memory Management", &WAMRWASINNTestSuite::test_memory_management),
            std::make_pair("Error Handling", &WAMRWASINNTestSuite::test_error_handling),
            std::make_pair("Large Tensor Handling", &WAMRWASINNTestSuite::test_large_tensors),
            std::make_pair("Concurrent Access", &WAMRWASINNTestSuite::test_concurrent_access)
        };
        
        for (const auto& test : tests) {
            try {
                std::cout << "  Running " << test.first << "... ";
                (this->*test.second)();
                std::cout << "PASSED" << std::endl;
                passed++;
            } catch (const std::exception& e) {
                std::cout << "FAILED: " << e.what() << std::endl;
                failed++;
            }
        }
        
        std::cout << "Test Results: " << passed << " passed, " << failed << " failed" << std::endl;
    }
    
private:
    void test_basic_recording() {
        // Test basic recording functionality
        WAMRWASINNContext ctx;
        ctx.enable_recording(true);
        
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::COMPUTE;
        op.sequence_id = 0;
        
        ctx.record_operation(op);
        
        if (ctx.recorded_operations.size() != 1) {
            throw std::runtime_error("Recording failed");
        }
    }
    
    void test_replay_functionality() {
        // Test replay functionality
        WAMRWASINNContext ctx;
        
        // Create test operations
        for (int i = 0; i < 5; ++i) {
            WAMRWASINNOperation op;
            op.sequence_id = i;
            op.type = WASINNOperationType::COMPUTE;
            ctx.recorded_operations.push_back(op);
        }
        
        ctx.enable_replay(true);
        ctx.reset_replay();
        
        // Verify replay order
        for (int i = 0; i < 5; ++i) {
            auto* op = ctx.get_next_operation();
            if (!op || op->sequence_id != i) {
                throw std::runtime_error("Replay order incorrect");
            }
        }
        
        // Should return null after all operations
        if (ctx.get_next_operation() != nullptr) {
            throw std::runtime_error("Replay should have ended");
        }
    }
    
    void test_memory_management() {
        // Test memory usage and cleanup
        WAMRWASINNContext ctx;
        
        // Create large number of operations
        for (int i = 0; i < 1000; ++i) {
            WAMRWASINNOperation op;
            op.sequence_id = i;
            op.type = WASINNOperationType::SET_INPUT;
            op.tensor_data.resize(1024);  // 1KB per operation
            ctx.recorded_operations.push_back(op);
        }
        
        size_t memory_usage = calculate_memory_usage(ctx);
        if (memory_usage < 1000 * 1024) {  // Should be at least 1MB
            throw std::runtime_error("Memory calculation incorrect");
        }
        
        // Clear and verify cleanup
        ctx.recorded_operations.clear();
        size_t after_clear = calculate_memory_usage(ctx);
        if (after_clear >= memory_usage) {
            throw std::runtime_error("Memory not properly freed");
        }
    }
    
    void test_error_handling() {
        // Test various error conditions
        WAMRWASINNContext ctx;
        
        // Test replay without operations
        ctx.enable_replay(true);
        if (ctx.get_next_operation() != nullptr) {
            throw std::runtime_error("Should return null for empty operations");
        }
        
        // Test recording when disabled
        ctx.enable_recording(false);
        size_t initial_count = ctx.recorded_operations.size();
        
        WAMRWASINNOperation op;
        ctx.record_operation(op);
        
        if (ctx.recorded_operations.size() != initial_count) {
            throw std::runtime_error("Recording should be disabled");
        }
    }
    
    void test_large_tensors() {
        // Test handling of large tensor data
        const size_t LARGE_SIZE = 100 * 1024 * 1024;  // 100MB
        
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::SET_INPUT;
        op.tensor_data.resize(LARGE_SIZE);
        
        // Fill with test pattern
        for (size_t i = 0; i < LARGE_SIZE; ++i) {
            op.tensor_data[i] = static_cast<uint8_t>(i % 256);
        }
        
        WAMRWASINNContext ctx;
        ctx.record_operation(op);
        
        // Verify data integrity
        auto& recorded_op = ctx.recorded_operations[0];
        if (recorded_op.tensor_data.size() != LARGE_SIZE) {
            throw std::runtime_error("Large tensor size mismatch");
        }
        
        // Verify data pattern
        for (size_t i = 0; i < std::min(LARGE_SIZE, size_t(1000)); ++i) {
            if (recorded_op.tensor_data[i] != static_cast<uint8_t>(i % 256)) {
                throw std::runtime_error("Large tensor data corruption");
            }
        }
    }
    
    void test_concurrent_access() {
        // Test thread safety
        WAMRWASINNContext ctx;
        ctx.enable_recording(true);
        
        std::atomic<int> operation_counter{0};
        std::vector<std::thread> threads;
        
        // Launch multiple threads to record operations
        for (int t = 0; t < 4; ++t) {
            threads.emplace_back([&ctx, &operation_counter]() {
                for (int i = 0; i < 100; ++i) {
                    WAMRWASINNOperation op;
                    op.sequence_id = operation_counter++;
                    op.type = WASINNOperationType::COMPUTE;
                    
                    // Note: This test assumes external synchronization
                    // The actual implementation may need mutex protection
                    ctx.record_operation(op);
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Verify all operations were recorded
        if (ctx.recorded_operations.size() != 400) {
            throw std::runtime_error("Concurrent recording failed");
        }
    }
};
```

This comprehensive troubleshooting guide provides detailed solutions for common issues, advanced debugging techniques, and a complete test suite to verify the WASI-NN record and replay functionality.