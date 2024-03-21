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

#include "ylt/struct_pack.hpp"
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

ReadStream *reader;
WriteStream *writer;
WAMRInstance *wamr = nullptr;
std::vector<std::unique_ptr<WAMRExecEnv>> as;

int main(int argc, char **argv) {
    spdlog::cfg::load_env_levels();
    cxxopts::Options options("MVVM", "Migratable Velocity Virtual Machine, to ship the VM state to another machine");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "The value for epoch value", cxxopts::value<bool>()->default_value("false"))(
        "i,source_addr", "The next hop to offload", cxxopts::value<std::string>()->default_value(""))(
        "e,source_port", "The next hop port to offload", cxxopts::value<int>()->default_value("0"))(
        "o,offload_addr", "The next hop to offload", cxxopts::value<std::string>()->default_value(""))(
        "s,offload_port", "The next hop port to offload", cxxopts::value<int>()->default_value("0"))(
        "c,count", "The value for epoch value", cxxopts::value<size_t>()->default_value("0"))(
        "r,rdma", "Whether to use RDMA device", cxxopts::value<bool>()->default_value("0"));
    // Can first discover from the wasi context.

    auto result = options.parse(argc, argv);
    if (result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    auto target = result["target"].as<std::string>();
    auto source_addr = result["source_addr"].as<std::string>();
    auto source_port = result["source_port"].as<int>();
    auto offload_addr = result["offload_addr"].as<std::string>();
    auto offload_port = result["offload_port"].as<int>();
    auto count = result["count"].as<size_t>();
    auto rdma = result["rdma"].as<bool>();

    snapshot_threshold = count;
    register_sigtrap();
    register_sigint();
    wamr = new WAMRInstance(target.c_str(), false);
    wamr->instantiate();

    wamr->get_int3_addr();
    wamr->replace_int3_with_nop();
    if (source_addr.empty())
        reader = new FreadStream((removeExtension(target) + ".bin").c_str()); // writer
#if !defined(_WIN32)
#if __linux__
    else if(rdma)
        reader = new RDMAReadStream(source_addr.c_str(), source_port);
#endif
    else
        reader = new SocketReadStream(source_addr.c_str(), source_port);
#endif
    auto a = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader).value();
    if (offload_addr.empty())
        writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
#if !defined(_WIN32)
#if __linux__
    else if(rdma)
        writer = new RDMAWriteStream(offload_addr.c_str(), offload_port);
#endif
    else
        writer = new SocketWriteStream(offload_addr.c_str(), offload_port);
    // is server for all and the is server?
    if (!a[a.size() - 1]
             ->module_inst.wasi_ctx.socket_fd_map.empty()) { // new ip, old ip // only if tcp requires keepalive
        // tell gateway to stop keep alive the server
        struct sockaddr_in addr {};
        int fd = 0;
        bool is_tcp_server;
        SocketAddrPool src_addr = wamr->local_addr;
        SPDLOG_DEBUG("new ip {}.{}.{}.{}:{}", src_addr.ip4[0], src_addr.ip4[1], src_addr.ip4[2], src_addr.ip4[3],
                     src_addr.port);
        // got from wamr
        for (auto &[_, socketMetaData] : a[a.size() - 1]->module_inst.wasi_ctx.socket_fd_map) {
            wamr->op_data.is_tcp |= socketMetaData.type;
            is_tcp_server |= socketMetaData.is_server;
        }
        is_tcp_server &= wamr->op_data.is_tcp;

        wamr->op_data.op = is_tcp_server ? MVVM_SOCK_RESUME_TCP_SERVER : MVVM_SOCK_RESUME;
        wamr->op_data.addr[0][0] = src_addr;

        // Create a socket
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            SPDLOG_ERROR("socket error");
            throw std::runtime_error("socket error");
        }
        addr.sin_family = AF_INET;
        addr.sin_port = htons(MVVM_SOCK_PORT);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &addr.sin_addr) <= 0) {
            SPDLOG_ERROR("Invalid address/ Address not supported");
            exit(EXIT_FAILURE);
        }

        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
            SPDLOG_ERROR("Connection Failed {}", errno);
            exit(EXIT_FAILURE);
        }
        // send the fd

        if (send(fd, &wamr->op_data, sizeof(struct mvvm_op_data), 0) == -1) {
            SPDLOG_ERROR("Send Error");
            exit(EXIT_FAILURE);
        }
        close(fd);
        SPDLOG_ERROR("sent the resume signal");
    }
#endif
    // get current time
    auto start = std::chrono::high_resolution_clock::now();
    // do iptables here
    wamr->recover(&a);
    // get current time
    auto end = std::chrono::high_resolution_clock::now();
    // get duration in us
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // print in s
    SPDLOG_INFO("Execution time: {} s", dur.count() / 1000000.0);
}