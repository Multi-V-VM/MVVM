//
// Created by victoryang00 on 4/29/23.
//

#include <iostream>
#include <string>

#include "struct_pack/struct_pack.hpp"
#include "wamr_exec_env.h"
#include "wasm_module_instance.h"
#include "wamr.h"
#include <cstdio>
#include <unistd.h>

struct fread_stream {
    FILE *file;
    bool read(char *data, std::size_t sz) { return fread(data, sz, 1, file) == 1; }
    bool ignore(std::size_t sz) { return fseek(file, sz, SEEK_CUR) == 0; }
    std::size_t tellg() {
        // if you worry about ftell performance, just use an variable to record it.
        return ftell(file);
    }
    fread_stream(const char *file_name) : file(fopen(file_name, "rb")) {}
    ~fread_stream() { fclose(file); }
};
auto reader = fread_stream("test.bin");
auto reader1 = fread_stream("test1.bin");
int main() {
    auto a = struct_pack::deserialize<WAMRExecEnv>(reader);
    auto b = struct_pack::deserialize<WAMRModuleInstance>(reader1);
    auto wamr = new WAMRInstance(&b, &a);
    return 0;
}