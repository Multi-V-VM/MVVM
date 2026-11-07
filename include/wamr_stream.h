#pragma once
#include <cstddef>
struct WriteStream {
    virtual bool write(const char *, std::size_t) const { return false; }
    virtual ~WriteStream() = default;
};
struct ReadStream {
    virtual bool read(char *, std::size_t) const { return false; }
    virtual bool ignore(std::size_t) const { return false; }
    virtual const char *read_view(std::size_t) { return nullptr; }
    virtual std::size_t tellg() const { return 0; }
    virtual ~ReadStream() = default;
};
