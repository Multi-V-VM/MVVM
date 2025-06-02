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

#ifndef MVVM_WAMR_WASI_NN_CONTEXT_H
#define MVVM_WAMR_WASI_NN_CONTEXT_H
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/model.h"
#include "wamr_serializer.h"
#include "wasi_nn.h"
#include "wasi_nn_private.h"
#include <memory>
#include <vector>
#include <unordered_map>
// https://github.com/WebAssembly/wasi-nn/blob/0f77c48ec195748990ff67928a4b3eef5f16c2de/wasi-nn.wit.md
/* Maximum number of graphs per WASM instance */
#define MAX_GRAPHS_PER_INST 10
/* Maximum number of graph execution context per WASM instance*/
#define MAX_GRAPH_EXEC_CONTEXTS_PER_INST 10

enum class WASINNOperationType {
    LOAD_MODEL,
    INIT_EXECUTION_CONTEXT,
    SET_INPUT,
    COMPUTE,
    GET_OUTPUT
};

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

struct WAMRWASINNModel {
    std::string model_name;
    std::vector<uint8_t> input_tensor;
    std::vector<uint32_t> dims;
};

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
    
    // Context mapping for replay
    std::unordered_map<graph, graph> graph_mapping;
    std::unordered_map<graph_execution_context, graph_execution_context> context_mapping;
    
    void dump_impl(WASINNContext *env);
    void restore_impl(WASINNContext *env);
    
    // Record and replay methods
    void record_operation(const WAMRWASINNOperation& op);
    WAMRWASINNOperation* get_next_operation();
    void enable_recording(bool enable) { recording_enabled = enable; }
    void enable_replay(bool enable) { replaying_enabled = enable; }
    void reset_replay() { replay_position = 0; }
};

template <SerializerTrait<WASINNContext *> T> void dump(T t, WASINNContext *env) { t->dump_impl(env); }
template <SerializerTrait<WASINNContext *> T> void restore(T t, WASINNContext *env) { t->restore_impl(env); }
#endif // MVVM_WAMR_WASI_NN_CONTEXT_H
