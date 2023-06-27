//
// Created by victoryang00 on 5/2/23.
//

#ifndef MVVM_WAMR_WASI_CONTEXT_H
#define MVVM_WAMR_WASI_CONTEXT_H

#include "logging.h"
#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <atomic>
#include <filesystem>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

struct WAMRArgvEnvironValues {
    std::vector<std::string> argv_list;
    std::vector<std::string> env_list;
};
// need to serialize the opened file in the file descripter and the socket opened also.

struct WAMRAddrPool {
    uint8 ip4[4];
    uint16 ip6[8];
    bool is_4;
    uint8 mask;
};
struct WAMRWASIContext {
    std::map<uint32, std::pair<std::string,int>> fd_map;
    std::vector<std::string> dir;
    std::vector<std::string> map_dir;
    WAMRArgvEnvironValues argv_environ;
    std::vector<WAMRAddrPool> addr_pool;
    std::vector<std::string> ns_lookup_list;
    uint32_t exit_code;
    void dump_impl(WASIContext *env);
    void restore_impl(WASIContext *env);
};
template <SerializerTrait<WASIContext *> T> void dump(T t, WASIContext *env) { t->dump_impl(env); }
template <SerializerTrait<WASIContext *> T> void restore(T t, WASIContext *env) { t->restore_impl(env); }

#endif // MVVM_WAMR_WASI_CONTEXT_H
