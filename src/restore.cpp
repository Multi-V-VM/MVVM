//
// Created by victoryang00 on 4/29/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_read_write.h"
#include "wasm_runtime.h"
#include <cxxopts.hpp>
#include <iostream>
#include <memory>
#include <string>

auto reader = FreadStream("test.bin");
WAMRInstance *wamr = nullptr;
void insert_fd(int fd, const char *path, int flags){};
void remove_fd(int fd) {}
void serialize_to_file(WASMExecEnv *instance) {}
void insert_socket(char const *, int){};
void remove_socket(char const *){};
void insert_lock(char const *, int){};
void insert_sem(char const *, int){};
void remove_lock(char const *){};
void remove_sem(char const *){};

void print_stack(AOTFrame *frame) {
    if (frame) {
        fprintf(stderr, "stack: ");
        for (int *i = (int *)frame->lp; i < (int *)frame->sp; i++) {
            fprintf(stderr, "%d ", *i);
        }
        fprintf(stderr, "\n");
    } else {
        LOGV(ERROR) << fmt::format("no cur_frame");
    }
}

void print_exec_env_debug_info(WASMExecEnv *exec_env) {
    LOGV(INFO) << fmt::format("----");
    if (!exec_env) {
        LOGV(ERROR) << fmt::format("no exec_env");
        return;
    }
    if (exec_env->cur_frame) {
        int call_depth = 0;
        auto p = (AOTFrame *)exec_env->cur_frame;
        while (p) {
            uint32 *frame_lp = p->lp;
            // LOGV(ERROR) << (size_t)((size_t)frame_lp - (size_t)p);
            LOGV(DEBUG) << fmt::format("depth {}, function {}, ip {}, lp {}, sp {}", call_depth, p->func_index,
                                       p->ip_offset, (void *)frame_lp, (void *)p->sp);
            call_depth++;
            print_stack(p);

            p = p->prev_frame;

            if (call_depth > 20)
                exit(-1);
        }
    } else {
        LOGV(ERROR) << fmt::format("no cur_frame");
    }
    LOGV(INFO) << fmt::format("----");
}

void print_memory(WASMExecEnv *exec_env) {
    if (!exec_env)
        return;
    auto module_inst = reinterpret_cast<WASMModuleInstance *>(exec_env->module_inst);
    if (!module_inst)
        return;
    for (size_t j = 0; j < module_inst->memory_count; j++) {
        auto mem = module_inst->memories[j];
        if (mem) {
            LOGV(INFO) << fmt::format("memory data size {}", mem->memory_data_size);
            if (mem->memory_data_size >= 70288 + 64) {
                // for (int *i = (int *)(mem->memory_data + 70288); i < (int *)(mem->memory_data + 70288 + 64); ++i) {
                //     fprintf(stdout, "%d ", *i);
                // }
                for (int *i = (int *)(mem->memory_data); i < (int *)(mem->memory_data_end); ++i) {
                    if (1 <= *i && *i <= 9)
                        fprintf(stdout, "%zu = %d\n", (uint8 *)i - mem->memory_data, *i);
                }
                fprintf(stdout, "\n");
            }
        }
    }
}

void sigtrap_handler(int sig) {
    auto exec_env = wamr->get_exec_env();
    // print_memory(exec_env);
    // print_exec_env_debug_info(exec_env);
}

void register_sigtrap() {
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
}

int main(int argc, char **argv) {
    cxxopts::Options options("MVVM", "Migratable Velocity Virtual Machine, to ship the VM state to another machine");
    options.add_options()("t,target", "The webassembly file to execute",
                          cxxopts::value<std::string>()->default_value("./test/counter.wasm"))(
        "j,jit", "Whether the jit mode or interp mode", cxxopts::value<bool>()->default_value("false"))(
        "h,help", "The value for epoch value", cxxopts::value<bool>()->default_value("false"));
    // Can first discover from the wasi context.

    register_sigtrap();

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