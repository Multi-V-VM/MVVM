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

#ifndef MVVM_WAMR_INTERP_FRAME_H
#define MVVM_WAMR_INTERP_FRAME_H

#include "aot_runtime.h"
#include "wamr_branch_block.h"
#include "wasm_interp.h"
#include "wasm_runtime.h"
#include <memory>
struct WAMRInterpFrame {
    uint32 size{};
    /* Instruction pointer of the bytecode array.  */
    uint32 ip{};
    int32 function_index{};
    std::string function_name{};
    /* Operand stack top pointer of the current frame. The bottom of
       the stack is the next cell after the last local variable. */
    uint32 sp{};

    /*
     * Frame data, the layout is:
     *  lp: parameters and local variables
     *  sp_bottom to sp_boundary: wasm operand stack
     *  csp_bottom to csp_boundary: wasm label stack
     *  jit spill cache: only available for fast jit
     */
    std::vector<uint32> stack_frame;

    void dump_impl(WASMInterpFrame *env);
    void restore_impl(WASMInterpFrame *env);

    void dump_impl(AOTFrame *env);
    void restore_impl(AOTFrame *env);
};

template <SerializerTrait<WASMInterpFrame *> T> void dump(T t, WASMInterpFrame *env) { t->dump_impl(env); }
template <SerializerTrait<WASMInterpFrame *> T> void restore(T t, WASMInterpFrame *env) { t->restore_impl(env); }

template <SerializerTrait<AOTFrame *> T> void dump(T t, AOTFrame *env) { t->dump_impl(env); }
template <SerializerTrait<AOTFrame *> T> void restore(T t, AOTFrame *env) { t->restore_impl(env); }

#endif // MVVM_WAMR_INTERP_FRAME_H