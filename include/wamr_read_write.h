/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2024 Regents of the Univeristy of California
 *  UC Santa Cruz Sluglab.
 */

#ifndef MVVM_WAMR_READ_WRITE_H
#define MVVM_WAMR_READ_WRITE_H
#include "struct_pack/struct_pack.hpp"
#include <spdlog/spdlog.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
struct WriteStream {
     virtual bool write(const char *data, std::size_t sz) const {};
};
struct ReadStream {
     virtual bool read(char *data, std::size_t sz) const {};
     virtual bool ignore(std::size_t sz) const {};
     virtual std::size_t tellg() const {};
};
struct FwriteStream : public WriteStream {
    FILE *file;
    bool write(const char *data, std::size_t sz) const { return fwrite(data, sz, 1, file) == 1; }
    explicit FwriteStream(const char *file_name) : file(fopen(file_name, "wb")) {}
    ~FwriteStream() { fclose(file); }
};

struct FreadStream : public ReadStream {
    FILE *file;
    bool read(char *data, std::size_t sz) const { return fread(data, sz, 1, file) == 1; }
    bool ignore(std::size_t sz) const { return fseek(file, sz, SEEK_CUR) == 0; }
    std::size_t tellg() const {
        // if you worry about ftell performance, just use an variable to record it.
        return ftell(file);
    }
    explicit FreadStream(const char *file_name) : file(fopen(file_name, "rb")) {}
    ~FreadStream() { fclose(file); }
};
static_assert(ReaderStreamTrait<FreadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<FwriteStream, char>, "Writer must conform to WriterStreamTrait");
#ifndef _WIN32
struct SocketWriteStream : public WriteStream {
    int sock_fd; // Socket file descriptor

    bool write(const char *data, std::size_t sz) const {
        std::size_t totalSent = 0;
        while (totalSent < sz) {
            ssize_t sent = send(sock_fd, data + totalSent, sz - totalSent, 0);
            if (sent == -1) {
                // Handle error. For simplicity, just returning false here.
                return false;
            }
            totalSent += sent;
        }
        return true;
    }
    explicit SocketWriteStream(const char *address, int port) {
        // Example: Initialize a client socket and connect to the given address and port
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            SPDLOG_ERROR("Socket creation failed\n");
            return;
        }

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        // Convert address from text to binary form
        inet_pton(AF_INET, address, &server_addr.sin_addr);

        if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            SPDLOG_ERROR("Connection failed\n");
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
    }

    ~SocketWriteStream() {
        close(sock_fd); // Close the socket descriptor
    }
};
struct SocketReadStream : public ReadStream {
    int sock_fd; // Socket file descriptor
    int client_fd;
    mutable std::size_t position = 0; // Track the amount of data read

    bool read(char *data, std::size_t sz) const {
        std::size_t totalReceived = 0;
        while (totalReceived < sz) {
            ssize_t received = recv(client_fd, data + totalReceived, sz - totalReceived, 0);
            if (received == -1) {
                // Handle error. For simplicity, just returning false here.
                return false;
            } else if (received == 0) {
                // Connection closed
                return false;
            }
            totalReceived += received;
        }
        position += totalReceived;
        return true;
    }

    explicit SocketReadStream(const char *address, int port) {
        // Example: Initialize a client socket and connect to the given address and port
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            SPDLOG_ERROR("Socket creation failed\n");
            return;
        }

        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        // Convert address from text to binary form
        inet_pton(AF_INET, address, &server_addr.sin_addr);
        auto addr_len = sizeof(server_addr);
        SPDLOG_INFO("[Server] Bind socket {} {}\n", address, port);
        if (bind(sock_fd, (struct sockaddr *)&server_addr, addr_len) < 0) {
            SPDLOG_ERROR("Bind failed");
            exit(EXIT_FAILURE);
        }

        SPDLOG_INFO("[Server] Listening on socket\n");
        if (listen(sock_fd, 3) < 0) {
            SPDLOG_ERROR("Listen failed");
            exit(EXIT_FAILURE);
        }
        client_fd = accept(sock_fd, (struct sockaddr *)&server_addr, (socklen_t *)&addr_len);
    }
    // "Ignore" sz bytes of data
    bool ignore(std::size_t sz) const {
        char buffer[1024]; // Temporary buffer to discard data
        std::size_t total_ignored = 0;

        while (total_ignored < sz) {
            std::size_t to_ignore = std::min(sz - total_ignored, sizeof(buffer));
            ssize_t ignored = recv(client_fd, buffer, to_ignore, 0);
            if (ignored <= 0) { // Check for error or close
                return false;
            }
            total_ignored += ignored;
            position += ignored; // Update position
        }

        return true;
    }

    // Report the current position
    std::size_t tellg() const { return position; }
    ~SocketReadStream() {
        close(sock_fd); // Close the socket descriptor
    }
};
static_assert(ReaderStreamTrait<SocketReadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<SocketWriteStream, char>, "Writer must conform to WriterStreamTrait");
#endif

#ifdef __LINUX__
#include <infiniband/verbs.h>

class RDMAReadStream {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_mr *mr;

