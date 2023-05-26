//
// Created by yiwei yang on 5/6/23.
//

#ifndef MVVM_WAMR_H
#define MVVM_WAMR_H

#include "bh_read_file.h"
#include "logging.h"
#include "wamr_exec_env.h"
#include "wamr_read_write.h"
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
    explicit WAMRInstance(const char *wasm_path);
    void recover(std::vector<std::unique_ptr<WAMRExecEnv>>* execEnv);
    bool load_wasm_binary(const char *wasm_path);
    WASMExecEnv *get_exec_env();
    [[maybe_unused]] WASMModuleInstance *get_module_instance();
    [[maybe_unused]] WASMModule *get_module();
    int invoke_main();
    ~WAMRInstance();
};
void serialize_to_file(WASMExecEnv *instance);
#endif // MVVM_WAMR_H
