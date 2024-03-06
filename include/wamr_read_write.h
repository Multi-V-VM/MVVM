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
    virtual bool write(const char *data, std::size_t sz) const = 0;
};
struct ReadStream {
    virtual bool read(char *data, std::size_t sz) const = 0;
    virtual bool ignore(std::size_t sz) const = 0;
    virtual std::size_t tellg() const = 0;
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

    //    bool read(char *data, std::size_t sz) const {
    //        ssize_t bytes_read = recv(client_fd, data, sz, 0);
    //        SPDLOG_DEBUG("{}, {}",data,sz);
    //        if (bytes_read > 0) {
    //            position += bytes_read;
    //            return static_cast<std::size_t>(bytes_read) == sz;
    //        }
    //        return false;
    //    }
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

#endif /* MVVM_WAMR_READ_WRITE_H */
