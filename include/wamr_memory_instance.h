/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andi Quinn
 *
 *  SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 *  Copyright 2025 Regents of the University of California
 *  UC Santa Cruz Sluglab.
 */

#ifndef MVVM_WAMR_MEMORY_INSTANCE_H
#define MVVM_WAMR_MEMORY_INSTANCE_H
#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
struct WAMRMemoryInstance {
    /* Module type */
    uint32 module_type;
    /* Shared memory flag */
    uint16 ref_count;
    /* Shared memory flag */
    uint8 is_shared_memory;
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
    std::span<uint8_t> memory_data;

    /* The app heap is inside linear memory.  Store its range rather than a
     * duplicate byte vector so restore can preserve the alias zero-copy. */
    uint64_t heap_data_offset;
    uint64_t heap_data_size;

    void dump_impl(WASMMemoryInstance *env) {
        module_type = env->module_type;
        ref_count = env->ref_count;
        num_bytes_per_page = env->num_bytes_per_page;
        cur_page_count = env->cur_page_count;
        max_page_count = env->max_page_count;
        is_shared_memory = env->is_shared_memory;
        memory_data = std::span<uint8_t>(env->memory_data, env->memory_data_size);
        if (env->heap_data == nullptr && env->heap_data_end == nullptr) {
            heap_data_offset = 0;
            heap_data_size = 0;
        } else {
            const auto memory_begin = reinterpret_cast<std::uintptr_t>(env->memory_data);
            const auto heap_begin = reinterpret_cast<std::uintptr_t>(env->heap_data);
            const auto heap_end = reinterpret_cast<std::uintptr_t>(env->heap_data_end);
            if (env->memory_data == nullptr || env->heap_data == nullptr || heap_end < heap_begin ||
                heap_begin < memory_begin || heap_begin - memory_begin > env->memory_data_size ||
                heap_end - heap_begin > env->memory_data_size - (heap_begin - memory_begin))
                throw std::runtime_error("WAMR app heap is not contained in linear memory");
            heap_data_offset = static_cast<uint64_t>(heap_begin - memory_begin);
            heap_data_size = static_cast<uint64_t>(heap_end - heap_begin);
        }
    };
    void restore_impl(WASMMemoryInstance *env);
};

template <SerializerTrait<WASMMemoryInstance *> T> void dump(T t, WASMMemoryInstance *env) { t->dump_impl(env); }
template <SerializerTrait<WASMMemoryInstance *> T> void restore(T t, WASMMemoryInstance *env) { t->restore_impl(env); }

#endif // MVVM_WAMR_MEMORY_INSTANCE_H
