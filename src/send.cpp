//
// Created by victoryang00 on 4/8/23.
//

#include "struct_pack/struct_pack.hpp"
#include "wasm_exec_env.h"
#include <csignal>
#include <cstdio>
#include <iostream>
#include <string>
#include <unistd.h>
struct fwrite_stream {
    FILE *file;
    bool write(const char *data, std::size_t sz) { return fwrite(data, sz, 1, file) == 1; }
    fwrite_stream(const char *file_name) : file(fopen(file_name, "wb")) {}
    ~fwrite_stream() { fclose(file); }
};

auto writer = fwrite_stream("test.bin");
void sig_handler(int signo) {
    if (signo == SIGINT) { // start serializing the struct
        struct WAMRExecEnv a[10];
        struct_pack::serialize_to(writer, a);
    };
}

int main() {
    if (signal(SIGINT, sig_handler) == SIG_ERR)
        printf("\ncan't catch SIGINT\n");
    // A long long wait so that we can easily issue a signal to this process
    while (1)
        sleep(1);

    return 0;
}