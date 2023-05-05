//
// Created by yiwei yang on 4/29/23.
//

#ifndef MVVM_WASM_MODULE_INSTANCE_H
#define MVVM_WASM_MODULE_INSTANCE_H
#include "wasm_memory_instance.h"
#include "wasm_runtime.h"
struct WAMRModuleInstance {
    /* Module instance type, for module instance loaded from
       WASM bytecode binary, this field is Wasm_Module_Bytecode;
       for module instance loaded from AOT file, this field is
       Wasm_Module_AoT, and this structure should be treated as
       AOTModuleInstance structure. */
    uint32 module_type;

    uint32 memory_count;
    std::unique_ptr<WAMRMemoryInstance> memories;

    /* global and table info */
    uint32 global_data_size;
    uint32 table_count;
    std::unique_ptr<uint8> global_data;
    /* For AOTModuleInstance, it denotes `AOTTableInstance *` */
    //        DefPointer(WASMTableInstance **, tables);

    /* import func ptrs + llvm jit func ptrs */
    //        DefPointer(void **, func_ptrs);

    /* function type indexes */
    std::unique_ptr<uint32> func_type_indexes;

    uint32 export_func_count;
    uint32 export_global_count;
    uint32 export_memory_count;
    uint32 export_table_count;
    /* For AOTModuleInstance, it denotes `AOTFunctionInstance *` */
    std::unique_ptr<WASMExportFuncInstance> export_functions;
    std::unique_ptr<WASMExportGlobInstance> export_globals;
    std::unique_ptr<WASMExportMemInstance> export_memories;
    std::unique_ptr<WASMExportTabInstance> export_tables;

    /* The exception buffer of wasm interpreter for current thread. */
    char cur_exception[EXCEPTION_BUF_LEN];

    /* The WASM module or AOT module, for AOTModuleInstance,
       it denotes `AOTModule *` */
    //    DefPointer(WASMModule *, module);

    //#if WASM_ENABLE_LIBC_WASI
    //    /* WASI context */
    //    DefPointer(WASIContext *, wasi_ctx);
    //#else
    //    DefPointer(void *, wasi_ctx);
    //#endif
    //    DefPointer(WASMExecEnv *, exec_env_singleton);
    //    /* Array of function pointers to import functions,
    //       not available in AOTModuleInstance */
    //    DefPointer(void **, import_func_ptrs);
    //    /* Array of function pointers to fast jit functions,
    //       not available in AOTModuleInstance:
    //       Only when the multi-tier JIT macros are all enabled and the running
    //       mode of current module instance is set to Mode_Fast_JIT, runtime
    //       will allocate new memory for it, otherwise it always points to the
    //       module->fast_jit_func_ptrs */
    //    DefPointer(void **, fast_jit_func_ptrs);
    //    /* The custom data that can be set/get by wasm_{get|set}_custom_data */
    //    DefPointer(void *, custom_data);
    //    /* Stack frames, used in call stack dump and perf profiling */
    //    DefPointer(Vector *, frames);
    //    /* Function performance profiling info list, only available
    //       in AOTModuleInstance */
    //    DefPointer(struct AOTFuncPerfProfInfo *, func_perf_profilings);
    //    /* WASM/AOT module extra info, for AOTModuleInstance,
    //       it denotes `AOTModuleInstanceExtra *` */
    //    DefPointer(WASMModuleInstanceExtra *, e);

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
    //        WASMMemoryInstance memory_instances[1];
    //        uint8 bytes[1];
    //    } global_table_data;
};

#endif // MVVM_WASM_MODULE_INSTANCE_H
