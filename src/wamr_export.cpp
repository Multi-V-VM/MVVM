//
// Created by victoryang00 on 10/19/23.
//

#include "wamr.h"
#include "wamr_wasi_context.h"
extern WAMRInstance *wamr;

void insert_sock_open_data(uint32_t poolfd, int af, int socktype, uint32_t sockfd) {
    SocketMetaData newSocketData{};
    newSocketData = wamr->socket_fd_map_[sockfd];

    WasiSockOpenData openData{};
    openData.poolfd = poolfd;
    openData.af = af;
    openData.socktype = socktype;
    openData.sockfd = sockfd;

    newSocketData.socketOpenData = openData;
    wamr->socket_fd_map_[sockfd] = newSocketData;
}
#if !defined(__WINCRYPT_H__)
void insert_sock_send_to_data(uint32_t sock, const iovec_app_t *si_data, uint32 si_data_len, uint16_t si_flags,
                              const __wasi_addr_t *dest_addr, uint32 *so_data_len) {
    SocketMetaData newSocketData{};
    newSocketData = wamr->socket_fd_map_[sock];

    WasiSockSendToData sendToData{};
    sendToData.sock = sock;
    sendToData.si_data_len = si_data_len;
    sendToData.si_flags = si_flags;

    sendToData.si_data.buf_offset = si_data->buf_offset;
    sendToData.si_data.buf_len = si_data->buf_len;

    if (dest_addr->kind == IPv4) {
        sendToData.dest_addr.ip.is_4 = true;
        sendToData.dest_addr.ip.ip4[0] = dest_addr->addr.ip4.addr.n0;
        sendToData.dest_addr.ip.ip4[1] = dest_addr->addr.ip4.addr.n1;
        sendToData.dest_addr.ip.ip4[2] = dest_addr->addr.ip4.addr.n2;
        sendToData.dest_addr.ip.ip4[3] = dest_addr->addr.ip4.addr.n3;

        sendToData.dest_addr.ip.is_4 = true;
        sendToData.dest_addr.port = dest_addr->addr.ip4.port;
    } else {
        sendToData.dest_addr.ip.is_4 = false;
        sendToData.dest_addr.ip.ip6[0] = dest_addr->addr.ip6.addr.n0;
        sendToData.dest_addr.ip.ip6[1] = dest_addr->addr.ip6.addr.n1;
        sendToData.dest_addr.ip.ip6[2] = dest_addr->addr.ip6.addr.n2;
        sendToData.dest_addr.ip.ip6[3] = dest_addr->addr.ip6.addr.n3;
        sendToData.dest_addr.ip.ip6[4] = dest_addr->addr.ip6.addr.h0;
        sendToData.dest_addr.ip.ip6[5] = dest_addr->addr.ip6.addr.h1;
        sendToData.dest_addr.ip.ip6[6] = dest_addr->addr.ip6.addr.h2;
        sendToData.dest_addr.ip.ip6[7] = dest_addr->addr.ip6.addr.h3;

        sendToData.dest_addr.ip.is_4 = false;
        sendToData.dest_addr.port = dest_addr->addr.ip6.port;
    }

    newSocketData.socketSentToData = sendToData;

    wamr->socket_fd_map_[sock] = newSocketData;
}

