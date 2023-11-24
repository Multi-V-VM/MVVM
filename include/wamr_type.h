//
// Created by victoryang00 on 5/23/23.
//

#ifndef MVVM_WAMR_TYPE_H
#define MVVM_WAMR_TYPE_H
#include "wasm_runtime.h"
struct WAMRType {
    uint16 param_count;
    uint16 result_count;
    uint16 param_cell_num;
    uint16 ret_cell_num;
    uint16 ref_count;
    /* types of params and results */
    //    uint8 types[1];
    void dump_impl(WASMType *env) {
        param_count = env->param_count;
        result_count = env->result_count;
        param_cell_num = env->param_cell_num;
        ret_cell_num = env->ret_cell_num;
        ref_count = env->ref_count;
    };
    bool equal_impl(WASMType *type) const {
        return param_count == type->param_count && result_count == type->result_count &&
               param_cell_num == type->param_cell_num && ret_cell_num == type->ret_cell_num &&
               ref_count == type->ref_count;
    };
};
template <CheckerTrait<WASMType *> T> void dump(T t, WASMType *env) { t->dump_impl(env); }
template <CheckerTrait<WASMType *> T> bool equal(T t, WASMType *env) { return t->equal_impl(env); }

#endif // MVVM_WAMR_TYPE_H
