//
// Created by victoryang00 on 10/19/23.
//

#include "wamr.h"
#include "wamr_block_addr.h"
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

    if(dest_addr->kind == IPv4){
        sendToData.dest_addr.ip.is_4 = true;
        sendToData.dest_addr.ip.ip4[0] = dest_addr->addr.ip4.addr.n0;
        sendToData.dest_addr.ip.ip4[1] = dest_addr->addr.ip4.addr.n1;
        sendToData.dest_addr.ip.ip4[2] = dest_addr->addr.ip4.addr.n2;
        sendToData.dest_addr.ip.ip4[3] = dest_addr->addr.ip4.addr.n3;

        sendToData.dest_addr.ip.is_4 = true;
        sendToData.dest_addr.port = dest_addr->addr.ip4.port;
    }else{
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

    if(src_addr->kind == IPv4){
        recvFromData.src_addr.ip.is_4 = true;
        recvFromData.src_addr.ip.ip4[0] = src_addr->addr.ip4.addr.n0;
        recvFromData.src_addr.ip.ip4[1] = src_addr->addr.ip4.addr.n1;
        recvFromData.src_addr.ip.ip4[2] = src_addr->addr.ip4.addr.n2;
        recvFromData.src_addr.ip.ip4[3] = src_addr->addr.ip4.addr.n3;

        recvFromData.src_addr.ip.is_4 = true;
        recvFromData.src_addr.port = src_addr->addr.ip4.port;
    }else{
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
    wamr->socket_fd_map_[sock] =  newSocketData;
}

/**fopen, fseek*/
void insert_fd(int fd, const char *path, int flags, int offset) {
    if (fd > 2) {
        printf("\n #insert_fd(fd,filename,flags, offset) %d %s %d %d \n\n", fd, path, flags, offset);

        if (wamr->fd_map_.find(fd) != wamr->fd_map_.end()) {
            LOGV(ERROR) << "fd already exist" << fd;
            if (offset == 0) {
                // fOpen call
                std::string curPath;
                int curFlags;
                int curOffset;
                std::tie(curPath, curFlags, curOffset) = wamr->fd_map_[fd];
                wamr->fd_map_[fd] = std::make_tuple(std::string(path), flags, curOffset);
            } else {
                // fSeek call
                std::string curPath;
                int curFlags;
                int curOffset;
                std::tie(curPath, curFlags, curOffset) = wamr->fd_map_[fd];
                wamr->fd_map_[fd] = std::make_tuple(curPath, curFlags, offset);
            }

        } else
            wamr->fd_map_.insert(std::make_pair(fd, std::make_tuple(std::string(path), flags, offset)));
    }
}

/* update fd->offset**/
void insert_fd_fseek();
/**fclose */
void remove_fd(int fd) {
    if (wamr->fd_map_.find(fd) != wamr->fd_map_.end())
        wamr->fd_map_.erase(fd);
    else
        LOGV(ERROR) << "fd not found" << fd;
}

/*
    create fd-socketmetadata map and store the "domain", "type", "protocol" value
*/
void insert_socket(int fd, int domain, int type, int protocol) {
    printf("\n #insert_socket(fd, domain, type, protocol) %d %d %d %d \n\n", fd, domain, type, protocol);

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
