//
// Created by victoryang00 on 5/22/23.
//

#ifndef MVVM_WAMR_FUNCTION_INSTANCE_H
#define MVVM_WAMR_FUNCTION_INSTANCE_H
#include "wasm_runtime.h"
#include "wamr_serializer.h"
struct WAMRFunctionInstance{

    void dump(WASMFunctionInstance *env){};
    void restore(WASMFunctionInstance *env){

    };

};
template <SerializerTrait<WASMFunctionInstance *> T> void dump(T t, WASMFunctionInstance *env) { t->dump(env); }
template <SerializerTrait<WASMFunctionInstance *> T> void restore(T t, WASMFunctionInstance *env) { t->restore(env); }

#endif // MVVM_WAMR_FUNCTION_INSTANCE_H
