//
// Created by victoryang00 on 4/8/23.
//

#include "thread_manager.h"
#include "wamr.h"
// file map, direcotry handle

auto wamr = new WAMRInstance("./test/multi-thread.wasm");
auto writer = FwriteStream("test.bin");
std::vector<std::unique_ptr<WAMRExecEnv>> as;
std::mutex as_mtx;

void serialize_to_file(WASMExecEnv *instance) {
    /** Sounds like AoT/JIT is in this?*/
    //    auto curr_instance = instance;
    //    int cur_count =0;
    //    while (curr_instance != nullptr) {
    //        auto a = new WAMRExecEnv();
    //        dump(a, curr_instance);
    //
    //        as.emplace_back(a);
    //        curr_instance = curr_instance->next;
    //        cur_count++;
    //    }
    //    curr_instance = instance->prev;
    //    while (curr_instance != nullptr) {
    //        auto a = new WAMRExecEnv();
    //        dump(a, curr_instance);
    //        as.emplace_back(a);
    //        curr_instance = curr_instance->prev;
    //    }
    auto cluster =wasm_exec_env_get_cluster(instance);
    if (cluster) {
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

        as.emplace_back(a);
        as.back().get()->cur_count = cur_count;
        if (as.size() == all_count - 1) {
            struct_pack::serialize_to(writer, as);
        }
    }else{
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
    std::vector<std::unique_ptr<WAMRExecEnv>> a;
    dump(&a, wamr->get_exec_env());
    struct_pack::serialize_to(writer, a);
    exit(0);
}

// Signal handler function for SIGINT
void sigint_handler(int sig) {
    // Your logic here
    printf("Caught signal %d, performing custom logic...\n", sig);
    wamr->get_exec_env()->is_checkpoint = true;
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
int main() {
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