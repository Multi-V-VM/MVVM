//
// Created by yiwei yang on 5/6/23.
//

#include "wamr.h"

WAMRInstance::WAMRInstance() {

    wasm_module_t module;
    wasm_module_inst_t module_inst;
    wasm_function_inst_t func;
    wasm_exec_env_t exec_env;
    uint32 size, stack_size = 8092, heap_size = 8092;
    wasm_runtime_init();

    buffer = read_wasm_binary_to_buffer(…, &size);

    /* add line below if we want to export native functions to WASM app */
    //    wasm_runtime_register_natives();

    /* parse the WASM file from buffer and create a WASM module */
    module = wasm_runtime_load(buffer, size, error_buf, sizeof(error_buf));

    /* create an instance of the WASM module (WASM linear memory is ready) */
    module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
    execEnv = wasm_runtime_create_exec_env(module_inst, stack_size);
}
