//
// Created by victoryang00 on 5/19/23.
//

#ifndef MVVM_WAMR_READ_WRITE_H
#define MVVM_WAMR_READ_WRITE_H
#include "struct_pack/struct_pack.hpp"
#ifndef _WIN32
#include <unistd.h>
#endif
struct FwriteStream {
    FILE *file;
    bool write(const char *data, std::size_t sz) const { return fwrite(data, sz, 1, file) == 1; }
    explicit FwriteStream(const char *file_name) : file(fopen(file_name, "wb")) {}
    ~FwriteStream() { fclose(file); }
};

struct FreadStream {
    FILE *file;
    bool read(char *data, std::size_t sz) const { return fread(data, sz, 1, file) == 1; }
    [[nodiscard]] bool ignore(std::size_t sz) const { return fseek(file, sz, SEEK_CUR) == 0; }
    [[nodiscard]] std::size_t tellg() const {
        // if you worry about ftell performance, just use an variable to record it.
        return ftell(file);
    }
    explicit FreadStream(const char *file_name) : file(fopen(file_name, "rb")) {}
    ~FreadStream() { fclose(file); }
};
#endif /* MVVM_WAMR_READ_WRITE_H */
