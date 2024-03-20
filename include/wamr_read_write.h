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
#include <cstdint>
#include <spdlog/spdlog.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
struct WriteStream {
    virtual bool write(const char *data, std::size_t sz) const { return false; };
    virtual ~WriteStream() = default;
};
struct ReadStream {
    virtual bool read(char *data, std::size_t sz) const { return false; };
    virtual bool ignore(std::size_t sz) const { return false; };
    virtual std::size_t tellg() const { return 0; };
    virtual ~ReadStream() = default;
};
struct FwriteStream : public WriteStream {
    FILE *file;
    bool write(const char *data, std::size_t sz) const override { return fwrite(data, sz, 1, file) == 1; }
    explicit FwriteStream(const char *file_name) : file(fopen(file_name, "wb")) {}
    ~FwriteStream() override { fclose(file); }
};
struct FreadStream : public ReadStream {
    FILE *file;
    bool read(char *data, std::size_t sz) const override { return fread(data, sz, 1, file) == 1; }
    bool ignore(std::size_t sz) const override { return fseek(file, sz, SEEK_CUR) == 0; }
    std::size_t tellg() const override { return ftell(file); }
    explicit FreadStream(const char *file_name) : file(fopen(file_name, "rb")) {}
    ~FreadStream() override { fclose(file); }
};
static_assert(ReaderStreamTrait<FreadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<FwriteStream, char>, "Writer must conform to WriterStreamTrait");
#ifndef _WIN32
struct SocketWriteStream : public WriteStream {
    int sock_fd;
    bool write(const char *data, std::size_t sz) const override {
        std::size_t totalSent = 0;
        while (totalSent < sz) {
            ssize_t sent = send(sock_fd, data + totalSent, sz - totalSent, 0);
            if (sent == -1) {
                return false;
            }
            totalSent += sent;
        }
        return true;
    }
    explicit SocketWriteStream(const char *address, int port) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            SPDLOG_ERROR("Socket creation failed\n");
            return;
        }
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, address, &server_addr.sin_addr);
        if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            SPDLOG_ERROR("Connection failed\n");
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
    }
    ~SocketWriteStream() override { close(sock_fd); }
};
struct SocketReadStream : public ReadStream {
    int sock_fd;
    int client_fd;
    mutable std::size_t position = 0;
    bool read(char *data, std::size_t sz) const override {
        std::size_t totalReceived = 0;
        while (totalReceived < sz) {
            ssize_t received = recv(client_fd, data + totalReceived, sz - totalReceived, 0);
            if (received == -1) {
                return false;
            } else if (received == 0) {
                return false;
            }
            totalReceived += received;
        }
        position += totalReceived;
        return true;
    }
    explicit SocketReadStream(const char *address, int port) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            SPDLOG_ERROR("Socket creation failed\n");
            return;
        }
        sockaddr_in server_addr;
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
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
    bool ignore(std::size_t sz) const override {
        char buffer[1024];
        std::size_t total_ignored = 0;
        while (total_ignored < sz) {
            std::size_t to_ignore = std::min(sz - total_ignored, sizeof(buffer));
            ssize_t ignored = recv(client_fd, buffer, to_ignore, 0);
            if (ignored <= 0) {
                return false;
            }
            total_ignored += ignored;
            position += ignored;
        }
        return true;
    }
    std::size_t tellg() const override { return position; }
    ~SocketReadStream() override { close(sock_fd); }
};
static_assert(ReaderStreamTrait<SocketReadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<SocketWriteStream, char>, "Writer must conform to WriterStreamTrait");
#endif

