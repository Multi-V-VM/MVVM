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
#include "ylt/struct_pack.hpp"
#include <cstdint>
#include <spdlog/spdlog.h>
#include <vector>
#ifndef _WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <s2n.h>
#include <sys/socket.h>
#include <unistd.h>
// Include our attestation wrapper instead of the direct s2n header
#include "wamr_attestation.h"
#endif

struct WriteStream {
    virtual bool write(const char *data, std::size_t sz) const { return false; };
    virtual ~WriteStream() = default;
};
struct ReadStream {
    virtual bool read(char *data, std::size_t sz) const { return false; };
    virtual bool ignore(std::size_t sz) const { return false; };
    virtual const char *read_view(size_t len) { return nullptr; }
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
    const char *read_view(size_t len) override {
        char *buffer = new char[len];
        if (fread(buffer, len, 1, file) != 1) {
            delete[] buffer;
            return nullptr;
        }
        return buffer;
    }
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
            SPDLOG_ERROR("Socket creation failed");
            return;
        }
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, address, &server_addr.sin_addr);
        if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            SPDLOG_ERROR("Connection failed");
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
    const char *read_view(size_t len) override {
        char *buffer = new char[len];
        std::size_t totalReceived = 0;
        while (totalReceived < len) {
            ssize_t received = recv(client_fd, buffer + totalReceived, len - totalReceived, 0);
            if (received == -1 || received == 0) {
                return nullptr;
            }
            totalReceived += received;
        }
        position += len;

        return buffer;
    }
    explicit SocketReadStream(const char *address, int port) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            SPDLOG_ERROR("Socket creation failed");
            return;
        }
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, address, &server_addr.sin_addr);
        auto addr_len = sizeof(server_addr);
        SPDLOG_INFO("[Server] Bind socket {} {}", address, port);
        if (bind(sock_fd, (struct sockaddr *)&server_addr, addr_len) < 0) {
            SPDLOG_ERROR("Bind failed");
            exit(EXIT_FAILURE);
        }
        SPDLOG_INFO("[Server] Listening on socket");
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

// -----------------------------------------------------------------------------
// 1) Global s2n initialization & config setup
//    In a real program, do this in main() or a dedicated init function.
// -----------------------------------------------------------------------------

static s2n_config *g_server_config = nullptr;
static s2n_config *g_client_config = nullptr;

// Example cert/key strings for server (self-signed). You'd likely load from files:
static const char SERVER_CERT_PEM[] = R"PEM(
-----BEGIN CERTIFICATE-----
... Your Server Certificate ...
-----END CERTIFICATE-----
)PEM";

static const char SERVER_KEY_PEM[] = R"PEM(
-----BEGIN PRIVATE KEY-----
... Your Server Private Key ...
-----END PRIVATE KEY-----
)PEM";

// -----------------------------------------------------------------------------
// 2) TLS-Enabled SecureSocketWriteStream with Attestation (Client-Side)
// -----------------------------------------------------------------------------
struct SecureSocketWriteStream : public WriteStream {
    int sock_fd;
    s2n_connection *conn; // s2n connection handle
    // Added for attestation
    wamr_attestation_type attestation_type;
    bool attestation_enabled;
    std::vector<uint8_t> attestation_evidence;

    bool write(const char *data, std::size_t sz) const override {
        std::size_t totalSent = 0;
        while (totalSent < sz) {
            s2n_blocked_status blocked;
            ssize_t sent = s2n_send(conn, data + totalSent, sz - totalSent, &blocked);
            if (sent <= 0) {
                SPDLOG_ERROR("s2n_send failed");
                return false;
            }
            totalSent += sent;
        }
        return true;
    }

    // Set the attestation type
    void enable_attestation(wamr_attestation_type type) {
        attestation_type = type;
        attestation_enabled = true;
    }

    // Set attestation evidence
    void set_attestation_evidence(const std::vector<uint8_t> &evidence) { attestation_evidence = evidence; }

