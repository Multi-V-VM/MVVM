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
#endif
#include <arpa/inet.h>
#include <sys/socket.h>
// file map, direcotry handle

WAMRInstance *wamr = nullptr;
std::ostringstream re{};
FwriteStream *writer;
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;
void serialize_to_file(WASMExecEnv *instance) {
    // gateway
    if (wamr->addr_.size() != 0) {
        // tell gateway to keep alive the server
        auto convertAddr =[](const char *addr){
            struct sockaddr_in addr_in;
            inet_pton(AF_INET, addr, &addr_in.sin_addr);
            return addr_in.sin_addr.s_addr;
        };
        struct sockaddr_in addr;
        char buf[100];
        int fd = 0;
        int rc;
        struct mvvm_op_data op_data = {.op = MVVM_SOCK_SUSPEND,
                                       .server_ip = convertAddr(wamr->addr_[0]),
                                       .server_port = 0,
                                       .client_ip = 0,
                                       .client_port = 0};

        // Create a socket
        if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
            LOGV(ERROR) << "socket error";
            throw std::runtime_error("socket error");
        }

        // Create a socket
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            std::cerr << "socket error" << std::endl;
            throw std::runtime_error("socket error");
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(MVVM_SOCK_PORT);

        // Convert IPv4 address from text to binary form
        if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &addr.sin_addr) <= 0) {
            perror("Invalid address/ Address not supported");
            exit(EXIT_FAILURE);
        }

        // Connect to the server
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            std::cerr << "Connection Failed" << std::endl;
            exit(EXIT_FAILURE);
        }

        std::cout << "Connected successfully" << std::endl;

        memcpy(buf, &op_data, sizeof(mvvm_op_data));
        rc = send(fd, buf, strlen(buf) + 1, 0);
        if (rc == -1) {
            perror("send error");
            exit(EXIT_FAILURE);
        }

        // Clean up
        close(fd);

        // send the fd
        // struct msghdr msg = {0};
        if (send(fd, &op_data, sizeof(op_data), 0) == -1) {
            perror("send error");
            exit(EXIT_FAILURE);
        }
    }
#if !defined(_WIN32)
    auto cluster = wasm_exec_env_get_cluster(instance);
    // wasm_cluster_suspend_all_except_self(cluster, instance);
    auto all_count = bh_list_length(&cluster->exec_env_list);
    int cur_count = 0;
    if (all_count > 1) {
        auto elem = (WASMExecEnv *)bh_list_first_elem(&cluster->exec_env_list);
        while (elem) {
            if (elem == instance) {
                break;
            }
            cur_count++;
            elem = (WASMExecEnv *)bh_list_elem_next(elem);
        }
    } // gets the element index
#else
    auto all_count = 1;
#endif
    auto a = new WAMRExecEnv();
    dump(a, instance);
    a->cur_count = gettid();

    std::unique_lock as_ul(as_mtx);
    as.emplace_back(a);
    if (as.size() == all_count - 1) {
#if !defined(_WIN32)
        kill(getpid(), SIGINT);
#else
        raise(SIGINT);
#endif
    }
    if (as.size()== all_count){
        struct_pack::serialize_to(*writer, as);
        exit(0);
    }
#if !defined(_WIN32)
    // Is there some better way to sleep until exit?
    std::condition_variable as_cv;
    as_cv.wait(as_ul);
#endif
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
        "p,addr", "The address exposed to WAMR", cxxopts::value<std::vector<std::string>>()->default_value("0.0.0.0/36"))(
        "n,ns_pool", "The ns lookup pool exposed to WAMR",
        cxxopts::value<std::vector<std::string>>()->default_value(""))("h,help", "The value for epoch value",
                                                                       cxxopts::value<bool>()->default_value("false"))(
        "f,function", "The function to test execution",
        cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "c,count", "The function index to test execution", cxxopts::value<int>()->default_value("0"));
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
    auto is_jit = result["jit"].as<bool>();
    auto dir = result["dir"].as<std::vector<std::string>>();
    auto map_dir = result["map_dir"].as<std::vector<std::string>>();
    auto env = result["env"].as<std::vector<std::string>>();
    auto arg = result["arg"].as<std::vector<std::string>>();
    auto addr = result["addr"].as<std::vector<std::string>>();
    auto ns_pool = result["ns_pool"].as<std::vector<std::string>>();
    auto count = result["count"].as<int>();

    if (arg.size() == 1 && arg[0].empty())
        arg.clear();
    arg.insert(arg.begin(), target);

    for (const auto &e : arg) {
        LOGV(INFO) << "arg " << e;
    }
    snapshot_threshold = count;
    register_sigtrap();

    writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();
    wamr->get_int3_addr();
//    wamr->replace_int3_with_nop();

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
        perror("Error: cannot handle SIGINT");
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
    LOGV(INFO) << fmt::format("Execution time: {} s\n", (double)dur.count()/1000000);
    return 0;
}
