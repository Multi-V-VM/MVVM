/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andi Quinn
 *
 *  SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 *  Copyright 2025 Regents of the University of California
 *  UC Santa Cruz Sluglab.
 */

#if defined(WAMR_BUILD_WASI_NN) && WAMR_BUILD_WASI_NN != 0

#include "wamr_wasi_nn_context.h"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_read_write.h"
#include "wasi_nn.h"
#include "wasm_export.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define MAX_MODEL_SIZE 85000000
#define MAX_OUTPUT_TENSOR_SIZE 1000000
#define INPUT_TENSOR_DIMS 4
#define EPSILON 1e-8

// External variables that will be provided by the main executable
extern WAMRInstance *wamr;
extern WriteStream *writer;
extern std::vector<std::unique_ptr<WAMRExecEnv>> as;
extern std::string offload_addr;
extern std::string target;

// Global map to store MVVM contexts per module instance
static std::unordered_map<wasm_module_inst_t, WAMRWASINNContext *> g_mvvm_contexts;
static std::mutex g_contexts_mutex;

// Context methods implementation
void WAMRWASINNContext::record_operation(const WAMRWASINNOperation &op) {
    if (!recording_enabled)
        return;

    // 记录操作
    recorded_operations.push_back(op);
    operation_count++;

    // 更新内存使用统计
    size_t op_size = op.get_memory_size();
    total_memory_size += op_size;

    // 更新峰值内存使用
    size_t current_size = calculate_current_size();
    if (current_size > peak_memory_size) {
        peak_memory_size = current_size;
    }
}

WAMRWASINNOperation *WAMRWASINNContext::get_next_operation() {
    if (!replaying_enabled || replay_position >= recorded_operations.size()) {
        return nullptr;
    }
    return &recorded_operations[replay_position++];
}

void WAMRWASINNContext::dump_impl(WASINNContext *env) {
    // Store existing models (only if wamr instance is available)
    if (wamr != nullptr) {
        for (auto &model : wamr->nn_context) {
            this->models.emplace_back(model);
        }
    }

    // Record current state information
    this->is_initialized = env->is_model_loaded;
    this->current_encoding = env->current_encoding;

    // Stop recording during dump to avoid recording the dump process itself
    bool was_recording = recording_enabled;
    recording_enabled = false;

    // Restore recording state
    recording_enabled = was_recording;
}

