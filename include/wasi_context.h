//
// Created by yiwei yang on 5/2/23.
//

#ifndef MVVM_WASI_CONTEXT_H
#define MVVM_WASI_CONTEXT_H
#include "wasm_runtime.h"
class WAMRWASIContext {
    struct fd_table *curfds;
    struct fd_prestats *prestats;
    struct argv_environ_values *argv_environ;
    struct addr_pool *addr_pool;
    char *ns_lookup_buf;
    char **ns_lookup_list;
    char *argv_buf;
    char **argv_list;
    char *env_buf;
    char **env_list;
    uint32_t exit_code;
}   ;
#endif // MVVM_WASI_CONTEXT_H
