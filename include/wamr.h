//
// Created by yiwei yang on 5/6/23.
//

#ifndef MVVM_WAMR_H
#define MVVM_WAMR_H

#include "bh_read_file.h"
#include "logging.h"
#include "wamr_exec_env.h"
#include "wasm_module_instance.h"
#include "wasm_runtime.h"

class WAMRInstance {
    WASMExecEnv *exec_env;
    WASMModuleInstanceCommon *module_inst;
    WASMModuleCommon *module;
    WASMFunctionInstanceCommon *func;
    char *buffer;
    char error_buf[128];
    uint32 buf_size, stack_size = 8092, heap_size = 8092;

public:
    WAMRInstance(char *wasm_path);
    WAMRInstance(WAMRModuleInstance *moduleInstance, WAMRExecEnv *execEnv);
    bool load_wasm_binary(char *wasm_path);
    int invoke_main();
    ~WAMRInstance();
};

#endif // MVVM_WAMR_H