void WAMRWASINNContext::restore_impl(WASINNContext *env) {
    // Enable replay mode
    enable_replay(true);
    enable_recording(false);
    reset_replay();

    // Clear mappings from previous restore
    graph_mapping.clear();
    context_mapping.clear();

    // Replay all recorded operations
    while (replay_position < recorded_operations.size()) {
        WAMRWASINNOperation *op = get_next_operation();
        if (!op)
            break;

        switch (op->type) {
        case WASINNOperationType::LOAD_MODEL: {
            // Load model from file
            FILE *pFile = fopen(op->model_name.c_str(), "r");
            if (pFile == nullptr)
                continue;

            uint8_t *buffer = (uint8_t *)malloc(sizeof(uint8_t) * MAX_MODEL_SIZE);
            if (buffer == nullptr) {
                fclose(pFile);
                continue;
            }

            size_t result = fread(buffer, 1, MAX_MODEL_SIZE, pFile);
            if (result <= 0) {
                fclose(pFile);
                free(buffer);
                continue;
            }

            graph_builder_array arr;
            arr.size = 1;
            arr.buf = (graph_builder *)malloc(sizeof(graph_builder));
            if (arr.buf == nullptr) {
                fclose(pFile);
                free(buffer);
                continue;
            }

            arr.buf[0].size = result;
            arr.buf[0].buf = buffer;

            graph new_graph;
            // Note: In a real implementation, this would need a proper wasm_exec_env_t
            // For now, we'll just create a dummy success response for testing
            error res = success;
            new_graph = op->graph_id; // Use the original graph ID
            if (res == success) {
                graph_mapping[op->graph_id] = new_graph;
            }

            fclose(pFile);
            free(buffer);
            free(arr.buf);
            break;
        }

        case WASINNOperationType::INIT_EXECUTION_CONTEXT: {
            auto graph_it = graph_mapping.find(op->graph_id);
            if (graph_it != graph_mapping.end()) {
                graph_execution_context new_ctx;
                // Note: In a real implementation, this would call the proper WASI-NN API
                // For now, create a dummy success response for testing
                new_ctx = op->ctx_id; // Use the original context ID
                context_mapping[op->ctx_id] = new_ctx;
            }
            break;
        }

        case WASINNOperationType::SET_INPUT: {
            auto ctx_it = context_mapping.find(op->ctx_id);
            if (ctx_it != context_mapping.end()) {
                tensor_dimensions dims;
                dims.size = op->tensor_dims.size();
                dims.buf = (uint32_t *)malloc(dims.size * sizeof(uint32_t));
                if (dims.buf == nullptr)
                    continue;

                for (size_t i = 0; i < dims.size; ++i) {
                    dims.buf[i] = op->tensor_dims[i];
                }

                tensor input_tensor;
                input_tensor.dimensions = &dims;
                input_tensor.type = op->data_type;
                input_tensor.data = (uint8_t *)op->tensor_data.data();

                // Note: In a real implementation, this would call the proper WASI-NN API
                // For now, just continue for testing purposes

                free(dims.buf);
            }
            break;
        }

        case WASINNOperationType::COMPUTE: {
            auto ctx_it = context_mapping.find(op->ctx_id);
            if (ctx_it != context_mapping.end()) {
                // Note: In a real implementation, this would call the proper WASI-NN API
                // For now, just continue for testing purposes
            }
            break;
        }

        case WASINNOperationType::GET_OUTPUT: {
            auto ctx_it = context_mapping.find(op->ctx_id);
            if (ctx_it != context_mapping.end()) {
                uint8_t *output_buffer = (uint8_t *)malloc(MAX_OUTPUT_TENSOR_SIZE);
                if (output_buffer != nullptr) {
                    uint32_t output_size = MAX_OUTPUT_TENSOR_SIZE;
                    // Note: In a real implementation, this would call the proper WASI-NN API
                    // For now, just continue for testing purposes
                    free(output_buffer);
                }
            }
            break;
        }
        }
    }

    // Restore environment state
    env->is_model_loaded = this->is_initialized;
    env->current_encoding = this->current_encoding;

    // Re-enable recording for future operations
    enable_recording(true);
    enable_replay(false);
}

// Recorder methods implementation
extern "C" {

void wamr_wasi_nn_recorder_init(wasm_module_inst_t instance) {
    std::lock_guard<std::mutex> lock(g_contexts_mutex);

    if (g_mvvm_contexts.find(instance) == g_mvvm_contexts.end()) {
        g_mvvm_contexts[instance] = new WAMRWASINNContext();
    }
}

void wamr_wasi_nn_recorder_cleanup(wasm_module_inst_t instance) {
    std::lock_guard<std::mutex> lock(g_contexts_mutex);

    auto it = g_mvvm_contexts.find(instance);
    if (it != g_mvvm_contexts.end()) {
        delete it->second;
        g_mvvm_contexts.erase(it);
    }
}

WAMRWASINNContext *wamr_get_wasi_nn_mvvm_context(wasm_module_inst_t instance) {
    std::lock_guard<std::mutex> lock(g_contexts_mutex);

    auto it = g_mvvm_contexts.find(instance);
    if (it != g_mvvm_contexts.end()) {
        return it->second;
    }
    return nullptr;
}

// Wrapper functions with recording capability
error wamr_wasi_nn_load_with_recording(wasm_exec_env_t exec_env, graph_builder_wasm *builder,
                                       uint32_t builder_wasm_size, graph_encoding encoding, execution_target target,
                                       graph *g, const char *model_name) {

    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext *mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);

    // Call the original WASI-NN load function
    extern error wasi_nn_load(wasm_exec_env_t, graph_builder_wasm *, uint32_t, graph_encoding, execution_target,
                              graph *);
    error result = wasi_nn_load(exec_env, builder, builder_wasm_size, encoding, target, g);

    // Record the operation if successful and recording is enabled
    if (result == success && mvvm_ctx && mvvm_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::LOAD_MODEL;
        op.sequence_id = mvvm_ctx->operation_sequence++;
        op.model_name = model_name ? std::string(model_name) : "";
        op.encoding = encoding;
        op.target = target;
        op.graph_id = *g;

        mvvm_ctx->record_operation(op);
    }

    return result;
}

