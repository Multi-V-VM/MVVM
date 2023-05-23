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
#include <optional>

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
    void dump(WASMFunction *env) {
#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
        field_name = env->u.func->field_name;
        LOGV(DEBUG) << "field_name:" << field_name;
#else
    ::dump(&func_type, env->func_type);
#endif
    };
    bool equal(WASMFunction *env) {
#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
        return field_name == env->u.func->field_name;
#else
        if (!(func_type.equal(env->func_type))) {
            return false;
        }
#endif
    };
};

template <CheckerTrait<WASMFunction *> T> void dump(T t, WASMFunction *env) { t->dump(env); }
template <CheckerTrait<WASMFunction *> T> bool equal(T t, WASMFunction *env) { t->equal(env); }

struct WAMRFunctionImport {
    std::string field_name;
    void dump(WASMFunctionImport *env) {
        field_name = env->field_name;
        LOGV(DEBUG) << "field_name:" << field_name;
    };
    bool equal(WASMFunctionImport *env) { return field_name == env->field_name; };
};

template <CheckerTrait<WASMFunctionImport *> T> void dump(T t, WASMFunctionImport *env) { t->dump(env); }
template <CheckerTrait<WASMFunctionImport *> T> bool equal(T t, WASMFunctionImport *env) { t->equal(env); }

struct WAMRFunctionInstance {
    /* whether it is import function or WASM function */
    bool is_import_func;
    /* parameter count */
    uint16 param_count;
    /* local variable count, 0 for import function */
    uint16 local_count;
    /* cell num of parameters */
    uint16 param_cell_num;
    /* cell num of return type */
    uint16 ret_cell_num;
    /* cell num of local variables, 0 for import function */
    uint16 local_cell_num;
    // #if WASM_ENABLE_FAST_INTERP != 0
    //     /* cell num of consts */
    //     uint16 const_cell_num;
    // #endif
    std::vector<uint16> local_offsets;
    /* parameter types */
    std::vector<uint8> param_types;
    /* local types, NULL for import function */
    std::vector<uint8> local_types;
    //    union {
    //        WASMFunctionImport *func_import;
    std::optional<WAMRFunction> func;
    std::optional<WAMRFunctionImport> func_import;
    //    } u;
    void dump(WASMFunctionInstance *env) {
        is_import_func = env->is_import_func;
        LOGV(DEBUG) << "is_import_func:" << is_import_func;
        if (!is_import_func) {
            ::dump(func, env->u.func);
        } else {
            ::dump(func_import, env->u.func_import);
        }
        param_count = env->param_count;
        LOGV(DEBUG) << "param_count:" << param_count;
        /* local variable count, 0 for import function */
        local_count = env->local_count;
        LOGV(DEBUG) << "local_count:" << local_count;
        /* cell num of parameters */
        param_cell_num = env->param_cell_num;
        LOGV(DEBUG) << "param_cell_num:" << param_cell_num;
        /* cell num of return type */
        ret_cell_num = env->ret_cell_num;
        LOGV(DEBUG) << "ret_cell_num:" << ret_cell_num;
        /* cell num of local variables, 0 for import function */
        local_cell_num = env->local_cell_num;
        LOGV(DEBUG) << "local_cell_num:" << local_cell_num;
        local_offsets = std::vector(env->local_offsets, env->local_offsets + (param_count + local_count));
        /* parameter types */
        param_types = std::vector(env->param_types, env->param_types + param_count);
        /* local types, NULL for import function */
        local_types = std::vector(env->local_types, env->local_types + local_count);
    };
    void restore(WASMFunctionInstance *env);
};
template <SerializerTrait<WASMFunctionInstance *> T> void dump(T t, WASMFunctionInstance *env) { t->dump(env); }
template <SerializerTrait<WASMFunctionInstance *> T> void restore(T t, WASMFunctionInstance *env) { t->restore(env); }

#endif // MVVM_WAMR_FUNCTION_INSTANCE_H
