//
// Created by victoryang00 on 4/29/23.
//

#include <iostream>
#include <string>

#include "struct_pack/struct_pack.hpp"

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

int main() {


    return 0;
}