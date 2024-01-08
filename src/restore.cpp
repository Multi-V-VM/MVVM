//
// Created by victoryang00 on 4/29/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_export.h"
#include "wamr_read_write.h"
#include "wasm_runtime.h"
#include <arpa/inet.h>
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <string>

FreadStream *reader;
WAMRInstance *wamr = nullptr;
bool is_debug = false;

void serialize_to_file(WASMExecEnv *instance) {}
int main(int argc, char **argv) {
    cxxopts::Options options("MVVM", "Migratable Velocity Virtual Machine, to ship the VM state to another machine");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "The value for epoch value", cxxopts::value<bool>()->default_value("false"))(
        "c,count", "The value for epoch value", cxxopts::value<size_t>()->default_value("0"));
    // Can first discover from the wasi context.
    auto removeExtension = [](std::string &filename) {
        size_t dotPos = filename.find_last_of('.');
        std::string res;
        if (dotPos != std::string::npos) {
            // Extract the substring before the period
            res = filename.substr(0, dotPos);
        } else {
            // If there's no period in the string, it means there's no extension.
            LOGV(ERROR) << "No extension found.";
        }
        return res;
    };

    auto result = options.parse(argc, argv);
    if (result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    auto target = result["target"].as<std::string>();
    auto count = result["count"].as<size_t>();

    snapshot_threshold = count;
    register_sigtrap();

    reader = new FreadStream((removeExtension(target) + ".bin").c_str());
    wamr = new WAMRInstance(target.c_str(), false);
    auto a = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader).value();
    if (!wamr->socket_fd_map_.empty()) { // new ip, old ip // only if tcp requires keepalive
        // tell gateway to stop keep alive the server
        struct sockaddr_in addr {};
        char buf[100];
        int fd = 0;

        // Create a socket
        if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            LOGV(ERROR) << "socket error";
            throw std::runtime_error("socket error");
        }
        addr.sin_family = AF_INET;
        addr.sin_port = htons(MVVM_SOCK_PORT);
        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            exit(EXIT_FAILURE);
        }
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            perror("connect error");
            exit(EXIT_FAILURE);
        }
        // send the fd
        // struct msghdr msg = {0};

        SocketAddrPool src_addr = {.ip4 = {0}, .ip6 = {0}, .is_4 = true, .port = 0};
        SocketAddrPool dest_addr = {.ip4 = {0}, .ip6 = {0}, .is_4 = true, .port = 0};
        struct mvvm_op_data op_data = {.op = MVVM_SOCK_RESUME, .src_addr = src_addr, .dest_addr = dest_addr};
        if (send(fd, &op_data, sizeof(op_data), 0) == -1) {
            perror("send error");
            exit(EXIT_FAILURE);
        }
    }
    wamr->recover(&a);
    return 0;
}