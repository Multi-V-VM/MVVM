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
    for (auto [fd, res] : wamr->fd_map_) {
        // differ from path from file
        wamr->invoke_fopen(fd, std::get<0>(res),std::get<1>(res));
        wamr->invoke_fseek(fd, std::get<2>(res));
    }
};