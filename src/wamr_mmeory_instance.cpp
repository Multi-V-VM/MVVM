#include "wamr.h"
#include "wamr_memory_instance.h"
extern WAMRInstance *wamr;
void WAMRMemoryInstance::restore_impl(WASMMemoryInstance *env) {
    env->module_type = module_type;
    env->ref_count = ref_count + 1;
    LOGV(ERROR) << "ref_count:" << env->ref_count;
    env->is_shared_memory = is_shared_memory;
    env->num_bytes_per_page = num_bytes_per_page;
    env->cur_page_count = cur_page_count;
    env->max_page_count = max_page_count;
    env->memory_data_size = memory_data.size();
#if !defined(_WIN32)
    if (env->ref_count > 0) // shared memory
        env->memory_data =
            (uint8 *)mmap(NULL, wamr->heap_size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    else
#endif
        env->memory_data = (uint8 *)malloc(env->memory_data_size);
    memcpy(env->memory_data, memory_data.data(), env->memory_data_size);
    env->memory_data_end = env->memory_data + memory_data.size();
    env->heap_data = (uint8 *)malloc(heap_data.size());
    memcpy(env->heap_data, heap_data.data(), heap_data.size());
    env->heap_data_end = env->heap_data + heap_data.size();
};