//
// Created by victoryang00 on 5/22/23.
//

#ifndef MVVM_WAMR_FUNCTION_INSTANCE_H
#define MVVM_WAMR_FUNCTION_INSTANCE_H
#include "logging.h"
#include "wamr_serializer.h"
#include "wamr_type.h"
#include "wasm_runtime.h"
#include <string>
#include <vector>
// AOT do not have custom name section
struct WAMRFunction {
#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
    std::string field_name;
#endif
    /* the type of function */
    WAMRType func_type;
    uint32 local_count;
    //    uint8 *local_types;

    /* cell num of parameters */
    uint16 param_cell_num;
    /* cell num of return type */
    uint16 ret_cell_num;
    /* cell num of local variables */
    uint16 local_cell_num;
    /* offset of each local, including function parameters
       and local variables */
    //    uint16 *local_offsets;

    uint32 max_stack_cell_num;
    uint32 max_block_num;
    uint32 code_size;
    //    uint8 *code;
    void dump_impl(WASMFunction *env) {
#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
        field_name = env->field_name;
        LOGV(DEBUG) << "field_name:" << field_name;
#else
        dump(&func_type, env->func_type);
        local_count = env->local_count;
        LOGV(DEBUG) << "local_count:" << local_count;
        param_cell_num = env->param_cell_num;
        LOGV(DEBUG) << "param_cell_num:" << param_cell_num;
        ret_cell_num = env->ret_cell_num;
        LOGV(DEBUG) << "ret_cell_num:" << ret_cell_num;
        local_cell_num = env->local_cell_num;
        LOGV(DEBUG) << "local_cell_num:" << local_cell_num;
        max_stack_cell_num = env->max_stack_cell_num;
        LOGV(DEBUG) << "max_stack_cell_num:" << max_stack_cell_num;
        max_block_num = env->max_block_num;
        LOGV(DEBUG) << "max_block_num:" << max_block_num;
        code_size = env->code_size;
#endif
    };
    bool equal_impl(WASMFunction *env) const {
#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
        return field_name == env->field_name;
#else
        if (!func_type.equal_impl(env->func_type)) {
            return false;
        }
        if (param_cell_num != env->param_cell_num) {
            return false;
        }
        if (ret_cell_num != env->ret_cell_num) {
            return false;
        }
        if (local_cell_num != env->local_cell_num) {
            return false;
        }
        if (max_stack_cell_num != env->max_stack_cell_num) {
            return false;
        }
        if (max_block_num != env->max_block_num) {
            return false;
        }
        if (code_size != env->code_size) {
            return false;
        }
        return true;
#endif
    };
};

template <CheckerTrait<WASMFunction *> T> void dump(T t, WASMFunction *env) { t->dump_impl(env); }
template <CheckerTrait<WASMFunction *> T> bool equal(T t, WASMFunction *env) { return t->equal_impl(env); }

struct WAMRFunctionImport {
    std::string field_name;
    void dump_impl(WASMFunctionImport *env) {
        field_name = env->field_name;
        LOGV(DEBUG) << "field_name:" << field_name;
    };
    bool equal_impl(WASMFunctionImport *env) const { return field_name == env->field_name; };
};

template <CheckerTrait<WASMFunctionImport *> T> void dump(T t, WASMFunctionImport *env) { t->dump_impl(env); }
template <CheckerTrait<WASMFunctionImport *> T> bool equal(T t, WASMFunctionImport *env) { return t->equal_impl(env); }

struct WAMRFunctionInstance {
    /* AOT required func index */
    uint16 func_index{};
    /* whether it is import function or WASM function */
    bool is_import_func{};
    /* parameter count */
    uint16 param_count{};
    /* local variable count, 0 for import function */
    uint16 local_count{};
    WAMRFunction func{};
    WAMRFunctionImport func_import;
    void dump_impl(WASMFunctionInstance *env) {
        is_import_func = env->is_import_func;
        LOGV(DEBUG) << "is_import_func:" << is_import_func;
        if (!is_import_func) {
            dump(&func, env->u.func);
        } else {
            dump(&func_import, env->u.func_import);
        }
        param_count = env->param_count;
        LOGV(DEBUG) << "param_count:" << param_count;
        /* local variable count, 0 for import function */
        local_count = env->local_count;
        LOGV(DEBUG) << "local_count:" << local_count;
    };
    void restore_impl(WASMFunctionInstance *env);
};

template <SerializerTrait<WASMFunctionInstance *> T> void dump(T t, WASMFunctionInstance *env) { t->dump_impl(env); }
template <SerializerTrait<WASMFunctionInstance *> T> void restore(T t, WASMFunctionInstance *env) {
    t->restore_impl(env); }

#endif // MVVM_WAMR_FUNCTION_INSTANCE_H
