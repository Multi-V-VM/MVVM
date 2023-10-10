//
// Created by victoryang00 on 5/2/23.
//
#include "wamr_wasi_context.h"
#include "wamr.h"
#include <string>
extern WAMRInstance *wamr;
void WAMRWASIContext::dump_impl(WASIContext *env) {
    for (int i = 0; i < wamr->dir_.size(); i++) {
        dir.emplace_back(wamr->dir_[i]);
    }
    for (int i = 0; i < wamr->map_dir_.size(); i++) {
        map_dir.emplace_back(wamr->map_dir_[i]);
    }
    this->exit_code = env->exit_code;
    for (auto [fd, res] : wamr->fd_map_) {
        auto [path, flags, offset] = res;
        auto dumped_res = std::make_tuple(path, flags, offset);
        this->fd_map[fd] = dumped_res;
    }
}
void WAMRWASIContext::restore_impl(WASIContext *env) {
    int r;
    // std::string stdin_fd= "/dev/stdin";
    // wamr->invoke_fopen(stdin_fd, 0);
    // std::string stdout_fd= "/dev/stdout";
    // wamr->invoke_fopen(stdout_fd, 0);
    // std::string stderr_fd= "/dev/stderr";
    // wamr->invoke_fopen(stderr_fd, 0);
    for (auto [fd, res] : this->fd_map) {
        // differ from path from file
        LOGV(INFO) << "fd: " << fd << " path: " << std::get<0>(res) << " flags: " << std::get<1>(res)
                   << " offset: " << std::get<2>(res);
        r = wamr->invoke_fopen(std::get<0>(res), std::get<1>(res));
        if (fd != 0 && fd != 1 && fd != 2)
            wamr->invoke_frenumber(r, fd);
        wamr->invoke_fseek(fd, std::get<2>(res));
    }
};