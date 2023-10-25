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
//    for (auto [fd, res] : this->fd_map) {
//        // differ from path from file
//        wamr->invoke_fopen(fd, std::get<0>(res),std::get<1>(res));
//        wamr->invoke_fseek(fd, std::get<2>(res));
//    }
};