# WASI-NN Record and Replay Usage Guide

This document describes how to use the WASI-NN record and replay functionality implemented in the MVVM project.

## Overview

The WASI-NN record and replay system allows capturing all WASI-NN operations during execution and replaying them during restoration. This is essential for maintaining consistent neural network inference behavior across checkpoints and migrations.

## Architecture

### Components

1. **WAMRWASINNContext**: Main context structure that manages recording and replay state
2. **WAMRWASINNOperation**: Structure representing a single WASI-NN operation
3. **WASINNOperationType**: Enum defining different types of operations that can be recorded
4. **wamr_wasi_nn_recorder**: C interface for integrating with WAMR

### Supported Operations

The system records and replays the following WASI-NN operations:

- `LOAD_MODEL`: Loading ML models
- `INIT_EXECUTION_CONTEXT`: Creating execution contexts
- `SET_INPUT`: Setting input tensors
- `COMPUTE`: Running inference
- `GET_OUTPUT`: Retrieving output tensors

## Usage

### Integration with WAMR

To use the record and replay functionality:

1. **Initialize the recorder** for a module instance:
   ```c
   wamr_wasi_nn_recorder_init(module_instance);
   ```

2. **Use wrapped WASI-NN functions** instead of direct calls:
   ```c
   // Instead of wasi_nn_load, use:
   wamr_wasi_nn_load_with_recording(exec_env, builder, size, encoding, target, graph, model_name);
   
   // Instead of wasi_nn_set_input, use:
   wamr_wasi_nn_set_input_with_recording(exec_env, ctx, index, tensor);
   ```

3. **Enable/disable recording** as needed:
   ```cpp
   WAMRWASINNContext* ctx = wamr_get_wasi_nn_mvvm_context(instance);
   ctx->enable_recording(true);  // Enable recording
   ctx->enable_recording(false); // Disable recording
   ```

4. **Cleanup** when done:
   ```c
   wamr_wasi_nn_recorder_cleanup(module_instance);
   ```

### Checkpoint and Restore

During checkpoint:
```cpp
WAMRWASINNContext mvvm_ctx;
mvvm_ctx.dump_impl(wasi_nn_context);
// The recorded operations are now stored in mvvm_ctx.recorded_operations
```

During restore:
```cpp
WAMRWASINNContext mvvm_ctx;
// Load previously recorded operations...
mvvm_ctx.restore_impl(wasi_nn_context);
// All recorded operations are replayed
```

## Data Structures

### WAMRWASINNOperation

Each operation contains:
- `type`: The type of operation (LOAD_MODEL, SET_INPUT, etc.)
- `sequence_id`: Sequential ID for ordering
- `model_name`: Path to model file (for LOAD_MODEL)
- `tensor_data`: Input/output tensor data
- `tensor_dims`: Tensor dimensions
- `graph_id`, `ctx_id`: Graph and context identifiers

### Recording State

- `recording_enabled`: Whether to record new operations
- `replaying_enabled`: Whether currently in replay mode
- `operation_sequence`: Counter for operation ordering
- `recorded_operations`: Vector of all recorded operations
- `replay_position`: Current position during replay

## Example Usage

```cpp
#include "wamr_wasi_nn_recorder.h"

// Initialize
wasm_module_inst_t instance = /* ... */;
wamr_wasi_nn_recorder_init(instance);

// Get context for control
WAMRWASINNContext* ctx = wamr_get_wasi_nn_mvvm_context(instance);

// Record operations
ctx->enable_recording(true);

// Load model
graph g;
wamr_wasi_nn_load_with_recording(exec_env, builder, size, 
                                tensorflowlite, cpu, &g, "/path/to/model.tflite");

// Initialize context
graph_execution_context exec_ctx;
wamr_wasi_nn_init_execution_context_with_recording(exec_env, g, &exec_ctx);

// Set input
wamr_wasi_nn_set_input_with_recording(exec_env, exec_ctx, 0, input_tensor);

// Compute
wamr_wasi_nn_compute_with_recording(exec_env, exec_ctx);

// Get output
wamr_wasi_nn_get_output_with_recording(exec_env, exec_ctx, 0, output_buffer, 
                                      buffer_size, &output_size);

// During checkpoint
WASINNContext wasi_nn_ctx;
ctx->dump_impl(&wasi_nn_ctx);

// During restore
ctx->restore_impl(&wasi_nn_ctx);

// Cleanup
wamr_wasi_nn_recorder_cleanup(instance);
```

## Testing

Run the test suite:
```bash
cd test
./wasi_nn_record_replay_test
```

The test verifies:
- Recording of all operation types
- Proper sequencing of operations
- Replay functionality
- Data integrity during record/replay cycles

## Thread Safety

The implementation uses mutex protection for the global context map to ensure thread safety when multiple module instances are used concurrently.

## Limitations

1. Model files must be accessible during both recording and replay
2. Tensor data is stored in memory, which may use significant space for large models
3. Currently supports TensorFlow Lite backend only
4. File paths in recorded operations must remain valid during replay

## Future Enhancements

- Support for additional ML backends (ONNX, PyTorch, etc.)
- Compression of recorded tensor data
- Partial replay capabilities
- Integration with distributed checkpointing systems