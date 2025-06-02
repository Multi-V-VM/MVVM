# WASI-NN Record and Replay API Reference

## Table of Contents
1. [Core Types and Enums](#core-types-and-enums)
2. [Context Management](#context-management)
3. [Recording Functions](#recording-functions)
4. [Replay Functions](#replay-functions)
5. [Configuration and Control](#configuration-and-control)
6. [Error Handling](#error-handling)
7. [Utility Functions](#utility-functions)
8. [C++ Class Interface](#c-class-interface)

## Core Types and Enums

### WASINNOperationType

Enumeration of supported WASI-NN operation types.

```cpp
enum class WASINNOperationType {
    LOAD_MODEL,              // Loading ML models from file
    INIT_EXECUTION_CONTEXT,  // Creating execution contexts for inference
    SET_INPUT,               // Setting input tensors for inference
    COMPUTE,                 // Running inference computation
    GET_OUTPUT               // Retrieving inference results
};
```

**Values:**
- `LOAD_MODEL`: Operation for loading ML models into memory
- `INIT_EXECUTION_CONTEXT`: Operation for creating execution contexts
- `SET_INPUT`: Operation for providing input data to models
- `COMPUTE`: Operation for running inference
- `GET_OUTPUT`: Operation for retrieving inference results

### WAMRWASINNOperation

Structure representing a single recorded WASI-NN operation.

```cpp
struct WAMRWASINNOperation {
    WASINNOperationType type;
    uint32_t sequence_id;
    
    // For LOAD_MODEL
    std::string model_name;
    graph_encoding encoding;
    execution_target target;
    graph graph_id;
    
    // For INIT_EXECUTION_CONTEXT
    graph_execution_context ctx_id;
    
    // For SET_INPUT
    uint32_t input_index;
    std::vector<uint8_t> tensor_data;
    std::vector<uint32_t> tensor_dims;
    tensor_type data_type;
    
    // For COMPUTE - no additional data needed
    
    // For GET_OUTPUT
    uint32_t output_index;
    std::vector<uint8_t> output_data;
    uint32_t output_size;
};
```

**Fields:**
- `type`: Type of the operation
- `sequence_id`: Sequential identifier for operation ordering
- `model_name`: Path to model file (LOAD_MODEL only)
- `encoding`: Model encoding format (LOAD_MODEL only)
- `target`: Execution target (LOAD_MODEL only)
- `graph_id`: Graph identifier
- `ctx_id`: Execution context identifier
- `input_index`: Input tensor index (SET_INPUT only)
- `tensor_data`: Tensor data bytes
- `tensor_dims`: Tensor dimensions array
- `data_type`: Tensor data type
- `output_index`: Output tensor index (GET_OUTPUT only)
- `output_data`: Output tensor data (GET_OUTPUT only)
- `output_size`: Size of output data (GET_OUTPUT only)

### WAMRWASINNContext

Main context structure for managing record and replay state.

```cpp
struct WAMRWASINNContext {
    bool is_initialized;
    graph_encoding current_encoding;
    std::vector<WAMRWASINNModel> models;
    
    // Record and replay functionality
    bool recording_enabled;
    bool replaying_enabled;
    uint32_t operation_sequence;
    std::vector<WAMRWASINNOperation> recorded_operations;
    size_t replay_position;
    
    // Context mapping for replay
    std::unordered_map<graph, graph> graph_mapping;
    std::unordered_map<graph_execution_context, graph_execution_context> context_mapping;
    
    // Methods
    void dump_impl(WASINNContext *env);
    void restore_impl(WASINNContext *env);
    void record_operation(const WAMRWASINNOperation& op);
    WAMRWASINNOperation* get_next_operation();
    void enable_recording(bool enable);
    void enable_replay(bool enable);
    void reset_replay();
};
```

## Context Management

### wamr_wasi_nn_recorder_init

Initialize the WASI-NN recorder for a module instance.

```c
void wamr_wasi_nn_recorder_init(wasm_module_inst_t instance);
```

**Parameters:**
- `instance`: WASM module instance to initialize recording for

**Description:**
Initializes the recording infrastructure for the specified WASM module instance. This function must be called before using any recording functionality.

**Example:**
```c
wasm_module_inst_t instance = /* initialize your module */;
wamr_wasi_nn_recorder_init(instance);
```

**Thread Safety:** Thread-safe

### wamr_wasi_nn_recorder_cleanup

Cleanup the WASI-NN recorder for a module instance.

```c
void wamr_wasi_nn_recorder_cleanup(wasm_module_inst_t instance);
```

**Parameters:**
- `instance`: WASM module instance to cleanup

**Description:**
Releases all resources associated with the WASI-NN recorder for the specified module instance. Should be called before destroying the module instance.

**Example:**
```c
wamr_wasi_nn_recorder_cleanup(instance);
```

**Thread Safety:** Thread-safe

### wamr_get_wasi_nn_mvvm_context

Retrieve the MVVM WASI-NN context for a module instance.

```c
WAMRWASINNContext* wamr_get_wasi_nn_mvvm_context(wasm_module_inst_t instance);
```

**Parameters:**
- `instance`: WASM module instance

**Returns:**
- Pointer to `WAMRWASINNContext` or `NULL` if not found

**Description:**
Returns the MVVM context associated with the module instance. The context is used for direct control over recording and replay functionality.

**Example:**
```c
WAMRWASINNContext* ctx = wamr_get_wasi_nn_mvvm_context(instance);
if (ctx != NULL) {
    ctx->enable_recording(true);
}
```

**Thread Safety:** Thread-safe

## Recording Functions

### wamr_wasi_nn_load_with_recording

Load a model while recording the operation.

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

**Parameters:**
- `exec_env`: WASM execution environment
- `builder`: Model data builder
- `builder_wasm_size`: Size of builder data
- `encoding`: Model encoding (e.g., `tensorflowlite`)
- `target`: Execution target (`cpu`, `gpu`, `tpu`)
- `g`: Output graph handle
- `model_name`: Path to model file (for recording)

**Returns:**
- `success` on successful operation
- Error code on failure

**Description:**
Loads a ML model into memory while recording the operation for later replay. The model file path is stored for replay purposes.

**Example:**
```c
graph model_graph;
error result = wamr_wasi_nn_load_with_recording(
    exec_env, 
    &builder, 
    builder_size,
    tensorflowlite, 
    cpu, 
    &model_graph, 
    "/path/to/model.tflite"
);

if (result != success) {
    printf("Failed to load model: %d\n", result);
}
```

**Thread Safety:** Thread-safe per execution environment

### wamr_wasi_nn_init_execution_context_with_recording

Initialize execution context while recording the operation.

```c
error wamr_wasi_nn_init_execution_context_with_recording(
    wasm_exec_env_t exec_env,
    graph g,
    graph_execution_context *ctx
);
```

**Parameters:**
- `exec_env`: WASM execution environment
- `g`: Graph handle
- `ctx`: Output execution context handle

**Returns:**
- `success` on successful operation
- Error code on failure

**Description:**
Creates an execution context for the specified graph while recording the operation.

**Example:**
```c
graph_execution_context exec_ctx;
error result = wamr_wasi_nn_init_execution_context_with_recording(
    exec_env, 
    model_graph, 
    &exec_ctx
);
```

### wamr_wasi_nn_set_input_with_recording

Set input tensor while recording the operation.

```c
error wamr_wasi_nn_set_input_with_recording(
    wasm_exec_env_t exec_env,
    graph_execution_context ctx,
    uint32_t index,
    tensor_wasm *input_tensor
);
```

**Parameters:**
- `exec_env`: WASM execution environment
- `ctx`: Execution context handle
- `index`: Input tensor index
- `input_tensor`: Input tensor data

**Returns:**
- `success` on successful operation
- Error code on failure

**Description:**
Sets input tensor data for inference while recording the complete tensor information including data, dimensions, and type.

**Example:**
```c
tensor_wasm input;
// ... setup input tensor ...

error result = wamr_wasi_nn_set_input_with_recording(
    exec_env, 
    exec_ctx, 
    0, 
    &input
);
```

### wamr_wasi_nn_compute_with_recording

Run inference while recording the operation.

```c
error wamr_wasi_nn_compute_with_recording(
    wasm_exec_env_t exec_env,
    graph_execution_context ctx
);
```

**Parameters:**
- `exec_env`: WASM execution environment
- `ctx`: Execution context handle

**Returns:**
- `success` on successful operation
- Error code on failure

**Description:**
Runs inference computation while recording the operation.

**Example:**
```c
error result = wamr_wasi_nn_compute_with_recording(exec_env, exec_ctx);
```

### wamr_wasi_nn_get_output_with_recording

Get output tensor while recording the operation.

```c
error wamr_wasi_nn_get_output_with_recording(
    wasm_exec_env_t exec_env,
    graph_execution_context ctx,
    uint32_t index,
    tensor_data output_tensor,
    uint32_t output_tensor_len,
    uint32_t *output_tensor_size
);
```

**Parameters:**
- `exec_env`: WASM execution environment
- `ctx`: Execution context handle
- `index`: Output tensor index
- `output_tensor`: Buffer for output data
- `output_tensor_len`: Maximum buffer size
- `output_tensor_size`: Actual output size (output parameter)

**Returns:**
- `success` on successful operation
- Error code on failure

**Description:**
Retrieves inference output while recording the output data for replay.

**Example:**
```c
uint8_t output_buffer[MAX_OUTPUT_SIZE];
uint32_t output_size = MAX_OUTPUT_SIZE;

error result = wamr_wasi_nn_get_output_with_recording(
    exec_env, 
    exec_ctx, 
    0, 
    output_buffer, 
    MAX_OUTPUT_SIZE, 
    &output_size
);
```

## Replay Functions

### WAMRWASINNContext::restore_impl

Restore WASI-NN state from recorded operations.

```cpp
void WAMRWASINNContext::restore_impl(WASINNContext *env);
```

**Parameters:**
- `env`: Native WASI-NN context to restore

**Description:**
Replays all recorded operations to restore the WASI-NN state. This includes reloading models, recreating execution contexts, and restoring input tensors.

**Example:**
```cpp
WAMRWASINNContext ctx;
// ... load recorded operations ...

WASINNContext native_ctx;
ctx.restore_impl(&native_ctx);
```

**Thread Safety:** Not thread-safe (use external synchronization)

### WAMRWASINNContext::get_next_operation

Get the next operation during replay.

```cpp
WAMRWASINNOperation* get_next_operation();
```

**Returns:**
- Pointer to next operation or `nullptr` if replay is complete

**Description:**
Returns the next operation in the replay sequence. Used internally during restore process.

**Example:**
```cpp
WAMRWASINNContext ctx;
ctx.enable_replay(true);

WAMRWASINNOperation* op;
while ((op = ctx.get_next_operation()) != nullptr) {
    // Process operation
    process_operation(*op);
}
```

### WAMRWASINNContext::reset_replay

Reset replay position to beginning.

```cpp
void reset_replay();
```

**Description:**
Resets the replay position to the beginning of the recorded operations list.

**Example:**
```cpp
ctx.reset_replay();
// Replay will start from the first operation
```

## Configuration and Control

### WAMRWASINNContext::enable_recording

Enable or disable operation recording.

```cpp
void enable_recording(bool enable);
```

**Parameters:**
- `enable`: `true` to enable recording, `false` to disable

**Description:**
Controls whether new operations are recorded. Useful for selectively recording only certain phases of execution.

**Example:**
```cpp
// Disable recording during initialization
ctx.enable_recording(false);
initialize_system();

// Enable recording during inference
ctx.enable_recording(true);
run_inference();
```

### WAMRWASINNContext::enable_replay

Enable or disable replay mode.

```cpp
void enable_replay(bool enable);
```

**Parameters:**
- `enable`: `true` to enable replay mode, `false` to disable

**Description:**
Controls whether the context is in replay mode. When enabled, get_next_operation() will return recorded operations.

**Example:**
```cpp
ctx.enable_replay(true);
ctx.reset_replay();
// Now ready for replay
```

### WAMRWASINNContext::record_operation

Manually record an operation.

```cpp
void record_operation(const WAMRWASINNOperation& op);
```

**Parameters:**
- `op`: Operation to record

**Description:**
Manually adds an operation to the recorded operations list. Typically used internally but can be used for custom operation recording.

**Example:**
```cpp
WAMRWASINNOperation custom_op;
custom_op.type = WASINNOperationType::COMPUTE;
custom_op.sequence_id = ctx.operation_sequence++;

ctx.record_operation(custom_op);
```

## Error Handling

### Error Codes

Standard WASI-NN error codes are used throughout the API:

```c
typedef enum {
    success = 0,
    invalid_argument = 1,
    invalid_encoding = 2,
    timeout = 3,
    runtime_error = 4,
    unsupported_operation = 5,
    too_large = 6,
    not_found = 7
} error;
```

### Error Handling Patterns

#### Basic Error Checking

```c
error result = wamr_wasi_nn_load_with_recording(/* ... */);
if (result != success) {
    fprintf(stderr, "Operation failed with error: %d\n", result);
    return result;
}
```

#### Exception-Safe C++ Wrapper

```cpp
class WAMRWASINNException : public std::exception {
    error error_code;
    std::string message;
    
public:
    WAMRWASINNException(error code, const std::string& msg) 
        : error_code(code), message(msg) {}
    
    const char* what() const noexcept override {
        return message.c_str();
    }
    
    error get_error_code() const { return error_code; }
};

void safe_load_model(/* parameters */) {
    error result = wamr_wasi_nn_load_with_recording(/* ... */);
    if (result != success) {
        throw WAMRWASINNException(result, "Failed to load model");
    }
}
```

#### Error Recovery

```cpp
bool try_operation_with_retry(std::function<error()> operation, int max_retries = 3) {
    for (int attempt = 0; attempt < max_retries; ++attempt) {
        error result = operation();
        
        if (result == success) {
            return true;
        }
        
        if (result == timeout && attempt < max_retries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * (attempt + 1)));
            continue;
        }
        
        // Non-recoverable error or max retries reached
        break;
    }
    
    return false;
}
```

## Utility Functions

### Validation Functions

```cpp
// Validate operation sequence integrity
bool validate_operation_sequence(const std::vector<WAMRWASINNOperation>& operations) {
    for (size_t i = 0; i < operations.size(); ++i) {
        if (operations[i].sequence_id != i) {
            return false;
        }
    }
    return true;
}

// Check if context is ready for replay
bool is_ready_for_replay(const WAMRWASINNContext& ctx) {
    return !ctx.recorded_operations.empty() && 
           ctx.replaying_enabled && 
           ctx.replay_position < ctx.recorded_operations.size();
}

// Validate tensor data consistency
bool validate_tensor_data(const WAMRWASINNOperation& op) {
    if (op.type != WASINNOperationType::SET_INPUT) {
        return true;  // Only validate SET_INPUT operations
    }
    
    size_t expected_elements = 1;
    for (uint32_t dim : op.tensor_dims) {
        expected_elements *= dim;
    }
    
    size_t element_size = get_tensor_element_size(op.data_type);
    size_t expected_bytes = expected_elements * element_size;
    
    return op.tensor_data.size() == expected_bytes;
}
```

### Serialization Helpers

```cpp
// Serialize operations to binary format
std::vector<uint8_t> serialize_operations(const std::vector<WAMRWASINNOperation>& operations) {
    std::ostringstream stream;
    
    // Write number of operations
    uint32_t count = operations.size();
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    
    // Write each operation
    for (const auto& op : operations) {
        serialize_operation(stream, op);
    }
    
    std::string data = stream.str();
    return std::vector<uint8_t>(data.begin(), data.end());
}

// Deserialize operations from binary format
std::vector<WAMRWASINNOperation> deserialize_operations(const std::vector<uint8_t>& data) {
    std::istringstream stream(std::string(data.begin(), data.end()));
    
    // Read number of operations
    uint32_t count;
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    
    std::vector<WAMRWASINNOperation> operations;
    operations.reserve(count);
    
    // Read each operation
    for (uint32_t i = 0; i < count; ++i) {
        operations.push_back(deserialize_operation(stream));
    }
    
    return operations;
}
```

### Memory Management Helpers

```cpp
// Calculate memory usage of recorded operations
size_t calculate_memory_usage(const WAMRWASINNContext& ctx) {
    size_t total = sizeof(WAMRWASINNContext);
    
    for (const auto& op : ctx.recorded_operations) {
        total += sizeof(WAMRWASINNOperation);
        total += op.model_name.size();
        total += op.tensor_data.size();
        total += op.tensor_dims.size() * sizeof(uint32_t);
        total += op.output_data.size();
    }
    
    return total;
}

// Optimize memory usage by compressing tensor data
void optimize_memory_usage(WAMRWASINNContext& ctx) {
    for (auto& op : ctx.recorded_operations) {
        if (op.tensor_data.size() > COMPRESSION_THRESHOLD) {
            op.tensor_data = compress_tensor_data(op.tensor_data);
        }
    }
}
```

## C++ Class Interface

### RAII Wrapper Class

```cpp
class WAMRWASINNRecorder {
private:
    wasm_module_inst_t instance;
    WAMRWASINNContext* context;
    bool initialized;
    
public:
    explicit WAMRWASINNRecorder(wasm_module_inst_t inst) 
        : instance(inst), context(nullptr), initialized(false) {
        wamr_wasi_nn_recorder_init(instance);
        context = wamr_get_wasi_nn_mvvm_context(instance);
        if (context) {
            initialized = true;
        }
    }
    
    ~WAMRWASINNRecorder() {
        if (initialized) {
            wamr_wasi_nn_recorder_cleanup(instance);
        }
    }
    
    // Delete copy constructor and assignment operator
    WAMRWASINNRecorder(const WAMRWASINNRecorder&) = delete;
    WAMRWASINNRecorder& operator=(const WAMRWASINNRecorder&) = delete;
    
    // Move constructor and assignment operator
    WAMRWASINNRecorder(WAMRWASINNRecorder&& other) noexcept
        : instance(other.instance), context(other.context), initialized(other.initialized) {
        other.initialized = false;
        other.context = nullptr;
    }
    
    WAMRWASINNRecorder& operator=(WAMRWASINNRecorder&& other) noexcept {
        if (this != &other) {
            if (initialized) {
                wamr_wasi_nn_recorder_cleanup(instance);
            }
            
            instance = other.instance;
            context = other.context;
            initialized = other.initialized;
            
            other.initialized = false;
            other.context = nullptr;
        }
        return *this;
    }
    
    WAMRWASINNContext* get_context() const {
        return context;
    }
    
    bool is_initialized() const {
        return initialized;
    }
    
    void enable_recording(bool enable = true) {
        if (context) {
            context->enable_recording(enable);
        }
    }
    
    void enable_replay(bool enable = true) {
        if (context) {
            context->enable_replay(enable);
        }
    }
    
    size_t get_operation_count() const {
        return context ? context->recorded_operations.size() : 0;
    }
    
    void reset_replay() {
        if (context) {
            context->reset_replay();
        }
    }
    
    void clear_operations() {
        if (context) {
            context->recorded_operations.clear();
            context->operation_sequence = 0;
            context->replay_position = 0;
        }
    }
};
```

### Usage Example

```cpp
#include "wamr_wasi_nn_recorder.h"

int main() {
    // Initialize WASM runtime
    wasm_module_inst_t instance = /* initialize */;
    
    {
        // RAII recorder management
        WAMRWASINNRecorder recorder(instance);
        
        if (!recorder.is_initialized()) {
            std::cerr << "Failed to initialize recorder" << std::endl;
            return 1;
        }
        
        // Enable recording
        recorder.enable_recording(true);
        
        // Use WASI-NN functions with recording
        error result = wamr_wasi_nn_load_with_recording(
            exec_env, &builder, size, tensorflowlite, cpu, &graph, "model.tflite"
        );
        
        if (result != success) {
            std::cerr << "Failed to load model" << std::endl;
            return 1;
        }
        
        // ... perform inference operations ...
        
        // Create checkpoint
        WASINNContext checkpoint_context;
        recorder.get_context()->dump_impl(&checkpoint_context);
        
        // Save checkpoint to file
        save_checkpoint_to_file(recorder.get_context()->recorded_operations, "checkpoint.dat");
        
        std::cout << "Recorded " << recorder.get_operation_count() << " operations" << std::endl;
        
    } // Recorder automatically cleans up here
    
    return 0;
}
```

This API reference provides comprehensive documentation for all functions, types, and usage patterns in the WASI-NN record and replay system.