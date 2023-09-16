//
// Created by victoryang00 on 4/8/23.
//

#include "thread_manager.h"
#include "wamr.h"
#include <cstdio>
#include <cxxopts.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
// file map, direcotry handle

WAMRInstance *wamr = nullptr;
std::ostringstream re{};
auto writer = FwriteStream("test.bin");
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;
/**fopen, fseek*/
void insert_fd(int fd, const char *path, int flags, int offset) {
    if (fd > 2) {
        printf("\n #insert_fd(fd,filename,flags, offset) %d %s %d %d \n\n", fd, path, flags, offset);

        if (wamr->fd_map_.find(fd) != wamr->fd_map_.end()) {
            LOGV(ERROR) << "fd already exist" << fd;
            if (offset == 0) {
                // fOpen call
                std::string curPath;
                int curFlags;
                int curOffset;
                std::tie(curPath, curFlags, curOffset) = wamr->fd_map_[fd];
                wamr->fd_map_[fd] = std::make_tuple(std::string(path), flags, curOffset);
            } else {
                // fSeek call
                std::string curPath;
                int curFlags;
                int curOffset;
                std::tie(curPath, curFlags, curOffset) = wamr->fd_map_[fd];
                wamr->fd_map_[fd] = std::make_tuple(curPath, curFlags, offset);
            }

        } else
            wamr->fd_map_.insert(std::make_pair(fd, std::make_tuple(std::string(path), flags, offset)));
    }
}

/* update fd->offset**/
void insert_fd_fseek();
/**fclose */
void remove_fd(int fd) {
    if (wamr->fd_map_.find(fd) != wamr->fd_map_.end())
        wamr->fd_map_.erase(fd);
    else
        LOGV(ERROR) << "fd not found" << fd;
}
void insert_socket(int fd) {}
void serialize_to_file(WASMExecEnv *instance) {
    /** Sounds like AoT/JIT is in this?*/
    // Note: insert fd
    std::ifstream stdoutput;
    stdoutput.open("output.txt");
std:
    string current_str;
    std::string fd_output;
    std::string filename_output;
    std::string flags_output;

    if (stdoutput.is_open()) {
        while (stdoutput.good()) {
            stdoutput >> current_str;
            if (current_str == "fopen_test(fd,filename,flags)") {
                stdoutput >> fd_output;
                stdoutput >> filename_output;
                stdoutput >> flags_output;
                insert_fd(std::stoi(fd_output), filename_output.c_str(), std::stoi(flags_output), 0);
            }
        }
    }
    stdoutput.close();

    //
    std::cout << "dasfasdfasf" << re.str() << "dasfasdfasf\n";
    auto cluster = wasm_exec_env_get_cluster(instance);
    if (bh_list_length(&cluster->exec_env_list) > 1) {
        auto elem = (WASMExecEnv *)bh_list_first_elem(&cluster->exec_env_list);
        int cur_count = 0;
        while (elem) {
            if (elem == instance) {
                break;
            }
            cur_count++;
            elem = (WASMExecEnv *)bh_list_elem_next(elem);
        }
        auto all_count = bh_list_length(&cluster->exec_env_list);
        auto a = new WAMRExecEnv();
        dump(a, instance);
        as_mtx.lock();
        as.emplace_back(a);
        as.back().get()->cur_count = cur_count;
        if (as.size() == all_count) {
            struct_pack::serialize_to(writer, as);
            LOGV(INFO) << "serialize to file" << cur_count << " " << all_count << "\n";
        }
        as_mtx.unlock();
        if (cur_count == 0)
            std::this_thread::sleep_for(std::chrono::seconds(100));
    } else {
        auto a = new WAMRExecEnv();
        dump(a, instance);
        as.emplace_back(a);
        as.back().get()->cur_count = 0;
        struct_pack::serialize_to(writer, as);
    }

    exit(0);
}

#ifndef MVVM_DEBUG
void sigtrap_handler(int sig) {
    printf("Caught signal %d, performing custom logic...\n", sig);

    // You can exit the program here, if desired
    serialize_to_file(this->exec_env);
}

// Signal handler function for SIGINT
void sigint_handler(int sig) {
    // Your logic here
    printf("Caught signal %d, performing custom logic...\n", sig);
    wamr->get_exec_env()->is_checkpoint = true;
    // check whether the current function is sleep
    struct sigaction sa {};

    // Clear the structure
    sigemptyset(&sa.sa_mask);

    // Set the signal handler function
    sa.sa_handler = sigtrap_handler;

    // Set the flags
    sa.sa_flags = SA_RESTART;

    // Register the signal handler for SIGTRAP
    if (sigaction(SIGTRAP, &sa, nullptr) == -1) {
        perror("Error: cannot handle SIGTRAP");
        exit(-1);
    }
}
#endif
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
        "p,addr", "The address exposed to WAMR", cxxopts::value<std::vector<std::string>>()->default_value(""))(
        "n,ns_pool", "The ns lookup pool exposed to WAMR",
        cxxopts::value<std::vector<std::string>>()->default_value(""))("h,help", "The value for epoch value",
                                                                       cxxopts::value<bool>()->default_value("false"));

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
    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();
    freopen("output.txt", "w", stdout);

#ifndef MVVM_DEBUG
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
    // Main program loop
    wamr->invoke_main();
    return 0;
}