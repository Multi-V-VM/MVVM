#include "wamr_module_instance.h"
#include "wamr.h"

extern WAMRInstance *wamr;

void WAMRModuleInstance::dump_impl(WASMModuleInstance *env) {
    for (int i = 0; i < env->memory_count; i++) {
        // TODO: if the referenced memory has been serialized, just skip.
        auto local_mem = WAMRMemoryInstance();
        dump(&local_mem, env->memories[i]);
        memories.push_back(local_mem);
    }
    global_data = std::vector<uint8>(env->global_data, env->global_data + env->global_data_size);
    LOGV(DEBUG) << env->global_data_size;
    for (int i = 0; i < env->global_data_size; i++) {
        LOGV(DEBUG) << env->global_data[i];
    }
    dump(&wasi_ctx, env->wasi_ctx);

    if (wamr->is_aot) {
        auto module = (AOTModule *)env->module;
        aux_data_end_global_index = module->aux_data_end_global_index;
        aux_data_end = module->aux_data_end;
        aux_heap_base_global_index = module->aux_heap_base_global_index;
        aux_heap_base = module->aux_heap_base;
        aux_stack_top_global_index = module->aux_stack_top_global_index;
        aux_stack_bottom = module->aux_stack_bottom;
        aux_stack_size = module->aux_stack_size;
    } else {
        auto module = env->module;
        aux_data_end_global_index = module->aux_data_end_global_index;
        aux_data_end = module->aux_data_end;
        aux_heap_base_global_index = module->aux_heap_base_global_index;
        aux_heap_base = module->aux_heap_base;
        aux_stack_top_global_index = module->aux_stack_top_global_index;
        aux_stack_bottom = module->aux_stack_bottom;
        aux_stack_size = module->aux_stack_size;
    }
    dump(&global_table_data, env->global_table_data.memory_instances);
}

void WAMRModuleInstance::restore_impl(WASMModuleInstance *env) {
    env->memory_count = memories.size();
    for (int i = 0; i < env->memory_count; i++) {
        restore(&memories[i], env->memories[i]);
    }
    memcpy(env->global_data, global_data.data(), global_data.size());
    env->global_data_size = global_data.size();
    //                env->global_data = global_data.data();
    //                env->global_data_size = global_data.size() - 1;
    LOGV(DEBUG) << env->global_data_size;
    LOGV(DEBUG) << env->global_data;
    for (int i = 0; i < env->global_data_size; i++) {
        LOGV(DEBUG) << env->global_data[i];
    }
    if (wamr->is_aot) {
        auto module = (AOTModule *)env->module;
        module->aux_data_end_global_index = aux_data_end_global_index;
        module->aux_data_end = aux_data_end;
        module->aux_heap_base_global_index = aux_heap_base_global_index;
        module->aux_heap_base = aux_heap_base;
        module->aux_stack_top_global_index = aux_stack_top_global_index;
        module->aux_stack_bottom = aux_stack_bottom;
        module->aux_stack_size = aux_stack_size;
    } else {
        auto module = env->module;
        module->aux_data_end_global_index = aux_data_end_global_index;
        module->aux_data_end = aux_data_end;
        module->aux_heap_base_global_index = aux_heap_base_global_index;
        module->aux_heap_base = aux_heap_base;
        module->aux_stack_top_global_index = aux_stack_top_global_index;
        module->aux_stack_bottom = aux_stack_bottom;
        module->aux_stack_size = aux_stack_size;
    }
    restore(&global_table_data, env->global_table_data.memory_instances);
    restore(&wasi_ctx, env->wasi_ctx);
}