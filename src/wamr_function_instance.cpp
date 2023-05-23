//
// Created by victoryang00 on 5/22/23.
//

#include "wamr_function_instance.h"
extern WASMExecEnv* wamr;
void WAMRFunctionInstance::restore(WASMFunctionInstance *env) {

        // iterate all the function instance and put to the function instance
#if WASM_ENABLE_CUSTOM_NAME_SECTION != 0
        auto target_module = ((WASMModuleInstance*)wamr->module_inst)->module;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = target_module->functions[i];
            if (cur_func.field_name -> == env->u.func) {
                field_name = wamr->wasm_module->function_names[i];
                LOGV(DEBUG) << "field_name:" << field_name;
                break;
            }
        }
#else
        auto target_module = ((WASMModuleInstance*)wamr->module_inst)->module;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = target_module->functions[i];
            if (this->func== cur_func) {
                env =
                break;
            }
        }
//        is_import_func = env->is_import_func;
//        LOGV(DEBUG) << "is_import_func:" << is_import_func;
//        param_count= env->param_count;
//        LOGV(DEBUG) <<"param_count:"<<param_count;
//        /* local variable count, 0 for import function */
//        local_count= env->local_count;
//        LOGV(DEBUG) <<"local_count:"<<local_count;
//        /* cell num of parameters */
//        param_cell_num= env->param_cell_num;
//        LOGV(DEBUG) <<"param_cell_num:"<<param_cell_num;
//        /* cell num of return type */
//        ret_cell_num= env->ret_cell_num;
//        LOGV(DEBUG) <<"ret_cell_num:"<<ret_cell_num;
//        /* cell num of local variables, 0 for import function */
//        local_cell_num= env->local_cell_num;
//        LOGV(DEBUG) <<"local_cell_num:"<<local_cell_num;
//        local_offsets =std::vector(env->local_offsets, env->local_offsets+(param_count + local_count));
//        /* parameter types */
//        param_types = std::vector(env->param_types, env->param_types+param_count);
//        /* local types, NULL for import function */
//        local_types= std::vector(env->local_types, env->local_types+local_count);
#endif
}
