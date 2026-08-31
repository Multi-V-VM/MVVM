#include "wamr_cxl_stream.h"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <linux/memfd.h>
#include <linux/mempolicy.h>
#include <linux/stat.h>
#include <stdexcept>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <vector>

namespace mvvm::cxl {
namespace {

constexpr std::uint64_t kMagic = 0x4d56564d43584c31ULL; // "MVVMCXL1"
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kWriting = 1;
constexpr std::uint32_t kReady = 2;
constexpr std::uint32_t kFailed = 3;

struct alignas(64) Header {
    std::uint64_t magic;
    std::uint32_t version;
    std::uint32_t header_size;
    alignas(8) std::uint64_t payload_capacity;
    alignas(8) std::uint64_t committed_size;
    alignas(4) std::uint32_t state;
    std::uint32_t reserved;
    alignas(8) std::uint64_t generation;
};

static_assert(std::atomic_ref<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic_ref<std::uint32_t>::is_always_lock_free);

std::size_t pageSize() {
    const long value = ::sysconf(_SC_PAGESIZE);
    if (value <= 0)
        throw std::runtime_error("sysconf(_SC_PAGESIZE) failed");
    return static_cast<std::size_t>(value);
}

Header *header(void *mapping) { return static_cast<Header *>(mapping); }
const Header *header(const void *mapping) { return static_cast<const Header *>(mapping); }

void closeOnError(int fd) {
    if (fd >= 0)
        ::close(fd);
}

bool fileHasDaxAttribute(int fd) {
    struct statx info{};
    const int rc =
        static_cast<int>(::syscall(SYS_statx, fd, "", AT_EMPTY_PATH | AT_STATX_SYNC_AS_STAT, STATX_BASIC_STATS, &info));
    return rc == 0 && (info.stx_attributes_mask & STATX_ATTR_DAX) != 0 && (info.stx_attributes & STATX_ATTR_DAX) != 0;
}

void *mapFile(int fd, std::size_t length, bool hardware_dax) {
    int flags = MAP_SHARED;
#if defined(MAP_SHARED_VALIDATE) && defined(MAP_SYNC)
    if (hardware_dax)
        flags = MAP_SHARED_VALIDATE | MAP_SYNC;
#else
    if (hardware_dax)
        throw std::runtime_error("this libc/kernel does not expose MAP_SYNC for DAX mappings");
#endif
    void *mapping = ::mmap(nullptr, length, PROT_READ | PROT_WRITE, flags, fd, 0);
    if (mapping == MAP_FAILED) {
        const int saved_errno = errno;
        throw std::runtime_error("mmap failed: " + std::string(std::strerror(saved_errno)));
    }
    return mapping;
}

int createMemfd() { return static_cast<int>(::syscall(SYS_memfd_create, "mvvm-cxl-shared-region", MFD_CLOEXEC)); }

void bindMappingToNode(void *mapping, std::size_t length, int node) {
    if (node < 0)
        throw std::invalid_argument("NUMA node must be non-negative");
    constexpr std::size_t kBitsPerWord = sizeof(unsigned long) * 8;
    /* Linux validates mbind masks against the configured MAX_NUMNODES width.
     * This kernel exposes that width through the Mems_allowed mask. */
    std::ifstream status_file("/proc/self/status");
    std::string status_line;
    std::size_t mask_bits = 0;
    while (std::getline(status_file, status_line)) {
        if (!status_line.starts_with("Mems_allowed:"))
            continue;
        for (const char character : status_line.substr(status_line.find(':') + 1))
            if (std::isxdigit(static_cast<unsigned char>(character)))
                mask_bits += 4;
        break;
    }
    if (mask_bits == 0 || static_cast<std::size_t>(node) >= mask_bits)
        throw std::runtime_error("cannot determine a valid kernel NUMA mask width");
    const std::size_t word_count = (mask_bits + kBitsPerWord - 1) / kBitsPerWord;
    std::vector<unsigned long> node_mask(word_count);
    node_mask[static_cast<std::size_t>(node) / kBitsPerWord] |= 1UL << (static_cast<unsigned int>(node) % kBitsPerWord);
    const unsigned long max_node = static_cast<unsigned long>(mask_bits + 1);
    if (::syscall(SYS_mbind, mapping, length, MPOL_BIND, node_mask.data(), max_node, MPOL_MF_MOVE | MPOL_MF_STRICT) !=
        0) {
        const int saved_errno = errno;
        throw std::runtime_error("mbind to NUMA node " + std::to_string(node) +
                                 " failed: " + std::strerror(saved_errno));
    }
}

void populateWritablePages(void *mapping, std::size_t length, int node) {
#if defined(MADV_POPULATE_WRITE)
    if (::madvise(mapping, length, MADV_POPULATE_WRITE) == 0)
        return;
    const int saved_errno = errno;
    throw std::runtime_error("pre-faulting the NUMA node " + std::to_string(node) +
                             " shared region failed: " + std::strerror(saved_errno));
#else
    (void)mapping;
    (void)length;
    (void)node;
    throw std::runtime_error("MADV_POPULATE_WRITE is required for the NUMA CXL backend");
#endif
}

} // namespace

SharedRegion::SharedRegion(int fd, void *mapping, std::size_t mapping_size, std::size_t payload_offset,
                           bool hardware_dax, int numa_node)
    : fd_(fd), mapping_(mapping), mapping_size_(mapping_size), payload_offset_(payload_offset),
      hardware_dax_(hardware_dax), numa_node_(numa_node) {}

SharedRegion::~SharedRegion() {
    if (mapping_ != MAP_FAILED && mapping_ != nullptr)
        ::munmap(mapping_, mapping_size_);
    if (fd_ >= 0)
        ::close(fd_);
}

std::shared_ptr<SharedRegion> SharedRegion::mapNew(int fd, std::size_t payload_capacity, bool hardware_dax) {
    const std::size_t payload_offset = pageSize();
    if (payload_capacity == 0 || payload_capacity > std::numeric_limits<std::size_t>::max() - payload_offset) {
        closeOnError(fd);
        throw std::invalid_argument("invalid CXL payload capacity");
    }
    const std::size_t mapping_size = payload_offset + payload_capacity;
    if (::ftruncate(fd, static_cast<off_t>(mapping_size)) != 0) {
        const int saved_errno = errno;
        closeOnError(fd);
        throw std::runtime_error("ftruncate failed: " + std::string(std::strerror(saved_errno)));
    }

    void *mapping = nullptr;
    try {
        mapping = mapFile(fd, mapping_size, hardware_dax);
    } catch (...) {
        closeOnError(fd);
        throw;
    }
    std::memset(mapping, 0, payload_offset);
    Header *metadata = header(mapping);
    metadata->magic = kMagic;
    metadata->version = kVersion;
    metadata->header_size = static_cast<std::uint32_t>(payload_offset);
    metadata->payload_capacity = payload_capacity;
    metadata->committed_size = 0;
    metadata->state = kWriting;
    metadata->generation = 1;
    return std::shared_ptr<SharedRegion>(new SharedRegion(fd, mapping, mapping_size, payload_offset, hardware_dax));
}

std::shared_ptr<SharedRegion> SharedRegion::mapExisting(int fd, bool hardware_dax) {
    struct stat info{};
    if (::fstat(fd, &info) != 0 || info.st_size < static_cast<off_t>(sizeof(Header))) {
        closeOnError(fd);
        throw std::runtime_error("invalid CXL checkpoint file size");
    }
    const std::size_t mapping_size = static_cast<std::size_t>(info.st_size);
    void *mapping = nullptr;
    try {
        mapping = mapFile(fd, mapping_size, hardware_dax);
    } catch (...) {
        closeOnError(fd);
        throw;
    }
    const Header *metadata = header(mapping);
    const bool valid = metadata->magic == kMagic && metadata->version == kVersion &&
                       metadata->header_size >= sizeof(Header) && metadata->header_size <= mapping_size &&
                       metadata->payload_capacity == mapping_size - metadata->header_size;
    if (!valid) {
        ::munmap(mapping, mapping_size);
        closeOnError(fd);
        throw std::runtime_error("invalid CXL checkpoint header");
    }
    return std::shared_ptr<SharedRegion>(
        new SharedRegion(fd, mapping, mapping_size, metadata->header_size, hardware_dax));
}

std::shared_ptr<SharedRegion> SharedRegion::createDaxFile(const std::string &path, std::size_t payload_capacity) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        throw std::runtime_error("open DAX file failed: " + std::string(std::strerror(errno)));
    if (!fileHasDaxAttribute(fd)) {
        closeOnError(fd);
        throw std::runtime_error("checkpoint file is not on a filesystem reporting STATX_ATTR_DAX");
    }
    return mapNew(fd, payload_capacity, true);
}

