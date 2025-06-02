/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 *  Copyright 2025 Regents of the University of California
 *  UC Santa Cruz Sluglab.
 */

#define WASM_ENABLE_WASI_NN 1
#if WASM_ENABLE_WASI_NN != 0
#include "wamr_wasi_nn_context.h"
#include "wamr.h"
#include "wasi_nn.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define MAX_MODEL_SIZE 85000000
#define MAX_OUTPUT_TENSOR_SIZE 1000000
#define INPUT_TENSOR_DIMS 4
#define EPSILON 1e-8
extern WAMRInstance *wamr;

void WAMRWASINNContext::record_operation(const WAMRWASINNOperation& op) {
    if (!recording_enabled) return;
    recorded_operations.push_back(op);
}

WAMRWASINNOperation* WAMRWASINNContext::get_next_operation() {
    if (!replaying_enabled || replay_position >= recorded_operations.size()) {
        return nullptr;
    }
    return &recorded_operations[replay_position++];
}

void WAMRWASINNContext::dump_impl(WASINNContext *env) {
    // Store existing models
    for (auto &model : wamr->nn_context) {
        this->models.emplace_back(model);
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
        WAMRWASINNOperation* op = get_next_operation();
        if (!op) break;
        
        switch (op->type) {
            case WASINNOperationType::LOAD_MODEL: {
                // Load model from file
                FILE *pFile = fopen(op->model_name.c_str(), "r");
                if (pFile == nullptr) continue;
                
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
                error res = load(&arr, op->encoding, op->target, &new_graph);
                if (res == error::success) {
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
                    if (init_execution_context(graph_it->second, &new_ctx) == success) {
                        context_mapping[op->ctx_id] = new_ctx;
                    }
                }
                break;
            }
            
            case WASINNOperationType::SET_INPUT: {
                auto ctx_it = context_mapping.find(op->ctx_id);
                if (ctx_it != context_mapping.end()) {
                    tensor_dimensions dims;
                    dims.size = op->tensor_dims.size();
                    dims.buf = (uint32_t *)malloc(dims.size * sizeof(uint32_t));
                    if (dims.buf == nullptr) continue;
                    
                    for (size_t i = 0; i < dims.size; ++i) {
                        dims.buf[i] = op->tensor_dims[i];
                    }
                    
                    tensor input_tensor;
                    input_tensor.dimensions = &dims;
                    input_tensor.type = op->data_type;
                    input_tensor.data = (uint8_t *)op->tensor_data.data();
                    
                    set_input(ctx_it->second, op->input_index, &input_tensor);
                    
                    free(dims.buf);
                }
                break;
            }
            
            case WASINNOperationType::COMPUTE: {
                auto ctx_it = context_mapping.find(op->ctx_id);
                if (ctx_it != context_mapping.end()) {
                    compute(ctx_it->second);
                }
                break;
            }
            
            case WASINNOperationType::GET_OUTPUT: {
                auto ctx_it = context_mapping.find(op->ctx_id);
                if (ctx_it != context_mapping.end()) {
                    uint8_t *output_buffer = (uint8_t *)malloc(MAX_OUTPUT_TENSOR_SIZE);
                    if (output_buffer != nullptr) {
                        uint32_t output_size = MAX_OUTPUT_TENSOR_SIZE;
                        get_output(ctx_it->second, op->output_index, output_buffer, &output_size);
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
// Wrapper functions to intercept WASI-NN operations for recording
static WAMRWASINNContext* get_current_wamr_nn_context() {
    // Get the current WAMR WASI-NN context
    // This should be integrated with the existing WAMR context management
    return &wamr->nn_context_mvvm;
}

// Intercepted WASI-NN load function
error wamr_nn_load_with_recording(graph_builder_array *builder, graph_encoding encoding,
                                 execution_target target, graph *g, const char* model_name) {
    WAMRWASINNContext* ctx = get_current_wamr_nn_context();
    
    error result = load(builder, encoding, target, g);
    
    if (result == success && ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::LOAD_MODEL;
        op.sequence_id = ctx->operation_sequence++;
        op.model_name = model_name ? model_name : "";
        op.encoding = encoding;
        op.target = target;
        op.graph_id = *g;
        
        ctx->record_operation(op);
    }
    
    return result;
}

// Intercepted WASI-NN init_execution_context function
error wamr_nn_init_execution_context_with_recording(graph g, graph_execution_context *ctx) {
    WAMRWASINNContext* wamr_ctx = get_current_wamr_nn_context();
    
    error result = init_execution_context(g, ctx);
    
    if (result == success && wamr_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::INIT_EXECUTION_CONTEXT;
        op.sequence_id = wamr_ctx->operation_sequence++;
        op.graph_id = g;
        op.ctx_id = *ctx;
        
        wamr_ctx->record_operation(op);
    }
    
    return result;
}

// Intercepted WASI-NN set_input function
error wamr_nn_set_input_with_recording(graph_execution_context ctx, uint32_t index, tensor *tensor) {
    WAMRWASINNContext* wamr_ctx = get_current_wamr_nn_context();
    
    error result = set_input(ctx, index, tensor);
    
    if (result == success && wamr_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::SET_INPUT;
        op.sequence_id = wamr_ctx->operation_sequence++;
        op.ctx_id = ctx;
        op.input_index = index;
        
        // Record tensor data
        if (tensor && tensor->data && tensor->dimensions) {
            size_t total_elements = 1;
            for (uint32_t i = 0; i < tensor->dimensions->size; ++i) {
                total_elements *= tensor->dimensions->buf[i];
                op.tensor_dims.push_back(tensor->dimensions->buf[i]);
            }
            
            size_t data_size = total_elements * sizeof(float); // Assuming float data
            op.tensor_data.resize(data_size);
            memcpy(op.tensor_data.data(), tensor->data, data_size);
            op.data_type = tensor->type;
        }
        
        wamr_ctx->record_operation(op);
    }
    
    return result;
}

// Intercepted WASI-NN compute function
error wamr_nn_compute_with_recording(graph_execution_context ctx) {
    WAMRWASINNContext* wamr_ctx = get_current_wamr_nn_context();
    
    error result = compute(ctx);
    
    if (result == success && wamr_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::COMPUTE;
        op.sequence_id = wamr_ctx->operation_sequence++;
        op.ctx_id = ctx;
        
        wamr_ctx->record_operation(op);
    }
    
    return result;
}

// Intercepted WASI-NN get_output function
error wamr_nn_get_output_with_recording(graph_execution_context ctx, uint32_t index,
                                       tensor_data output_tensor, uint32_t *output_tensor_size) {
    WAMRWASINNContext* wamr_ctx = get_current_wamr_nn_context();
    
    error result = get_output(ctx, index, output_tensor, output_tensor_size);
    
    if (result == success && wamr_ctx->recording_enabled) {
        WAMRWASINNOperation op;
        op.type = WASINNOperationType::GET_OUTPUT;
        op.sequence_id = wamr_ctx->operation_sequence++;
        op.ctx_id = ctx;
        op.output_index = index;
        
        // Record output data
        if (output_tensor && output_tensor_size) {
            op.output_data.resize(*output_tensor_size);
            memcpy(op.output_data.data(), output_tensor, *output_tensor_size);
            op.output_size = *output_tensor_size;
        }
        
        wamr_ctx->record_operation(op);
    }
    
    return result;
}

#endif