void insert_sock_recv_from_data(uint32_t sock, iovec_app_t *ri_data, uint32 ri_data_len, uint16_t ri_flags,
                                __wasi_addr_t *src_addr, uint32 *ro_data_len) {
    SocketMetaData newSocketData{};
    newSocketData = wamr->socket_fd_map_[sock];

    WasiSockRecvFromData recvFromData{};
    recvFromData.sock = sock;
    recvFromData.ri_data_len = ri_data_len;
    recvFromData.ri_flags = ri_flags;

    recvFromData.ri_data.buf_offset = ri_data->buf_offset;
    recvFromData.ri_data.buf_len = ri_data->buf_len;

    if (src_addr->kind == IPv4) {
        recvFromData.src_addr.ip.is_4 = true;
        recvFromData.src_addr.ip.ip4[0] = src_addr->addr.ip4.addr.n0;
        recvFromData.src_addr.ip.ip4[1] = src_addr->addr.ip4.addr.n1;
        recvFromData.src_addr.ip.ip4[2] = src_addr->addr.ip4.addr.n2;
        recvFromData.src_addr.ip.ip4[3] = src_addr->addr.ip4.addr.n3;

        recvFromData.src_addr.ip.is_4 = true;
        recvFromData.src_addr.port = src_addr->addr.ip4.port;
    } else {
        recvFromData.src_addr.ip.is_4 = false;
        recvFromData.src_addr.ip.ip6[0] = src_addr->addr.ip6.addr.n0;
        recvFromData.src_addr.ip.ip6[1] = src_addr->addr.ip6.addr.n1;
        recvFromData.src_addr.ip.ip6[2] = src_addr->addr.ip6.addr.n2;
        recvFromData.src_addr.ip.ip6[3] = src_addr->addr.ip6.addr.n3;
        recvFromData.src_addr.ip.ip6[4] = src_addr->addr.ip6.addr.h0;
        recvFromData.src_addr.ip.ip6[5] = src_addr->addr.ip6.addr.h1;
        recvFromData.src_addr.ip.ip6[6] = src_addr->addr.ip6.addr.h2;
        recvFromData.src_addr.ip.ip6[7] = src_addr->addr.ip6.addr.h3;

        recvFromData.src_addr.ip.is_4 = false;
        recvFromData.src_addr.port = src_addr->addr.ip6.port;
    }

    newSocketData.socketRecvFromData = recvFromData;
    wamr->socket_fd_map_[sock] = newSocketData;
}
#endif

/** fopen, fseek, fwrite, fread */
void insert_fd(int fd, const char *path, int flags, int offset, enum fd_op op) {
    if (fd > 2) {
        LOGV(INFO) << fmt::format("insert_fd(fd,filename,flags, offset) fd:{} path:{} flags:{} offset:{} op:{}", fd,
                                  path, flags, offset, op);
        std::string path_;
        std::vector<std::tuple<int, int, enum fd_op>> ops_;
        std::tie(path_, ops_) = wamr->fd_map_[fd];
        ops_.emplace_back(flags, offset, op);
        if (strcmp(path, "") != 0)
            wamr->fd_map_[fd] = std::make_tuple(std::string(path), ops_);
        else
            wamr->fd_map_[fd] = std::make_tuple(path_, ops_);
    }
}
/** frename */
void rename_fd(int old_fd, char const *old_path, int new_fd, char const *new_path) {
    LOGV(INFO) << fmt::format(
        "rename_fd(int old_fd, char const *old_path, int new_fd, char const *new_path) old:{} old_fd:{} new_fd:{}, new_path:{}", old_fd,
        old_path, new_fd, new_path);
    if (wamr->fd_map_.find(old_fd) != wamr->fd_map_.end()) {
        auto new_fd_ = wamr->fd_map_[old_fd];
        std::string path_;
        std::vector<std::tuple<int, int, enum fd_op>> ops_;
        std::tie(path_, ops_) = new_fd_;
        if (strcmp(old_path, "") == 0)
            wamr->fd_map_[new_fd] = std::make_tuple(std::string(new_path), ops_);
        else
            wamr->fd_map_[new_fd] = std::make_tuple(path_, ops_);
        wamr->fd_map_.erase(old_fd);
    }
};
/** fclose */
void remove_fd(int fd) {
    LOGV(INFO) << fmt::format("remove_fd(fd) fd{}", fd);
    if (wamr->fd_map_.find(fd) != wamr->fd_map_.end())
        wamr->fd_map_.erase(fd);
    else {
        if (wamr->socket_fd_map_.find(fd) != wamr->socket_fd_map_.end())
            wamr->socket_fd_map_.erase(fd);
        else
            LOGV(ERROR) << "fd not found " << fd;
    }
}
/*
    create fd-socketmetadata map and store the "domain", "type", "protocol" value
*/
void insert_socket(int fd, int domain, int type, int protocol) {
    // LOGV(INFO) << fmt::format("insert_socket(fd, domain, type, protocol) {} {} {} {}", fd, domain, type, protocol);

    if (wamr->socket_fd_map_.find(fd) != wamr->socket_fd_map_.end()) {
        LOGV(ERROR) << "socket_fd already exist" << fd;
    } else {
        SocketMetaData metaData{};
        metaData.domain = domain;
        metaData.type = type;
        metaData.protocol = protocol;
        wamr->socket_fd_map_.insert(std::make_pair(fd, metaData));
    }
}

