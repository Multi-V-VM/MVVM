//
// Created by victoryang00 on 5/30/23.
//

#include "wamr_block_addr.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRBlockAddr::dump_impl(BlockAddr *env) {
    this->start_addr =
        env->start_addr -
        wamr->get_exec_env()->cur_frame->function->u.func->code; // here we need to get the offset from the code start.
    //    LOGV(DEBUG) << "start_addr " << this->start_addr;
    if (env->else_addr)
        this->else_addr = env->else_addr - wamr->get_exec_env()->cur_frame->function->u.func->code;
    if (env->end_addr)
        this->end_addr = env->end_addr - wamr->get_exec_env()->cur_frame->function->u.func->code;
}
void WAMRBlockAddr::restore_impl(BlockAddr *env) const {
    env->start_addr = wamr->get_exec_env()->cur_frame->function->u.func->code + start_addr;
    if (else_addr)
        env->else_addr = wamr->get_exec_env()->cur_frame->function->u.func->code + else_addr;
    if (end_addr)
        env->end_addr = wamr->get_exec_env()->cur_frame->function->u.func->code + end_addr;
}
