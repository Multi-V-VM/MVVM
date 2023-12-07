//
// Created by victoryang00 on 4/8/23.
//

#if WASM_ENABLE_AOT != 0
#include "aot_runtime.h"
#endif
#include "logging.h"
#include "thread_manager.h"
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
// file map, direcotry handle

WAMRInstance *wamr = nullptr;
std::ostringstream re{};
FwriteStream *writer;
std::vector<std::unique_ptr<WAMRExecEnv>> as;
extern const char *func_to_stop;
extern int func_to_stop_count;
// mini dumper
void dump_tls(WASMModule *module, WASMModuleInstanceExtra *instance) {
    WASMGlobal *aux_data_end_global = NULL, *aux_heap_base_global = NULL;
    WASMGlobal *aux_stack_top_global = NULL, *global;
    uint32 aux_data_end = (uint32)-1, aux_heap_base = (uint32)-1;
    uint32 aux_stack_top = (uint32)-1, global_index, func_index, i;
    uint32 aux_data_end_global_index = (uint32)-1;
    uint32 aux_heap_base_global_index = (uint32)-1;
    /* Resolve aux stack top global */
    if (wamr->is_aot) {
        for (int global_index = 0; global_index < instance->global_count; global_index++) {
            auto global = ((AOTModule*)module)->globals + global_index;
            if (global->is_mutable /* heap_base and data_end is
                                              not mutable */
                && global->type == VALUE_TYPE_I32 && global->init_expr.init_expr_type == INIT_EXPR_TYPE_I32_CONST &&
                (uint32)global->init_expr.u.i32 <= aux_heap_base) {
                // LOGV(INFO) << "TLS" << global->init_expr << "\n";
                // this is not the place been accessed, but initialized.
            } else {
                break;
            }
        }
    } else {
        for (int global_index = 0; global_index < instance->global_count; global_index++) {
            auto global = module->globals + global_index;
            if (global->is_mutable /* heap_base and data_end is
                                              not mutable */
                && global->type == VALUE_TYPE_I32 && global->init_expr.init_expr_type == INIT_EXPR_TYPE_I32_CONST &&
                (uint32)global->init_expr.u.i32 <= aux_heap_base) {
                // LOGV(INFO) << "TLS" << global->init_expr << "\n";
                // this is not the place been accessed, but initialized.
            } else {
                break;
            }
        }
    }
}

WAMRExecEnv* to_wamr_execenv(int tid, WASMExecEnv* instance){
    auto a = new WAMRExecEnv();
    dump(a, instance);
    a->cur_count = tid;
    return a;
}
void serialize_to_file(WASMExecEnv *instance) {
    // gateway
    if (wamr->addr_.size() != 0) {
        // tell gateway to keep alive the server
    }
    auto cluster = wasm_exec_env_get_cluster(wamr->exec_env);
    auto all_count = bh_list_length(&cluster->exec_env_list);
    std::unique_lock as_ul(wamr->as_mtx);
    printf("get lock\n");
    wamr->ready++;
    //If we're not all ready
    if(wamr->ready < all_count){
        printf("thread %d, with %d ready out of %d total\n", gettid(), wamr->ready, all_count);
	// Then wait for someone else to get here and finish the job
        std::condition_variable as_cv;
        as_cv.wait(as_ul);
    }
    // Everyone is ready to be serialized
    int cur_count = 0;
    auto elem = (WASMExecEnv *)bh_list_first_elem(&cluster->exec_env_list);
    while (elem) {
        dump_tls(((WASMModuleInstance *)elem->module_inst)->module, ((WASMModuleInstance *)elem->module_inst)->e);
        auto a = to_wamr_execenv(cur_count, elem);
        as.emplace_back(a);
        cur_count++;
        elem = (WASMExecEnv *)bh_list_elem_next(elem);
    }
    struct_pack::serialize_to(*writer, as);
    exit(0);
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
        "p,addr", "The address exposed to WAMR", cxxopts::value<std::vector<std::string>>()->default_value(""))(
        "n,ns_pool", "The ns lookup pool exposed to WAMR",
        cxxopts::value<std::vector<std::string>>()->default_value(""))("h,help", "The value for epoch value",
                                                                       cxxopts::value<bool>()->default_value("false"))(
        "f,function", "The function to test execution",
        cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "c,count", "The function index to test execution", cxxopts::value<size_t>()->default_value("0"));
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
    auto count = result["count"].as<size_t>();

    if (arg.size() == 1 && arg[0].empty())
        arg.clear();
    arg.insert(arg.begin(), target);

    for (const auto &e : arg) {
        LOGV(INFO) << "arg " << e;
    }
    snapshot_threshold = count;
    register_sigtrap();

    func_to_stop = result["function"].as<std::string>().c_str();
    func_to_stop_count = result["count"].as<int>();
    writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();
    wamr->get_int3_addr();
//    wamr->replace_int3_with_nop();

    // freopen("output.txt", "w", stdout);

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
    // get current time
    auto start = std::chrono::high_resolution_clock::now();

    // Main program loop
    wamr->invoke_main();

    // get current time
    auto end = std::chrono::high_resolution_clock::now();
    // get duration in us
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // print in s
    std::cout << fmt::format("Execution time: {} s\n", (double)dur.count()/1000000);
    return 0;
}
