//
// Created by yiwei yang on 4/29/23.
//

#ifndef MVVM_WAMR_MEMORY_INSTANCE_H
#define MVVM_WAMR_MEMORY_INSTANCE_H
#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <memory>
template <uint64 memory_data_size, uint64 heap_data_size> struct WAMRMemoryInstance {
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
    std::array<uint8, memory_data_size> memory_data;

    /* Heap data base address */
    std::array<uint8, heap_data_size> heap_data;

    void dump(WASMMemoryInstance *env){};
    void restore(WASMMemoryInstance *env){};
};

template <SerializerTrait<WASMMemoryInstance *> T> void dump(T t, WASMMemoryInstance *env) { t->dump(env); }

template <SerializerTrait<WASMMemoryInstance *> T> void restore(T t, WASMMemoryInstance *env) { t->restore(env); }

#endif // MVVM_WAMR_MEMORY_INSTANCE_H
