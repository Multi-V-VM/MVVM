//
// Created by victoryang00 on 5/19/23.
//

#include "wamr_interp_frame.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRInterpFrame::dump(WASMInterpFrame *env) {
        if (env->ip)
            ip = env->ip - env->function->u.func->code; // here we need to get the offset from the code start.

        if(env->lp) {
            uint8 *local_lp = reinterpret_cast<uint8 *>(env->lp);
            lp = local_lp - wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
        }

        if (env->sp){
            uint8 *local_sp = reinterpret_cast<uint8 *>(env->sp);
            sp = local_sp - wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
        }
        auto csp_size = (env->csp - env->csp_bottom)/sizeof(WASMInterpFrame);
         for (int i = 0; i < csp_size; i++) {
            WAMRBranchBlock* local_csp= new WAMRBranchBlock();
             ::dump(local_csp, env->csp+i);
         }
}