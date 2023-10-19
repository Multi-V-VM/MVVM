//
// Created by victoryang00 on 10/19/23.
//

#include "wamr_block_addr.h"
#include "wamr.h"
extern WAMRInstance *wamr;


void insert_sock_open_data(uint32_t poolfd, int af, int socktype, uint32_t* sockfd) {
    if (wamr->sock_open_data.poolfd == 0) {
        wamr->sock_open_data.poolfd = poolfd;
        wamr->sock_open_data.af = af;
        wamr->sock_open_data.socktype = socktype;
        wamr->sock_open_data.sockfd = sockfd;
    } else {
        LOGV(ERROR) << "sock_open_data already exist";
    }
}

void insert_sock_send_to_data(uint32_t sock, const iovec_app1_t* si_data, uint32 si_data_len, uint16_t si_flags, const __wasi_addr_t* dest_addr, uint32* so_data_len) {
    if(wamr->sock_sendto_data.sock == 0) {
        wamr->sock_sendto_data.sock = sock;
        wamr->sock_sendto_data.si_data = si_data;
        wamr->sock_sendto_data.si_data_len = si_data_len;
        wamr->sock_sendto_data.si_flags = si_flags;
        wamr->sock_sendto_data.dest_addr = dest_addr;
        wamr->sock_sendto_data.so_data_len = so_data_len;
    } else {
        LOGV(ERROR) << "sock_sendto_data already exist";
    }
}

void insert_sock_recv_from_data(uint32_t sock, iovec_app1_t* ri_data, uint32 ri_data_len, uint16_t ri_flags, __wasi_addr_t* src_addr, uint32* ro_data_len) {
    if(wamr->sock_recvfrom_data.sock == 0) {
        wamr->sock_recvfrom_data.sock = sock;
        wamr->sock_recvfrom_data.ri_data = ri_data;
        wamr->sock_recvfrom_data.ri_data_len = ri_data_len;
        wamr->sock_recvfrom_data.ri_flags = ri_flags;
        wamr->sock_recvfrom_data.src_addr = src_addr;
        wamr->sock_recvfrom_data.ro_data_len = ro_data_len;
    } else {
        LOGV(ERROR) << "sock_recvfrom_data already exist";
    }
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
void insert_socket(int fd) {}
