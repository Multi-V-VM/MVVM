//
// Created by victoryang00 on 5/2/23.
//

#ifndef MVVM_WAMR_WASI_CONTEXT_H
#define MVVM_WAMR_WASI_CONTEXT_H

#include "wamr_serializer.h"
#include "wasm_runtime.h"
#include <dirent.h>
#include <atomic>
#include <fmt/printf.h>
#include <filesystem>
#include <map>
#include <memory>
#include <ranges>
#include <string>
#include <vector>

// TODO: add more context
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
struct WAMRFDObjectEntry {
    uint8 type{};
    int number{};
    std::string file{};
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
    uint16 ip46[8];
    bool is_4;
    uint8 mask;
};

struct WAMRWASIContext {
    WAMRFDTable curfds;
    //    std::map<uint32, std::pair<std::string,WAMRFDStat>, std::less<>> fd_map;
    WAMRPreStats prestats; // mapping from map_dir to dir
    WAMRArgvEnvironValues argv_environ;
    WAMRAddrPool addr_pool;
    std::vector<std::string> ns_lookup_list;
    uint32_t exit_code;
    void dump(WASIContext *env) {
        // Need to open the file and reinitialize the file descripter by map.
        this->curfds.size = env->curfds->size;
        this->curfds.used = env->curfds->used;
        wasi_fd_entry *entry = ((wasi_fd_entry *)env->curfds->entries);
        for (int i = 0; i < env->curfds->size; i++) {
            auto dumped_fo = WAMRFDObjectEntry();
            if (entry[i].object != nullptr) {
                dumped_fo.type = entry[i].object->type;
                dumped_fo.number = entry[i].object->number;

                // open type? read write? or just rw
                if (entry[i].object->directory.handle != nullptr) {
                    auto d = readdir(entry[i].object->directory.handle);
                    //got from dir path, is that one on one?
//                   dumped_fo.dir=fmt::sprintf("%s/%s",dir_path, d->d_name);
                    dumped_fo.dir = d->d_name;
                    LOGV(DEBUG) << dumped_fo.dir;
                    dumped_fo.offset = entry[i].object->directory.offset;
                }
            }
            dumped_fo.rights_base = entry[i].rights_base;
            dumped_fo.rights_inheriting = entry[i].rights_inheriting;
            this->curfds.entries.emplace_back(dumped_fo);
        }
#ifndef MVVM_DEBUG
        char path[256] = "/proc/self/fd";
        char fname[256];
        DIR *d;
        int len, path_len, fd, flags;

        path_len = strlen(path);
        d = opendir(path);

        if (!d) {
            throw(std::runtime_error("open self fd error"));
        }

        struct dirent *e;
        while ((e = readdir(d)) != nullptr) {
            if (e->d_name[0] == '.')
                continue; // skip "." and ".."

            if (strlen(path) + 1 + strlen(e->d_name) >= 256) // overflow
            {
                closedir(d);
                throw(std::runtime_error("open self fd overflow"));
            }
            path[path_len] = '/';
            path[path_len + 1] = 0;
            strcat(path, e->d_name);

            len = readlink(path, fname, sizeof(fname) - 1);
            if (len > 0) {
                fname[len] = 0;
            }
            fd = atoi(e->d_name);
            flags = fcntl(fd, F_GETFL);
            fd_map[fd] = std::make_pair(fname, flags);
        }
        closedir(d);
#endif
        this->prestats.size = env->prestats->size;
        this->prestats.used = env->prestats->used;

        for (int i = 0; i < env->curfds->size; i++) {
            auto dumped_pres = WAMRFDPrestat();
            dumped_pres.dir = ((const char *)env->prestats->prestats[i]);
            LOGV(DEBUG) << dumped_pres.dir;
            this->prestats.prestats.emplace_back(dumped_pres);
        }
        this->argv_environ.argv_list;

    };
    void restore(WASIContext *env) {
        // Need to open the file and reinitialize the file descripter by map.
        env->curfds->size = this->curfds.size;
        env->curfds->used = this->curfds.used;
        env->curfds->entries = (fd_entry *)malloc(sizeof(wasi_fd_entry) * this->curfds.size);

        auto i = 0;
        auto entries =  (wasi_fd_entry *)env->curfds->entries;
        for (auto &&entry :  this->curfds.entries) {
            if (entry.number) {
                entries[i].object->type = entry.type;
                entries[i].object->number = entry.number;
                // need to open to this number
            }
            if (!entry.dir.empty()) {
                entries[i].object->directory.handle = opendir(entry.dir.c_str());
                entries[i].object->directory.offset = entry.offset;
            }
            entries[i].rights_base = entry.rights_base;
            entries[i].rights_inheriting = entry.rights_inheriting;
            i++;
        }

#ifndef MVVM_DEBUG
        for (auto [fd, stat] : fd_map) {
            while (true) {
                auto fd_ = open(stat.first.c_str(), stat.second);
                if (fd < fd_) {
                    close(fd_);
                    continue;
                }
                break;
            }
        }
#endif
    };
};

template <SerializerTrait<WASIContext *> T> void dump(T t, WASIContext *env) { t->dump(env); }
template <SerializerTrait<WASIContext *> T> void restore(T t, WASIContext *env) { t->restore(env); }

#endif // MVVM_WAMR_WASI_CONTEXT_H