error wamr_wasi_nn_init_execution_context_with_recording(wasm_exec_env_t exec_env, graph g,
                                                         graph_execution_context *ctx) {

    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext *mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);

    // Call the original WASI-NN init_execution_context function
    extern error wasi_nn_init_execution_context(wasm_exec_env_t, graph, graph_execution_context *);
    error result = wasi_nn_init_execution_context(exec_env, g, ctx);

    // Record the operation if successful and recording is enabled
    if (result == success && mvvm_ctx && mvvm_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::INIT_EXECUTION_CONTEXT;
        op.sequence_id = mvvm_ctx->operation_sequence++;
        op.graph_id = g;
        op.ctx_id = *ctx;

        mvvm_ctx->record_operation(op);
    }

    return result;
}

error wamr_wasi_nn_set_input_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx, uint32_t index,
                                            tensor_wasm *input_tensor) {

    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext *mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);

    // Convert to native tensor for recording
    tensor input_tensor_native = {0};
    error conversion_result = tensor_app_native(instance, input_tensor, &input_tensor_native);

    // Call the original WASI-NN set_input function
    extern error wasi_nn_set_input(wasm_exec_env_t, graph_execution_context, uint32_t, tensor_wasm *);
    error result = wasi_nn_set_input(exec_env, ctx, index, input_tensor);

    // Record the operation if successful and recording is enabled
    if (result == success && mvvm_ctx && mvvm_ctx->recording_enabled && conversion_result == success) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::SET_INPUT;
        op.sequence_id = mvvm_ctx->operation_sequence++;
        op.ctx_id = ctx;
        op.input_index = index;

        // Record tensor data
        if (input_tensor_native.data && input_tensor_native.dimensions) {
            size_t total_elements = 1;
            for (uint32_t i = 0; i < input_tensor_native.dimensions->size; ++i) {
                total_elements *= input_tensor_native.dimensions->buf[i];
                op.tensor_dims.push_back(input_tensor_native.dimensions->buf[i]);
            }

            // Calculate data size based on tensor type
            size_t element_size = 4; // Default to float32
            switch (input_tensor_native.type) {
            case fp16:
                element_size = 2;
                break;
            case fp32:
                element_size = 4;
                break;
            case up8:
                element_size = 1;
                break;
            case ip32:
                element_size = 4;
                break;
            default:
                element_size = 4;
                break;
            }

            size_t data_size = total_elements * element_size;
            op.tensor_data.resize(data_size);
            memcpy(op.tensor_data.data(), input_tensor_native.data, data_size);
            op.data_type = input_tensor_native.type;
        }

        mvvm_ctx->record_operation(op);
    }

    // Cleanup native tensor
    if (input_tensor_native.dimensions) {
        wasm_runtime_free(input_tensor_native.dimensions);
    }

    return result;
}

