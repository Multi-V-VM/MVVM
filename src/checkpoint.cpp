//
// Created by victoryang00 on 4/8/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wasm_exec_env.h"
#include "wasm_module_instance.h"
#include "wamr.h"
#include <csignal>
#include <cstdio>
#include <unistd.h>

struct fwrite_stream {
    FILE *file;
    bool write(const char *data, std::size_t sz) { return fwrite(data, sz, 1, file) == 1; }
    fwrite_stream(const char *file_name) : file(fopen(file_name, "wb")) {}
    ~fwrite_stream() { fclose(file); }
};

auto writer = fwrite_stream("test.bin");
auto writer1 = fwrite_stream("test1.bin");

// Signal handler function for SIGINT
void sigint_handler(int sig) {
    // Your logic here
    printf("Caught signal %d, performing custom logic...\n", sig);
    // You can exit the program here, if desired
    struct WAMRExecEnv a[10];
    struct WAMRModuleInstance b[10];
    struct_pack::serialize_to(writer, a);
    struct_pack::serialize_to(writer1, b);
    exit(0);
}

int main() {
    // Define the sigaction structure
    struct sigaction sa;

    // Clear the structure
    sigemptyset(&sa.sa_mask);

    // Set the signal handler function
    sa.sa_handler = sigint_handler;

    // Set the flags
    sa.sa_flags = SA_RESTART;

    // Register the signal handler for SIGINT
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("Error: cannot handle SIGINT");
        return 1;
    }

    // Main program loop
    auto wamr = new WAMRInstance("test.wasm");
    return 0;
}