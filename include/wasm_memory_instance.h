//
// Created by yiwei yang on 4/29/23.
//

#ifndef MVVM_WASM_MEMORY_INSTANCE_H
#define MVVM_WASM_MEMORY_INSTANCE_H
#include "wasm_runtime.h"
#include "wamr_serializer.h"
#include <memory>
class WAMRMemoryInstance : public std::enable_shared_from_this<WAMRSerializer> {
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
    /* Memory data size */
    uint32 memory_data_size;
    /**
     * Memory data begin address, Note:
     *   the app-heap might be inserted in to the linear memory,
     *   when memory is re-allocated, the heap data and memory data
     *   must be copied to new memory also
     */
    std::unique_ptr<uint8> memory_data;

    uint32 heap_data_size;
    /* Heap data base address */
    std::unique_ptr<uint8> heap_data;

    void dump() override;
    void restore() override;
};
#endif // MVVM_WASM_MEMORY_INSTANCE_H
