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
#include <cstdio>
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
FwriteStream *writer;
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;
long snapshot_memory = 0;

void serialize_to_file(WASMExecEnv *instance) {}

std::vector<std::vector<size_t>> stack_record;
void unwind(WASMExecEnv *instance) {
    auto cur_frame = (AOTFrame *)instance->cur_frame;
    std::vector<size_t> stack;
    while (cur_frame != nullptr) {
        auto func_index = cur_frame->func_index;
        stack.emplace_back(func_index);
        cur_frame = cur_frame->prev_frame;
    }
    stack_record.emplace_back(stack);
}

void profile_sigint_handler(int sig) {
    wamr->replace_nop_with_int3();
}

void profile_sigtrap_handler(int sig) {
    auto exec_env = wamr->get_exec_env();
    unwind(exec_env);
    wamr->replace_int3_with_nop();
}

void profile_register_sigtrap() {
    struct sigaction sa {};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = profile_sigtrap_handler;
    sa.sa_flags = SA_RESTART;

    // Register the signal handler for SIGTRAP
    if (sigaction(SIGTRAP, &sa, nullptr) == -1) {
        SPDLOG_ERROR("Error: cannot handle SIGTRAP");
        exit(-1);
    }
}

void profile_register_sigint() {
    struct sigaction sa {};
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = profile_sigint_handler;
    sa.sa_flags = SA_RESTART;

    // Register the signal handler for SIGINT
    if (sigaction(SIGINT, &sa, nullptr) == -1) {
        SPDLOG_ERROR("Error: cannot handle SIGINT");
        exit(-1);
    }
}

int main(int argc, char *argv[]) {
    spdlog::cfg::load_env_levels();
    cxxopts::Options options(
        "MVVM_profile",
        "Migratable Velocity Virtual Machine profile part, to find the hotspot function.");
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
        "c,count", "The step index to test execution", cxxopts::value<int>()->default_value("0"))(
        "w,wasm", "The wasm file",  cxxopts::value<std::string>()->default_value(""));
    auto removeExtension = [](std::string &filename) {
        size_t dotPos = filename.find_last_of('.');
        std::string res;
        if (dotPos != std::string::npos) {
            // Extract the substring before the period
            res = filename.substr(0, dotPos);
        } else {
            // If there's no period in the string, it means there's no extension.
            SPDLOG_ERROR("No extension found.");
        }
        return res;
    };
    auto result = options.parse(argc, argv);
    if (result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        exit(EXIT_SUCCESS);
    }
    auto target = result["target"].as<std::string>();
    auto wasm_file = result["wasm"].as<std::string>();
    if (wasm_file.empty()) {
        SPDLOG_ERROR("Please specify the wasm file");
        exit(EXIT_FAILURE);
    }
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
        SPDLOG_DEBUG("Conflict arguments, please choose either count or function");
        exit(EXIT_FAILURE);
    }

    if (arg.size() == 1 && arg[0].empty())
        arg.clear();
    arg.insert(arg.begin(), target);

    for (const auto &e : arg) {
        SPDLOG_DEBUG("arg {}", e);
    }

    writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
    wamr = new WAMRInstance(target.c_str(), is_jit);
    wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
    wamr->instantiate();
    wamr->get_int3_addr();
    wamr->replace_int3_with_nop();
    wamr->replace_mfence_with_nop();

    std::atomic<bool> enable_send_sigint{false};
    auto send_sigint_thread = std::thread([&]() {
        while (true) {
            // sample every 50ms
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (enable_send_sigint.load())
                kill(getpid(), SIGINT);
        }
    });
    send_sigint_thread.detach();

    profile_register_sigint();
    profile_register_sigtrap();

    // get current time
    auto start = std::chrono::high_resolution_clock::now();

    enable_send_sigint.store(true);

    // Main program loop
    wamr->invoke_main();

    enable_send_sigint.store(false);

    // get current time
    auto end = std::chrono::high_resolution_clock::now();
    // get duration in us
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    // print in s
    SPDLOG_INFO("Execution time: {} s", dur.count() / 1000000.0);

    std::map<size_t, size_t> func_count, last_func_count;
    std::map<size_t, std::string> func_name;
    for (const auto &stack : stack_record) {
        if (stack.empty())
            continue;
        last_func_count[stack[0]]++;
        for (auto f : stack) {
            func_count[f]++;
            func_name[f] = "";
        }
    }

    // resolve func_name
    std::vector<size_t> func_idx;
    func_idx.reserve(func_count.size());
    for (const auto &e : func_count) {
        func_idx.emplace_back(e.first);
    }
    auto get_func_name = "python3 /workspaces/MVVM/artifact/get_func_name.py " + wasm_file;
    for (const auto &e : func_idx) {
        get_func_name += " " + std::to_string(e);
    }
    FILE *pipe = popen(get_func_name.c_str(), "r");
    if (!pipe) {
        SPDLOG_ERROR("popen failed!");
        exit(EXIT_FAILURE);
    }
    char buffer[128];
    std::string get_func_name_output;
    while (!feof(pipe)) {
        if (fgets(buffer, 128, pipe) != nullptr)
            get_func_name_output += buffer;
    }
    std::stringstream ss(get_func_name_output);
    for (const auto &e : func_idx) {
        std::string name;
        ss >> name;
        func_name[e] = name;
    }
    pclose(pipe);
    
    // print the result
    std::cout << "Last level function called count\n"
              << "--------------------------------\n"
              << std::endl;
    for (const auto &e : last_func_count) {
        std::cout << std::format("{} {}\n", func_name[e.first], e.second);
    }
    std::cout << std::endl;
    std::cout << "Total function called count\n"
              << "--------------------------\n"
              << std::endl;
    for (const auto &e : func_count) {
        std::cout << std::format("{} {}\n", func_name[e.first], e.second);
    }

    return 0;
}
