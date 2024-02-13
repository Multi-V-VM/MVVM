//
// Created by victoryang00 on 5/2/23.
//
#include "wamr_wasi_context.h"
#include "logging.h"
#include "platform_wasi_types.h"
#include "wamr.h"
#include <chrono>
#include <fmt/core.h>
#include <string>
#include <sys/types.h>
extern WAMRInstance *wamr;
using namespace std::chrono_literals;

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
struct sockaddr_in sockaddr_from_ip4(const SocketAddrPool &addr) {
    struct sockaddr_in sockaddr4 {};
    memset(&sockaddr4, 0, sizeof(sockaddr4));
    sockaddr4.sin_family = AF_INET;
    sockaddr4.sin_port = addr.port;
    sockaddr4.sin_addr.s_addr = (addr.ip4[0] << 24) | (addr.ip4[1] << 16) | (addr.ip4[2] << 8) | addr.ip4[3];
    return sockaddr4;
}

struct sockaddr_in6 sockaddr_from_ip6(const SocketAddrPool &addr) {
    struct sockaddr_in6 sockaddr6 {};
    memset(&sockaddr6, 0, sizeof(sockaddr6));
    sockaddr6.sin6_family = AF_INET6;
    sockaddr6.sin6_port = addr.port;
    std::string ipv6_addr_str =
        fmt::format("{:#}:{:#}:{:#}:{:#}:{:#}:{:#}:{:#}:{:#}", addr.ip6[0], addr.ip6[1], addr.ip6[2], addr.ip6[3],
                    addr.ip6[4], addr.ip6[5], addr.ip6[6], addr.ip6[7]);
    if (inet_pton(AF_INET6, ipv6_addr_str.c_str(), &sockaddr6.sin6_addr) != 1) {
        exit(EXIT_FAILURE);
    }

    return sockaddr6;
}
#endif

