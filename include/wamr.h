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
    explicit WAMRInstance(char *wasm_path);
    template <uint32 memory_count, uint64 memory_data_size, uint64 heap_data_size, uint32 stack_frame_size,
              uint32 csp_size, uint64 stack_data_size>
    explicit WAMRInstance(WAMRExecEnv<memory_count, memory_data_size, heap_data_size, stack_frame_size, csp_size,
                                      stack_data_size> *execEnv){};
    bool load_wasm_binary(char *wasm_path);
    WASMExecEnv *get_exec_env();
    WASMModuleInstance *get_module_instance();
    WASMModule *get_module();
    int invoke_main();
    ~WAMRInstance();
};
void serialize_to_file(WASMExecEnv *instance);
#endif // MVVM_WAMR_H
