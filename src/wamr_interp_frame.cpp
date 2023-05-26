//
// Created by victoryang00 on 5/19/23.
//

#include "wamr_interp_frame.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRInterpFrame::dump(WASMInterpFrame *env) {
    if (env->ip)
        ip = env->ip - env->function->u.func->code; // here we need to get the offset from the code start.
    lp = reinterpret_cast<uint8 *>(env->lp) - wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
    if (env->sp) {
        sp = reinterpret_cast<uint8 *>(env->sp) -
             wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
    }
    auto csp_size = (env->csp - env->csp_bottom);
    for (int i = 0; i < csp_size; i++) {
        auto *local_csp = new WAMRBranchBlock();
        ::dump(local_csp, env->csp - i);
    }
    if (env->function)
        ::dump(&function, env->function);
}
void WAMRInterpFrame::restore(WASMInterpFrame *env) {
    ::restore(&function, env->function);
    if (ip)
        env->ip = env->function->u.func->code + ip.value();
    env->lp = wamr->get_exec_env()->wasm_stack.s.bottom + lp.value();
    if (sp)
        env->sp = wamr->get_exec_env()->wasm_stack.s.bottom + sp.value();
}
