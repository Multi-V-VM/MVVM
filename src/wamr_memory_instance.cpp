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

#include "wamr_memory_instance.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

void WAMRMemoryInstance::restore_impl(WASMMemoryInstance *env) {
    if (memory_data.size() > std::numeric_limits<uint32>::max())
        throw std::runtime_error("restored WAMR linear memory exceeds the runtime size field");
    if (heap_data_offset > memory_data.size() || heap_data_size > memory_data.size() - heap_data_offset)
        throw std::runtime_error("restored WAMR heap range is outside linear memory");
    if (ref_count == std::numeric_limits<uint16>::max())
        throw std::runtime_error("restored WAMR shared-memory reference count overflow");

    env->module_type = module_type;
    /* Keep one reference pinned for the externally owned DAX mapping.  WAMR's
     * final deinstantiate must not pass this address to wasm_runtime_free(). */
    env->ref_count = std::max<uint16>(static_cast<uint16>(ref_count + 1), 2);
    env->is_shared_memory = true;
    env->num_bytes_per_page = num_bytes_per_page;
    env->cur_page_count = cur_page_count;
    env->max_page_count = max_page_count;
    env->memory_data_size = memory_data.size();
    env->memory_data = memory_data.data();
    env->memory_data_end = env->memory_data + (memory_data.size());
    env->heap_data = env->memory_data + heap_data_offset;
    env->heap_data_end = env->heap_data + heap_data_size;
};
