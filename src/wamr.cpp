//
// Created by yiwei yang on 5/6/23.
//

#include "wamr.h"

WAMRInstance::WAMRInstance(char *wasm_path) {

    RuntimeInitArgs wasm_args;
    memset(&wasm_args, 0, sizeof(RuntimeInitArgs));
    wasm_args.mem_alloc_type = Alloc_With_Allocator;
    wasm_args.mem_alloc_option.allocator.malloc_func = ((void *)malloc);
    wasm_args.mem_alloc_option.allocator.realloc_func = ((void *)realloc);
    wasm_args.mem_alloc_option.allocator.free_func = ((void *)free);
    //    wasm_args.mem_alloc_type = Alloc_With_Pool;
    //    wasm_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    //    wasm_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);

    if (!wasm_runtime_full_init(&wasm_args)) {
        LOGV(ERROR) << "Init runtime environment failed.\n";
        throw;
    }
    if (!load_wasm_binary(wasm_path)) {
        LOGV(ERROR) << "Load wasm binary failed.\n";
        throw;
    }
    module = wasm_runtime_load((uint8_t *)buffer, buf_size, error_buf, sizeof(error_buf));
    if (!module) {
        LOGV(ERROR) << fmt::format("Load wasm module failed. error: {}", error_buf);
        throw;
    }
    module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
    if (!module_inst) {
        LOGV(ERROR) << fmt::format("Instantiate wasm module failed. error: {}", error_buf);
        throw;
    }
    exec_env = wasm_runtime_create_exec_env(module_inst, stack_size);
}

bool WAMRInstance::load_wasm_binary(char *wasm_path) {
    buffer = bh_read_file_to_buffer(wasm_path, &buf_size);
    if (!buffer) {
        LOGV(ERROR) << "Open wasm app file failed.\n";
        return false;
    }
    if ((get_package_type((const uint8_t *)buffer, buf_size) != Wasm_Module_Bytecode) &&
        (get_package_type((const uint8_t *)buffer, buf_size) != Wasm_Module_AoT)) {
        LOGV(ERROR) << "WASM bytecode or AOT object is expected but other file format";

        BH_FREE(buffer);
        return false;
    }

    return true;
}

WAMRInstance::~WAMRInstance() {
    if (!exec_env)
        wasm_runtime_destroy_exec_env(exec_env);
    if (!module_inst)
        wasm_runtime_deinstantiate(module_inst);
    if (!module)
        wasm_runtime_unload(module);
    wasm_runtime_destroy();
}

int WAMRInstance::invoke_main() {
    func = nullptr;

    if (!(func = wasm_runtime_lookup_wasi_start_function(module_inst))) {
        LOGV(ERROR) << "The wasi mode main function is not found.";
        return -1;
    }

    return wasm_runtime_call_wasm(exec_env, func, 0, nullptr);
}
WAMRInstance::WAMRInstance(WAMRModuleInstance *moduleInstance, WAMRExecEnv *execEnv) {
    // restore logic.
}

WASMExecEnv *WAMRInstance::get_exec_env() { return exec_env; }

WASMModuleInstance *WAMRInstance::get_module_instance() { return reinterpret_cast<WASMModuleInstance *>(module_inst); }

WASMModule *WAMRInstance::get_module() { return reinterpret_cast<WASMModule *>(module); }