    mutable std::size_t position = 0;
    void *buffer;
    std::size_t buffer_size;

public:
    explicit RDMAReadStream(const char *device_name, std::size_t buffer_size) : buffer_size(buffer_size) {
        // Initialize RDMA device
        struct ibv_device **dev_list = ibv_get_device_list(NULL);
        context = ibv_open_device(*dev_list);

        // Allocate Protection Domain
        pd = ibv_alloc_pd(context);

        // Create Completion Queue
        cq = ibv_create_cq(context, 1, NULL, NULL, 0);

        // Initialize QP
        struct ibv_qp_init_attr qp_init_attr;
        memset(&qp_init_attr, 0, sizeof(qp_init_attr));
        qp_init_attr.send_cq = cq;
        qp_init_attr.recv_cq = cq;
        qp_init_attr.qp_type = IBV_QPT_RC; // Reliable Connection
        qp_init_attr.cap.max_send_wr = 1; // Max Work Requests
        qp_init_attr.cap.max_recv_wr = 1;
        qp_init_attr.cap.max_send_sge = 1; // Max Scatter/Gather Elements
        qp_init_attr.cap.max_recv_sge = 1;
        qp = ibv_create_qp(pd, &qp_init_attr);

        // Allocate and register memory region
        buffer = std::malloc(buffer_size);
        mr = ibv_reg_mr(pd, buffer, buffer_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);

        // Connection setup, exchange QP info with the peer, etc., are omitted for simplicity
    }

    bool read(char *data, std::size_t sz) const {
        if (sz > buffer_size) {
            std::cerr << "Requested read size exceeds buffer size" << std::endl;
            return false;
        }

        struct ibv_sge sge;
        memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)buffer;
        sge.length = sz;
        sge.lkey = mr->lkey;

        struct ibv_send_wr wr;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = 0; // Use this for identifying the WR in the completion queue
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_READ;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = remote_address; // Needs to be set beforehand
        wr.wr.rdma.rkey = remote_key; // Needs to be exchanged with the peer beforehand

        struct ibv_send_wr *bad_wr;
        if (ibv_post_send(qp, &wr, &bad_wr)) {
            std::cerr << "Failed to post RDMA read WR" << std::endl;
            return false;
        }

        // Poll for completion
        struct ibv_wc wc;
        do {
            int nc = ibv_poll_cq(cq, 1, &wc);
            if (nc < 0) {
                std::cerr << "Poll CQ failed" << std::endl;
                return false;
            }
        } while (wc.status != IBV_WC_SUCCESS || wc.wr_id != wr.wr_id);

        // Copy data to user buffer
        memcpy(data, buffer, sz);
        position += sz; // Update position
        return true;
    }
    bool ignore(std::size_t sz) const {
        char buffer[1024]; // Temporary buffer to discard data
        std::size_t total_ignored = 0;

        while (total_ignored < sz) {
            std::size_t to_ignore = std::min(sz - total_ignored, sizeof(buffer));
            ssize_t ignored = read(buffer, to_ignore);
            if (ignored <= 0) { // Check for error or close
                return false;
            }
            total_ignored += ignored;
            position += ignored; // Update position
        }

        return true;
    }
    std::size_t tellg() const {
        return position;
    }

    ~RDMAReadStream() {
        ibv_dereg_mr(mr);
        std::free(buffer);
        ibv_destroy_qp(qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
        // Note: Proper error checking and handling are omitted for brevity
    }
};
class RDMAWriteStream {
    struct ibv_context *context;
    struct ibv_pd *pd;
    struct ibv_cq *cq;
    struct ibv_qp *qp;
    struct ibv_mr *mr;

    void *buffer;
    std::size_t buffer_size;
    uint64_t remote_address;
    uint32_t remote_key;

public:
    explicit RDMAWriteStream(const char *device_name, std::size_t buffer_size) : buffer_size(buffer_size) {
        // Initialization (device, PD, CQ, QP, buffer, and MR) is similar to RDMAReadStream

        // Assume remote_address and remote_key are set up through some initialization method
    }

    virtual bool write(const char *data, std::size_t sz) const {
        if (sz > buffer_size) {
            std::cerr << "Write size exceeds buffer size" << std::endl;
            return false;
        }

        // Copy the data to the local buffer
        memcpy(buffer, data, sz);

        // Prepare scatter/gather entry
        struct ibv_sge sge;
        memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)buffer;
        sge.length = sz;
        sge.lkey = mr->lkey;

        // Prepare the work request
        struct ibv_send_wr wr;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = 0; // Unique identifier
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_RDMA_WRITE;
        wr.send_flags = IBV_SEND_SIGNALED;
        wr.wr.rdma.remote_addr = remote_address;
        wr.wr.rdma.rkey = remote_key;

        struct ibv_send_wr *bad_wr;

        // Post the RDMA write work request
        if (ibv_post_send(qp, &wr, &bad_wr)) {
            std::cerr << "Failed to post RDMA write WR" << std::endl;
            return false;
        }

        // Poll for completion
        struct ibv_wc wc;
        do {
            int nc = ibv_poll_cq(cq, 1, &wc);
            if (nc < 0) {
                std::cerr << "Poll CQ failed" << std::endl;
                return false;
            }
        } while (wc.status != IBV_WC_SUCCESS || wc.wr_id != wr.wr_id);

        return true; // Successfully written
    }

    ~RDMAWriteStream() {
        // Deallocate resources
        ibv_dereg_mr(mr);
        std::free(buffer);
        ibv_destroy_qp(qp);
        ibv_destroy_cq(cq);
        ibv_dealloc_pd(pd);
        ibv_close_device(context);
    }
};
static_assert(ReaderStreamTrait<RDMAReadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<RDMAWriteStream, char>, "Writer must conform to WriterStreamTrait");
#endif

#endif /* MVVM_WAMR_READ_WRITE_H */
