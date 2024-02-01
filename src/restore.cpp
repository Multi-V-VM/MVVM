//
// Created by victoryang00 on 4/29/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_export.h"
#include "wamr_read_write.h"
#include "wasm_runtime.h"
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <string>
#if !defined(_WIN32)
#include <arpa/inet.h>
#endif

FreadStream *reader;
FwriteStream *writer;
WAMRInstance *wamr = nullptr;

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
    wamr->time = std::chrono::high_resolution_clock::now();
    auto a = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader).value();
    // is server for all and the is server?
#if !defined(_WIN32)
    if (!a[a.size() - 1]
             ->module_inst.wasi_ctx.socket_fd_map.empty()) { // new ip, old ip // only if tcp requires keepalive
        // tell gateway to stop keep alive the server
        struct sockaddr_in addr {};
        int fd = 0;
        bool is_tcp_server;
        SocketAddrPool src_addr = wamr->local_addr;
        LOGV(INFO) << "new ip is "
                   << fmt::format("{}.{}.{}.{}:{}", src_addr.ip4[0], src_addr.ip4[1], src_addr.ip4[2], src_addr.ip4[3],
                                  src_addr.port);
        // got from wamr
        for (auto &[fd, socketMetaData] : a[a.size() - 1]->module_inst.wasi_ctx.socket_fd_map) {
            wamr->op_data.is_tcp |= socketMetaData.type;
            is_tcp_server |= socketMetaData.is_server;
        }
        is_tcp_server &= wamr->op_data.is_tcp;

        wamr->op_data.op = is_tcp_server ? MVVM_SOCK_RESUME_TCP_SERVER : MVVM_SOCK_RESUME;
        wamr->op_data.addr[0][0] = src_addr;

        // Create a socket
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            LOGV(ERROR) << "socket error";
            throw std::runtime_error("socket error");
        }
        addr.sin_family = AF_INET;
        addr.sin_port = htons(MVVM_SOCK_PORT);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &addr.sin_addr) <= 0) {
            LOGV(ERROR) << "Invalid address/ Address not supported";
            exit(EXIT_FAILURE);
        }

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            LOGV(ERROR) << "Connection Failed " << errno;
            exit(EXIT_FAILURE);
        }
        // send the fd

        if (send(fd, &wamr->op_data, sizeof(struct mvvm_op_data), 0) == -1) {
            LOGV(ERROR) << "Send Error";
            exit(EXIT_FAILURE);
        }
        close(fd);
        LOGV(ERROR) << "sent the resume signal";
    }
#endif
    // do iptables here
    wamr->recover(&a);
    return 0;
}