error wamr_wasi_nn_compute_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx) {

    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext *mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);

    // Call the original WASI-NN compute function
    extern error wasi_nn_compute(wasm_exec_env_t, graph_execution_context);
    error result = wasi_nn_compute(exec_env, ctx);

    // Record the operation if successful and recording is enabled
    if (result == success && mvvm_ctx && mvvm_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::COMPUTE;
        op.sequence_id = mvvm_ctx->operation_sequence++;
        op.ctx_id = ctx;

        mvvm_ctx->record_operation(op);
    }

    return result;
}

error wamr_wasi_nn_get_output_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx, uint32_t index,
                                             tensor_data output_tensor, uint32_t output_tensor_len,
                                             uint32_t *output_tensor_size) {

    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext *mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);

    // Call the original WASI-NN get_output function
    extern error wasi_nn_get_output(wasm_exec_env_t, graph_execution_context, uint32_t, tensor_data, uint32_t,
                                    uint32_t *);
    error result = wasi_nn_get_output(exec_env, ctx, index, output_tensor, output_tensor_len, output_tensor_size);

    // Record the operation if successful and recording is enabled
    if (result == success && mvvm_ctx && mvvm_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::GET_OUTPUT;
        op.sequence_id = mvvm_ctx->operation_sequence++;
        op.ctx_id = ctx;
        op.output_index = index;

        // Record output data
        if (output_tensor && output_tensor_size && *output_tensor_size > 0) {
            op.output_data.resize(*output_tensor_size);
            memcpy(op.output_data.data(), output_tensor, *output_tensor_size);
            op.output_size = *output_tensor_size;
        }

        mvvm_ctx->record_operation(op);
    }

    return result;
}

// 获取记录器的内存使用情况
size_t wamr_wasi_nn_get_recorder_memory_size(wasm_module_inst_t instance) {
    WAMRWASINNContext *ctx = wamr_get_wasi_nn_mvvm_context(instance);
    if (ctx) {
        return ctx->get_current_memory_size();
    }
    return 0;
}

size_t wamr_wasi_nn_get_recorder_peak_memory_size(wasm_module_inst_t instance) {
    WAMRWASINNContext *ctx = wamr_get_wasi_nn_mvvm_context(instance);
    if (ctx) {
        return ctx->get_peak_memory_size();
    }
    return 0;
}

size_t wamr_wasi_nn_get_recorder_operation_count(wasm_module_inst_t instance) {
    WAMRWASINNContext *ctx = wamr_get_wasi_nn_mvvm_context(instance);
    if (ctx) {
        return ctx->get_operation_count();
    }
    return 0;
}

} // extern "C"

// Helper functions
static WAMRWASINNContext *get_current_wamr_nn_context() {
    // Get the current WAMR WASI-NN context (only if wamr instance is available)
    if (wamr != nullptr) {
        return &wamr->nn_context_mvvm;
    }
    return nullptr;
}

#else // WASM_ENABLE_WASI_NN == 0

// Minimal type definitions for stub implementations
#include <cstdint>
#include <cstddef>

// Forward declarations
struct WAMRWASINNContext;
typedef struct WAMRWASINNContext WAMRWASINNContext;

// Basic WASM types
typedef void *wasm_module_inst_t;
typedef void *wasm_exec_env_t;

typedef enum {
    success = 0,
    invalid_argument,
    invalid_encoding,
    missing_memory,
    busy,
    runtime_error,
    unsupported_operation,
    too_large,
    not_found
} error;

typedef uint32_t graph;
typedef uint32_t graph_execution_context;
typedef enum { tensorflowlite = 4 } graph_encoding;
typedef enum { cpu = 0, gpu = 1, tpu = 2 } execution_target;

typedef struct {
    uint32_t buf_offset;
    uint32_t size;
} graph_builder_wasm;

typedef struct {
    uint32_t buf_offset;
    uint32_t size;
} tensor_wasm;

typedef uint8_t *tensor_data;