    explicit SecureSocketWriteStream(const char *address, int port,
                                     wamr_attestation_type att_type = WAMR_ATTESTATION_NONE)
        : sock_fd(-1), conn(nullptr), attestation_type(att_type),
          attestation_enabled(att_type != WAMR_ATTESTATION_NONE) {
        if (s2n_init() < 0) {
            SPDLOG_ERROR("s2n_init() failed");
            throw;
        }

        // ----------------- Create SERVER config -----------------
        g_server_config = s2n_config_new();
        if (!g_server_config) {
            SPDLOG_ERROR("s2n_config_new() for server failed");
            throw;
        }
        // Sets recommended TLS 1.2/1.3 cipher suites. Adjust as needed:
        if (s2n_config_set_cipher_preferences(g_server_config, "default_tls13") < 0) {
            SPDLOG_ERROR("Server: s2n_config_set_cipher_preferences() failed");
            throw;
        }
        // Provide cert & key (PEM strings). For multi-cert, call multiple times:
        if (s2n_config_add_cert_chain_and_key(g_server_config, SERVER_CERT_PEM, SERVER_KEY_PEM) < 0) {
            SPDLOG_ERROR("Server: s2n_config_add_cert_chain_and_key() failed");
            throw;
        }

        // ----------------- Create CLIENT config -----------------
        g_client_config = s2n_config_new();
        if (!g_client_config) {
            SPDLOG_ERROR("s2n_config_new() for client failed");
            throw;
        }
        if (s2n_config_set_cipher_preferences(g_client_config, "default_tls13") < 0) {
            SPDLOG_ERROR("Client: s2n_config_set_cipher_preferences() failed");
            throw;
        }

        // In a real client, you might want to set trust stores, verify peer cert, etc.
        // For example:
        // s2n_config_set_verify_host_callback(g_client_config, ...);

        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            SPDLOG_ERROR("Client socket creation failed");
            return;
        }
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, address, &server_addr.sin_addr);

        if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
            SPDLOG_ERROR("Client: connect() failed");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Create a new s2n connection in CLIENT mode
        conn = s2n_connection_new(S2N_CLIENT);
        if (!conn) {
            SPDLOG_ERROR("s2n_connection_new(S2N_CLIENT) failed");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Attach the client config (global)
        if (s2n_connection_set_config(conn, g_client_config) < 0) {
            SPDLOG_ERROR("Client: s2n_connection_set_config() failed");
            s2n_connection_free(conn);
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Configure attestation if enabled
        if (attestation_enabled) {
            if (!wamr_set_attestation_type(conn, attestation_type)) {
                SPDLOG_ERROR("Failed to set attestation type");
                s2n_connection_free(conn);
                close(sock_fd);
                sock_fd = -1;
                return;
            }

            // If attestation evidence was already provided, set it
            if (!attestation_evidence.empty()) {
                if (!wamr_set_attestation_evidence(conn, attestation_evidence)) {
                    SPDLOG_ERROR("Failed to set attestation evidence");
                    s2n_connection_free(conn);
                    close(sock_fd);
                    sock_fd = -1;
                    return;
                }
            }
        }

        // Associate the socket with s2n
        if (s2n_connection_set_fd(conn, sock_fd) < 0) {
            SPDLOG_ERROR("Client: s2n_connection_set_fd() failed");
            s2n_connection_free(conn);
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Perform the TLS handshake
        s2n_blocked_status blocked;
        int rc;
        do {
            rc = s2n_negotiate(conn, &blocked);
        } while ((rc != 0) && (blocked == S2N_BLOCKED_ON_READ || blocked == S2N_BLOCKED_ON_WRITE));

        if (rc < 0) {
            SPDLOG_ERROR("Client: TLS handshake failed: {}", s2n_strerror(s2n_errno, "EN"));
            s2n_connection_free(conn);
            close(sock_fd);
            sock_fd = -1;
            return;
        }
        SPDLOG_INFO("Client: TLS handshake complete");

        // If attestation was enabled, verify the peer's attestation evidence
        if (attestation_enabled) {
            if (!wamr_verify_attestation_evidence(conn)) {
                SPDLOG_ERROR("Failed to verify peer attestation evidence");
                s2n_connection_free(conn);
                close(sock_fd);
                sock_fd = -1;
                return;
            }
            SPDLOG_INFO("Peer attestation evidence verified successfully");
        }
    }

    ~SecureSocketWriteStream() override {
        if (sock_fd != -1 && conn) {
            // Graceful TLS shutdown
            s2n_blocked_status blocked;
            s2n_shutdown(conn, &blocked);
            s2n_connection_free(conn);
            close(sock_fd);
        }
    }
};

// -----------------------------------------------------------------------------
// 3) TLS-Enabled SecureSocketReadStream with Attestation (Server-Side)
// -----------------------------------------------------------------------------
struct SecureSocketReadStream : public ReadStream {
    int sock_fd;
    int client_fd;
    mutable size_t position = 0;
    s2n_connection *conn; // s2n connection handle
    // Added for attestation
    wamr_attestation_type attestation_type;
    bool attestation_enabled;
    std::vector<uint8_t> attestation_evidence;

    bool read(char *data, std::size_t sz) const override {
        std::size_t totalReceived = 0;
        while (totalReceived < sz) {
            s2n_blocked_status blocked;
            ssize_t received = s2n_recv(conn, data + totalReceived, sz - totalReceived, &blocked);
            if (received <= 0) {
                SPDLOG_ERROR("Server: s2n_recv() error or connection closed");
                return false;
            }
            totalReceived += received;
        }
        position += totalReceived;
        return true;
    }

    const char *read_view(size_t len) override {
        char *buffer = new char[len];
        std::size_t totalReceived = 0;
        while (totalReceived < len) {
            s2n_blocked_status blocked;
            ssize_t received = s2n_recv(conn, buffer + totalReceived, len - totalReceived, &blocked);
            if (received <= 0) {
                SPDLOG_ERROR("Server: s2n_recv() error or connection closed");
                delete[] buffer; // prevent leak
                return nullptr;
            }
            totalReceived += received;
        }
        position += totalReceived;
        return buffer; // Caller must free/handle this pointer
    }

    // Set the attestation type
    void enable_attestation(wamr_attestation_type type) {
        attestation_type = type;
        attestation_enabled = true;
    }

    // Set attestation evidence
    void set_attestation_evidence(const std::vector<uint8_t> &evidence) { attestation_evidence = evidence; }

    explicit SecureSocketReadStream(const char *address, int port,
                                    wamr_attestation_type att_type = WAMR_ATTESTATION_NONE)
        : sock_fd(-1), client_fd(-1), conn(nullptr), attestation_type(att_type),
          attestation_enabled(att_type != WAMR_ATTESTATION_NONE) {
        sock_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (sock_fd == -1) {
            SPDLOG_ERROR("Server socket creation failed");
            return;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        inet_pton(AF_INET, address, &server_addr.sin_addr);

        auto addr_len = static_cast<socklen_t>(sizeof(server_addr));
        SPDLOG_INFO("Server: Binding to %s:%d", address, port);
        if (bind(sock_fd, (struct sockaddr *)&server_addr, addr_len) < 0) {
            SPDLOG_ERROR("Server: bind() failed");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        SPDLOG_INFO("Server: Listening...");
        if (listen(sock_fd, 3) < 0) {
            SPDLOG_ERROR("Server: listen() failed");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        client_fd = accept(sock_fd, (struct sockaddr *)&server_addr, &addr_len);
        if (client_fd == -1) {
            SPDLOG_ERROR("Server: accept() failed");
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Create a new s2n connection in SERVER mode
        conn = s2n_connection_new(S2N_SERVER);
        if (!conn) {
            SPDLOG_ERROR("Server: s2n_connection_new(S2N_SERVER) failed");
            close(client_fd);
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Configure attestation if enabled
        if (attestation_enabled) {
            if (!wamr_set_attestation_type(conn, attestation_type)) {
                SPDLOG_ERROR("Failed to set attestation type");
                s2n_connection_free(conn);
                close(client_fd);
                close(sock_fd);
                sock_fd = -1;
                return;
            }

            // If attestation evidence was already provided, set it
            if (!attestation_evidence.empty()) {
                if (!wamr_set_attestation_evidence(conn, attestation_evidence)) {
                    SPDLOG_ERROR("Failed to set attestation evidence");
                    s2n_connection_free(conn);
                    close(client_fd);
                    close(sock_fd);
                    sock_fd = -1;
                    return;
                }
            }
        }

        // Attach the server config (global)
        if (s2n_connection_set_config(conn, g_server_config) < 0) {
            SPDLOG_ERROR("Server: s2n_connection_set_config() failed");
            s2n_connection_free(conn);
            close(client_fd);
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Associate the newly accepted client socket
        if (s2n_connection_set_fd(conn, client_fd) < 0) {
            SPDLOG_ERROR("Server: s2n_connection_set_fd() failed");
            s2n_connection_free(conn);
            close(client_fd);
            close(sock_fd);
            sock_fd = -1;
            return;
        }

        // Perform the TLS handshake
        s2n_blocked_status blocked;
        int rc;
        do {
            rc = s2n_negotiate(conn, &blocked);
        } while ((rc != 0) && (blocked == S2N_BLOCKED_ON_READ || blocked == S2N_BLOCKED_ON_WRITE));

        if (rc < 0) {
            SPDLOG_ERROR("Server: TLS handshake failed: {}", s2n_strerror(s2n_errno, "EN"));
            s2n_connection_free(conn);
            close(client_fd);
            close(sock_fd);
            sock_fd = -1;
            return;
        }
        SPDLOG_INFO("Server: TLS handshake complete");

        // If attestation was enabled, verify the peer's attestation evidence
        if (attestation_enabled) {
            if (!wamr_verify_attestation_evidence(conn)) {
                SPDLOG_ERROR("Failed to verify peer attestation evidence");
                s2n_connection_free(conn);
                close(client_fd);
                close(sock_fd);
                sock_fd = -1;
                return;
            }
            SPDLOG_INFO("Peer attestation evidence verified successfully");
        }
    }

    bool ignore(std::size_t sz) const override {
        char buffer[1024];
        std::size_t total_ignored = 0;
        while (total_ignored < sz) {
            std::size_t to_ignore = std::min(sz - total_ignored, sizeof(buffer));
            s2n_blocked_status blocked;
            ssize_t ignored = s2n_recv(conn, buffer, to_ignore, &blocked);
            if (ignored <= 0) {
                SPDLOG_ERROR("Server: ignore() -> s2n_recv() error");
                return false;
            }
            total_ignored += ignored;
            position += ignored;
        }
        return true;
    }

    std::size_t tellg() const override { return position; }

    ~SecureSocketReadStream() override {
        // Graceful TLS shutdown
        if (sock_fd != -1 && conn) {
            s2n_blocked_status blocked;
            s2n_shutdown(conn, &blocked);
            s2n_connection_free(conn);
            close(client_fd);
            close(sock_fd);
        }
    }
};

// -----------------------------------------------------------------------------
// 4) Ensure the classes fulfill the stream trait requirements
// -----------------------------------------------------------------------------
static_assert(ReaderStreamTrait<SecureSocketReadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<SecureSocketWriteStream, char>, "Writer must conform to WriterStreamTrait");
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
#define BUFFER_SIZE (1024 * 1024 * 1024L)
class RDMAEndpoint {
public:
    struct rdma_event_channel *cm_event_channel = nullptr;
    struct rdma_cm_id *cm_server_id = nullptr, *cm_client_id = nullptr;
    struct ibv_pd *pd = nullptr;
    struct ibv_comp_channel *io_completion_channel = nullptr;
    struct ibv_cq *cq = nullptr;
    struct ibv_qp_init_attr qp_init_attr{};
    struct ibv_qp *client_qp = nullptr;
    struct ibv_mr *client_metadata_mr = nullptr, *server_buffer_mr = nullptr, *server_metadata_mr = nullptr;
    struct rdma_buffer_attr client_metadata_attr{};
    struct rdma_buffer_attr server_metadata_attr{};
    struct ibv_recv_wr client_recv_wr{};
    struct ibv_recv_wr *bad_client_recv_wr = nullptr;
    struct ibv_send_wr server_send_wr{};
    struct ibv_send_wr *bad_server_send_wr = nullptr;
    struct ibv_sge client_recv_sge{};
    struct ibv_sge server_send_sge{};
    RDMAEndpoint() = default;
    static void show_rdma_cmid(struct rdma_cm_id *id) {
        if (!id) {
            SPDLOG_ERROR("Passed ptr is nullptr");
            return;
        }
        SPDLOG_DEBUG("RDMA cm id at {:p} ", ((void *)id));
        if (id->verbs && id->verbs->device)
            SPDLOG_DEBUG("dev_ctx: {:p} (device name: {}) ", ((void *)id->verbs), id->verbs->device->name);
        if (id->channel)
            SPDLOG_DEBUG("cm event channel {:p} ", ((void *)id->channel));
        SPDLOG_DEBUG("QP: {:p} , port_space {:x}, port_num {} ", ((void *)id->qp), id->ps, id->port_num);
    }
    static void show_rdma_buffer_attr(struct rdma_buffer_attr *attr) {
        if (!attr) {
            SPDLOG_ERROR("Passed attr is nullptr");
            return;
        }
        SPDLOG_DEBUG("---------------------------------------------------------");
        SPDLOG_DEBUG("buffer attr, addr: {:p} , len: {} , stag : 0x{:x} ", (void *)attr->address,
                     (unsigned int)attr->length, (uint8_t)attr->stag.local_stag);
        SPDLOG_DEBUG("---------------------------------------------------------");
    }
    static struct ibv_mr *rdma_buffer_alloc(struct ibv_pd *pd, uint32_t size, enum ibv_access_flags permission) {
        struct ibv_mr *mr = nullptr;
        if (!pd) {
            SPDLOG_ERROR("Protection domain is nullptr ");
            return nullptr;
        }
        void *buf = calloc(1, size);
        if (!buf) {
            SPDLOG_ERROR("failed to allocate buffer, -ENOMEM");
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
            SPDLOG_ERROR("Protection domain is nullptr, ignoring ");
            return nullptr;
        }
        mr = ibv_reg_mr(pd, addr, length, permission);
        if (!mr) {
            SPDLOG_ERROR("Failed to create mr on buffer, errno: {} ", -errno);
            return nullptr;
        }
        return mr;
    }
    static void rdma_buffer_free(struct ibv_mr *mr) {
        if (!mr) {
            SPDLOG_ERROR("Passed memory region is nullptr, ignoring");
            return;
        }
        void *to_free = mr->addr;
        rdma_buffer_deregister(mr);
        free(to_free);
    }
    static void rdma_buffer_deregister(struct ibv_mr *mr) {
        if (!mr) {
            SPDLOG_ERROR("Passed memory region is nullptr, ignoring");
            return;
        }
        ibv_dereg_mr(mr);
    }
    static int process_rdma_cm_event(struct rdma_event_channel *echannel, enum rdma_cm_event_type expected_event,
                                     struct rdma_cm_event **cm_event) {
        int ret = 1;
        ret = rdma_get_cm_event(echannel, cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to retrieve a cm event, errno: {} ", -errno);
            return -errno;
        }
        if (0 != (*cm_event)->status) {
            SPDLOG_ERROR("CM event has non zero status: {}", (*cm_event)->status);
            ret = -((*cm_event)->status);
            rdma_ack_cm_event(*cm_event);
            return ret;
        }
        if ((*cm_event)->event != expected_event) {
            SPDLOG_ERROR("Unexpected event received: {} [ expecting: {} ]", rdma_event_str((*cm_event)->event),
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
            SPDLOG_ERROR("Failed to get next CQ event due to {} ", -errno);
            return -errno;
        }
        ret = ibv_req_notify_cq(cq_ptr, 0);
        if (ret) {
            SPDLOG_ERROR("Failed to request further notifications {} ", -errno);
            return -errno;
        }
        total_wc = 0;
        do {
            ret = ibv_poll_cq(cq_ptr, max_wc - total_wc, wc + total_wc);
            if (ret < 0) {
                SPDLOG_ERROR("Failed to poll cq for wc due to {} ", ret);
            }
            total_wc += ret;
        } while (total_wc < max_wc);
        for (i = 0; i < total_wc; i++) {
            if (wc[i].status != IBV_WC_SUCCESS) {
                SPDLOG_ERROR("Work completion (WC) has error status: {} at index {}", ibv_wc_status_str(wc[i].status),
                             i);
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
    mutable std::vector<uint8_t> buffer;
    mutable long buffer_size;
    int setup_client_resources() {
        int ret = -1;
        if (!cm_client_id) {
            SPDLOG_ERROR("Client id is still nullptr ");
            return -EINVAL;
        }
        pd = ibv_alloc_pd(cm_client_id->verbs);
        if (!pd) {
            SPDLOG_ERROR("Failed to allocate a protection domain errno: {}", -errno);
            return -errno;
        }
        io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
        if (!io_completion_channel) {
            SPDLOG_ERROR("Failed to create an I/O completion event channel, {}", -errno);
            return -errno;
        }
        cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, nullptr, io_completion_channel, 0);
        if (!cq) {
            SPDLOG_ERROR("Failed to create a completion queue (cq), errno: {}", -errno);
            return -errno;
        }
        ret = ibv_req_notify_cq(cq, 0);
        if (ret) {
            SPDLOG_ERROR("Failed to request notifications on CQ errno: {} ", -errno);
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
            SPDLOG_ERROR("Failed to create QP due to errno: {}", -errno);
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
            SPDLOG_ERROR("Creating cm event channel failed with errno : ({})", -errno);
            return -errno;
        }
        ret = rdma_create_id(cm_event_channel, &cm_server_id, nullptr, RDMA_PS_TCP);
        if (ret) {
            SPDLOG_ERROR("Creating server cm id failed with errno: {} ", -errno);
            return -errno;
        }
        ret = rdma_bind_addr(cm_server_id, (struct sockaddr *)server_addr);
        if (ret) {
            SPDLOG_ERROR("Failed to bind server address, errno: {} ", -errno);
            return -errno;
        }
        ret = rdma_listen(cm_server_id, 8);
        if (ret) {
            SPDLOG_ERROR("rdma_listen failed to listen on server address, errno: {} ", -errno);
            return -errno;
        }
        SPDLOG_INFO("Server is listening successfully at: {} , port: {} ", inet_ntoa(server_addr->sin_addr),
                    ntohs(server_addr->sin_port));
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_CONNECT_REQUEST, &cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to get cm event, ret = {} ", ret);
            return ret;
        }
        cm_client_id = cm_event->id;
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to acknowledge the cm event errno: {} ", -errno);
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
            SPDLOG_ERROR("Client resources are not properly setup");
            return -EINVAL;
        }
        client_metadata_mr =
            rdma_buffer_register(pd, &client_metadata_attr, sizeof(client_metadata_attr), (IBV_ACCESS_LOCAL_WRITE));
        if (!client_metadata_mr) {
            SPDLOG_ERROR("Failed to register client attr buffer");
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
            SPDLOG_ERROR("Failed to pre-post the receive buffer, errno: {} ", ret);
            return ret;
        }
        memset(&conn_param, 0, sizeof(conn_param));
        conn_param.initiator_depth = 3;
        conn_param.responder_resources = 3;
        ret = rdma_accept(cm_client_id, &conn_param);
        if (ret) {
            SPDLOG_ERROR("Failed to accept the connection, errno: {} ", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to get the cm event, errnp: {} ", -errno);
            return -errno;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to acknowledge the cm event {}", -errno);
            return -errno;
        }
        memcpy(&remote_sockaddr, rdma_get_peer_addr(cm_client_id), sizeof(struct sockaddr_in));
        SPDLOG_INFO("A new connection is accepted from {} ", inet_ntoa(remote_sockaddr.sin_addr));
        return ret;
    }
    int send_server_metadata_to_client() {
        struct ibv_wc wc{};
        int ret = -1;
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            SPDLOG_ERROR("Failed to receive , ret = {} ", ret);
            return ret;
        }
        show_rdma_buffer_attr(&client_metadata_attr);
        SPDLOG_DEBUG("The client has requested buffer length of : {} bytes ", (uint32_t)client_metadata_attr.length);
        server_buffer_mr =
            rdma_buffer_alloc(pd, client_metadata_attr.length,
                              (enum ibv_access_flags)(((int)IBV_ACCESS_LOCAL_WRITE) | ((int)IBV_ACCESS_REMOTE_READ) |
                              ((int)IBV_ACCESS_REMOTE_WRITE)));
        if (!server_buffer_mr) {
            SPDLOG_ERROR("Server failed to create a buffer ");
            return -ENOMEM;
        }
        server_metadata_attr.address = (uint64_t)server_buffer_mr->addr;
        server_metadata_attr.length = (uint32_t)server_buffer_mr->length;
        server_metadata_attr.stag.local_stag = (uint32_t)server_buffer_mr->lkey;
        server_metadata_mr =
            rdma_buffer_register(pd, &server_metadata_attr, sizeof(server_metadata_attr), IBV_ACCESS_LOCAL_WRITE);
        if (!server_metadata_mr) {
            SPDLOG_ERROR("Server failed to create to hold server metadata ");
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
            SPDLOG_ERROR("Posting of server metdata failed, errno: {} ", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            SPDLOG_ERROR("Failed to send server metadata, ret = {} ", ret);
            return ret;
        }
        usleep(1);
        SPDLOG_DEBUG("Received buffer contents: ");
        for (int i = 0; i < 4; i++) {
            SPDLOG_DEBUG("{}", ((uint8_t *)server_buffer_mr->addr)[i]);
        }
        return 0;
    }
    int disconnect_and_cleanup() {
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_DISCONNECTED, &cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to get disconnect event, ret = {} ", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to acknowledge the cm event {}", -errno);
            return -errno;
        }
        SPDLOG_DEBUG("A disconnect event is received from the client...");
        rdma_destroy_qp(cm_client_id);
        ret = rdma_destroy_id(cm_client_id);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy client id cleanly, {} ", -errno);
        }
        ret = ibv_destroy_cq(cq);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy completion queue cleanly, {} ", -errno);
        }
        ret = ibv_destroy_comp_channel(io_completion_channel);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy completion channel cleanly, {} ", -errno);
        }
        rdma_buffer_free(server_buffer_mr);
        rdma_buffer_deregister(server_metadata_mr);
        rdma_buffer_deregister(client_metadata_mr);
        ret = ibv_dealloc_pd(pd);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy client protection domain cleanly, {} ", -errno);
        }
        ret = rdma_destroy_id(cm_server_id);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy server id cleanly, {} ", -errno);
        }
        rdma_destroy_event_channel(cm_event_channel);
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
        long received = 0;
        {
            ret = start_rdma_server(&server_sockaddr);
            if (ret) {
                SPDLOG_ERROR("RDMA server failed to start cleanly, ret = {} ", ret);
            }
            ret = setup_client_resources();
            if (ret) {
                SPDLOG_ERROR("Failed to setup client resources, ret = {} ", ret);
            }
            ret = accept_client_connection();
            if (ret) {
                SPDLOG_ERROR("Failed to handle client cleanly, ret = {} ", ret);
            }
            ret = send_server_metadata_to_client();
            if (ret) {
                SPDLOG_ERROR("Failed to send server metadata to the client, ret = {} ", ret);
            }
            received += client_metadata_attr.length;
        }
        uint8_t *ptr = (uint8_t *)server_buffer_mr->addr;
        size_t size = client_metadata_attr.length;
        buffer = std::vector(ptr, ptr + size);

        while (received % BUFFER_SIZE == 0) {
            disconnect_and_cleanup();
            ret = start_rdma_server(&server_sockaddr);
            if (ret) {
                SPDLOG_ERROR("RDMA server failed to start cleanly, ret = {} ", ret);
            }
            ret = setup_client_resources();
            if (ret) {
                SPDLOG_ERROR("Failed to setup client resources, ret = {} ", ret);
            }
            ret = accept_client_connection();
            if (ret) {
                SPDLOG_ERROR("Failed to handle client cleanly, ret = {} ", ret);
            }
            ret = send_server_metadata_to_client();
            if (ret) {
                SPDLOG_ERROR("Failed to send server metadata to the client, ret = {} ", ret);
            }
            received += client_metadata_attr.length;
            uint8_t *ptr2 = (uint8_t *)server_buffer_mr->addr;
            size_t size2 = client_metadata_attr.length;
            buffer.insert(buffer.end(), ptr2, ptr2 + size2);
        }
        buffer_size = received;
        disconnect_and_cleanup();
    }
    bool read(char *data, std::size_t sz) const override {
        if (position + sz > buffer_size) {
            std::throw_with_nested(std::runtime_error("Buffer overflow"));
        }
        memcpy(data, buffer.data() + position, sz);
        position += sz;
        return true;
    }
    const char *read_view(size_t len) override {
        if (position + len > buffer_size) {
            std::throw_with_nested(std::runtime_error("Buffer overflow"));
        }
        const char *ret = (const char *)(buffer.data() + position);
        position += len;
        return ret;
    }
    bool ignore(std::size_t sz) const override {
        position += sz;
        return true;
    }
    std::size_t tellg() const override { return position; }
    ~RDMAReadStream() override = default;
};
class RDMAWriteStream : public WriteStream, public RDMAEndpoint {
public:
    mutable std::vector<char> buffer{};
    mutable long position = 0;
    struct ibv_cq *client_cq = nullptr;
    struct ibv_sge client_send_sge{};
    struct ibv_sge server_recv_sge{};
    struct sockaddr_in server_sockaddr{};
    struct rdma_conn_param conn_param{};
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
            SPDLOG_ERROR("Creating cm event channel failed, errno: {} ", -errno);
            return -errno;
        }
        ret = rdma_create_id(cm_event_channel, &cm_client_id, nullptr, RDMA_PS_TCP);
        if (ret) {
            SPDLOG_ERROR("Creating cm id failed with errno: {} ", -errno);
            return -errno;
        }
        ret = rdma_resolve_addr(cm_client_id, nullptr, (struct sockaddr *)s_addr, 2000);
        if (ret) {
            SPDLOG_ERROR("Failed to resolve address, errno: {} ", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ADDR_RESOLVED, &cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to receive a valid event, ret = {} ", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to acknowledge the CM event, errno: {}", -errno);
            return -errno;
        }
        ret = rdma_resolve_route(cm_client_id, 2000);
        if (ret) {
            SPDLOG_ERROR("Failed to resolve route, erno: {} ", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ROUTE_RESOLVED, &cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to receive a valid event, ret = {} ", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to acknowledge the CM event, errno: {} ", -errno);
            return -errno;
        }
        SPDLOG_DEBUG("Trying to connect to server at : {} port: {} ", inet_ntoa(s_addr->sin_addr),
                     ntohs(s_addr->sin_port));
        pd = ibv_alloc_pd(cm_client_id->verbs);
        if (!pd) {
            SPDLOG_ERROR("Failed to alloc pd, errno: {} ", -errno);
            return -errno;
        }
        io_completion_channel = ibv_create_comp_channel(cm_client_id->verbs);
        if (!io_completion_channel) {
            SPDLOG_ERROR("Failed to create IO completion event channel, errno: {}", -errno);
            return -errno;
        }
        client_cq = ibv_create_cq(cm_client_id->verbs, CQ_CAPACITY, nullptr, io_completion_channel, 0);
        if (!client_cq) {
            SPDLOG_ERROR("Failed to create CQ, errno: {} ", -errno);
            return -errno;
        }
        ret = ibv_req_notify_cq(client_cq, 0);
        if (ret) {
            SPDLOG_ERROR("Failed to request notifications, errno: {}", -errno);
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
            SPDLOG_ERROR("Failed to create QP, errno: {} ", -errno);
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
            SPDLOG_ERROR("Failed to setup the server metadata mr , -ENOMEM");
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
            SPDLOG_ERROR("Failed to pre-post the receive buffer, errno: {} ", ret);
            return ret;
        }
        return 0;
    }
    int client_connect_to_server() {
        struct rdma_conn_param conn_param{};
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        bzero(&conn_param, sizeof(conn_param));
        conn_param.initiator_depth = 3;
        conn_param.responder_resources = 3;
        conn_param.retry_count = 3;
        ret = rdma_connect(cm_client_id, &conn_param);
        if (ret) {
            SPDLOG_ERROR("Failed to connect to remote host , errno: {}", -errno);
            return -errno;
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_ESTABLISHED, &cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to get cm event, ret = {} ", ret);
            return ret;
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to acknowledge cm event, errno: {}", -errno);
            return -errno;
        }
        SPDLOG_DEBUG("The client is connected successfully ");
        return 0;
    }
    int client_xchange_metadata_with_server(char *b, long sz) {
        struct ibv_wc wc[2];
        int ret = -1;
        client_src_mr =
            rdma_buffer_register(pd, b, sz,
                                 (enum ibv_access_flags)(((int)IBV_ACCESS_LOCAL_WRITE) | ((int)IBV_ACCESS_REMOTE_READ) |
                                                         ((int)IBV_ACCESS_REMOTE_WRITE)));
        if (!client_src_mr) {
            SPDLOG_ERROR("Failed to register the first buffer, ret = {} ", ret);
            return ret;
        }
        client_metadata_attr.address = (uint64_t)client_src_mr->addr;
        client_metadata_attr.length = client_src_mr->length;
        client_metadata_attr.stag.local_stag = client_src_mr->lkey;
        client_metadata_mr =
            rdma_buffer_register(pd, &client_metadata_attr, sizeof(client_metadata_attr), IBV_ACCESS_LOCAL_WRITE);
        if (!client_metadata_mr) {
            SPDLOG_ERROR("Failed to register the client metadata buffer, ret = {} ", ret);
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
            SPDLOG_ERROR("Failed to send client metadata, errno: {} ", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, wc, 2);
        if (ret != 2) {
            SPDLOG_ERROR("We failed to get 2 work completions , ret = {} ", ret);
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
            SPDLOG_ERROR("Failed to write client src buffer, errno: {} ", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            SPDLOG_ERROR("We failed to get 1 work completions , ret = {} ", ret);
            return ret;
        }
        ret = ibv_post_send(client_qp, &client_send_wr, &bad_client_send_wr);
        if (ret) {
            SPDLOG_ERROR("Failed to write client src buffer, errno: {} ", -errno);
            return -errno;
        }
        ret = process_work_completion_events(io_completion_channel, &wc, 1);
        if (ret != 1) {
            SPDLOG_ERROR("We failed to get 1 work completions , ret = {} ", ret);
            return ret;
        }
        return 0;
    }
    int client_disconnect_and_clean() {
        struct rdma_cm_event *cm_event = nullptr;
        int ret = -1;
        ret = rdma_disconnect(cm_client_id);
        if (ret) {
            SPDLOG_ERROR("Failed to disconnect, errno: {} ", -errno);
        }
        ret = process_rdma_cm_event(cm_event_channel, RDMA_CM_EVENT_DISCONNECTED, &cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to get RDMA_CM_EVENT_DISCONNECTED event, ret = {}", ret);
        }
        ret = rdma_ack_cm_event(cm_event);
        if (ret) {
            SPDLOG_ERROR("Failed to acknowledge cm event, errno: {}", -errno);
        }
        rdma_destroy_qp(cm_client_id);
        ret = rdma_destroy_id(cm_client_id);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy client id cleanly, {} ", -errno);
        }
        ret = ibv_destroy_cq(client_cq);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy completion queue cleanly, {} ", -errno);
        }
        ret = ibv_destroy_comp_channel(io_completion_channel);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy completion channel cleanly, {} ", -errno);
        }
        rdma_buffer_deregister(server_metadata_mr);
        rdma_buffer_deregister(client_metadata_mr);
        rdma_buffer_deregister(client_src_mr);
        ret = ibv_dealloc_pd(pd);
        if (ret) {
            SPDLOG_ERROR("Failed to destroy client protection domain cleanly, {} ", -errno);
        }
        rdma_destroy_event_channel(cm_event_channel);
        SPDLOG_DEBUG("Client resource clean up is complete ");
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
            SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
        }
        ret = client_pre_post_recv_buffer();
        if (ret) {
            SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
        }
        ret = client_connect_to_server();
        if (ret) {
            SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
        }
    }
    virtual bool write(const char *data, std::size_t sz) const {
        buffer.insert(buffer.end(), data, data + sz);
        position += sz;
        return true;
    }
    ~RDMAWriteStream() {
        int ret = -1;
        for (int i = 0; i < 4; i++) {
            SPDLOG_DEBUG("{:x}", (uint8_t)buffer[i]);
        }
        for (int i = 0; i < 4; i++) {
            SPDLOG_DEBUG("{:x}", (uint8_t)buffer[i + BUFFER_SIZE]);
        }
        long to_send = position;
        ret = client_xchange_metadata_with_server(buffer.data(), std::min(to_send, BUFFER_SIZE));
        if (ret) {
            SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
        }
        ret = client_remote_memory_ops();
        if (ret) {
            SPDLOG_ERROR("Failed to finish remote memory ops, ret = {} ", ret);
        }
        ret = client_disconnect_and_clean();
        if (ret) {
            SPDLOG_ERROR("Failed to clean up client resources, ret = {} ", ret);
        }
        to_send -= BUFFER_SIZE;
        while (to_send > 0) {
            usleep(1024 * 200);
            ret = client_prepare_connection(&server_sockaddr);
            if (ret) {
                SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
            }
            ret = client_pre_post_recv_buffer();
            if (ret) {
                SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
            }
            ret = client_connect_to_server();
            if (ret) {
                SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
            }
            SPDLOG_DEBUG("{}", position - to_send);
            for (int i = 0; i < 4; i++) {
                SPDLOG_DEBUG("{:x}", (uint8_t)buffer[i + position - to_send]);
            }
            ret = client_xchange_metadata_with_server(buffer.data() + (position - to_send),
                                                      std::min(to_send, BUFFER_SIZE));
            if (ret) {
                SPDLOG_ERROR("Failed to setup client connection , ret = {} ", ret);
            }
            ret = client_remote_memory_ops();
            if (ret) {
                SPDLOG_ERROR("Failed to finish remote memory ops, ret = {} ", ret);
            }
            ret = client_disconnect_and_clean();
            if (ret) {
                SPDLOG_ERROR("Failed to clean up client resources, ret = {} ", ret);
            }
            to_send -= BUFFER_SIZE;
        }
    };
};
static_assert(ReaderStreamTrait<RDMAReadStream, char>, "Reader must conform to ReaderStreamTrait");
static_assert(WriterStreamTrait<RDMAWriteStream, char>, "Writer must conform to WriterStreamTrait");
#endif

#endif /* MVVM_WAMR_READ_WRITE_H */
