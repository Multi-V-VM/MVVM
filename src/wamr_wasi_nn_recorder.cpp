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

#include "wamr_wasi_nn_recorder.h"
#include "wamr.h"
#include "wasi_nn_app_native.h"
#include <cstring>
#include <unordered_map>

// Global map to store MVVM contexts per module instance
static std::unordered_map<wasm_module_inst_t, WAMRWASINNContext*> g_mvvm_contexts;
static std::mutex g_contexts_mutex;

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

WAMRWASINNContext* wamr_get_wasi_nn_mvvm_context(wasm_module_inst_t instance) {
    std::lock_guard<std::mutex> lock(g_contexts_mutex);
    
    auto it = g_mvvm_contexts.find(instance);
    if (it != g_mvvm_contexts.end()) {
        return it->second;
    }
    return nullptr;
}

// Wrapper functions with recording capability

error wamr_wasi_nn_load_with_recording(wasm_exec_env_t exec_env, 
                                      graph_builder_wasm *builder,
                                      uint32_t builder_wasm_size, 
                                      graph_encoding encoding,
                                      execution_target target, 
                                      graph *g,
                                      const char* model_name) {
    
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext* mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);
    
    // Call the original WASI-NN load function
    extern error wasi_nn_load(wasm_exec_env_t, graph_builder_wasm *, uint32_t, graph_encoding, execution_target, graph *);
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

error wamr_wasi_nn_init_execution_context_with_recording(wasm_exec_env_t exec_env,
                                                        graph g,
                                                        graph_execution_context *ctx) {
    
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext* mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);
    
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

error wamr_wasi_nn_set_input_with_recording(wasm_exec_env_t exec_env,
                                           graph_execution_context ctx,
                                           uint32_t index,
                                           tensor_wasm *input_tensor) {
    
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext* mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);
    
    // Convert to native tensor for recording
    tensor input_tensor_native = { 0 };
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
                case fp16: element_size = 2; break;
                case fp32: element_size = 4; break;
                case fp64: element_size = 8; break;
                case bf16: element_size = 2; break;
                case u8: element_size = 1; break;
                case i32: element_size = 4; break;
                case i64: element_size = 8; break;
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

error wamr_wasi_nn_compute_with_recording(wasm_exec_env_t exec_env,
                                         graph_execution_context ctx) {
    
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext* mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);
    
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

error wamr_wasi_nn_get_output_with_recording(wasm_exec_env_t exec_env,
                                            graph_execution_context ctx,
                                            uint32_t index,
                                            tensor_data output_tensor,
                                            uint32_t output_tensor_len,
                                            uint32_t *output_tensor_size) {
    
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
    WAMRWASINNContext* mvvm_ctx = wamr_get_wasi_nn_mvvm_context(instance);
    
    // Call the original WASI-NN get_output function
    extern error wasi_nn_get_output(wasm_exec_env_t, graph_execution_context, uint32_t, tensor_data, uint32_t, uint32_t *);
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

} // extern "C"