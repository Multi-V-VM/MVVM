//
// Created by victoryang00 on 4/8/23.
//

#include "logging.h"
#include "thread_manager.h"
#include "wamr.h"
#include <cstdio>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
// file map, direcotry handle

WAMRInstance *wamr = nullptr;
std::ostringstream re{};
auto writer = FwriteStream("test.bin");
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;
/**fopen, fseek*/
void insert_fd(int fd, const char *path, int flags) {
    printf("\n #insert_fd(fd,filename,flags) %d %s %d \n\n", fd, path, flags);

    if (wamr->fd_map_.find(fd) != wamr->fd_map_.end()) {
        LOGV(ERROR) << "fd already exist" << fd;
        wamr->fd_map_[fd] = std::make_pair(std::string(path), flags);
    } else
        wamr->fd_map_.insert(std::make_pair(fd, std::make_pair(std::string(path), flags)));
}

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
    std::string current_str;
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
                insert_fd(std::stoi(fd_output), filename_output.c_str(), std::stoi(flags_output));
            }
        }
    }
    stdoutput.close();

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
    fprintf(stderr, "Caught signal %d, performing custom logic...\n", sig);

    // fprintf(stderr, "Do nothing but exit -1\n");
    // exit(-1);

    // You can exit the program here, if desired
    // serialize_to_file(this->exec_env);
}

// Signal handler function for SIGINT
void sigint_handler(int sig) {
    // Your logic here
    fprintf(stderr, "Caught signal %d, performing custom logic...\n", sig);
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
    } else {
        LOGV_DEBUG << "SIGTRAP registered";
    }

    auto module = wamr->get_module();
    auto code = (unsigned char *)module->code;
    auto code_size = module->code_size;
    
    LOGV_DEBUG << "Replacing nop to int 3";
    auto arch = ArchType::x86_64;

    LOGV_DEBUG << "Making the code section writable";
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_WRITE;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }

    for (auto addr : wamr->mvvm_aot_metadatas.at(arch).nops) {
        if (code[addr] != 0x90) {
            LOGV_FATAL << "code at " << addr << " is not nop(0x90)";
        } else {
            code[addr] = 0xcc; // int 3
        }
    }
    LOGV_DEBUG << "Complete replacing";

    LOGV_DEBUG << "Making the code section executable";
    {
        int map_prot = MMAP_PROT_READ | MMAP_PROT_EXEC;

        uint8 *mmap_addr = module->literal - sizeof(uint32);
        uint32 total_size = sizeof(uint32) + module->literal_size + module->code_size;
        os_mprotect(mmap_addr, total_size, map_prot);
    }

    LOGV_DEBUG << "Exit sigint handler";
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

    auto mvvm_meta_file = target + ".mvvm";
    if (!std::filesystem::exists(mvvm_meta_file)) {
        printf("MVVM metadata file %s does not exists. Exit.\n", mvvm_meta_file.c_str());
        return -1;
    }

    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();
    wamr->load_mvvm_aot_metadata(mvvm_meta_file.c_str());

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