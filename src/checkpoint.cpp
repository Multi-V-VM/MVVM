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

#include "aot_runtime.h"
#include "wamr.h"
#include <cxxopts.hpp>
#include <sstream>
#include <string>
#include <thread>
#if WASM_ENABLE_LIB_PTHREAD != 0
#include "thread_manager.h"
#endif
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

WAMRInstance *wamr = nullptr;
std::ostringstream re{};
WriteStream *writer;
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;
long snapshot_memory = 0;

int main(int argc, char *argv[]) {
    spdlog::cfg::load_env_levels();
    cxxopts::Options options(
        "MVVM_checkpoint",
        "Migratable Velocity Virtual Machine checkpoint part, to ship the VM state to another machine.");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "d,dir", "The directory list exposed to WAMR", cxxopts::value<std::vector<std::string>>()->default_value("./"))(
        "m,map_dir", "The mapped directory list exposed to WAMR",
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
        "o,offload_addr", "The next hop to offload", cxxopts::value<std::string>()->default_value(""))(
        "s,offload_port", "The next hop port to offload", cxxopts::value<int>()->default_value("0"))(
        "c,count", "The step index to test execution", cxxopts::value<int>()->default_value("0"));

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
    auto offload_addr = result["offload_addr"].as<std::string>();
    auto offload_port = result["offload_port"].as<int>();
    auto ns_pool = result["ns_pool"].as<std::vector<std::string>>();
    snapshot_threshold = result["count"].as<int>();
    stop_func_threshold = result["function_count"].as<int>();
    is_debug = result["is_debug"].as<bool>();
    stop_func_index = result["function"].as<int>();
    if (snapshot_threshold != 0 && stop_func_index != 0) {
        SPDLOG_DEBUG("Conflict arguments, please choose either count or function");
        exit(EXIT_FAILURE);
    }

    if (arg.size() == 1 && arg[0].empty())
        arg.clear();
    arg.insert(arg.begin(), target);

    for (const auto &e : arg) {
        SPDLOG_DEBUG("arg {}", e);
    }
    register_sigtrap();
    register_sigint();
    if (offload_addr.empty())
        writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
#ifndef _WIN32
    else
        writer = new SocketWriteStream(offload_addr.c_str(), offload_port);
#endif
    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();
    wamr->get_int3_addr();
    wamr->replace_int3_with_nop();

    // get current time
    auto start = std::chrono::high_resolution_clock::now();

    // Main program loop
    wamr->invoke_main();

    // get current time
    auto end = std::chrono::high_resolution_clock::now();
    // get duration in us
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // print in s
    SPDLOG_INFO("Execution time: {} s", dur.count() / 1000000.0);
    return 0;
}
