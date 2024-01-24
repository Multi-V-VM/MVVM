//
// Created by victoryang00 on 4/8/23.
//

#include "aot_runtime.h"
#include "logging.h"
#include "wamr.h"
#include "wamr_wasi_context.h"
#include "wasm_runtime.h"
#include <condition_variable>
#include <cstdio>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#if !defined(_WIN32)
#include "thread_manager.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

WAMRInstance *wamr = nullptr;
std::ostringstream re{};
FwriteStream *writer;
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;
void serialize_to_file(WASMExecEnv *instance) {
// gateway
#if !defined(_WIN32)
    auto cluster = wasm_exec_env_get_cluster(instance);
    auto all_count = bh_list_length(&cluster->exec_env_list);
    // fill vector

    std::unique_lock as_ul(wamr->as_mtx);
    printf("get lock\n");
    wamr->ready++;
    wamr->lwcp_list[gettid()]++;
    if (wamr->ready == all_count) {
        wamr->should_snapshot_socket = true;
    }
    // If we're not all ready
    printf("thread %d, with %ld ready out of %d total\n", gettid(), wamr->ready, all_count);
    if (!wamr->socket_fd_map_.empty() && wamr->should_snapshot_socket) {
        // tell gateway to keep alive the server
        struct sockaddr_in addr {};
        int fd = 0;
        ssize_t rc;
        SocketAddrPool src_addr{};
        bool is_server = false;
        for (auto [tmp_fd, sock_data] : wamr->socket_fd_map_) {
            if (sock_data.is_server) {
                is_server = true;
                break;
            }
        }
        wamr->op_data.op = is_server ? MVVM_SOCK_SUSPEND_TCP_SERVER : MVVM_SOCK_SUSPEND;

        for (auto [tmp_fd, sock_data] : wamr->socket_fd_map_) {
            int idx = wamr->op_data.size;
            src_addr = sock_data.socketAddress;
            auto tmp_ip4 =
                fmt::format("{}.{}.{}.{}", src_addr.ip4[0], src_addr.ip4[1], src_addr.ip4[2], src_addr.ip4[3]);
            auto tmp_ip6 =
                fmt::format("{}:{}:{}:{}:{}:{}:{}:{}", src_addr.ip6[0], src_addr.ip6[1], src_addr.ip6[2],
                            src_addr.ip6[3], src_addr.ip6[4], src_addr.ip6[5], src_addr.ip6[6], src_addr.ip6[7]);
            if (src_addr.is_4 && tmp_ip4 == "0.0.0.0" || !src_addr.is_4 && tmp_ip6 == "0:0:0:0:0:0:0:0") {
                src_addr = wamr->local_addr;
                src_addr.port = sock_data.socketAddress.port;
            }
            LOGV(INFO) << "addr: "
                       << fmt::format("{}.{}.{}.{}", src_addr.ip4[0], src_addr.ip4[1], src_addr.ip4[2], src_addr.ip4[3])
                       << " port: " << src_addr.port;

            wamr->op_data.addr[idx][0] = src_addr;
            tmp_ip4 = fmt::format("{}.{}.{}.{}", sock_data.socketSentToData.dest_addr.ip.ip4[0],
                                  sock_data.socketSentToData.dest_addr.ip.ip4[1],
                                  sock_data.socketSentToData.dest_addr.ip.ip4[2],
                                  sock_data.socketSentToData.dest_addr.ip.ip4[3]);
            tmp_ip6 = fmt::format(
                "{}:{}:{}:{}:{}:{}:{}:{}", sock_data.socketSentToData.dest_addr.ip.ip6[0],
                sock_data.socketSentToData.dest_addr.ip.ip6[1], sock_data.socketSentToData.dest_addr.ip.ip6[2],
                sock_data.socketSentToData.dest_addr.ip.ip6[3], sock_data.socketSentToData.dest_addr.ip.ip6[4],
                sock_data.socketSentToData.dest_addr.ip.ip6[5], sock_data.socketSentToData.dest_addr.ip.ip6[6],
                sock_data.socketSentToData.dest_addr.ip.ip6[7]);
            if (tmp_ip4 == "0.0.0.0" || tmp_ip6 == "0:0:0:0:0:0:0:0") {
                if (!wamr->op_data.is_tcp) {
                    if (sock_data.socketSentToData.dest_addr.ip.is_4 && tmp_ip4 == "0.0.0.0" ||
                        !sock_data.socketSentToData.dest_addr.ip.is_4 && tmp_ip6 == "0:0:0:0:0:0:0:0") {
                        wamr->op_data.addr[idx][1].is_4 = sock_data.socketRecvFromDatas[0].src_addr.ip.is_4;
                        std::memcpy(wamr->op_data.addr[idx][1].ip4, sock_data.socketRecvFromDatas[0].src_addr.ip.ip4,
                                    sizeof(sock_data.socketRecvFromDatas[0].src_addr.ip.ip4));
                        std::memcpy(wamr->op_data.addr[idx][1].ip6, sock_data.socketRecvFromDatas[0].src_addr.ip.ip6,
                                    sizeof(sock_data.socketRecvFromDatas[0].src_addr.ip.ip6));
                        wamr->op_data.addr[idx][1].port = sock_data.socketRecvFromDatas[0].src_addr.port;
                    } else {
                        wamr->op_data.addr[idx][1].is_4 = sock_data.socketSentToData.dest_addr.ip.is_4;
                        std::memcpy(wamr->op_data.addr[idx][1].ip4, sock_data.socketSentToData.dest_addr.ip.ip4,
                                    sizeof(sock_data.socketSentToData.dest_addr.ip.ip4));
                        std::memcpy(wamr->op_data.addr[idx][1].ip6, sock_data.socketSentToData.dest_addr.ip.ip6,
                                    sizeof(sock_data.socketSentToData.dest_addr.ip.ip6));
                        wamr->op_data.addr[idx][1].port = sock_data.socketSentToData.dest_addr.port;
                    }
                } else {
                    // if it's not socket
                    if (!is_server) {
                        int tmp_fd = 0;
                        unsigned int size_ = sizeof(sockaddr_in);
                        sockaddr_in *ss = (sockaddr_in *)malloc(size_);
                        wamr->invoke_sock_getsockname(tmp_fd, (sockaddr **)&ss, &size_);
                        if (ss->sin_family == AF_INET) {
                            auto *ipv4 = (struct sockaddr_in *)ss;
                            uint32_t ip = ntohl(ipv4->sin_addr.s_addr);
                            wamr->op_data.addr[idx][1].is_4 = true;
                            wamr->op_data.addr[idx][1].ip4[0] = (ip >> 24) & 0xFF;
                            wamr->op_data.addr[idx][1].ip4[1] = (ip >> 16) & 0xFF;
                            wamr->op_data.addr[idx][1].ip4[2] = (ip >> 8) & 0xFF;
                            wamr->op_data.addr[idx][1].ip4[3] = ip & 0xFF;
                            wamr->op_data.addr[idx][1].port = ntohs(ipv4->sin_port);
                        } else {
                            auto *ipv6 = (struct sockaddr_in6 *)ss;
                            wamr->op_data.addr[idx][1].is_4 = false;
                            const auto *bytes = (const uint8_t *)ipv6->sin6_addr.s6_addr;
                            for (int i = 0; i < 16; i += 2) {
                                wamr->op_data.addr[idx][1].ip6[i / 2] = (bytes[i] << 8) + bytes[i + 1];
                            }
                            wamr->op_data.addr[idx][1].port = ntohs(ipv6->sin6_port);
                        }
                        free(ss);
                    } else if (sock_data.is_server) {
                        wamr->op_data.size--;
                    }
                }
            }
            LOGV(INFO) << "dest_addr: "
                       << fmt::format("{}.{}.{}.{}", wamr->op_data.addr[idx][1].ip4[0],
                                      wamr->op_data.addr[idx][1].ip4[1], wamr->op_data.addr[idx][1].ip4[2],
                                      wamr->op_data.addr[idx][1].ip4[3])
                       << " dest_port: " << wamr->op_data.addr[idx][1].port;
            wamr->op_data.size += 1;
        }
        // Create a socket
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            LOGV(ERROR) << "socket error";
            throw std::runtime_error("socket error");
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(MVVM_SOCK_PORT);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &addr.sin_addr) <= 0) {
            LOGV(ERROR) << "AF_INET not supported";
            exit(EXIT_FAILURE);
        }
        // Connect to the server
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            LOGV(ERROR) << "Connection Failed " << errno;
            exit(EXIT_FAILURE);
        }

        LOGV(INFO) << "Connected successfully";
        rc = send(fd, &wamr->op_data, sizeof(struct mvvm_op_data), 0);
        if (rc == -1) {
            LOGV(ERROR) << "send error";
            exit(EXIT_FAILURE);
        }

        // Clean up
        close(fd);
    }
    if (wamr->ready < all_count) {
        // Then wait for someone else to get here and finish the job
        std::condition_variable as_cv;
        as_cv.wait(as_ul);
    }
    // If we're all ready
    // double check
    {
        auto ready_count = 0;
        for (auto [k, v] : wamr->lwcp_list) {
            printf("%ld: %d\n", k, v);
            if (v > 0)
                ready_count++;
        }
        if (ready_count != all_count) {
            printf("we have a discrepancy between ready count and number of threads that say they are\n");
            printf("ready: %d, all: %d\n", ready_count, all_count);
            // not actually ready
            std::condition_variable as_cv;
            as_cv.wait(as_ul);
        }
    }
    // wasm_cluster_suspend_all_except_self(cluster, instance);
    auto elem = (WASMExecEnv *)bh_list_first_elem(&cluster->exec_env_list);
    while (elem) {
        instance = elem;
#endif // windows has no threads so only does it once
        auto a = new WAMRExecEnv();
        dump(a, instance);
        as.emplace_back(a);
#if !defined(_WIN32)
        elem = (WASMExecEnv *)bh_list_elem_next(elem);
    }
    // finish filling vector
