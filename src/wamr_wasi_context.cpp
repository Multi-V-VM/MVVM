//
// Created by victoryang00 on 5/2/23.
//
#include "wamr_wasi_context.h"
#include "logging.h"
#include "platform_wasi_types.h"
#include "wamr.h"
#include <string>
#include <sys/types.h>
extern WAMRInstance *wamr;
#if !defined(_WIN32)
#include <sys/socket.h>
struct sockaddr_in sockaddr_from_ip4(const SocketAddrPool &addr) {
    struct sockaddr_in sockaddr4;
    memset(&sockaddr4, 0, sizeof(sockaddr4));
    sockaddr4.sin_family = AF_INET;
    sockaddr4.sin_port = addr.port;
    sockaddr4.sin_addr.s_addr = (addr.ip4[0] << 24) | (addr.ip4[1] << 16) | (addr.ip4[2] << 8) | addr.ip4[3];
    return sockaddr4;
}

struct sockaddr_in6 sockaddr_from_ip6(const SocketAddrPool &addr) {
    struct sockaddr_in6 sockaddr6;
    memset(&sockaddr6, 0, sizeof(sockaddr6));
    sockaddr6.sin6_family = AF_INET6;
    sockaddr6.sin6_port = addr.port;
    sockaddr6.sin6_addr.s6_addr16[0] = addr.ip6[0];
    sockaddr6.sin6_addr.s6_addr16[1] = addr.ip6[1];
    sockaddr6.sin6_addr.s6_addr16[2] = addr.ip6[2];
    sockaddr6.sin6_addr.s6_addr16[3] = addr.ip6[3];
    sockaddr6.sin6_addr.s6_addr16[4] = addr.ip6[4];
    sockaddr6.sin6_addr.s6_addr16[5] = addr.ip6[5];
    sockaddr6.sin6_addr.s6_addr16[6] = addr.ip6[6];
    sockaddr6.sin6_addr.s6_addr16[7] = addr.ip6[7];

    return sockaddr6;
}
#endif

void WAMRWASIContext::dump_impl(WASIArguments *env) {
    uint8 *buf = (uint8 *)malloc(1024);
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
#if !defined(_WIN32)
    if (gettid() == getpid()) {
        is_debug = false;
        for (auto [fd, socketMetaData] : wamr->socket_fd_map_) {
            ssize_t rc;
            wamr->socket_fd_map_[fd].is_collection = true;

            if (!wamr->op_data.is_tcp) {
                while (wamr->socket_fd_map_[fd].is_collection) { // drain udp socket
                    // get source from previous packets
                    // emunate the recvfrom syscall
                    if (socketMetaData.socketAddress.is_4) {
                        struct sockaddr_in sockaddr4 = sockaddr_from_ip4(socketMetaData.socketAddress);
                        socklen_t sockaddr4_size = sizeof(sockaddr4);
                        rc = wamr->invoke_recvfrom(fd, &buf, sizeof(buf) - 1, 0, (struct sockaddr *)&sockaddr4,
                                                   &sockaddr4_size);
                    } else {
                        struct sockaddr_in6 sockaddr6 = sockaddr_from_ip6(socketMetaData.socketAddress);
                        socklen_t sockaddr6_size = sizeof(sockaddr6);
                        rc = wamr->invoke_recvfrom(fd, &buf, sizeof(buf) - 1, 0, (struct sockaddr *)&sockaddr6,
                                                   &sockaddr6_size);
                    }
                    if (rc == -1) {
                        LOGV(ERROR) << "recvfrom error";
                        return;
                    }
                }
            } else {
                while (wamr->socket_fd_map_[fd].is_collection) { // drain tcp socket whether it's tcp or udp
                    // fd got from the virtual machine
                    rc = wamr->invoke_recv(fd, &buf, sizeof(buf), 0);
                    if (rc == -1) {
                        LOGV(ERROR) << "recv error";
                        return;
                    }
                }
            }
        }
    }
#endif
}
void WAMRWASIContext::restore_impl(WASIArguments *env) {
    int r;

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
                if (r != fd)
                    wamr->invoke_frenumber(r, fd);
                wamr->fd_map_[fd] = res;
                break;
            case MVVM_FWRITE:
            case MVVM_FREAD:
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
};