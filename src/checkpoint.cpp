//
// Created by victoryang00 on 4/8/23.
//

#include "thread_manager.h"
#include "wamr.h"
#include "wamr_wasi_context.h"
#include "wasm_runtime.h"
#include <condition_variable>
#include <cstdio>
#include <cxxopts.hpp>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
// file map, direcotry handle

WAMRInstance *wamr = nullptr;
std::ostringstream re{};
FwriteStream *writer;
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;
// mini dumper
void dump_tls(WASMModule *module, WASMModuleInstanceExtra *instance) {
WASMGlobal *aux_data_end_global = NULL, *aux_heap_base_global = NULL;
WASMGlobal *aux_stack_top_global = NULL, *global;
uint32 aux_data_end = (uint32)-1, aux_heap_base = (uint32)-1;
uint32 aux_stack_top = (uint32)-1, global_index, func_index, i;
uint32 aux_data_end_global_index = (uint32)-1;
uint32 aux_heap_base_global_index = (uint32)-1;
/* Resolve aux stack top global */
for (int global_index = 0; global_index < instance->global_count; global_index++) {
auto global = module->globals + global_index;
if (global->is_mutable /* heap_base and data_end is
                                  not mutable */
&& global->type == VALUE_TYPE_I32 && global->init_expr.init_expr_type == INIT_EXPR_TYPE_I32_CONST &&
(uint32)global->init_expr.u.i32 <= aux_heap_base) {
//        LOGV(INFO) << "TLS" << global->init_expr << "\n";
// this is not the place been accessed, but initialized.
} else {
break;
}
}
}
void serialize_to_file(WASMExecEnv *instance) {
    /** Sounds like AoT/JIT is in this?*/
    // Note: insert fd
    std::ifstream stdoutput;
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

    auto cluster = wasm_exec_env_get_cluster(instance);
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
    auto a = new WAMRExecEnv();
    dump_tls(((WASMModuleInstance *)instance->module_inst)->module, ((WASMModuleInstance *)instance->module_inst)->e);
    dump(a, instance);
    std::unique_lock as_ul(as_mtx);
    as.emplace_back(a);
    as.back().get()->cur_count = cur_count;
    if (as.size() == all_count) {
        struct_pack::serialize_to(*writer, as);
        LOGV(INFO) << "serialize to file" << cur_count << " " << all_count << "\n";
        exit(0);
    }
    // Is there some better way to sleep until exit?
    std::condition_variable as_cv;
    as_cv.wait(as_ul);
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
    writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();

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