void WAMRWASIContext::dump_impl(WASIArguments *env) {
    for (auto &i : wamr->dir_) {
        dir.emplace_back(i);
    }
    for (auto &i : wamr->map_dir_) {
        map_dir.emplace_back(i);
    }
    for (auto &i : wamr->env_) {
        env_list.emplace_back(i);
    }
    for (auto &i : wamr->arg_) {
        arg.emplace_back(i);
    }
    for (auto &i : wamr->addr_) {
        addr_pool.emplace_back(i);
    }
    for (auto &i : wamr->ns_pool_) {
        ns_lookup_list.emplace_back(i);
    }
    for (auto &[k, v] : wamr->tid_start_arg_map) {
        tid_start_arg_map[k] = v;
    }
    for (auto &[k, v] : wamr->child_tid_map) {
        child_tid_map[k] = v;
        LOGV(ERROR) << "child_tid_map: " << k << " " << v;
    }
    // only one thread has fd_map
    if (wamr->should_snapshot)
        for (auto [fd, res] : wamr->fd_map_) {
            auto [path, op] = res;
            auto dumped_res = std::make_tuple(path, op);
            this->fd_map[fd] = dumped_res;
        }
#if !defined(_WIN32)
    auto buf = (uint8 *)malloc(1024);
    // only one thread has socket_map
    if (wamr->should_snapshot)
        for (auto [fd, socketMetaData] : wamr->socket_fd_map_) {
            ssize_t rc;
            if (wamr->socket_fd_map_[fd].socketRecvFromDatas.empty()) {
                this->socket_fd_map[fd] = socketMetaData;
                continue;
            }
            wamr->socket_fd_map_[fd].is_collection = true;

            if (!wamr->op_data.is_tcp) {
                while (wamr->socket_fd_map_[fd].is_collection) { // drain udp socket
                    // get source from previous packets
                    // emunate the recvfrom syscall
                    if (socketMetaData.socketAddress.is_4) {
                        struct sockaddr_in sockaddr4 = sockaddr_from_ip4(socketMetaData.socketAddress);
                        socklen_t sockaddr4_size = sizeof(sockaddr4);
                        rc = wamr->invoke_recvfrom(fd, &buf, 1024, 0, (struct sockaddr *)&sockaddr4, &sockaddr4_size);
                    } else {
                        struct sockaddr_in6 sockaddr6 = sockaddr_from_ip6(socketMetaData.socketAddress);
                        socklen_t sockaddr6_size = sizeof(sockaddr6);
                        rc = wamr->invoke_recvfrom(fd, &buf, 1024, 0, (struct sockaddr *)&sockaddr6, &sockaddr6_size);
                    }
                    if (rc == -1) {
                        LOGV(ERROR) << "recvfrom error";
                        return;
                    }
                }
            } else {
                if (wamr->socket_fd_map_[fd].is_server) {
                    this->socket_fd_map[fd] = socketMetaData;
                    continue;
                }
                while (wamr->socket_fd_map_[fd].is_collection) { // drain tcp socket
                    rc = wamr->invoke_recv(fd, &buf, 1024, 0);
                    if (rc == -1) {
                        LOGV(ERROR) << "recv error";
                        return;
                    }
                }
            }

            this->socket_fd_map[fd] = socketMetaData;
        }

    this->sync_ops.assign(wamr->sync_ops.begin(), wamr->sync_ops.end());
#endif
}
void WAMRWASIContext::restore_impl(WASIArguments *env) {
    int r;

    if (!wamr->should_snapshot) {
#if defined(_WIN32)
        wamr->should_snapshot = true;
#endif
        for (auto &[k, v] : tid_start_arg_map) {
            LOGV(ERROR) << "tid_start_arg_map: " << k << " " << v.second;
            wamr->tid_start_arg_map[k] = v;
        }
        for (auto &[k, v] : child_tid_map) {
            wamr->child_tid_map[k] = v;
            LOGV(ERROR) << "child_tid_map: " << k << " " << v;
        }
        for (auto [fd, res] : this->fd_map) {
            // differ from path from file
            auto path = std::get<0>(res);
            LOGV(INFO) << "fd: " << fd << " path: " << path;
            for (auto [flags, offset, op] : std::get<1>(res)) {
                // differ from path from file
                if (wamr->policy == "replay") {

                    switch (op) {
                    case MVVM_FOPEN:
                        r = wamr->invoke_fopen(path, offset);
                        LOGV(ERROR) << r;
                        if (r != fd)
                            wamr->invoke_frenumber(r, fd);
                        wamr->fd_map_[fd] = res;
                        break;
                    case MVVM_FWRITE:
                        wamr->invoke_fwrite(fd, flags);
                        break;
                    case MVVM_FREAD:
                        wamr->invoke_fread(fd, flags);
                        break;
                    case MVVM_FSEEK:
                        wamr->invoke_fseek(fd, flags);
                        break;
                    }
                } else if (wamr->policy == "compression") {
                    if (op == MVVM_FOPEN) {
                        r = wamr->invoke_fopen(path, offset);
                        LOGV(ERROR) << r;
                        if (r != fd)
                            wamr->invoke_frenumber(r, fd);
                        wamr->fd_map_[fd] = res;
                    } else {
                        wamr->invoke_fseek(fd, flags);
                    }
                }
            }
        }
    }
#if !defined(_WIN32)
    // is tcp socket
    if (!wamr->should_snapshot) {
        wamr->should_snapshot = true;
        bool is_tcp_server = false;
        int old_fd = 0;
        int res;
        bool is_4 = false;
        for (auto [fd, socketMetaData] : this->socket_fd_map) {
            wamr->op_data.is_tcp |= socketMetaData.type;
            is_tcp_server |= socketMetaData.is_server;
            is_4 |= socketMetaData.socketAddress.is_4;
            if (!socketMetaData.is_server) {
                old_fd = fd;
            }
        }
        is_tcp_server &= wamr->op_data.is_tcp;

        for (auto [fd, socketMetaData] : this->socket_fd_map) {
            if (!socketMetaData.is_server) {
                if (!wamr->op_data.is_tcp) { // udp
                    res =
                        wamr->invoke_sock_open(socketMetaData.domain, socketMetaData.type, socketMetaData.protocol, fd);
                } else if (socketMetaData.socketAddress.is_4 || is_4) {
                    struct sockaddr_in sockaddr4 = sockaddr_from_ip4(socketMetaData.socketAddress);
                    inet_pton(AF_INET, MVVM_SOCK_ADDR6, &sockaddr4.sin_addr);

                    socklen_t sockaddr4_size = sizeof(sockaddr4);

                    wamr->invoke_sock_connect(fd, (struct sockaddr *)&sockaddr4, sizeof(sockaddr4));
                } else {
                    struct sockaddr_in6 sockaddr6 = sockaddr_from_ip6(socketMetaData.socketAddress);
                    inet_pton(AF_INET, MVVM_SOCK_ADDR6, &sockaddr6.sin6_addr);

                    socklen_t sockaddr6_size = sizeof(sockaddr6);
                    wamr->invoke_sock_connect(fd, (struct sockaddr *)&sockaddr6, sizeof(sockaddr6));
                }

                // renumber or not?
                // LOGV(INFO) << "tmp_sock_fd " << res << " fd" << fd;
                wamr->socket_fd_map_[fd] = socketMetaData;
            }
        }
        for (auto [fd, socketMetaData] : this->socket_fd_map) {
            if (socketMetaData.is_server) {
                if (!wamr->op_data.is_tcp) { // udp
                    res =
                        wamr->invoke_sock_open(socketMetaData.domain, socketMetaData.type, socketMetaData.protocol, fd);
                } else if (socketMetaData.socketAddress.is_4 || is_4) {
                    struct sockaddr_in sockaddr4 = sockaddr_from_ip4(socketMetaData.socketAddress);
                    inet_pton(AF_INET, MVVM_SOCK_ADDR6, &sockaddr4.sin_addr);

                    socklen_t sockaddr4_size = sizeof(sockaddr4);

                    if (is_tcp_server) {
                        wamr->new_sock_map_[fd] =
                            wamr->invoke_sock_accept(old_fd, (struct sockaddr *)&sockaddr4, sizeof(sockaddr4));

                        // This ip should be old ip?
                    } else {
                        wamr->invoke_sock_connect(fd, (struct sockaddr *)&sockaddr4, sizeof(sockaddr4));
                    }
                } else {
                    struct sockaddr_in6 sockaddr6 = sockaddr_from_ip6(socketMetaData.socketAddress);
                    inet_pton(AF_INET, MVVM_SOCK_ADDR6, &sockaddr6.sin6_addr);

                    socklen_t sockaddr6_size = sizeof(sockaddr6);
                    if (is_tcp_server) {
                        wamr->new_sock_map_[fd] =
                            wamr->invoke_sock_accept(old_fd, (struct sockaddr *)&sockaddr6, sizeof(sockaddr6));
                    } else {
                        wamr->invoke_sock_connect(fd, (struct sockaddr *)&sockaddr6, sizeof(sockaddr6));
                    }
                }

                // renumber or not?
                // LOGV(INFO) << "tmp_sock_fd " << res << " fd" << fd;
                wamr->socket_fd_map_[fd] = socketMetaData;
            }
        }
        wamr->sync_ops.assign(this->sync_ops.begin(), this->sync_ops.end());
    }
#endif
};
