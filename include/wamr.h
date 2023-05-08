//
// Created by yiwei yang on 5/6/23.
//

#ifndef MVVM_WAMR_H
#define MVVM_WAMR_H

#include "logging.h"
#include "wasm_exec_env.h"
#include "wasm_module_instance.h"
#include "wasm_runtime.h"
#include "bh_read_file.h"

class WAMRInstance {
    WASMExecEnv execEnv;
    WASMModuleInstance moduleInstance;
    WASMModule module;
    WASMFunctionInstance func;
    char *buffer;
    char error_buf[128];
    uint32 buf_size, stack_size = 8092, heap_size = 8092;

public:
    WAMRInstance(char *wasm_path);
    WAMRInstance(WAMRModuleInstance *moduleInstance, WAMRExecEnv *execEnv);
    bool load_wasm_binary(char *wasm_path);
};

#endif // MVVM_WAMR_H