void update_socket_fd_address(int fd, SocketAddrPool *address) {
    printf("\n #update_socket_fd_address(fd, address) %d \n\n", fd);

    if (wamr->socket_fd_map_.find(fd) == wamr->socket_fd_map_.end()) {
        // note: ? fd here is not same as insert_socket?
        // set default value
        insert_socket(fd, 0, 0, 0);
    }

    SocketMetaData metaData{};
    metaData.domain = wamr->socket_fd_map_[fd].domain;
    metaData.type = wamr->socket_fd_map_[fd].type;
    metaData.protocol = wamr->socket_fd_map_[fd].protocol;

    metaData.socketAddress.port = address->port;
    if (address->is_4) {
        metaData.socketAddress.is_4 = true;
        metaData.socketAddress.ip4[0] = address->ip4[0];
        metaData.socketAddress.ip4[1] = address->ip4[1];
        metaData.socketAddress.ip4[2] = address->ip4[2];
        metaData.socketAddress.ip4[3] = address->ip4[3];

    } else {
        metaData.socketAddress.ip6[0] = address->ip6[0];
        metaData.socketAddress.ip6[1] = address->ip6[1];
        metaData.socketAddress.ip6[2] = address->ip6[2];
        metaData.socketAddress.ip6[3] = address->ip6[3];
        metaData.socketAddress.ip6[4] = address->ip6[4];
        metaData.socketAddress.ip6[5] = address->ip6[5];
        metaData.socketAddress.ip6[6] = address->ip6[6];
        metaData.socketAddress.ip6[7] = address->ip6[7];
    }
    wamr->socket_fd_map_[fd] = metaData;
}

#ifndef MVVM_DEBUG
void print_stack(AOTFrame *frame) {
    if (frame) {
        fprintf(stderr, "stack: ");
        for (int *i = (int *)frame->lp; i < (int *)frame->sp; i++) {
            fprintf(stderr, "%d ", *i);
        }
        fprintf(stderr, "\n");
    } else {
        LOGV(ERROR) << fmt::format("no cur_frame");
    }
}

void print_exec_env_debug_info(WASMExecEnv *exec_env) {
    LOGV(DEBUG) << fmt::format("----");
    if (!exec_env) {
        LOGV(ERROR) << fmt::format("no exec_env");
        return;
    }
    if (exec_env->cur_frame) {
        int call_depth = 0;
        auto p = (AOTFrame *)exec_env->cur_frame;
        while (p) {
            uint32 *frame_lp = p->lp;
            // LOGV(ERROR) << (size_t)((size_t)frame_lp - (size_t)p);
            LOGV(DEBUG) << fmt::format("depth {}, function {}, ip {}, lp {}, sp {}", call_depth, p->func_index,
                                       p->ip_offset, (void *)frame_lp, (void *)p->sp);
            call_depth++;
            print_stack(p);

            p = p->prev_frame;
        }
    } else {
        LOGV(ERROR) << fmt::format("no cur_frame");
    }
    LOGV(DEBUG) << fmt::format("----");
}

const size_t snapshot_threshold = 50;
size_t call_count = 0;
bool checkpoint = false;
void sigtrap_handler(int sig) {
    // fprintf(stderr, "Caught signal %d, performing custom logic...\n", sig);

    auto exec_env = wamr->get_exec_env();
    // print_exec_env_debug_info(exec_env);
    // print_memory(exec_env);

    call_count++;

    if (call_count == snapshot_threshold || checkpoint) {
        fprintf(stderr, "serializing\n");
        serialize_to_file(exec_env);
        fprintf(stderr, "serialized\n");
        exit(-1);
    }
}

void register_sigtrap() {
    struct sigaction sa {};

    // Clear the structure
    sigemptyset(&sa.sa_mask);

    // Set the signal handler function
    sa.sa_handler = sigtrap_handler;

    // Set the flags
    sa.sa_flags = SA_RESTART;

    // Register the signal handler for SIGTRAP
    if (sigaction(SIGTRAP, &sa, nullptr) == -1) {
        perror("Error: cannot handle SIGTRAP");
        exit(-1);
    } else {
        LOGV_DEBUG << "SIGTRAP registered";
    }
}

// Signal handler function for SIGINT
void sigint_handler(int sig) {
    fprintf(stderr, "Caught signal %d, performing custom logic...\n", sig);
    checkpoint = true;
    wamr->replace_nop_with_int3();
}
#endif