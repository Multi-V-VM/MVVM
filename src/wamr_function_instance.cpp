//
// Created by victoryang00 on 5/22/23.
//

#include "wamr_function_instance.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRFunctionInstance::restore_impl(WASMFunctionInstance *env) {
    // iterate all the function instance and put to the function instance
    auto target_module = wamr->get_module_instance()->e;
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
            }
#endif
            if (equal(&func, cur_func.u.func)) {
                *env = cur_func;
                break;
            }
        } else {
            if (equal(&func_import, cur_func.u.func_import)) {
                *env = cur_func;
                break;
            }
        }
    }
}
