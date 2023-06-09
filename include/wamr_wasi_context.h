//
// Created by victoryang00 on 5/2/23.
//

#ifndef MVVM_WAMR_WASI_CONTEXT_H
#define MVVM_WAMR_WASI_CONTEXT_H

#include "logging.h"
#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <atomic>
#include <dirent.h>
#include <filesystem>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

struct wasi_fd_object {
    std::atomic_uint refcount;
    __wasi_filetype_t type;
    int number;

    union {
        // Data associated with directory file descriptors.
        struct {
            struct mutex lock; // Lock to protect members below.
            DIR *handle; // Directory handle.
            __wasi_dircookie_t offset; // Offset of the directory.
        } directory;
    };
};
struct wasi_fd_entry {
    struct wasi_fd_object *object;
    __wasi_rights_t rights_base;
    __wasi_rights_t rights_inheriting;
};
struct wasi_fd_prestat {
    const char *dir;
};
struct wasi_fd_prestats {
    struct rwlock lock;
    struct wasi_fd_prestat *prestats;
    size_t size;
    size_t used;
};
struct WAMRFDObjectEntry {
    uint8 type{};
    int number{};
    std::string dir{};
    uint64 offset{};
    uint64 rights_base{};
    uint64 rights_inheriting{};
};
struct WAMRFDTable {
    std::vector<WAMRFDObjectEntry> entries;
    uint32 size{};
    uint32 used{};
};
struct WAMRFDPrestat {
    std::string dir; // the path link prestats for dir need to load target directory
};

struct WAMRPreStats {
    std::vector<WAMRFDPrestat> prestats;
    uint32 size{};
    uint32 used{};
};

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
    WAMRFDTable curfds;
    std::map<uint32, std::pair<std::string,int>> fd_map;
    std::vector<std::string> dir;
    std::vector<std::string> map_dir;
    WAMRPreStats prestats; // mapping from map_dir to dir
    WAMRArgvEnvironValues argv_environ;
    std::vector<WAMRAddrPool> addr_pool;
    std::vector<std::string> ns_lookup_list;
    uint32_t exit_code;
    void dump(WASIContext *env);
    void restore(WASIContext *env);
};

template <SerializerTrait<WASIContext *> T> void dump(T t, WASIContext *env) { t->dump(env); }
template <SerializerTrait<WASIContext *> T> void restore(T t, WASIContext *env) { t->restore(env); }

#endif // MVVM_WAMR_WASI_CONTEXT_H
