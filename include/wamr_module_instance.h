//
// Created by yiwei yang on 4/29/23.
//

#ifndef MVVM_WAMR_MODULE_INSTANCE_H
#define MVVM_WAMR_MODULE_INSTANCE_H
#include "wamr_memory_instance.h"
#include "wamr_module_instance_extra.h"
#include "wamr_wasi_context.h"
#include "wasm_runtime.h"
#include <algorithm>
struct WAMRModuleInstance {
    /* Module instance type, for module instance loaded from
       WASM bytecode binary, this field is Wasm_Module_Bytecode;
       for module instance loaded from AOT file, this field is
       Wasm_Module_AoT, and this structure should be treated as
       AOTModuleInstance structure. */
    uint32 module_type;

    std::vector<WAMRMemoryInstance> memories;

    /* global and table info */
    std::vector<uint8> global_data;
    /* For AOTModuleInstance, it denotes `AOTTableInstance *` */
    std::vector<WASMTableInstance> tables;

    /* import func ptrs + llvm jit func ptrs */
    //        DefPointer(void **, func_ptrs);

    /* function type indexes */
    // std::unique_ptr<uint32> func_type_indexes;

    // uint32 export_func_count;
    // uint32 export_global_count;
    // uint32 export_memory_count;
    // uint32 export_table_count;
    /* For AOTModuleInstance, it denotes `AOTFunctionInstance *` */
    //    std::unique_ptr<WASMExportFuncInstance> export_functions;
    //    std::unique_ptr<WASMExportGlobInstance> export_globals;
    //    std::unique_ptr<WASMExportMemInstance> export_memories;
    //    std::unique_ptr<WASMExportTabInstance> export_tables;

    /* The exception buffer of wasm interpreter for current thread. */
    //    char cur_exception[EXCEPTION_BUF_LEN];

    /* The WASM module or AOT module, for AOTModuleInstance,
       it denotes `AOTModule *` */
    //    DefPointer(WASMModule *, module);// has a lot to do
    /* total global variable size */
    //    uint32 global_data_size;

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

    // #if WASM_ENABLE_LIBC_WASI
    //     /* WASI context */
    WAMRWASIContext wasi_ctx;
    // #else
    //     DefPointer(void *, wasi_ctx);
    // #endif
    //     DefPointer(WASMExecEnv *, exec_env_singleton);
    //     /* Array of function pointers to import functions,
    //        not available in AOTModuleInstance */
    //     DefPointer(void **, import_func_ptrs);
    //     /* Array of function pointers to fast jit functions,
    //        not available in AOTModuleInstance:
    //        Only when the multi-tier JIT macros are all enabled and the running
    //        mode of current module instance is set to Mode_Fast_JIT, runtime
    //        will allocate new memory for it, otherwise it always points to the
    //        module->fast_jit_func_ptrs */
    //     DefPointer(void **, fast_jit_func_ptrs);
    //     /* The custom data that can be set/get by wasm_{get|set}_custom_data */
    //     DefPointer(void *, custom_data);
    //     /* Stack frames, used in call stack dump and perf profiling */
    //     DefPointer(Vector *, frames);
    //     /* Function performance profiling info list, only available
    //        in AOTModuleInstance */
    //     DefPointer(struct AOTFuncPerfProfInfo *, func_perf_profilings);
    //     /* WASM/AOT module extra info, for AOTModuleInstance,
    //        it denotes `AOTModuleInstanceExtra *` */
    WAMRModuleInstanceExtra extra;

    /* Default WASM operand stack size */
    //    uint32 default_wasm_stack_size;
    //    uint32 reserved[3];

    /*
     * +------------------------------+ <-- memories
     * | WASMMemoryInstance[mem_count], mem_count is always 1 for LLVM JIT/AOT
     * +------------------------------+ <-- global_data
     * | global data
     * +------------------------------+ <-- tables
     * | WASMTableInstance[table_count]
     * +------------------------------+ <-- e
     * | WASMModuleInstanceExtra
     * +------------------------------+
     */
    //    union {
    //        uint64 _make_it_8_byte_aligned_;
    WAMRMemoryInstance global_table_data;
    //        uint8 bytes[1];
    //    } global_table_data;

    void dump(WASMModuleInstance *env) {
        module_type = env->module_type;
        for (int i = 0; i < env->memory_count; i++) {
            // TODO: if the referenced memory has been serialized, just skip.
            auto local_mem = WAMRMemoryInstance();
            ::dump(&local_mem, env->memories[i]);
            memories.push_back(local_mem);
        }
        global_data = std::vector<uint8>(env->global_data, env->global_data + env->global_data_size);
         LOGV(DEBUG) << env->global_data_size;
         for (int i = 0; i < env->global_data_size; i++) {
             LOGV(DEBUG) << env->global_data[i];
         }
//         tables = std::vector<WASMTableInstance>(env->tables, env->tables + env->table_count);
        tables.reserve(env->table_count);
        std::generate_n(
            std::back_inserter(tables), env->table_count,
            [i = 0, env]() mutable { return *(env->tables[i++]); } // or whatever your 'body' lambda would look like.
        );
        ::dump(&wasi_ctx, env->wasi_ctx);
        ::dump(&extra, env->e);
        aux_data_end_global_index = env->module->aux_data_end_global_index;
        aux_data_end = env->module->aux_data_end;
        aux_heap_base_global_index = env->module->aux_heap_base_global_index;
        aux_heap_base = env->module->aux_heap_base;
        aux_stack_top_global_index = env->module->aux_stack_top_global_index;
        aux_stack_bottom = env->module->aux_stack_bottom;
        aux_stack_size = env->module->aux_stack_size;
        ::dump(&global_table_data, env->global_table_data.memory_instances);
    };
    void restore(WASMModuleInstance *env) {
        env->module_type = module_type;
        env->memory_count = memories.size();
        for (int i = 0; i < env->memory_count; i++) {
            ::restore(&memories[i], env->memories[i]);
        }
        memcpy(env->global_data, global_data.data(), global_data.size());
        env->global_data_size = global_data.size();
//                env->global_data = global_data.data();
//                env->global_data_size = global_data.size() - 1;
         LOGV(DEBUG) << env->global_data_size;
         LOGV(DEBUG) << env->global_data;
         for (int i = 0; i < env->global_data_size; i++) {
             LOGV(DEBUG) << env->global_data[i];
         }
        env->table_count = tables.size();
        for (int i = 0; i < env->table_count; i++) {
            *env->tables[i] = tables[i];
        }
        ::restore(&wasi_ctx, env->wasi_ctx);
        ::restore(&extra, env->e);
        env->module->aux_data_end_global_index = aux_data_end_global_index;
        env->module->aux_data_end = aux_data_end;
        env->module->aux_heap_base_global_index = aux_heap_base_global_index;
        env->module->aux_heap_base = aux_heap_base;
        env->module->aux_stack_top_global_index = aux_stack_top_global_index;
        env->module->aux_stack_bottom = aux_stack_bottom;
        env->module->aux_stack_size = aux_stack_size;
        ::restore(&global_table_data, env->global_table_data.memory_instances);
    };
};

template <SerializerTrait<WASMModuleInstance *> T> void dump(T t, WASMModuleInstance *env) { t->dump(env); }
template <SerializerTrait<WASMModuleInstance *> T> void restore(T t, WASMModuleInstance *env) { t->restore(env); }

#endif // MVVM_WAMR_MODULE_INSTANCE_H
