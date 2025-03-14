/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2024 Regents of the Univeristy of California
 *  UC Santa Cruz Sluglab.
 */

#include "wamr_branch_block.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRBranchBlock::dump_impl(WASMBranchBlock *env) {
    if (env->begin_addr)
        begin_addr = env->begin_addr - wamr->get_func()->code; // here we need to get the offset from the code start.

    if (env->target_addr) {
        target_addr = env->target_addr - wamr->get_func()->code; // offset to the wasm_stack_top
    }

    if (env->frame_sp) {
        frame_sp = reinterpret_cast<uint8 *>(env->frame_sp) -
                   wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
    }

    cell_num = env->cell_num;
}
void WAMRBranchBlock::restore_impl(WASMBranchBlock *env) const {
    if (begin_addr)
        env->begin_addr = wamr->get_func()->code + begin_addr;

    if (target_addr)
        env->target_addr = wamr->get_func()->code + target_addr;

    if (frame_sp) {
        uint8 *local_sp = wamr->get_exec_env()->wasm_stack.s.bottom + frame_sp;
        env->frame_sp = reinterpret_cast<uint32 *>(local_sp);
    }

    env->cell_num = cell_num;
}
