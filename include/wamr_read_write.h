#ifndef A8D12233_B4B7_456E_8685_EAE8BC3C6CCD
#define A8D12233_B4B7_456E_8685_EAE8BC3C6CCD
#include <unistd.h>
#include "struct_pack/struct_pack.hpp"
struct fwrite_stream {
    FILE *file;
    bool write(const char *data, std::size_t sz) const { return fwrite(data, sz, 1, file) == 1; }
    explicit fwrite_stream(const char *file_name) : file(fopen(file_name, "wb")) {}
    ~fwrite_stream() { fclose(file); }
};

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
#endif /* A8D12233_B4B7_456E_8685_EAE8BC3C6CCD */
