//
// Created by yiwei yang on 5/8/23.
//

#include "wamr_exec_env.h"
void WAMRExecEnv::dump(WASMExecEnv *env) {
    this->module_inst->dump(reinterpret_cast<WASMModuleInstance *>(env->module_inst));
    flags = env->suspend_flags.flags;
    boundary = env->aux_stack_boundary.boundary;
    bottom = env->aux_stack_bottom.bottom;
    this->cur_frame.dump(env->cur_frame);
    wasm_stack_size = env->wasm_stack_size;
    for (int i = 0; i < wasm_stack_size; ++i) {
        wasm_stack[i] = *(env->wasm_stack.s.top + i);
    }
}

void WAMRExecEnv::restore(WASMExecEnv *env) {
    this->module_inst->restore(reinterpret_cast<WASMModuleInstance *>(env->module_inst));
}