#endif

    struct_pack::serialize_to(*writer, as);
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    cxxopts::Options options(
        "MVVM_checkpoint",
        "Migratable Velocity Virtual Machine checkpoint part, to ship the VM state to another machine.");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "d,dir", "The directory list exposed to WAMR", cxxopts::value<std::vector<std::string>>()->default_value("./"))(
        "m,map_dir", "The mapped directory list exposed to WAMRe",
        cxxopts::value<std::vector<std::string>>()->default_value(""))(
        "e,env", "The environment list exposed to WAMR",
        cxxopts::value<std::vector<std::string>>()->default_value("a=b"))(
        "a,arg", "The arg list exposed to WAMR", cxxopts::value<std::vector<std::string>>()->default_value(""))(
        "p,addr", "The address exposed to WAMR",
        cxxopts::value<std::vector<std::string>>()->default_value("0.0.0.0/36"))(
        "n,ns_pool", "The ns lookup pool exposed to WAMR",
        cxxopts::value<std::vector<std::string>>()->default_value(""))("h,help", "The value for epoch value",
                                                                       cxxopts::value<bool>()->default_value("false"))(
        "i,is_debug", "The value for is_debug value", cxxopts::value<bool>()->default_value("false"))(
        "f,function", "The function index to test execution", cxxopts::value<int>()->default_value("0"))(
        "x,function_count", "The function count to stop", cxxopts::value<int>()->default_value("0"))(
        "c,count", "The step index to test execution", cxxopts::value<int>()->default_value("0"));
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
        exit(EXIT_SUCCESS);
    }
    auto target = result["target"].as<std::string>();
    auto is_jit = result["jit"].as<bool>();
    auto dir = result["dir"].as<std::vector<std::string>>();
    auto map_dir = result["map_dir"].as<std::vector<std::string>>();
    auto env = result["env"].as<std::vector<std::string>>();
    auto arg = result["arg"].as<std::vector<std::string>>();
    auto addr = result["addr"].as<std::vector<std::string>>();
    auto ns_pool = result["ns_pool"].as<std::vector<std::string>>();
    snapshot_threshold = result["count"].as<int>();
    stop_func_threshold = result["function_count"].as<int>();
    is_debug = result["is_debug"].as<bool>();
    stop_func_index = result["function"].as<int>();
    if (snapshot_threshold != 0 && stop_func_index != 0) {
        LOGV(ERROR) << "Conflict arguments, please choose either count or function";
        exit(EXIT_FAILURE);
    }

    if (arg.size() == 1 && arg[0].empty())
        arg.clear();
    arg.insert(arg.begin(), target);

    for (const auto &e : arg) {
        LOGV(INFO) << "arg " << e;
    }
    register_sigtrap();

    writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();
    wamr->get_int3_addr();
    wamr->replace_int3_with_nop();

    // freopen("output.txt", "w", stdout);
#if defined(_WIN32)
    // Define the sigaction structure
    signal(SIGINT, sigint_handler);
#else
    // Define the sigaction structure
    struct sigaction sa {};

    // Clear the structure
    sigemptyset(&sa.sa_mask);

    // Set the signal handler function
    sa.sa_handler = sigint_handler;

    // Set the flags
    sa.sa_flags = SA_RESTART;

    // Register the signal handler for SIGINT
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        LOGV(ERROR) << "Error: cannot handle SIGINT";
        return 1;
    }
#endif
    // get current time
    auto start = std::chrono::high_resolution_clock::now();

    // Main program loop
    wamr->invoke_main();

    // get current time
    auto end = std::chrono::high_resolution_clock::now();
    // get duration in us
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // print in s
    LOGV(INFO) << fmt::format("Execution time: {} s\n", (double)dur.count() / 1000000);
    return 0;
}