// Additional stub type definitions  
enum class WASINNOperationType { LOAD_MODEL, INIT_EXECUTION_CONTEXT, SET_INPUT, COMPUTE, GET_OUTPUT };
typedef enum { fp16, fp32, up8, ip32 } tensor_type;

#include <string>
#include <vector>

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
    
    // For GET_OUTPUT
    uint32_t output_index;
    std::vector<uint8_t> output_data;
    uint32_t output_size;
};

struct WASINNContext {
    bool is_model_loaded;
    graph_encoding current_encoding;
};

// Complete WAMRWASINNContext definition
struct WAMRWASINNContext {
    bool is_initialized = false;
    graph_encoding current_encoding = tensorflowlite;
    
    // Record and replay data
    std::vector<WAMRWASINNOperation> recorded_operations;
    size_t replay_position = 0;
    bool recording_enabled = true;
    bool replaying_enabled = false;
    
    // Size tracking
    size_t total_memory_size = 0;
    size_t peak_memory_size = 0;
    size_t operation_count = 0;
    
    // Methods
    void record_operation(const WAMRWASINNOperation &op);
    WAMRWASINNOperation *get_next_operation();
    void dump_impl(WASINNContext *env);
    void restore_impl(WASINNContext *env);
    
    // Inline methods
    void enable_recording(bool enable) { recording_enabled = enable; }
    void enable_replay(bool enable) { replaying_enabled = enable; }
    void reset_replay() { replay_position = 0; }
};

// Stub implementations when WASI-NN is disabled
extern "C" {

void wamr_wasi_nn_recorder_init(wasm_module_inst_t instance) {
    // Stub implementation
}

void wamr_wasi_nn_recorder_cleanup(wasm_module_inst_t instance) {
    // Stub implementation  
}

WAMRWASINNContext *wamr_get_wasi_nn_mvvm_context(wasm_module_inst_t instance) {
    return nullptr;
}

error wamr_wasi_nn_load_with_recording(wasm_exec_env_t exec_env, graph_builder_wasm *builder,
                                       uint32_t builder_wasm_size, graph_encoding encoding, execution_target target,
                                       graph *g, const char *model_name) {
    return invalid_encoding;
}

error wamr_wasi_nn_init_execution_context_with_recording(wasm_exec_env_t exec_env, graph g,
                                                         graph_execution_context *ctx) {
    return invalid_encoding;
}

error wamr_wasi_nn_set_input_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx, uint32_t index,
                                            tensor_wasm *input_tensor) {
    return invalid_encoding;
}

error wamr_wasi_nn_compute_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx) {
    return invalid_encoding;
}

error wamr_wasi_nn_get_output_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx, uint32_t index,
                                             tensor_data output_tensor, uint32_t output_tensor_len,
                                             uint32_t *output_tensor_size) {
    return invalid_encoding;
}

size_t wamr_wasi_nn_get_recorder_memory_size(wasm_module_inst_t instance) {
    return 0;
}

size_t wamr_wasi_nn_get_recorder_peak_memory_size(wasm_module_inst_t instance) {
    return 0;
}

size_t wamr_wasi_nn_get_recorder_operation_count(wasm_module_inst_t instance) {
    return 0;
}

} // extern "C"

// Stub implementations for WAMRWASINNContext methods when WASI-NN is disabled
void WAMRWASINNContext::record_operation(const WAMRWASINNOperation &op) {
    // Basic implementation for testing - just record the operation
    recorded_operations.push_back(op);
}

WAMRWASINNOperation *WAMRWASINNContext::get_next_operation() {
    // Basic implementation for testing
    if (replay_position >= recorded_operations.size()) {
        return nullptr;
    }
    return &recorded_operations[replay_position++];
}

void WAMRWASINNContext::dump_impl(WASINNContext *env) {
    // Stub implementation - do nothing
}

void WAMRWASINNContext::restore_impl(WASINNContext *env) {
    // Stub implementation - do nothing
}

#endif // WAMR_BUILD_WASI_NN