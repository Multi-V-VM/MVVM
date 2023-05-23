//
// Created by victoryang00 on 5/22/23.
//

#include "wamr_branch_block.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRBranchBlock::dump(WASMBranchBlock *env) {
    if (env->begin_addr)
        begin_addr = env->begin_addr -
                     wamr->get_exec_env()->wasm_stack.s.bottom; // here we need to get the offset from the code start.

    if (env->target_addr) {
        target_addr = env->target_addr - wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
    }

    if (env->frame_sp) {
        uint8 *local_sp = reinterpret_cast<uint8 *>(env->frame_sp);
        frame_sp = local_sp - wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
    }

    cell_num = env->cell_num;
}
