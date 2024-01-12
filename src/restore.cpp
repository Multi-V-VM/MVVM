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
FwriteStream *writer;
WAMRInstance *wamr = nullptr;
bool is_debug = false;
int stop_func_index;

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
    if (!a[0]->module_inst.wasi_ctx.socket_fd_map.empty()) { // new ip, old ip // only if tcp requires keepalive
        // tell gateway to stop keep alive the server
        struct sockaddr_in addr {};
        int fd = 0;

        SocketAddrPool src_addr = {.ip4 = {0}, .ip6 = {0}, .is_4 = true, .port = 0}; // get current ip
#if !defined(_WIN32)
        struct ifaddrs *ifaddr, *ifa;

        if (getifaddrs(&ifaddr) == -1) {
            LOGV(ERROR) << "getifaddrs";
            exit(EXIT_FAILURE);
        }

        for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr)
                continue;

            if (ifa->ifa_addr->sa_family == AF_INET && src_addr.is_4) {
                // IPv4
                auto *ipv4 = (struct sockaddr_in *)ifa->ifa_addr;
                uint32_t ip = ntohl(ipv4->sin_addr.s_addr);
                if (is_ip_in_cidr(MVVM_SOCK_ADDR, MVVM_SOCK_MASK, ip)) {
                    // Extract IPv4 address
                    src_addr.ip4[0] = (ip >> 24) & 0xFF;
                    src_addr.ip4[1] = (ip >> 16) & 0xFF;
                    src_addr.ip4[2] = (ip >> 8) & 0xFF;
                    src_addr.ip4[3] = ip & 0xFF;
                }

            } else if (ifa->ifa_addr->sa_family == AF_INET6 && !src_addr.is_4) {
                // IPv6
                auto *ipv6 = (struct sockaddr_in6 *)ifa->ifa_addr;
                src_addr.is_4 = false;
                // Extract IPv6 address
                const auto *bytes = (const uint8_t *)ipv6->sin6_addr.s6_addr;
                if (is_ipv6_in_cidr(MVVM_SOCK_ADDR6, MVVM_SOCK_MASK6, &ipv6->sin6_addr)) {
                    for (int i = 0; i < 16; i += 2) {
                        src_addr.ip6[i / 2] = (bytes[i] << 8) + bytes[i + 1];
                    }
                }
            }
        }

        freeifaddrs(ifaddr);
#endif
        LOGV(INFO) << "new ip is "
                   << fmt::format("{}.{}.{}.{}", src_addr.ip4[0], src_addr.ip4[1], src_addr.ip4[2], src_addr.ip4[3]);
        wamr->op_data.op = MVVM_SOCK_RESUME;
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
        // struct msghdr msg = {0};

        if (send(fd, &wamr->op_data, sizeof(struct mvvm_op_data), 0) == -1) {
            LOGV(ERROR) << "Send Error";
            exit(EXIT_FAILURE);
        }
        close(fd);
    }
    wamr->recover(&a);
    return 0;
}