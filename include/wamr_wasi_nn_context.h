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

#ifndef MVVM_WAMR_WASI_NN_H
#define MVVM_WAMR_WASI_NN_H

#include "wasi_nn.h"
#include "wasi_nn_types.h"
#include "wasm_export.h"
#include "wasi_nn_app_native.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

/* Maximum number of graphs per WASM instance */
#define MAX_GRAPHS_PER_INST 10
/* Maximum number of graph execution context per WASM instance*/
#define MAX_GRAPH_EXEC_CONTEXTS_PER_INST 10

// The tensor types are defined in wasi_nn_app_native.h, so we include that

// Forward declaration
struct WAMRWASINNContext;

// We need to use the WAMR library's WASINNContext, but avoid conflicts
// Include the required header that defines it
#include "wasi_nn_private.h"

// 操作类型枚举
enum class WASINNOperationType { LOAD_MODEL, INIT_EXECUTION_CONTEXT, SET_INPUT, COMPUTE, GET_OUTPUT };

// 操作结构
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

    // 计算此操作的内存大小
    size_t get_memory_size() const {
        size_t size = sizeof(WAMRWASINNOperation);
        size += model_name.capacity();
        size += tensor_data.capacity();
        size += tensor_dims.capacity() * sizeof(uint32_t);
        size += output_data.capacity();
        return size;
    }
};

// 模型结构
struct WAMRWASINNModel {
    std::string model_name;
    std::vector<uint8_t> input_tensor;
    std::vector<uint32_t> dims;

    // 计算此模型的内存大小
    size_t get_memory_size() const {
        size_t size = sizeof(WAMRWASINNModel);
        size += model_name.capacity();
        size += input_tensor.capacity();
        size += dims.capacity() * sizeof(uint32_t);
        return size;
    }
};

// 上下文结构
struct WAMRWASINNContext {
    bool is_initialized = false;
    graph_encoding current_encoding = graph_encoding::tensorflowlite;
    std::vector<WAMRWASINNModel> models;

    // Record and replay functionality
    bool recording_enabled = true;
    bool replaying_enabled = false;
    uint32_t operation_sequence = 0;
    std::vector<WAMRWASINNOperation> recorded_operations;
    size_t replay_position = 0;

    // Size tracking
    size_t total_memory_size = 0; // 总内存使用量(字节)
    size_t peak_memory_size = 0; // 峰值内存使用量(字节)
    size_t operation_count = 0; // 记录的操作总数

    // Context mapping for replay
    std::unordered_map<graph, graph> graph_mapping;
    std::unordered_map<graph_execution_context, graph_execution_context> context_mapping;

    void dump_impl(WASINNContext *env);
    void restore_impl(WASINNContext *env);

    // Record and replay methods
    void record_operation(const WAMRWASINNOperation &op);
    WAMRWASINNOperation *get_next_operation();
    void enable_recording(bool enable) { recording_enabled = enable; }
    void enable_replay(bool enable) { replaying_enabled = enable; }
    void reset_replay() { replay_position = 0; }

    // 大小统计方法
    size_t get_current_memory_size() const { return total_memory_size; }
    size_t get_peak_memory_size() const { return peak_memory_size; }
    size_t get_operation_count() const { return operation_count; }
    void reset_statistics() {
        total_memory_size = 0;
        peak_memory_size = 0;
        operation_count = 0;
    }

    // 计算当前使用的内存大小
    size_t calculate_current_size() const {
        size_t size = sizeof(WAMRWASINNContext);

        // 模型大小
        for (const auto &model : models) {
            size += model.get_memory_size();
        }

        // 操作记录大小
        for (const auto &op : recorded_operations) {
            size += op.get_memory_size();
        }

        // 映射表大小
        size += graph_mapping.size() * (sizeof(graph) * 2);
        size += context_mapping.size() * (sizeof(graph_execution_context) * 2);

        return size;
    }
};

// 修复模板定义
template <typename T> void dump(T *t, WASINNContext *env) { t->dump_impl(env); }
template <typename T> void restore(T *t, WASINNContext *env) { t->restore_impl(env); }

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the WASI-NN recorder for a module instance
void wamr_wasi_nn_recorder_init(wasm_module_inst_t instance);

// Cleanup the WASI-NN recorder for a module instance
void wamr_wasi_nn_recorder_cleanup(wasm_module_inst_t instance);

// Get the MVVM WASI-NN context for a module instance
WAMRWASINNContext *wamr_get_wasi_nn_mvvm_context(wasm_module_inst_t instance);

// Wrapped WASI-NN functions with recording capability
error wamr_wasi_nn_load_with_recording(wasm_exec_env_t exec_env, graph_builder_wasm *builder,
                                       uint32_t builder_wasm_size, graph_encoding encoding, execution_target target,
                                       graph *g, const char *model_name);

error wamr_wasi_nn_init_execution_context_with_recording(wasm_exec_env_t exec_env, graph g,
                                                         graph_execution_context *ctx);

error wamr_wasi_nn_set_input_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx, uint32_t index,
                                            tensor_wasm *input_tensor);

error wamr_wasi_nn_compute_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx);

error wamr_wasi_nn_get_output_with_recording(wasm_exec_env_t exec_env, graph_execution_context ctx, uint32_t index,
                                             tensor_data output_tensor, uint32_t output_tensor_len,
                                             uint32_t *output_tensor_size);

// 新增函数: 获取记录器的内存使用情况
size_t wamr_wasi_nn_get_recorder_memory_size(wasm_module_inst_t instance);
size_t wamr_wasi_nn_get_recorder_peak_memory_size(wasm_module_inst_t instance);
size_t wamr_wasi_nn_get_recorder_operation_count(wasm_module_inst_t instance);

#ifdef __cplusplus
}
#endif

#endif // MVVM_WAMR_WASI_NN_H
