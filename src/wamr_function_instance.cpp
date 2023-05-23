//
// Created by victoryang00 on 5/22/23.
//

#include "wamr_function_instance.h"
extern WASMExecEnv *wamr;
void WAMRFunctionInstance::restore(WASMFunctionInstance *env) {

    // iterate all the function instance and put to the function instance
    auto target_module = ((WASMModuleInstance *)wamr->module_inst)->e;
    for (int i = 0; i < target_module->function_count; i++) {
        auto cur_func = target_module->functions[i];
        if (!is_import_func) {
#if WASM_ENABLE_CUSTOM_NAME_SECTION == 0
            if (param_count != cur_func.param_count) {
                continue;
            }
            /* local variable count, 0 for import function */
            if (local_count != env->local_count) {
                continue;
            } /* cell num of parameters */
            if (param_cell_num != env->param_cell_num) {
                continue;
            }
            /* cell num of return type */
            if (ret_cell_num != env->ret_cell_num) {
                continue;
            }
            /* cell num of local variables, 0 for import function */
            if (local_cell_num != env->local_cell_num) {
                continue;
            }

            for (int i = 0; i < param_count + local_count; i++) {
                if (local_offsets[i] != env->local_offsets[i]) {
                    continue;
                }
            }
            for (int i = 0; i < param_count; i++) {
                if (param_types[i] != env->param_types[i]) {
                    continue;
                }
            }
            /* local types, NULL for import function */
            for (int i = 0; i < local_count; i++) {
                if (local_types[i] != env->local_types[i]) {
                    continue;
                }
            }
#endif
            if (::equal(this->func, cur_func.u.func)) {
                *env = cur_func;
                break;
            }
        } else {
            if (::equal(this->func_import, cur_func.u.func_import)) {
                *env = cur_func;
                break;
            }
        }
    }

}
