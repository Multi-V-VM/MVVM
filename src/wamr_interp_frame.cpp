//
// Created by victoryang00 on 5/19/23.
//

#include "wamr_interp_frame.h"
#include "wamr.h"
 struct WASMInterpFrame1 {
    /* The frame of the caller that are calling the current function. */
    struct WASMInterpFrame *prev_frame;

    /* The current WASM function. */
    struct WASMFunctionInstance *function;

    /* Instruction pointer of the bytecode array.  */
    uint8 *ip;

    /* Operand stack top pointer of the current frame. The bottom of
       the stack is the next cell after the last local variable. */
    uint32 *sp_bottom;
    uint32 *sp_boundary;
    uint32 *sp;

    WASMBranchBlock *csp_bottom;
    WASMBranchBlock *csp_boundary;
    WASMBranchBlock *csp;

    /**
     * Frame data, the layout is:
     *  lp: parameters and local variables
     *  sp_bottom to sp_boundary: wasm operand stack
     *  csp_bottom to csp_boundary: wasm label stack
     *  jit spill cache: only available for fast jit
     */
    uint32* lp;
} ;
extern WAMRInstance *wamr;
void WAMRInterpFrame::dump(WASMInterpFrame *env) {
    if (env->function) {
        wamr->set_func(env->function->u.func);

        if (env->ip)
            ip = env->ip - env->function->u.func->code; // here we need to get the offset from the code start.

        lp = ((uint8 *) env->lp) - (uint8 *) wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
        if (env->sp) {
            sp = reinterpret_cast<uint8 *>(env->sp) -
                 ((uint8 *) wamr->get_exec_env()->wasm_stack.s.bottom); // offset to the wasm_stack_top
        }
        auto csp_size = (env->csp - env->csp_bottom);
        for (int i = 0; i < csp_size; i++) {
            auto *local_csp = new WAMRBranchBlock();
            ::dump(local_csp, env->csp - i);
            csp.emplace_back(local_csp);
        }
        if (env->function)
            ::dump(&function, env->function);
    }
}
void WAMRInterpFrame::restore(WASMInterpFrame *env) {
    env->function = reinterpret_cast<WASMFunctionInstance *>(malloc(sizeof(WASMFunctionInstance)));
    ::restore(&function, env->function);
    if (env->function)
        wamr->set_func(env->function->u.func);
    if (ip)
        env->ip = env->function->u.func->code + ip;
    if (sp)
        env->sp = reinterpret_cast<uint32 *>((uint8 *)wamr->get_exec_env()->wasm_stack.s.bottom + sp);
    if (lp) {
        env->lp = reinterpret_cast<uint32 *>((uint8 *)wamr->get_exec_env()->wasm_stack.s.bottom + lp);
    }
    int i = 0;
    env->csp_bottom = static_cast<WASMBranchBlock *>(malloc(sizeof(WASMBranchBlock) * csp.size()));
    std::reverse(csp.begin(), csp.end());
    for (auto &&csp_item : csp) {
        ::restore(csp_item.get(), env->csp_bottom + i);
        i++;
    }
    env->csp = env->csp_bottom + csp.size() - 1;
}
