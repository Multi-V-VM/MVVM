//
// Created by victoryang00 on 5/10/23.
//

#ifndef MVVM_WAMR_MODULE_INSTANCE_EXTRA_H
#define MVVM_WAMR_MODULE_INSTANCE_EXTRA_H

#if WASM_ENABLE_WASI_NN != 0
#include "wamr_wasi_nn_context.h"
#endif
#include "wasm_runtime.h"
#include <memory>
struct WAMRModuleInstanceExtra {
    uint32 global_count{};
    uint32 function_count{};
#if WASM_ENABLE_WASI_NN != 0
    WAMRWASINNContext wasi_nn_ctx{};
#endif
    void dump_impl(WASMModuleInstanceExtra *env) {
        global_count = env->global_count;
        function_count = env->function_count;
#if WASM_ENABLE_WASI_NN != 0
        dump(&wasi_nn_ctx, env->wasi_nn_ctx);
#endif
    };
    void restore_impl(WASMModuleInstanceExtra *env) {
        env->global_count = global_count;
        env->function_count = function_count;
#if WASM_ENABLE_WASI_NN != 0
        restore(&wasi_nn_ctx, env->wasi_nn_ctx);
#endif
    };
};

template <SerializerTrait<WASMModuleInstanceExtra *> T> void dump(T t, WASMModuleInstanceExtra *env) {
    t->dump_impl(env);
}

template <SerializerTrait<WASMModuleInstanceExtra *> T> void restore(T t, WASMModuleInstanceExtra *env) {
    t->restore_impl(env);
}

#endif // MVVM_WAMR_MODULE_INSTANCE_EXTRA_H
