/*
 * CXL/DAX backed struct_pack streams.
 *
 * The mapping is created before fork and remains MAP_SHARED in both the
 * checkpoint producer and restore consumer.  read_view() deliberately returns
 * an address inside that mapping so deserializing std::span does not allocate
 * or copy the WebAssembly linear memory.
 */
#ifndef MVVM_WAMR_CXL_STREAM_H
#define MVVM_WAMR_CXL_STREAM_H

#if !defined(__linux__)
#error "The CXL stream currently requires Linux DAX/mmap support"
#endif

#include "wamr_read_write.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace mvvm::cxl {

class SharedRegion {
public:
    /* Strict production path: the file must report STATX_ATTR_DAX and accept
     * MAP_SYNC.  The payload capacity excludes the private metadata page. */
    static std::shared_ptr<SharedRegion> createDaxFile(const std::string &path, std::size_t payload_capacity);
    static std::shared_ptr<SharedRegion> openDaxFile(const std::string &path);

    /* Functional fork-test backend.  It has identical MAP_SHARED semantics,
     * but does not claim that memory is physically attached over CXL. */
    static std::shared_ptr<SharedRegion> createForkTestRegion(std::size_t payload_capacity);

    ~SharedRegion();
    SharedRegion(const SharedRegion &) = delete;
    SharedRegion &operator=(const SharedRegion &) = delete;

    [[nodiscard]] std::byte *payload() const noexcept;
    [[nodiscard]] std::size_t payloadCapacity() const noexcept;
    [[nodiscard]] std::size_t committedSize() const noexcept;
    [[nodiscard]] bool contains(const void *address, std::size_t size = 1) const noexcept;
    [[nodiscard]] bool isHardwareDax() const noexcept { return hardware_dax_; }

private:
    friend class WriteStream;
    friend class ReadStream;

    SharedRegion(int fd, void *mapping, std::size_t mapping_size, std::size_t payload_offset, bool hardware_dax);
    static std::shared_ptr<SharedRegion> mapNew(int fd, std::size_t payload_capacity, bool hardware_dax);
    static std::shared_ptr<SharedRegion> mapExisting(int fd, bool hardware_dax);

    int fd_;
    void *mapping_;
    std::size_t mapping_size_;
    std::size_t payload_offset_;
    bool hardware_dax_;
};

class WriteStream final : public ::WriteStream {
public:
    explicit WriteStream(std::shared_ptr<SharedRegion> region);

    bool write(const char *data, std::size_t size) const override;
    bool commit() const;
    void reset() const;
    [[nodiscard]] std::size_t tellp() const noexcept { return position_; }

private:
    std::shared_ptr<SharedRegion> region_;
    mutable std::size_t position_ = 0;
    mutable bool failed_ = false;
};

class ReadStream final : public ::ReadStream {
public:
    explicit ReadStream(std::shared_ptr<SharedRegion> region);

    bool waitUntilReady(std::chrono::milliseconds timeout) const;
    bool read(char *data, std::size_t size) const override;
    bool ignore(std::size_t size) const override;
    const char *read_view(std::size_t size) override;
    std::size_t tellg() const override { return position_; }
    void rewind() const noexcept { position_ = 0; }

private:
    [[nodiscard]] bool canRead(std::size_t size) const noexcept;
    std::shared_ptr<SharedRegion> region_;
    mutable std::size_t position_ = 0;
};

static_assert(ReaderStreamTrait<ReadStream, char>);
static_assert(WriterStreamTrait<WriteStream, char>);

} // namespace mvvm::cxl

#endif
