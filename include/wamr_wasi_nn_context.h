/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2024 Regents of the Univeristy of California
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
// https://github.com/WebAssembly/wasi-nn/blob/0f77c48ec195748990ff67928a4b3eef5f16c2de/wasi-nn.wit.md
/* Maximum number of graphs per WASM instance */
#define MAX_GRAPHS_PER_INST 10
/* Maximum number of graph execution context per WASM instance*/
#define MAX_GRAPH_EXEC_CONTEXTS_PER_INST 10

struct WAMRWASINNGraph {
    std::vector<uint8_t> buffer;
};

struct WAMRWASINNInterpreter {
    //    std::unique_ptr<tflite::Interpreter> interpreter;
    std::vector<uint8_t> interpreter;
    uint32_t a; // placeholder
};

struct WAMRWASINNModel {
    //    std::unique_ptr<tflite::FlatBufferModel> model;
    std::vector<uint8_t> model;
    execution_target target;
};

struct WAMRWASINNContext {
    bool is_initialized;
    graph_encoding current_encoding;
    std::vector<WAMRWASINNGraph> graph; // TODO: support multiple graph
    uint32_t current_models;
    WAMRWASINNModel models[MAX_GRAPHS_PER_INST];
    uint32_t current_interpreters;
    WAMRWASINNInterpreter interpreters[MAX_GRAPH_EXEC_CONTEXTS_PER_INST];

    void dump_impl(WASINNContext *env);
    void restore_impl(WASINNContext *env);
};

template <SerializerTrait<WASINNContext *> T> void dump(T t, WASINNContext *env) { t->dump_impl(env); }
template <SerializerTrait<WASINNContext *> T> void restore(T t, WASINNContext *env) { t->restore_impl(env); }
#endif // MVVM_WAMR_WASI_NN_CONTEXT_H
