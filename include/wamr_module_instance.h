//
// Created by victoryang00 on 4/29/23.
//

#ifndef MVVM_WAMR_MODULE_INSTANCE_H
#define MVVM_WAMR_MODULE_INSTANCE_H

#if WASM_ENABLE_AOT != 0
#include "aot_runtime.h"
#endif
#include "wamr_memory_instance.h"
#include "wamr_wasi_context.h"
#include "wasm_runtime.h"
#include <algorithm>
struct WAMRModuleInstance {
    /* Module instance type, for module instance loaded from
       WASM bytecode binary, this field is Wasm_Module_Bytecode;
       for module instance loaded from AOT file, this field is
       Wasm_Module_AoT, and this structure should be treated as
       AOTModuleInstance structure. */
    std::vector<WAMRMemoryInstance> memories;
    /* global and table info */
    std::vector<uint8> global_data;
    /* the index of auxiliary __data_end global,
       -1 means unexported */
    uint32 aux_data_end_global_index;
    /* auxiliary __data_end exported by wasm app */
    uint32 aux_data_end;
    /* the index of auxiliary __heap_base global,
       -1 means unexported */
    uint32 aux_heap_base_global_index;
    /* auxiliary __heap_base exported by wasm app */
    uint32 aux_heap_base;

    /* the index of auxiliary stack top global,
       -1 means unexported */
    uint32 aux_stack_top_global_index;
    /* auxiliary stack bottom resolved */
    uint32 aux_stack_bottom;
    /* auxiliary stack size resolved */
    uint32 aux_stack_size;
    /* WASI context */
    WAMRWASIContext wasi_ctx;
    WAMRMemoryInstance global_table_data;

    void dump_impl(WASMModuleInstance *env);
    void restore_impl(WASMModuleInstance *env);
};

template <SerializerTrait<WASMModuleInstance *> T> void dump(T t, WASMModuleInstance *env) { t->dump_impl(env); }
template <SerializerTrait<WASMModuleInstance *> T> void restore(T t, WASMModuleInstance *env) { t->restore_impl(env); }

#endif // MVVM_WAMR_MODULE_INSTANCE_H
