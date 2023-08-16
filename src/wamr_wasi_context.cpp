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
    this->exit_code = env->exit_code;
}
void WAMRWASIContext::restore_impl(WASIContext *env) {
//#ifdef __linux__
//    wamr->invoke_preopen(0, "/dev/stdin");
//    wamr->invoke_preopen(1, "/dev/stdout");
//    wamr->invoke_preopen(2, "/dev/stderr");
//#endif
    for (auto [fd, res] : this->fd_map) {
        // differ from path from file
        wamr->invoke_open(fd, res.first, res.second);
    }
};