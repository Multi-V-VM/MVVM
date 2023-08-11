//
// Created by victoryang00 on 4/29/23.
//

#ifndef MVVM_WAMR_MEMORY_INSTANCE_H
#define MVVM_WAMR_MEMORY_INSTANCE_H
#include "logging.h"
#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <memory>
#include <vector>
struct WAMRMemoryInstance {
    /* Module type */
    uint32 module_type;
    /* Shared memory flag */
    bool is_shared;
    /* Number bytes per page */
    uint32 num_bytes_per_page;
    /* Current page count */
    uint32 cur_page_count;
    /* Maximum page count */
    uint32 max_page_count;
    /*
     * Memory data begin address, Note:
     *   the app-heap might be inserted in to the linear memory,
     *   when memory is re-allocated, the heap data and memory data
     *   must be copied to new memory also
     */
    std::vector<uint8> memory_data;

    /* Heap data base address */
    std::vector<uint8> heap_data;

    void dump_impl(WASMMemoryInstance *env) {
        module_type = env->module_type;
        is_shared = env->is_shared;
        num_bytes_per_page = env->num_bytes_per_page;
        cur_page_count = env->cur_page_count;
        max_page_count = env->max_page_count;
        memory_data.resize(env->memory_data_size);
        memcpy(memory_data.data(), env->memory_data, env->memory_data_size);
        //        LOGV(DEBUG)<< "memory_data_size:" << env->memory_data_size;
        //        for (int i = 0; i < 100; ++i) {
        //            LOGV(DEBUG)<< "memory_data:" <<i << memory_data[i];
        //            LOGV(DEBUG)<< "memory_data:" <<i << env->memory_data[i];
        //        }
        heap_data = std::vector<uint8>(env->heap_data, env->heap_data_end);
    };
    void restore_impl(WASMMemoryInstance *env) {
        env->module_type = module_type;
        env->is_shared = is_shared;
        env->num_bytes_per_page = num_bytes_per_page;
        env->cur_page_count = cur_page_count;
        env->max_page_count = max_page_count;
        env->memory_data_size = memory_data.size();
        env->memory_data = (uint8 *)malloc(env->memory_data_size);
        memcpy(env->memory_data, memory_data.data(), env->memory_data_size);
        env->memory_data_end = env->memory_data + memory_data.size();
        //        LOGV(DEBUG)<< "memory_data_size:" << env->memory_data_size;
        //        for (int i = 0; i < 100; ++i) {
        //            LOGV(DEBUG)<< "memory_data:" <<i << memory_data[i];
        //            LOGV(DEBUG)<< "memory_data:" <<i << env->memory_data[i];
        //        }
        env->heap_data = (uint8 *)malloc(heap_data.size());
        memcpy(env->heap_data, heap_data.data(), heap_data.size());
        env->heap_data_end = env->heap_data + heap_data.size();
    };
};

template <SerializerTrait<WASMMemoryInstance *> T> void dump(T t, WASMMemoryInstance *env) { t->dump_impl(env); }
template <SerializerTrait<WASMMemoryInstance *> T> void restore(T t, WASMMemoryInstance *env) { t->restore_impl(env); }

#endif // MVVM_WAMR_MEMORY_INSTANCE_H
