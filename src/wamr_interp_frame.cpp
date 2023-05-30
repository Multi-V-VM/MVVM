//
// Created by victoryang00 on 5/19/23.
//

#include "wamr_interp_frame.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRInterpFrame::dump(WASMInterpFrame *env) {
    if (env->ip)
        ip = env->ip - env->function->u.func->code; // here we need to get the offset from the code start.
    lp = reinterpret_cast<uint8 *>(env->lp) -((uint8*) wamr->get_exec_env()->wasm_stack.s.bottom); // offset to the wasm_stack_top
    if (env->sp) {
        sp = reinterpret_cast<uint8 *>(env->sp) -
            ((uint8*) wamr->get_exec_env()->wasm_stack.s.bottom); // offset to the wasm_stack_top
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
void WAMRInterpFrame::restore(WASMInterpFrame *env) {
    env->function = reinterpret_cast<WASMFunctionInstance *>(malloc(sizeof(WASMFunctionInstance)));
    ::restore(&function, env->function);
    if (ip)
        env->ip = env->function->u.func->code + ip;
    if (sp)
        env->sp = reinterpret_cast<uint32 *>((uint8*)wamr->get_exec_env()->wasm_stack.s.bottom + sp);
    if (lp)
        *env->lp = *reinterpret_cast<uint32 *>((uint8*)wamr->get_exec_env()->wasm_stack.s.bottom + lp);
    int i=0;
    env->csp_bottom = static_cast<WASMBranchBlock *>(malloc(sizeof(WASMBranchBlock) * csp.size()));
    for (auto &&csp_item : csp) {
        ::restore(csp_item.get(), env->csp_bottom+i);
        i++;
    }
    env->csp = env->csp_bottom + csp.size();
}
