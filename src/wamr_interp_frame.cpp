//
// Created by victoryang00 on 5/19/23.
//

#include "wamr_interp_frame.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRInterpFrame::dump_impl(WASMInterpFrame *env) {
    LOGV(ERROR) << "env->function is not valid in AOT, skip serializing";
    return;
    if (env->function) {
        // what's the counterpart for the aot?
#if WASM_ENABLE_AOT == 0
        wamr->set_func(env->function->u.func);
#endif
        if (env->ip)
            ip = env->ip - env->function->u.func->code; // here we need to get the offset from the code start.

        lp = ((uint8 *)env->lp) - (uint8 *)wamr->get_exec_env()->wasm_stack.s.bottom; // offset to the wasm_stack_top
        LOGV(DEBUG) << "lp" << env->lp[0];
        if (env->sp) {
            sp = reinterpret_cast<uint8 *>(env->sp) -
                 ((uint8 *)wamr->get_exec_env()->wasm_stack.s.bottom); // offset to the wasm_stack_top
        }
#if WASM_ENABLE_AOT == 0
        auto csp_size = (env->csp - env->csp_bottom) + 1;
        for (int i = 0; i < csp_size; i++) {
            auto *local_csp = new WAMRBranchBlock();
            dump(local_csp, env->csp - i);
            csp.emplace_back(local_csp);
        }
        dump(&function, env->function);
#else
        // get type + offset for the function
        auto module_ = wamr->get_module();
        uint32 *func_type_indexes = module_->func_type_indexes;
        auto func_type_idx = module_->func_type_indexes[module_->start_func_index - module_->import_func_count];
        auto func_type = module_->func_types[func_type_idx];
        auto func = new WASMFunctionInstance{.is_import_func = false,
                                             .param_count = func_type->param_count,
                                             .local_count = func_type->result_count,
                                             .local_cell_num = static_cast<uint16>(func_type_idx),
                                             .u = {.func = new WASMFunction{.func_type = func_type}}
        };
        dump(&function, func);
#endif
    }
}
void WAMRInterpFrame::restore_impl(WASMInterpFrame *env) {
    env->function = reinterpret_cast<WASMFunctionInstance *>(malloc(sizeof(WASMFunctionInstance)));
#if WASM_ENABLE_AOT == 0
    restore(&function, env->function);
    if (env->function)
        wamr->set_func(env->function->u.func);
#endif

    if (ip)
        env->ip = env->function->u.func->code + ip;

    if (sp)
        env->sp = reinterpret_cast<uint32 *>((uint8 *)wamr->get_exec_env()->wasm_stack.s.bottom + sp);

    if (lp)
        LOGV(ERROR) << "env_lp " << env->lp[0] << " "
                    << *reinterpret_cast<uint32 *>((uint8 *)wamr->get_exec_env()->wasm_stack.s.bottom + lp);
#if WASM_ENABLE_AOT == 0
    int i = 0;
    env->sp_bottom = ((uint32 *)env->lp) + env->function->param_cell_num + env->function->local_cell_num;
    env->csp_bottom = static_cast<WASMBranchBlock *>(malloc(sizeof(WASMBranchBlock) * csp.size()));

    if (env->ip && env->function->u.func && !env->function->is_import_func && env->sp_bottom) {
        env->sp_boundary = env->sp_bottom + env->function->u.func->max_stack_cell_num;
        std::reverse(csp.begin(), csp.end());
        for (auto &&csp_item : csp) {
            restore(csp_item.get(), env->csp_bottom + i);
            LOGV(ERROR) << "csp_bottom" << ((uint8 *)env->csp_bottom + i) - wamr->get_exec_env()->wasm_stack.s.bottom;
            i++;
        }
        env->csp = env->csp_bottom + csp.size() - 1;
        env->csp_boundary = env->csp_bottom + env->function->u.func->max_block_num;
        LOGV(DEBUG) << " csp_bottom" << env->csp_bottom << " sp_bottom" << env->sp_bottom << " sp" << sp
                    << ((uint8 *)env->sp) - wamr->get_exec_env()->wasm_stack.s.bottom << " lp" << lp;
    }
#endif
}
