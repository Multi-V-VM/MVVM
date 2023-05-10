//
// Created by victoryang00 on 4/29/23.
//

#include <iostream>
#include <string>

#include "struct_pack/struct_pack.hpp"
#include "wamr.h"
#include "wamr_exec_env.h"
#include "wamr_module_instance.h"
#include <cstdio>
#include <unistd.h>

struct fread_stream {
    FILE *file;
    bool read(char *data, std::size_t sz) const { return fread(data, sz, 1, file) == 1; }
    [[nodiscard]] bool ignore(std::size_t sz) const { return fseek(file, sz, SEEK_CUR) == 0; }
    [[nodiscard]] std::size_t tellg() const {
        // if you worry about ftell performance, just use an variable to record it.
        return ftell(file);
    }
    explicit fread_stream(const char *file_name) : file(fopen(file_name, "rb")) {}
    ~fread_stream() { fclose(file); }
};
auto reader = fread_stream("test.bin");
int main() {
//  first get the deserializer message, here just hard code
    auto a = struct_pack::deserialize<WAMRExecEnv<1,65534,8192,200,10,200>>(reader).value();
    auto wamr = new WAMRInstance( &a);
    wamr->invoke_main();
    return 0;
}