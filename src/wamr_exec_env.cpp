//
// Created by yiwei yang on 5/8/23.
//

#include "wamr_exec_env.h"
template <uint32 memory_count, uint64 memory_data_size, uint64 heap_data_size, uint32 stack_frame_size, uint32 csp_size,
          uint64 stack_data_size>
void WAMRExecEnv<memory_count, memory_data_size, heap_data_size, stack_frame_size, csp_size, stack_data_size>::dump(
    WASMExecEnv *env) {
    this->module_inst->dump(reinterpret_cast<WASMModuleInstance *>(env->module_inst));
    flags = env->suspend_flags.flags;
    boundary = env->aux_stack_boundary.boundary;
    bottom = env->aux_stack_bottom.bottom;
    this->cur_frame.dump(env->cur_frame);
    for (int i = 0; i < stack_data_size; ++i) {
        wasm_stack[i] = *(env->wasm_stack.s.top + i);
    }
}
template <uint32 memory_count, uint64 memory_data_size, uint64 heap_data_size, uint32 stack_frame_size, uint32 csp_size,
          uint64 stack_data_size>
void WAMRExecEnv<memory_count, memory_data_size, heap_data_size, stack_frame_size, csp_size, stack_data_size>::restore(
    WASMExecEnv *env) {
    this->module_inst->restore(reinterpret_cast<WASMModuleInstance *>(env->module_inst));
}