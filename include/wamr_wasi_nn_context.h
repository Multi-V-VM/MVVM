//
// Created by root on 5/11/23.
//

#ifndef MVVM_WAMR_WASI_NN_CONTEXT_H
#define MVVM_WAMR_WASI_NN_CONTEXT_H
//#include "wasm_runtime.h"
#include "wasi_nn.h"
struct WAMRWASINNContext{
    bool is_initialized;
    graph_encoding current_encoding;
    uint32_t current_models;
    // Model models[MAX_GRAPHS_PER_INST];
    // uint32_t current_interpreters;
    // Interpreter interpreters[MAX_GRAPH_EXEC_CONTEXTS_PER_INST];
};

template <SerializerTrait<WAMRWASINNContext *> T> void dump(T t, WASINNContext *env) { t->dump(env); }

template <SerializerTrait<WAMRWASINNContext *> T> void restore(T t, WASINNContext *env) { t->restore(env); }
#endif // MVVM_WAMR_WASI_NN_CONTEXT_H
