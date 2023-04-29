//
// Created by yiwei yang on 4/29/23.
//

#ifndef MVVM_WASMMODULE_H
#define MVVM_WASMMODULE_H
#include <stdlib.h>

struct WASMModule {
    /* Module type, for module loaded from WASM bytecode binary,
       this field is Wasm_Module_Bytecode;
       for module loaded from AOT file, this field is
       Wasm_Module_AoT, and this structure should be treated as
       AOTModule structure. */
    uint32 module_type;

    uint32 type_count;
    uint32 import_count;
    uint32 function_count;
    uint32 table_count;
    uint32 memory_count;
    uint32 global_count;
    uint32 export_count;
    uint32 table_seg_count;
    /* data seg count read from data segment section */
    uint32 data_seg_count;
#if WASM_ENABLE_BULK_MEMORY != 0
    /* data count read from datacount section */
    uint32 data_seg_count1;
#endif

    uint32 import_function_count;
    uint32 import_table_count;
    uint32 import_memory_count;
    uint32 import_global_count;

    WASMImport *import_functions;
    WASMImport *import_tables;
    WASMImport *import_memories;
    WASMImport *import_globals;

    WASMType **types;
    WASMImport *imports;
    WASMFunction **functions;
    WASMTable *tables;
    WASMMemory *memories;
    WASMGlobal *globals;
    WASMExport *exports;
    WASMTableSeg *table_segments;
    WASMDataSeg **data_segments;
    uint32 start_function;

    /* total global variable size */
    uint32 global_data_size;

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

    /* the index of malloc/free function,
       -1 means unexported */
    uint32 malloc_function;
    uint32 free_function;

    /* the index of __retain function,
       -1 means unexported */
    uint32 retain_function;

    /* Whether there is possible memory grow, e.g. memory.grow opcode */
    bool possible_memory_grow;

    StringList const_str_list;
#if WASM_ENABLE_FAST_INTERP == 0
    bh_list br_table_cache_list_head;
    bh_list *br_table_cache_list;
#endif

#if WASM_ENABLE_LIBC_WASI != 0
    WASIArguments wasi_args;
    bool import_wasi_api;
#endif

#if WASM_ENABLE_MULTI_MODULE != 0
    /* TODO: add mutex for mutli-thread? */
    bh_list import_module_list_head;
    bh_list *import_module_list;
#endif
#if WASM_ENABLE_DEBUG_INTERP != 0 || WASM_ENABLE_DEBUG_AOT != 0
    bh_list fast_opcode_list;
    uint8 *buf_code;
    uint64 buf_code_size;
#endif
#if WASM_ENABLE_DEBUG_INTERP != 0 || WASM_ENABLE_DEBUG_AOT != 0 \
    || WASM_ENABLE_FAST_JIT != 0
    uint8 *load_addr;
    uint64 load_size;
#endif

#if WASM_ENABLE_DEBUG_INTERP != 0                         \
    || (WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
        && WASM_ENABLE_LAZY_JIT != 0)
    /**
     * List of instances referred to this module. When source debugging
     * feature is enabled, the debugger may modify the code section of
     * the module, so we need to report a warning if user create several
     * instances based on the same module.
     *
     * Also add the instance to the list for Fast JIT to LLVM JIT
     * tier-up, since we need to lazily update the LLVM func pointers
     * in the instance.
     */
    struct WASMModuleInstance *instance_list;
    korp_mutex instance_list_lock;
#endif

#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
    const uint8 *name_section_buf;
    const uint8 *name_section_buf_end;
#endif

#if WASM_ENABLE_LOAD_CUSTOM_SECTION != 0
    WASMCustomSection *custom_section_list;
#endif

#if WASM_ENABLE_FAST_JIT != 0
    /**
     * func pointers of Fast JITed (un-imported) functions
     * for non Multi-Tier JIT mode:
     *   (1) when lazy jit is disabled, each pointer is set to the compiled
     *       fast jit jitted code
     *   (2) when lazy jit is enabled, each pointer is firstly inited as
     *       jit_global->compile_fast_jit_and_then_call, and then set to the
     *       compiled fast jit jitted code when it is called (the stub will
     *       compile the jit function and then update itself)
     * for Multi-Tier JIT mode:
     *   each pointer is firstly inited as compile_fast_jit_and_then_call,
     *   and then set to the compiled fast jit jitted code when it is called,
     *   and when the llvm jit func ptr of the same function is compiled, it
     *   will be set to call_to_llvm_jit_from_fast_jit of this function type
     *   (tier-up from fast-jit to llvm-jit)
     */
    void **fast_jit_func_ptrs;
    /* locks for Fast JIT lazy compilation */
    korp_mutex fast_jit_thread_locks[WASM_ORC_JIT_BACKEND_THREAD_NUM];
    bool fast_jit_thread_locks_inited[WASM_ORC_JIT_BACKEND_THREAD_NUM];
#endif

#if WASM_ENABLE_JIT != 0
    struct AOTCompData *comp_data;
    struct AOTCompContext *comp_ctx;
    /**
     * func pointers of LLVM JITed (un-imported) functions
     * for non Multi-Tier JIT mode:
     *   each pointer is set to the lookuped llvm jit func ptr, note that it
     *   is a stub and will trigger the actual compilation when it is called
     * for Multi-Tier JIT mode:
     *   each pointer is inited as call_to_fast_jit code block, when the llvm
     *   jit func ptr is actually compiled, it is set to the compiled llvm jit
     *   func ptr
     */
    void **func_ptrs;
    /* whether the func pointers are compiled */
    bool *func_ptrs_compiled;
#endif

#if WASM_ENABLE_FAST_JIT != 0 || WASM_ENABLE_JIT != 0
    /* backend compilation threads */
    korp_tid orcjit_threads[WASM_ORC_JIT_BACKEND_THREAD_NUM];
    /* backend thread arguments */
    OrcJitThreadArg orcjit_thread_args[WASM_ORC_JIT_BACKEND_THREAD_NUM];
    /* whether to stop the compilation of backend threads */
    bool orcjit_stop_compiling;
#endif

#if WASM_ENABLE_FAST_JIT != 0 && WASM_ENABLE_JIT != 0 \
    && WASM_ENABLE_LAZY_JIT != 0
    /* wait lock/cond for the synchronization of
       the llvm jit initialization */
    korp_mutex tierup_wait_lock;
    korp_cond tierup_wait_cond;
    bool tierup_wait_lock_inited;
    korp_tid llvm_jit_init_thread;
    /* whether the llvm jit is initialized */
    bool llvm_jit_inited;
    /* Whether to enable llvm jit compilation:
       it is set to true only when there is a module instance starts to
       run with running mode Mode_LLVM_JIT or Mode_Multi_Tier_JIT,
       since no need to enable llvm jit compilation for Mode_Interp and
       Mode_Fast_JIT, so as to improve performance for them */
    bool enable_llvm_jit_compilation;
    /* The count of groups which finish compiling the fast jit
       functions in that group */
    uint32 fast_jit_ready_groups;
#endif
};

#endif //MVVM_WASMMODULE_H
