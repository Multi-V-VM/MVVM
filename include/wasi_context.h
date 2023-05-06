//
// Created by yiwei yang on 5/2/23.
//

#ifndef MVVM_WASI_CONTEXT_H
#define MVVM_WASI_CONTEXT_H
#include "wasm_runtime.h"
#include "wamr_serializer.h"

#include <memory>
struct WAMRWASIContext {
    std::unique_ptr<struct fd_table> curfds;
    std::unique_ptr<struct fd_prestats> prestats;
    std::unique_ptr<struct argv_environ_values> argv_environ;
    std::unique_ptr<struct addr_pool> addr_pool;
    std::unique_ptr<char> ns_lookup_buf;
    std::unique_ptr<std::unique_ptr<char>> ns_lookup_list;
    std::unique_ptr<char> argv_buf;
    std::unique_ptr<std::unique_ptr<char>>argv_list;
    std::unique_ptr<char>env_buf;
    std::unique_ptr<std::unique_ptr<char>>env_list;
    uint32_t exit_code;
};
#endif // MVVM_WASI_CONTEXT_H
