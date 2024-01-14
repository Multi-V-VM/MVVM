//
// Created by victoryang00 on 5/2/23.
//
#include "wamr_wasi_context.h"
#include "logging.h"
#include "wamr.h"
#include <string>
#include <sys/socket.h>
extern WAMRInstance *wamr;

struct sockaddr_in sockaddr_from_ip4(const SocketAddrPool &addr) {
    struct sockaddr_in sockaddr4;
    memset(&sockaddr4, 0, sizeof(sockaddr4));
    sockaddr4.sin_family = AF_INET;
    sockaddr4.sin_port = htons(addr.port);
    memcpy(&sockaddr4.sin_addr.s_addr, addr.ip4, sizeof(addr.ip4));
    return sockaddr4;
}

struct sockaddr_in6 sockaddr_from_ip6(const SocketAddrPool &addr) {
    struct sockaddr_in6 sockaddr6;
    memset(&sockaddr6, 0, sizeof(sockaddr6));
    sockaddr6.sin6_family = AF_INET6;
    sockaddr6.sin6_port = htons(addr.port);
    memcpy(&sockaddr6.sin6_addr.s6_addr, addr.ip6, sizeof(addr.ip6));
    return sockaddr6;
}

void WAMRWASIContext::dump_impl(WASIArguments *env) {
    uint8 buf[1024];
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
        // dump from
        bool packet_is_fin = false;
        if (wamr->op_data.is_tcp) {
            while (!packet_is_fin) { // drain udp socket
                // fd got from the virtual machine
                recv(fd, buf, sizeof(buf), 0);
                auto fin_packet = (mvvm_op_data *)(buf);
                // emunate the recvfrom syscall

                // get the buffer size
                if (fin_packet->op != MVVM_SOCK_FIN) { 
                    insert_sock_recv_from_data(fd, buf, sizeof(buf), 0, nullptr);
                } else {
                    packet_is_fin = true;
                }
            }
        } else {
            // while (!packet_is_fin) { // drain tcp socket whether it's tcp or udp
            //     // fd got from the virtual machine
            //     if (socketMetaData.socketAddress.is_4) {
            //         struct sockaddr_in sockaddr4 = sockaddr_from_ip4(socketMetaData.socketAddress);
            //         socklen_t sockaddr4_size = sizeof(sockaddr4);
            //         recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&sockaddr4, &sockaddr4_size);

            //     } else {
            //         struct sockaddr_in6 sockaddr6 = sockaddr_from_ip6(socketMetaData.socketAddress);
            //         socklen_t sockaddr6_size = sizeof(sockaddr6);
            //         recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&sockaddr6, &sockaddr6_size);
            //     }
            //     auto fin_packet = (mvvm_op_data *)(buf);
            //     if (fin_packet->op != MVVM_SOCK_FIN) {
            //         insert_sock_recv_from_data(fd, ri_data, ri_data_len, 0, nullptr);
            //     } else {
            //         packet_is_fin = true;
            //     }
            // }
        }
    }
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