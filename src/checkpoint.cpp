//
// Created by victoryang00 on 4/8/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_module_instance.h"
#include "wasm_exec_env.h"
#include <csignal>
#include <cstdio>
#include <unistd.h>

struct fwrite_stream {
    FILE *file;
    bool write(const char *data, std::size_t sz) const { return fwrite(data, sz, 1, file) == 1; }
    explicit fwrite_stream(const char *file_name) : file(fopen(file_name, "wb")) {}
    ~fwrite_stream() { fclose(file); }
};

auto writer = fwrite_stream("test.bin");
auto wamr = new WAMRInstance("test.wasm");

// Signal handler function for SIGINT
void sigint_handler(int sig) {
    // Your logic here
    printf("Caught signal %d, performing custom logic...\n", sig);

    // You can exit the program here, if desired
    struct WAMRExecEnv<1,65534,8192,200,10,200> a;
    dump(&a, wamr->get_exec_env());
    struct_pack::serialize_to(writer, a);
    exit(0);
}

int main() {
    // Define the sigaction structure
    struct sigaction sa{};

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

    // Main program loop
    wamr->invoke_main();
    return 0;
}