std::shared_ptr<SharedRegion> SharedRegion::openDaxFile(const std::string &path) {
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("open DAX file failed: " + std::string(std::strerror(errno)));
    if (!fileHasDaxAttribute(fd)) {
        closeOnError(fd);
        throw std::runtime_error("checkpoint file is not on a filesystem reporting STATX_ATTR_DAX");
    }
    return mapExisting(fd, true);
}

std::shared_ptr<SharedRegion> SharedRegion::createForkTestRegion(std::size_t payload_capacity) {
    const int fd = createMemfd();
    if (fd < 0)
        throw std::runtime_error("memfd_create failed: " + std::string(std::strerror(errno)));
    return mapNew(fd, payload_capacity, false);
}

std::shared_ptr<SharedRegion> SharedRegion::createNumaForkRegion(std::size_t payload_capacity, int numa_node,
                                                                 bool prepopulate) {
    auto region = createForkTestRegion(payload_capacity);
    bindMappingToNode(region->mapping_, region->mapping_size_, numa_node);
    /* Pay shmem allocation and first-touch costs once, before the checkpoint
     * critical path.  The same region can then be reset and reused. */
    if (prepopulate)
        populateWritablePages(region->mapping_, region->mapping_size_, numa_node);
    region->numa_node_ = numa_node;
    return region;
}

