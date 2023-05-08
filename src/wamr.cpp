//
// Created by yiwei yang on 5/6/23.
//

#include "wamr.h"

WAMRInstance::WAMRInstance(char *wasm_path) {

    RuntimeInitArgs wasm_args;
    wasm_args.mem_alloc_type = Alloc_With_Allocator;
    wasm_args.mem_alloc_option.allocator.malloc_func = ((void *)malloc);
    wasm_args.mem_alloc_option.allocator.realloc_func = ((void *)realloc);
    wasm_args.mem_alloc_option.allocator.free_func = ((void *)free);

    if (!wasm_runtime_full_init(&wasm_args)) {
        LOGV(ERROR) << "Init runtime environment failed.\n";
        throw;
    }
    if (!load_wasm_binary(wasm_path)){
        LOGV(ERROR) << "Load wasm binary failed.\n";
        throw;
    }
    module = wasm_runtime_load((uint8_t *)buffer, buf_size, error_buf, sizeof(error_buf));
    if (!module) {
        LOGV(ERROR) <<  fmt::format("Load wasm module failed. error: {}", error_buf);
        throw;
    }

}

bool WAMRInstance::load_wasm_binary(char *wasm_path) {
    buffer = bh_read_file_to_buffer(wasm_path, &buf_size);
    if (!buffer) {
        LOGV(ERROR) << "Open wasm app file failed.\n";
        return false;
    }
    if ((get_package_type((const uint8_t *)buffer, buf_size) != Wasm_Module_Bytecode) &&
        (get_package_type((const uint8_t *)buffer, buf_size) != Wasm_Module_AoT)) {
        LOGV(ERROR) << "WASM bytecode or AOT object is expected but other file format";

        BH_FREE(buffer);
        return false;
    }

    return false;
}