#if __linux__
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
struct __attribute((packed)) rdma_buffer_attr {
    uint64_t address;
    uint32_t length;
    union stag {
        uint32_t local_stag;
        uint32_t remote_stag;
    } stag;
};
#define CQ_CAPACITY (16)
#define MAX_SGE (2)
#define MAX_WR (8)
#define DEFAULT_RDMA_PORT (20886)
class RDMAEndpoint {
public:
    struct rdma_event_channel *cm_event_channel = nullptr;
    struct rdma_cm_id *cm_server_id = nullptr, *cm_client_id = nullptr;
    struct ibv_pd *pd = nullptr;
    struct ibv_comp_channel *io_completion_channel = nullptr;
    struct ibv_cq *cq = nullptr;
    struct ibv_qp_init_attr qp_init_attr {};
    struct ibv_qp *client_qp = nullptr;
    struct ibv_mr *client_metadata_mr = nullptr, *server_buffer_mr = nullptr, *server_metadata_mr = nullptr;
    struct rdma_buffer_attr client_metadata_attr{}, server_metadata_attr{};
    struct ibv_recv_wr client_recv_wr {}, *bad_client_recv_wr = nullptr;
    struct ibv_send_wr server_send_wr {}, *bad_server_send_wr = nullptr;
    struct ibv_sge client_recv_sge {}, server_send_sge{};
    RDMAEndpoint() = default;
    static void show_rdma_cmid(struct rdma_cm_id *id) {
        if (!id) {
            printf("Passed ptr is nullptr\n");
            return;
        }
        printf("RDMA cm id at %p \n", id);
        if (id->verbs && id->verbs->device)
            printf("dev_ctx: %p (device name: %s) \n", id->verbs, id->verbs->device->name);
        if (id->channel)
            printf("cm event channel %p\n", id->channel);
        printf("QP: %p, port_space %x, port_num %u \n", id->qp, id->ps, id->port_num);
    }
    static void show_rdma_buffer_attr(struct rdma_buffer_attr *attr) {
        if (!attr) {
            printf("Passed attr is nullptr\n");
            return;
        }
        printf("---------------------------------------------------------\n");
        printf("buffer attr, addr: %p , len: %u , stag : 0x%x \n", (void *)attr->address, (unsigned int)attr->length,
               attr->stag.local_stag);
        printf("---------------------------------------------------------\n");
    }
    static struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd, uint32_t size, enum ibv_access_flags permission) {
        struct ibv_mr *mr = nullptr;
        if (!pd) {
            printf("Protection domain is nullptr \n");
            return nullptr;
        }
        void *buf = calloc(1, size);
        if (!buf) {
            printf("failed to allocate buffer, -ENOMEM\n");
            return nullptr;
        }
        mr = rdma_buffer_register(pd, buf, size, permission);
        if (!mr) {
            free(buf);
        }
        return mr;
    }
    static struct ibv_mr *rdma_buffer_register(struct ibv_pd *pd, void *addr, uint32_t length,
                                               enum ibv_access_flags permission) {
        struct ibv_mr *mr = nullptr;
        if (!pd) {
            printf("Protection domain is nullptr, ignoring \n");
            return nullptr;
        }
        mr = ibv_reg_mr(pd, addr, length, permission);
        if (!mr) {
            printf("Failed to create mr on buffer, errno: %d \n", -errno);
            return nullptr;
        }
        return mr;
    }
    static void rdma_buffer_free(struct ibv_mr *mr) {
        if (!mr) {
            printf("Passed memory region is nullptr, ignoring\n");
            return;
        }
        void *to_free = mr->addr;
        rdma_buffer_deregister(mr);
        free(to_free);
    }
    static void rdma_buffer_deregister(struct ibv_mr *mr) {
        if (!mr) {
            printf("Passed memory region is nullptr, ignoring\n");
            return;
        }
        ibv_dereg_mr(mr);
    }
    static int process_rdma_cm_event(struct rdma_event_channel *echannel, enum rdma_cm_event_type expected_event,
                                     struct rdma_cm_event **cm_event) {
        int ret = 1;
        ret = rdma_get_cm_event(echannel, cm_event);
        if (ret) {
            printf("Failed to retrieve a cm event, errno: %d \n", -errno);
            return -errno;
        }
        if (0 != (*cm_event)->status) {
            printf("CM event has non zero status: %d\n", (*cm_event)->status);
            ret = -((*cm_event)->status);
            rdma_ack_cm_event(*cm_event);
            return ret;
        }
        if ((*cm_event)->event != expected_event) {
            printf("Unexpected event received: %s [ expecting: %s ]", rdma_event_str((*cm_event)->event),
                   rdma_event_str(expected_event));
            rdma_ack_cm_event(*cm_event);
            return -1;
        }
        return ret;
    }
    static int process_work_completion_events(struct ibv_comp_channel *comp_channel, struct ibv_wc *wc, int max_wc) {
        struct ibv_cq *cq_ptr = nullptr;
        void *context = nullptr;
        int ret = -1, i, total_wc = 0;
        ret = ibv_get_cq_event(comp_channel, &cq_ptr, &context);
        if (ret) {
            printf("Failed to get next CQ event due to %d \n", -errno);
            return -errno;
        }
        ret = ibv_req_notify_cq(cq_ptr, 0);
        if (ret) {
            printf("Failed to request further notifications %d \n", -errno);
            return -errno;
        }
        total_wc = 0;
        do {
            ret = ibv_poll_cq(cq_ptr, max_wc - total_wc, wc + total_wc);
            if (ret < 0) {
                printf("Failed to poll cq for wc due to %d \n", ret);
            }
            total_wc += ret;
        } while (total_wc < max_wc);
        for (i = 0; i < total_wc; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                printf("Work completion (WC) has error status: %s at index %d", ibv_wc_status_str(wc[i].status), i);
                return -(wc[i].status);
            }
        }
        ibv_ack_cq_events(cq_ptr, 1);
        return total_wc;
    }
    ~RDMAEndpoint() = default;
};
class RDMAReadStream : public ReadStream, public RDMAEndpoint {
    mutable long position = 0;
    mutable uint8_t *buffer;
    mutable long buffer_size;
    int setup_client_resources() {
        int ret = -1;
        if (!cm_client_id) {
            printf("Client id is still nullptr \n");
            return -EINVAL;
        }
        pd = ibv_alloc_pd(cm_client_id->verbs);
        if (!pd) {
            printf("Failed to allocate a protection domain errno: %d\n", -errno);
            return -errno;
        }
        io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
        if (!io_completion_channel) {
            printf("Failed to create an I/O completion event channel, %d\n", -errno);
            return -errno;
        }
        cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, nullptr, io_completion_channel, 0);
        if (!cq) {
            printf("Failed to create a completion queue (cq), errno: %d\n", -errno);
            return -errno;
        }
        ret = ibv_req_notify_cq(cq, 0);
        if (ret) {
            printf("Failed to request notifications on CQ errno: %d \n", -errno);
            return -errno;
        }
        bzero(&qp_init_attr, sizeof qp_init_attr);
        qp_init_attr.cap.max_recv_sge = MAX_SGE;
        qp_init_attr.cap.max_recv_wr = MAX_WR;
        qp_init_attr.cap.max_send_sge = MAX_SGE;
        qp_init_attr.cap.max_send_wr = MAX_WR;
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.recv_cq = cq;
        qp_init_attr.send_cq = cq;
        ret = rdma_create_qp(cm_client_id, pd, &qp_init_attr);
        if (ret) {
            printf("Failed to create QP due to errno: %d\n", -errno);
            return -errno;
        }
        client_qp = cm_client_id->qp;
        return ret;
    }
    int start_rdma_server(struct sockaddr_in *server_addr) {
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        cm_event_channel = rdma_create_event_channel();
        if (!cm_event_channel) {
            printf("Creating cm event channel failed with errno : (%d)", -errno);
            return -errno;
        }
        ret = rdma_create_id(cm_event_channel, &cm_server_id, nullptr, RDMA_PS_TCP);
        if (ret) {
            printf("Creating server cm id failed with errno: %d ", -errno);
            return -errno;
        }
        ret = rdma_bind_addr(cm_server_id, (struct sockaddr *)server_addr);
        if (ret) {
            printf("Failed to bind server address, errno: %d \n", -errno);
            return -errno;
        }
        ret = rdma_listen(cm_server_id, 8);
        if (ret) {
            printf("rdma_listen failed to listen on server address, errno: %d ", -errno);
            return -errno;
        }
        printf("Server is listening successfully at: %s , port: %d \n", inet_ntoa(server_addr->sin_addr),
               ntohs(server_addr->sin_port));
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_CONNECT_REQUEST, &cm_event);
        if (ret) {
            printf("Failed to get cm event, ret = %d \n", ret);
            return ret;
        }
        cm_client_id = cm_event->id;
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            printf("Failed to acknowledge the cm event errno: %d \n", -errno);
            return -errno;
        }
        return ret;
    }
    int accept_client_connection() {
        struct rdma_conn_param conn_param;
        struct rdma_cm_event *cm_event = nullptr;
        struct sockaddr_in remote_sockaddr;
        int ret = -1;
        if (!cm_client_id || !client_qp) {
            printf("Client resources are not properly setup\n");
            return -EINVAL;
        }
        client_metadata_mr =
            rdma_buffer_register(pd, &client_metadata_attr, sizeof(client_metadata_attr), (IBV_ACCESS_LOCAL_WRITE));
        if (!client_metadata_mr) {
            printf("Failed to register client attr buffer\n");
            return -ENOMEM;
        }
        client_recv_sge.addr = (uint64_t)client_metadata_mr->addr;
        client_recv_sge.length = client_metadata_mr->length;
        client_recv_sge.lkey = client_metadata_mr->lkey;
        bzero(&client_recv_wr, sizeof(client_recv_wr));
        client_recv_wr.sg_list = &client_recv_sge;
        client_recv_wr.num_sge = 1;
        ret = ibv_post_recv(client_qp, &client_recv_wr, &bad_client_recv_wr);
        if (ret) {
            printf("Failed to pre-post the receive buffer, errno: %d \n", ret);
            return ret;
        }
        memset(&conn_param, 0, sizeof(conn_param));
        conn_param.initiator_depth = 3;
        conn_param.responder_resources = 3;
        ret = rdma_accept(cm_client_id, &conn_param);
        if (ret) {
            printf("Failed to accept the connection, errno: %d \n", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
        if (ret) {
            printf("Failed to get the cm event, errnp: %d \n", -errno);
            return -errno;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            printf("Failed to acknowledge the cm event %d\n", -errno);
            return -errno;
        }
        memcpy(&remote_sockaddr, rdma_get_peer_addr(cm_client_id), sizeof(struct sockaddr_in));
        printf("A new connection is accepted from %s \n", inet_ntoa(remote_sockaddr.sin_addr));
        return ret;
    }
    int send_server_metadata_to_client() {
        struct ibv_wc wc;
        int ret = -1;
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            printf("Failed to receive , ret = %d \n", ret);
            return ret;
        }
        show_rdma_buffer_attr(&client_metadata_attr);
        printf("The client has requested buffer length of : %u bytes \n", client_metadata_attr.length);
        server_buffer_mr =
            rdma_buffer_alloc(pd, client_metadata_attr.length,
                              (enum ibv_access_flags)(((int)IBV_ACCESS_LOCAL_WRITE) | ((int)IBV_ACCESS_REMOTE_READ) |
                                                      ((int)IBV_ACCESS_REMOTE_WRITE)));
        if (!server_buffer_mr) {
            printf("Server failed to create a buffer \n");
            return -ENOMEM;
        }
        server_metadata_attr.address = (uint64_t)server_buffer_mr->addr;
        server_metadata_attr.length = (uint32_t)server_buffer_mr->length;
        server_metadata_attr.stag.local_stag = (uint32_t)server_buffer_mr->lkey;
        server_metadata_mr =
            rdma_buffer_register(pd, &server_metadata_attr, sizeof(server_metadata_attr), IBV_ACCESS_LOCAL_WRITE);
        if (!server_metadata_mr) {
            printf("Server failed to create to hold server metadata \n");
            return -ENOMEM;
        }
        server_send_sge.addr = (uint64_t)&server_metadata_attr;
        server_send_sge.length = sizeof(server_metadata_attr);
        server_send_sge.lkey = server_metadata_mr->lkey;
        bzero(&server_send_wr, sizeof(server_send_wr));
        server_send_wr.sg_list = &server_send_sge;
        server_send_wr.num_sge = 1;
        server_send_wr.opcode = IBV_WR_SEND;
        server_send_wr.send_flags = IBV_SEND_SIGNALED;
        ret = ibv_post_send(client_qp, &server_send_wr, &bad_server_send_wr);
        if (ret) {
            printf("Posting of server metdata failed, errno: %d \n", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            printf("Failed to send server metadata, ret = %d \n", ret);
            return ret;
        }
        usleep(1);
        printf("Received buffer contents: ");
        for (int i = 0; i < 10; i++) {
            printf("%x", ((uint8_t *)server_buffer_mr->addr)[i]);
        }
        printf("\n");
        return 0;
    }
    int disconnect_and_cleanup() {
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_DISCONNECTED, &cm_event);
        if (ret) {
            printf("Failed to get disconnect event, ret = %d \n", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            printf("Failed to acknowledge the cm event %d\n", -errno);
            return -errno;
        }
        printf("A disconnect event is received from the client...\n");
        rdma_destroy_qp(cm_client_id);
        ret = rdma_destroy_id(cm_client_id);
        if (ret) {
            printf("Failed to destroy client id cleanly, %d \n", -errno);
        }
        ret = ibv_destroy_cq(cq);
        if (ret) {
            printf("Failed to destroy completion queue cleanly, %d \n", -errno);
        }
        ret = ibv_destroy_comp_channel(io_completion_channel);
        if (ret) {
            printf("Failed to destroy completion channel cleanly, %d \n", -errno);
        }
        rdma_buffer_free(server_buffer_mr);
        rdma_buffer_deregister(server_metadata_mr);
        rdma_buffer_deregister(client_metadata_mr);
        ret = ibv_dealloc_pd(pd);
        if (ret) {
            printf("Failed to destroy client protection domain cleanly, %d \n", -errno);
        }
        ret = rdma_destroy_id(cm_server_id);
        if (ret) {
            printf("Failed to destroy server id cleanly, %d \n", -errno);
        }
        rdma_destroy_event_channel(cm_event_channel);
        printf("Server shut-down is complete \n");
        return 0;
    }
public:
    explicit RDMAReadStream(const char *server_name, int port) : RDMAEndpoint() {
        struct sockaddr_in server_sockaddr;
        int ret = -1;
        bzero(&server_sockaddr, sizeof(server_sockaddr));
        server_sockaddr.sin_family = AF_INET;
        server_sockaddr.sin_port = htons(port);
        inet_pton(AF_INET, server_name, &server_sockaddr.sin_addr);
        ret = start_rdma_server(&server_sockaddr);
        if (ret) {
            printf("RDMA server failed to start cleanly, ret = %d \n", ret);
        }
        ret = setup_client_resources();
        if (ret) {
            printf("Failed to setup client resources, ret = %d \n", ret);
        }
        ret = accept_client_connection();
        if (ret) {
            printf("Failed to handle client cleanly, ret = %d \n", ret);
        }
        ret = send_server_metadata_to_client();
        if (ret) {
            printf("Failed to send server metadata to the client, ret = %d \n", ret);
        }
        buffer = (uint8_t *)server_buffer_mr->addr;
        buffer_size = client_metadata_attr.length;
    }
    bool read(char *data, std::size_t sz) const override {
        if (position + sz > buffer_size) {
            std::throw_with_nested(std::runtime_error("Buffer overflow"));
        }
        memcpy(data, buffer + position, sz);
        position += sz;
        return true;
    }
    bool ignore(std::size_t sz) const override {
        position += sz;
        return true;
    }
    std::size_t tellg() const override { return position; }
    ~RDMAReadStream() override { disconnect_and_cleanup(); };
};
class RDMAWriteStream : public WriteStream, public RDMAEndpoint {
public:
    mutable char *buffer;
    mutable long position = 0;
    struct ibv_cq *client_cq = nullptr;
    struct ibv_sge client_send_sge, server_recv_sge;
    struct sockaddr_in server_sockaddr;
    struct rdma_conn_param conn_param;
    struct rdma_cm_event *cm_event = nullptr;
    struct ibv_mr *client_metadata_mr = nullptr, *client_src_mr = nullptr, *client_dst_mr = nullptr,
                  *server_metadata_mr = nullptr;
    struct ibv_send_wr client_send_wr, *bad_client_send_wr = nullptr;
    struct ibv_recv_wr server_recv_wr, *bad_server_recv_wr = nullptr;
    int client_prepare_connection(struct sockaddr_in *s_addr) {
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        cm_event_channel = rdma_create_event_channel();
        if (!cm_event_channel) {
            printf("Creating cm event channel failed, errno: %d \n", -errno);
            return -errno;
        }
        ret = rdma_create_id(cm_event_channel, &cm_client_id, nullptr, RDMA_PS_TCP);
        if (ret) {
            printf("Creating cm id failed with errno: %d \n", -errno);
            return -errno;
        }
        ret = rdma_resolve_addr(cm_client_id, nullptr, (struct sockaddr *)s_addr, 2000);
        if (ret) {
            printf("Failed to resolve address, errno: %d \n", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event);
        if (ret) {
            printf("Failed to receive a valid event, ret = %d \n", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            printf("Failed to acknowledge the CM event, errno: %d\n", -errno);
            return -errno;
        }
        ret = rdma_resolve_route(cm_client_id, 2000);
        if (ret) {
            printf("Failed to resolve route, erno: %d \n", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event);
        if (ret) {
            printf("Failed to receive a valid event, ret = %d \n", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            printf("Failed to acknowledge the CM event, errno: %d \n", -errno);
            return -errno;
        }
        printf("Trying to connect to server at : %s port: %d \n", inet_ntoa(s_addr->sin_addr), ntohs(s_addr->sin_port));
        pd = ibv_alloc_pd(cm_client_id->verbs);
        if (!pd) {
            printf("Failed to alloc pd, errno: %d \n", -errno);
            return -errno;
        }
        io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
        if (!io_completion_channel) {
            printf("Failed to create IO completion event channel, errno: %d\n", -errno);
            return -errno;
        }
        client_cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, nullptr, io_completion_channel, 0);
        if (!client_cq) {
            printf("Failed to create CQ, errno: %d \n", -errno);
            return -errno;
        }
        ret = ibv_req_notify_cq(client_cq, 0);
        if (ret) {
            printf("Failed to request notifications, errno: %d\n", -errno);
            return -errno;
        }
        bzero(&qp_init_attr, sizeof qp_init_attr);
        qp_init_attr.cap.max_recv_sge = MAX_SGE;
        qp_init_attr.cap.max_recv_wr = MAX_WR;
        qp_init_attr.cap.max_send_sge = MAX_SGE;
        qp_init_attr.cap.max_send_wr = MAX_WR;
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.recv_cq = client_cq;
        qp_init_attr.send_cq = client_cq;
        ret = rdma_create_qp(cm_client_id, pd, &qp_init_attr);
        if (ret) {
            printf("Failed to create QP, errno: %d \n", -errno);
            return -errno;
        }
        client_qp = cm_client_id->qp;
        return 0;
    }
    int client_pre_post_recv_buffer() {
        int ret = -1;
        server_metadata_mr =
            rdma_buffer_register(pd, &server_metadata_attr, sizeof(server_metadata_attr), (IBV_ACCESS_LOCAL_WRITE));
        if (!server_metadata_mr) {
            printf("Failed to setup the server metadata mr , -ENOMEM\n");
            return -ENOMEM;
        }
        server_recv_sge.addr = (uint64_t)server_metadata_mr->addr;
        server_recv_sge.length = (uint32_t)server_metadata_mr->length;
        server_recv_sge.lkey = (uint32_t)server_metadata_mr->lkey;
        bzero(&server_recv_wr, sizeof(server_recv_wr));
        server_recv_wr.sg_list = &server_recv_sge;
        server_recv_wr.num_sge = 1;
        ret = ibv_post_recv(client_qp, &server_recv_wr, &bad_server_recv_wr);
        if (ret) {
            printf("Failed to pre-post the receive buffer, errno: %d \n", ret);
            return ret;
        }
        return 0;
    }
    int client_connect_to_server() {
        struct rdma_conn_param conn_param {};
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        bzero(&conn_param, sizeof(conn_param));
        conn_param.initiator_depth = 3;
        conn_param.responder_resources = 3;
        conn_param.retry_count = 3;
        ret = rdma_connect(cm_client_id, &conn_param);
        if (ret) {
            printf("Failed to connect to remote host , errno: %d\n", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
        if (ret) {
            printf("Failed to get cm event, ret = %d \n", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            printf("Failed to acknowledge cm event, errno: %d\n", -errno);
            return -errno;
        }
        printf("The client is connected successfully \n");
        return 0;
    }
    int client_xchange_metadata_with_server() {
        struct ibv_wc wc[2];
        int ret = -1;
        client_src_mr =
            rdma_buffer_register(pd, buffer, position,
                                 (enum ibv_access_flags)(((int)IBV_ACCESS_LOCAL_WRITE) | ((int)IBV_ACCESS_REMOTE_READ) |
                                                         ((int)IBV_ACCESS_REMOTE_WRITE)));
        if (!client_src_mr) {
            printf("Failed to register the first buffer, ret = %d \n", ret);
            return ret;
        }
        client_metadata_attr.address = (uint64_t)client_src_mr->addr;
        client_metadata_attr.length = client_src_mr->length;
        client_metadata_attr.stag.local_stag = client_src_mr->lkey;
        client_metadata_mr =
            rdma_buffer_register(pd, &client_metadata_attr, sizeof(client_metadata_attr), IBV_ACCESS_LOCAL_WRITE);
        if (!client_metadata_mr) {
            printf("Failed to register the client metadata buffer, ret = %d \n", ret);
            return ret;
        }
        client_send_sge.addr = (uint64_t)client_metadata_mr->addr;
        client_send_sge.length = (uint32_t)client_metadata_mr->length;
        client_send_sge.lkey = client_metadata_mr->lkey;
        bzero(&client_send_wr, sizeof(client_send_wr));
        client_send_wr.sg_list = &client_send_sge;
        client_send_wr.num_sge = 1;
        client_send_wr.opcode = IBV_WR_SEND;
        client_send_wr.send_flags = IBV_SEND_SIGNALED;
        ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
        if (ret) {
            printf("Failed to send client metadata, errno: %d \n", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, wc, 2);
        if (ret != 2) {
            printf("We failed to get 2 work completions , ret = %d \n", ret);
            return ret;
        }
        show_rdma_buffer_attr(&server_metadata_attr);
        return 0;
    }
    int client_remote_memory_ops() {
        struct ibv_wc wc;
        int ret = -1;
        client_send_sge.addr = (uint64_t)client_src_mr->addr;
        client_send_sge.length = (uint32_t)client_src_mr->length;
        client_send_sge.lkey = client_src_mr->lkey;
        bzero(&client_send_wr, sizeof(client_send_wr));
        client_send_wr.sg_list = &client_send_sge;
        client_send_wr.num_sge = 1;
        client_send_wr.opcode = IBV_WR_RDMA_WRITE;
        client_send_wr.send_flags = IBV_SEND_SIGNALED;
        client_send_wr.wr.rdma.rkey = server_metadata_attr.stag.remote_stag;
        client_send_wr.wr.rdma.remote_addr = server_metadata_attr.address;
        ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
        if (ret) {
            printf("Failed to write client src buffer, errno: %d \n", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            printf("We failed to get 1 work completions , ret = %d \n", ret);
            return ret;
        }
        ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
        if (ret) {
            printf("Failed to write client src buffer, errno: %d \n", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            printf("We failed to get 1 work completions , ret = %d \n", ret);
            return ret;
        }
        return 0;
    }
    int client_disconnect_and_clean() {
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        ret = rdma_disconnect(cm_client_id);
        if (ret) {
            printf("Failed to disconnect, errno: %d \n", -errno);
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_DISCONNECTED, &cm_event);
        if (ret) {
            printf("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = %d\n", ret);
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            printf("Failed to acknowledge cm event, errno: %d\n", -errno);
        }
        rdma_destroy_qp(cm_client_id);
        ret = rdma_destroy_id(cm_client_id);
        if (ret) {
            printf("Failed to destroy client id cleanly, %d \n", -errno);
        }
        ret = ibv_destroy_cq(client_cq);
        if (ret) {
            printf("Failed to destroy completion queue cleanly, %d \n", -errno);
        }
        ret = ibv_destroy_comp_channel(io_completion_channel);
        if (ret) {
            printf("Failed to destroy completion channel cleanly, %d \n", -errno);
        }
        rdma_buffer_deregister(server_metadata_mr);
        rdma_buffer_deregister(client_metadata_mr);
        rdma_buffer_deregister(client_src_mr);
        ret = ibv_dealloc_pd(pd);
        if (ret) {
            printf("Failed to destroy client protection domain cleanly, %d \n", -errno);
        }
        rdma_destroy_event_channel(cm_event_channel);
        printf("Client resource clean up is complete \n");
        return 0;
    }
public:
    explicit RDMAWriteStream(const char *server_name, int port) : RDMAEndpoint() {
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        bzero(&server_sockaddr, sizeof(server_sockaddr));
        server_sockaddr.sin_family = AF_INET;
        server_sockaddr.sin_port = htons(port);
        inet_pton(AF_INET, server_name, &server_sockaddr.sin_addr);
        ret = client_prepare_connection(&server_sockaddr);
        if (ret) {
            printf("Failed to setup client connection , ret = %d \n", ret);
        }
        ret = client_pre_post_recv_buffer();
        if (ret) {
            printf("Failed to setup client connection , ret = %d \n", ret);
        }
        ret = client_connect_to_server();
        if (ret) {
            printf("Failed to setup client connection , ret = %d \n", ret);
        }
    }
    virtual bool write(const char *data, std::size_t sz) const {
        memcpy(buffer + position, data, sz);
        position += sz;
        return true;
    }
    ~RDMAWriteStream() {
        int ret = -1;
        for (int i = 0; i < 100; i++) {
            printf("%x", (uint8_t)buffer[i]);
        }
        printf("\n");
        ret = client_xchange_metadata_with_server();
        if (ret) {
            printf("Failed to setup client connection , ret = %d \n", ret);
        }
        ret = client_remote_memory_ops();
        if (ret) {
            printf("Failed to finish remote memory ops, ret = %d \n", ret);
        }
        ret = client_disconnect_and_clean();
        if (ret) {
            printf("Failed to clean up client resources, ret = %d \n", ret);
        }
    };
};
static_assert(ReaderStreamTrait<RDMAReadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<RDMAWriteStream, char>, "Writer must conform to WriterStreamTrait");
#endif

#endif /* MVVM_WAMR_READ_WRITE_H */
