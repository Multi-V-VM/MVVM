//
// Created by yiwei yang on 5/2/23.
//

#ifndef MVVM_WAMR_WASI_CONTEXT_H
#define MVVM_WAMR_WASI_CONTEXT_H

#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <memory>
struct WAMRFDTable{

};
struct WAMRPreStats{

};
struct WAMRArgvEnvironValues{

};
struct WAMRAddrPool {

};
struct fd_table;
struct fd_prestats;
struct argv_environ_values;
struct addr_pool;
struct WAMRWASIContext {
    std::array<WAMRFDTable,10> curfds;
    std::array<WAMRPreStats, 10> prestats;
    std::unique_ptr<WAMRArgvEnvironValues> argv_environ;
    std::unique_ptr<WAMRAddrPool> addr_pool;
    std::unique_ptr<char> ns_lookup_buf;
    std::unique_ptr<std::string> ns_lookup_list;
    std::unique_ptr<char> argv_buf;
    std::unique_ptr<std::string> argv_list;
    std::unique_ptr<char> env_buf;
    std::unique_ptr<std::string> env_list;
    uint32_t exit_code;

    void dump(WASIContext *env);
    void restore(WASIContext *env);
};
template <SerializerTrait<WASIContext *> T> void dump(T &t, WASIContext *env) { t->dump(env); }

template <SerializerTrait<WASIContext *> T> void restore(T &t, WASIContext *env) { t->restore(env); }

#endif // MVVM_WAMR_WASI_CONTEXT_H