std::byte *SharedRegion::payload() const noexcept { return static_cast<std::byte *>(mapping_) + payload_offset_; }

std::size_t SharedRegion::payloadCapacity() const noexcept {
    return static_cast<std::size_t>(header(mapping_)->payload_capacity);
}

std::size_t SharedRegion::committedSize() const noexcept {
    auto &value = const_cast<std::uint64_t &>(header(mapping_)->committed_size);
    return static_cast<std::size_t>(std::atomic_ref<std::uint64_t>(value).load(std::memory_order_acquire));
}

bool SharedRegion::contains(const void *address, std::size_t size) const noexcept {
    const auto begin = reinterpret_cast<std::uintptr_t>(payload());
    const auto candidate = reinterpret_cast<std::uintptr_t>(address);
    const auto capacity = payloadCapacity();
    return candidate >= begin && candidate - begin <= capacity && size <= capacity - (candidate - begin);
}

int SharedRegion::pageNumaNode(const void *address) const {
    if (!contains(address))
        throw std::invalid_argument("address is outside the shared CXL region");
    void *page = const_cast<void *>(address);
    int status = -1;
    if (::syscall(SYS_move_pages, 0, 1UL, &page, nullptr, &status, 0) != 0) {
        const int saved_errno = errno;
        throw std::runtime_error("move_pages NUMA query failed: " + std::string(std::strerror(saved_errno)));
    }
    if (status < 0)
        throw std::runtime_error("move_pages NUMA query returned: " + std::string(std::strerror(-status)));
    return status;
}

WriteStream::WriteStream(std::shared_ptr<SharedRegion> region) : region_(std::move(region)) {
    if (!region_)
        throw std::invalid_argument("CXL write stream requires a region");
    reset();
}

void WriteStream::reset() const {
    Header *metadata = header(region_->mapping_);
    std::atomic_ref<std::uint32_t>(metadata->state).store(kWriting, std::memory_order_release);
    std::atomic_ref<std::uint64_t>(metadata->committed_size).store(0, std::memory_order_relaxed);
    std::atomic_ref<std::uint64_t>(metadata->generation).fetch_add(1, std::memory_order_relaxed);
    position_ = 0;
    failed_ = false;
}

bool WriteStream::write(const char *data, std::size_t size) const {
    if (failed_ || size > region_->payloadCapacity() - position_) {
        failed_ = true;
        std::atomic_ref<std::uint32_t>(header(region_->mapping_)->state).store(kFailed, std::memory_order_release);
        return false;
    }
    if (size != 0)
        std::memcpy(region_->payload() + position_, data, size);
    position_ += size;
    return true;
}

bool WriteStream::commit() const {
    if (failed_)
        return false;
    if (::msync(region_->mapping_, region_->mapping_size_, MS_SYNC) != 0)
        return false;
    Header *metadata = header(region_->mapping_);
    std::atomic_ref<std::uint64_t>(metadata->committed_size).store(position_, std::memory_order_release);
    std::atomic_ref<std::uint32_t>(metadata->state).store(kReady, std::memory_order_release);
    return ::msync(region_->mapping_, region_->payload_offset_, MS_SYNC) == 0;
}

ReadStream::ReadStream(std::shared_ptr<SharedRegion> region) : region_(std::move(region)) {
    if (!region_)
        throw std::invalid_argument("CXL read stream requires a region");
}

bool ReadStream::waitUntilReady(std::chrono::milliseconds timeout) const {
    Header *metadata = header(region_->mapping_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::uint32_t state = std::atomic_ref<std::uint32_t>(metadata->state).load(std::memory_order_acquire);
        if (state == kReady)
            return true;
        if (state == kFailed)
            return false;
        struct timespec pause{0, 1000000};
        ::nanosleep(&pause, nullptr);
    }
    return false;
}

bool ReadStream::canRead(std::size_t size) const noexcept {
    const std::size_t committed = region_->committedSize();
    return position_ <= committed && size <= committed - position_;
}

bool ReadStream::read(char *data, std::size_t size) const {
    if (!canRead(size))
        return false;
    if (size != 0)
        std::memcpy(data, region_->payload() + position_, size);
    position_ += size;
    return true;
}

bool ReadStream::ignore(std::size_t size) const {
    if (!canRead(size))
        return false;
    position_ += size;
    return true;
}

const char *ReadStream::read_view(std::size_t size) {
    if (!canRead(size))
        return nullptr;
    const char *view = reinterpret_cast<const char *>(region_->payload() + position_);
    position_ += size;
    return view;
}

} // namespace mvvm::cxl
