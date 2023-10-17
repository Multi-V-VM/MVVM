//
// Created by victoryang00 on 4/29/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_read_write.h"
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <string>

auto reader = FreadStream("test.bin");
WAMRInstance *wamr = nullptr;
void insert_sock_open_data(uint32_t poolfd, int af, int socktype, uint32_t *sockfd) {}

void insert_sock_send_to_data(uint32_t sock, const iovec_app1_t *si_data, uint32 si_data_len, uint16_t si_flags,
                              const __wasi_addr_t *dest_addr, uint32 *so_data_len) {}

void insert_sock_recv_from_data(uint32_t sock, iovec_app1_t *ri_data, uint32 ri_data_len, uint16_t ri_flags,
                                __wasi_addr_t *src_addr, uint32 *ro_data_len) {}
void insert_fd(int fd, const char *path, int flags, int offset){};
void remove_fd(int fd) {}
void serialize_to_file(WASMExecEnv *instance) {}
void insert_socket(char const *, int){};
void remove_socket(char const *){};
void insert_lock(char const *, int){};
void insert_sem(char const *, int){};
void remove_lock(char const *){};
void remove_sem(char const *){};
int main(int argc, char **argv) {
    cxxopts::Options options("MVVM", "Migratable Velocity Virtual Machine, to ship the VM state to another machine");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "The value for epoch value", cxxopts::value<bool>()->default_value("false"));
    // Can first discover from the wasi context.

    auto result = options.parse(argc, argv);
    if (result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        exit(0);
    }
    auto target = result["target"].as<std::string>();
    wamr = new WAMRInstance(target.c_str(), false);
    auto a = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(reader).value();
    wamr->instantiate();
    wamr->recover(&a);
    return 0;
}