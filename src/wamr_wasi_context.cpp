//
// Created by victoryang00 on 5/2/23.
//
#include "wamr_wasi_context.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRWASIContext::dump_impl(WASIContext *env) {
    for (int i = 0; i < wamr->dir_.size(); i++) {
        dir.emplace_back(wamr->dir_[i]);
    }
    for (int i = 0; i < wamr->map_dir_.size(); i++) {
        map_dir.emplace_back(wamr->map_dir_[i]);
    }
    for (auto [fd, res] : this->fd_map) {
        // differ from path from file
        LOGV(DEBUG)<<fmt::format("fd:{} path:{} option",fd,res.first);
    }
#if WASM_ENABLE_UVWASI != 0
#else

#endif
#if 0
    // Need to open the file and reinitialize the file descripter by map.
    this->curfds.size = env->curfds->size;
    this->curfds.used = env->curfds->used;
    wasi_fd_entry *entry = ((wasi_fd_entry *)env->curfds->entries);
    for (int i = 0; i < env->curfds->size; i++) {
        auto dumped_fo = WAMRFDObjectEntry();
        if (entry[i].object != nullptr) {
            dumped_fo.type = entry[i].object->type;
            dumped_fo.number = entry[i].object->number;
            if (dumped_fo.number > 2 && wamr->fd_map_.contains(dumped_fo.number)) {
                fd_map[dumped_fo.number] = wamr->fd_map_[dumped_fo.number];
                LOGV(DEBUG) << fmt::format("fd:{} path:{}", dumped_fo.number, fd_map[dumped_fo.number].first);
            }
            // open type? read write? or just rw
            // if (((uint64)entry[i].object->directory.handle) > 10000) {
            //     auto d = readdir(entry[i].object->directory.handle);
            //     // got from dir path, is that one on one?
            //     // dumped_fo.dir=fmt::sprintf("%s/%s",dir_path, d->d_name);
            //     dumped_fo.dir = d->d_name;
            //     dumped_fo.offset = entry[i].object->directory.offset;
            // }
            LOGV(DEBUG) << "type:" << dumped_fo.type;
            LOGV(DEBUG) << "number:" << dumped_fo.number;
        }
        dumped_fo.rights_base = entry[i].rights_base;
        dumped_fo.rights_inheriting = entry[i].rights_inheriting;
        this->curfds.entries.emplace_back(dumped_fo);
    }
#endif
#if 0
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
#if 0
    this->prestats.size = env->prestats->size;
    this->prestats.used = env->prestats->used;
    auto removeTrailingSlashes = [](const std::string &input) {
        size_t lastNotSlash = input.find_last_not_of('/');
        if (lastNotSlash != std::string::npos) {
            // lastNotSlash + 1 is the new length of the string
            return input.substr(0, lastNotSlash + 1);
        } else {
            // The string is all slashes
            return std::string("/");
        }
    };
    for (int i = 0; i < env->prestats->size; i++) {
        auto dumped_pres = WAMRFDPrestat();
        auto dir_ = (const char *)((wasi_fd_prestats *)env->prestats)->prestats[i].dir;
        if (dir_) {
            dumped_pres.dir = removeTrailingSlashes(dir_);
            LOGV(DEBUG) << dumped_pres.dir;
        }
        this->prestats.prestats.emplace_back(dumped_pres);
    }
    auto cur_pool = env->addr_pool;
    while (cur_pool) {
        if (cur_pool->addr.ip4 != 0) {
            auto dumped_addr = WAMRAddrPool();
            if (cur_pool->type == 0) {
                dumped_addr.is_4 = true;
                memcpy(dumped_addr.ip4, &cur_pool->addr.ip4, sizeof(uint32));
                LOGV(DEBUG) << fmt::format("ip4:{}", cur_pool->addr.ip4);
            } else {
                dumped_addr.is_4 = false;
                memcpy(dumped_addr.ip6, &cur_pool->addr.ip6, sizeof(uint16) * 8);
            }
            if (cur_pool->mask == 0) {
                dumped_addr.mask = UINT8_MAX;
            }
            this->addr_pool.emplace_back(dumped_addr);
        }
        cur_pool = cur_pool->next;
    }
    // TODO: need to set tcp alive
    this->argv_environ.argv_list =
        std::vector<std::string>(env->argv_environ->argv_list, env->argv_environ->argv_list + env->argv_environ->argc);
    this->argv_environ.env_list = std::vector<std::string>(
        env->argv_environ->environ_list, env->argv_environ->environ_list + env->argv_environ->environ_count);
#endif
    this->exit_code = env->exit_code;
}
void WAMRWASIContext::restore_impl(WASIContext *env) {
#if 0
    // Need to open the file and reinitialize the file descripter by map.
    env->curfds->size = this->curfds.size;
    env->curfds->used = this->curfds.used;
    env->curfds->entries = (fd_entry *)malloc(sizeof(wasi_fd_entry) * this->curfds.size);

    auto i = 0;
    auto entries = (wasi_fd_entry *)env->curfds->entries;
    for (auto &&entry : this->curfds.entries) {
        entries[i].object = (wasi_fd_object *)malloc(sizeof(wasi_fd_object));
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
    env->prestats->size = this->prestats.size;
    env->prestats->used = this->prestats.used;
    i = 0;
    env->prestats->prestats = (fd_prestat *)malloc(sizeof(wasi_fd_prestat) * this->prestats.size);
    auto prestats_ = (wasi_fd_prestat *)env->prestats->prestats;
    for (auto &&pre : this->prestats.prestats) {
        if (!pre.dir.empty()) {
            prestats_[i].dir = pre.dir.c_str();
        }
        i++;
    }
#endif
    for (auto [fd, res] : this->fd_map) {
        // differ from path from file
        wamr->invoke_open(fd, res.first, res.second);
    }
};