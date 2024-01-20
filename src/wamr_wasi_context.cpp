//
// Created by victoryang00 on 5/2/23.
//
#include "wamr_wasi_context.h"
#include "wamr.h"
#include <string>
extern WAMRInstance *wamr;
void WAMRWASIContext::dump_impl(WASIArguments *env) {
    for (auto &i : wamr->dir_) {
        dir.emplace_back(i);
    }
    for (auto &i : wamr->map_dir_) {
        map_dir.emplace_back(i);
    }
    for (auto [fd, res] : wamr->fd_map_) {
        auto [path, op] = res;
        auto dumped_res = std::make_tuple(path, op);
        this->fd_map[fd] = dumped_res;
    }

    for (auto [fd, socketMetaData] : wamr->socket_fd_map_) {
        SocketMetaData socketMetaDataCopy = socketMetaData;
        this->socket_fd_map[fd] = socketMetaDataCopy;
    }
    this->sync_ops.assign(wamr->sync_ops.begin(), wamr->sync_ops.end());
}
void WAMRWASIContext::restore_impl(WASIArguments *env) {
    int r;
    // std::string stdin_fd= "/dev/stdin";
    // wamr->invoke_fopen(stdin_fd, 0);
    // std::string stdout_fd= "/dev/stdout";
    // wamr->invoke_fopen(stdout_fd, 0);
    // std::string stderr_fd= "/dev/stderr";
    // wamr->invoke_fopen(stderr_fd, 0);
    // std::string cur_file = "./";
    // wamr->invoke_init_c();
    // LOGV(ERROR) << r;

    for (auto [fd, res] : this->fd_map) {
        // differ from path from file
        auto path = std::get<0>(res);
        LOGV(INFO) << "fd: " << fd << " path: " << path;
        for (auto [flags, offset, op] : std::get<1>(res)) {
            // differ from path from file
            LOGV(INFO) << "fd: " << fd << " path: " << path << " flag: " << flags << " op: " << op;
            switch (op) {
            case MVVM_FOPEN:
                r = wamr->invoke_fopen(path, fd);
                LOGV(ERROR) << r;
                // if (r != fd)
                //     wamr->invoke_frenumber(r, fd);
                wamr->fd_map_[fd] = res;
                break;
            case MVVM_FWRITE:
            case MVVM_FREAD:
            case MVVM_FTELL:
                wamr->invoke_ftell(fd, offset, flags);
                break;
            case MVVM_FSEEK:
                wamr->invoke_fseek(fd, flags);
                break;
            }
        }
    }
#if !defined(_WIN32)
    for (auto [fd, socketMetaData] : this->socket_fd_map) {
        // Unfinished recv need to reset, if it's done, remove the socket
        LOGV(INFO) << "fd: " << fd << " SocketMetaData[domain]: "
                   << socketMetaData.domain
                   //    << " SocketMetaData[socketAddress]: " << socketMetaData.socketAddress
                   << " SocketMetaData[protocol]: " << socketMetaData.protocol
                   << " SocketMetaData[type]: " << socketMetaData.type;
        uint32 tmp_sock_fd = socketMetaData.socketOpenData.sockfd;
        wamr->invoke_sock_open(socketMetaData.socketOpenData.poolfd, socketMetaData.socketOpenData.af,
                               socketMetaData.socketOpenData.socktype,
                               &tmp_sock_fd); // should be done after restore call chain
        // renumber or not?
        LOGV(INFO) << "tmp_sock_fd " << tmp_sock_fd << " fd" << fd;
        if (tmp_sock_fd != fd)
            wamr->invoke_frenumber(tmp_sock_fd, fd);
    }
#endif
    wamr->sync_ops.assign(this->sync_ops.begin(), this->sync_ops.end());
};
