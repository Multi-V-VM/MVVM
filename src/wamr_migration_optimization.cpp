/*
 * The WebAssembly Live Migration Project
 * Migration Overhead Optimization Implementation
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#include "wamr_migration_optimization.h"
#include "wamr_read_write.h"
#include <algorithm>
#include <cstring>
#include <numeric>
#include <signal.h>
#include <spdlog/spdlog.h>
#include <sys/mman.h>
#include <unistd.h>
#include <zlib.h>

namespace mvvm {

struct MigrationOptimizer::Impl {
    MigrationStrategy strategy = MigrationStrategy::ADAPTIVE;
    bool compression_enabled = true;
    size_t dirty_page_threshold = 1024; // pages
    std::unordered_map<uint64_t, PageTracker> page_map;
    std::vector<MemoryRegion> memory_regions;
    MigrationMetrics metrics;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
    std::atomic<size_t> dirty_page_count{0};

    // Page size (typically 4KB)
    static constexpr size_t PAGE_SIZE = 4096;
};

MigrationOptimizer::MigrationOptimizer() : pImpl(std::make_unique<Impl>()) {
    SPDLOG_DEBUG("MigrationOptimizer initialized");
}

MigrationOptimizer::~MigrationOptimizer() = default;

void MigrationOptimizer::setStrategy(MigrationStrategy strategy) {
    pImpl->strategy = strategy;
    SPDLOG_INFO("Migration strategy set to: {}", static_cast<int>(strategy));
}

void MigrationOptimizer::enableCompression(bool enable) { pImpl->compression_enabled = enable; }

void MigrationOptimizer::setDirtyPageThreshold(size_t threshold) { pImpl->dirty_page_threshold = threshold; }

void MigrationOptimizer::trackMemoryWrite(void *addr, size_t size) { updatePageTracking(addr, size); }

void MigrationOptimizer::markPageDirty(uint64_t page_number) {
    auto &page = pImpl->page_map[page_number];
    if (!page.is_dirty) {
        page.is_dirty = true;
        page.last_modified = std::chrono::steady_clock::now().time_since_epoch().count();
        pImpl->dirty_page_count++;
    }
}

std::vector<uint64_t> MigrationOptimizer::getDirtyPages() const {
    std::vector<uint64_t> dirty_pages;
    for (const auto &[page_num, tracker] : pImpl->page_map) {
        if (tracker.is_dirty) {
            dirty_pages.push_back(page_num);
        }
    }
    return dirty_pages;
}

void MigrationOptimizer::createIncrementalCheckpoint(WriteStream *writer) {
    startMetrics();

    // Write header
    uint32_t magic = 0x4D56564D; // "MVVM"
    uint32_t version = 1;
    writer->write(reinterpret_cast<const char *>(&magic), sizeof(magic));
    writer->write(reinterpret_cast<const char *>(&version), sizeof(version));

    // Get dirty pages
    auto dirty_pages = getDirtyPages();
    uint64_t num_dirty = dirty_pages.size();
    writer->write(reinterpret_cast<const char *>(&num_dirty), sizeof(num_dirty));

    SPDLOG_INFO("Creating incremental checkpoint with {} dirty pages", num_dirty);

    // Write dirty page data
    std::vector<uint8_t> compressed_buffer;
    for (uint64_t page_num : dirty_pages) {
        writer->write(reinterpret_cast<const char *>(&page_num), sizeof(page_num));

        // Calculate page address
        void *page_addr = reinterpret_cast<void *>(page_num * Impl::PAGE_SIZE);

        if (pImpl->compression_enabled) {
            compressed_buffer.resize(Impl::PAGE_SIZE * 2);
            size_t compressed_size =
                compressData(page_addr, Impl::PAGE_SIZE, compressed_buffer.data(), compressed_buffer.size());
            uint32_t size = compressed_size;
            writer->write(reinterpret_cast<const char *>(&size), sizeof(size));
            writer->write(reinterpret_cast<const char *>(compressed_buffer.data()), compressed_size);
            pImpl->metrics.compressed_bytes += compressed_size;
        } else {
            uint32_t size = Impl::PAGE_SIZE;
            writer->write(reinterpret_cast<const char *>(&size), sizeof(size));
            writer->write(reinterpret_cast<const char *>(page_addr), Impl::PAGE_SIZE);
        }

        pImpl->metrics.total_bytes_transferred += Impl::PAGE_SIZE;

        // Clear dirty flag after checkpoint
        pImpl->page_map[page_num].is_dirty = false;
    }

    pImpl->dirty_page_count = 0;
    endMetrics();
}

void MigrationOptimizer::restoreIncremental(ReadStream *reader) {
    startMetrics();

    // Read header
    uint32_t magic, version;
    reader->read(reinterpret_cast<char *>(&magic), sizeof(magic));
    reader->read(reinterpret_cast<char *>(&version), sizeof(version));

    if (magic != 0x4D56564D || version != 1) {
        throw std::runtime_error("Invalid checkpoint format");
    }

    // Read number of dirty pages
    uint64_t num_dirty;
    reader->read(reinterpret_cast<char *>(&num_dirty), sizeof(num_dirty));

    SPDLOG_INFO("Restoring {} dirty pages", num_dirty);

    // Restore dirty pages
    std::vector<uint8_t> buffer;
    for (uint64_t i = 0; i < num_dirty; i++) {
        uint64_t page_num;
        uint32_t size;
        reader->read(reinterpret_cast<char *>(&page_num), sizeof(page_num));
        reader->read(reinterpret_cast<char *>(&size), sizeof(size));

        void *page_addr = reinterpret_cast<void *>(page_num * Impl::PAGE_SIZE);

        if (size < Impl::PAGE_SIZE) {
            // Compressed data
            buffer.resize(size);
            reader->read(reinterpret_cast<char *>(buffer.data()), size);
            decompressData(buffer.data(), size, page_addr, Impl::PAGE_SIZE);
        } else {
            // Uncompressed data
            reader->read(reinterpret_cast<char *>(page_addr), size);
        }

        pImpl->metrics.total_bytes_transferred += size;
    }

    endMetrics();
}

void MigrationOptimizer::computeStateDelta(const void *old_state, const void *new_state, size_t size,
                                           std::vector<uint8_t> &delta) {
    delta.clear();
    const uint8_t *old_bytes = static_cast<const uint8_t *>(old_state);
    const uint8_t *new_bytes = static_cast<const uint8_t *>(new_state);

    // Simple run-length encoding of differences
    size_t i = 0;
    while (i < size) {
        if (old_bytes[i] == new_bytes[i]) {
            // Find run of unchanged bytes
            size_t run_start = i;
            while (i < size && old_bytes[i] == new_bytes[i])
                i++;
            size_t run_length = i - run_start;

            // Encode: 0x00 + length (4 bytes)
            delta.push_back(0x00);
            delta.insert(delta.end(), reinterpret_cast<uint8_t *>(&run_length),
                         reinterpret_cast<uint8_t *>(&run_length) + sizeof(run_length));
        } else {
            // Find run of changed bytes
            size_t change_start = i;
            while (i < size && old_bytes[i] != new_bytes[i])
                i++;
            size_t change_length = i - change_start;

            // Encode: 0xFF + length (4 bytes) + changed data
            delta.push_back(0xFF);
            delta.insert(delta.end(), reinterpret_cast<uint8_t *>(&change_length),
                         reinterpret_cast<uint8_t *>(&change_length) + sizeof(change_length));
            delta.insert(delta.end(), new_bytes + change_start, new_bytes + i);
        }
    }
}

void MigrationOptimizer::applyStateDelta(void *base_state, const std::vector<uint8_t> &delta) {
    uint8_t *state_bytes = static_cast<uint8_t *>(base_state);
    size_t delta_pos = 0;
    size_t state_pos = 0;

    while (delta_pos < delta.size()) {
        uint8_t op = delta[delta_pos++];
        size_t length;
        std::memcpy(&length, &delta[delta_pos], sizeof(length));
        delta_pos += sizeof(length);

        if (op == 0x00) {
            // Skip unchanged bytes
            state_pos += length;
        } else if (op == 0xFF) {
            // Apply changed bytes
            std::memcpy(state_bytes + state_pos, &delta[delta_pos], length);
            state_pos += length;
            delta_pos += length;
        }
    }
}

size_t MigrationOptimizer::compressData(const void *input, size_t input_size, void *output, size_t output_capacity) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    if (deflateInit(&stream, Z_DEFAULT_COMPRESSION) != Z_OK) {
        return 0;
    }

    stream.avail_in = input_size;
    stream.next_in = (Bytef *)input;
    stream.avail_out = output_capacity;
    stream.next_out = (Bytef *)output;

    int ret = deflate(&stream, Z_FINISH);
    deflateEnd(&stream);

    if (ret != Z_STREAM_END) {
        return 0;
    }

    return stream.total_out;
}

size_t MigrationOptimizer::decompressData(const void *input, size_t input_size, void *output, size_t output_capacity) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    stream.avail_in = input_size;
    stream.next_in = (Bytef *)input;
    stream.avail_out = output_capacity;
    stream.next_out = (Bytef *)output;

    if (inflateInit(&stream) != Z_OK) {
        return 0;
    }

    int ret = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (ret != Z_STREAM_END) {
        return 0;
    }

    return stream.total_out;
}

void MigrationOptimizer::setupLazyLoading(void *memory_base, size_t memory_size) {
    // Protect memory to trigger page faults on access
    if (mprotect(memory_base, memory_size, PROT_NONE) != 0) {
        SPDLOG_ERROR("Failed to setup lazy loading: {}", strerror(errno));
        return;
    }

    MemoryRegion region;
    region.base_addr = memory_base;
    region.size = memory_size;
    region.is_readonly = false;

    // Initialize page tracking
    size_t num_pages = (memory_size + Impl::PAGE_SIZE - 1) / Impl::PAGE_SIZE;
    region.pages.reserve(num_pages);
    for (size_t i = 0; i < num_pages; i++) {
        PageTracker tracker;
        tracker.page_number = reinterpret_cast<uint64_t>(memory_base) / Impl::PAGE_SIZE + i;
        tracker.is_dirty = false;
        tracker.last_modified = 0;
        tracker.checksum = 0;
        region.pages.push_back(tracker);
    }

    pImpl->memory_regions.push_back(region);
}

void MigrationOptimizer::handlePageFault(void *fault_addr) {
    // Find the page that caused the fault
    uint64_t page_num = reinterpret_cast<uint64_t>(fault_addr) / Impl::PAGE_SIZE;
    void *page_addr = reinterpret_cast<void *>(page_num * Impl::PAGE_SIZE);

    // Load the page data (would be from remote in real implementation)
    // For now, just make it accessible
    if (mprotect(page_addr, Impl::PAGE_SIZE, PROT_READ | PROT_WRITE) != 0) {
        SPDLOG_ERROR("Failed to handle page fault: {}", strerror(errno));
    }

    SPDLOG_DEBUG("Handled page fault for page {}", page_num);
}

void MigrationOptimizer::startMetrics() { pImpl->start_time = std::chrono::high_resolution_clock::now(); }

void MigrationOptimizer::endMetrics() {
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - pImpl->start_time);
    pImpl->metrics.checkpoint_time = duration;

    if (pImpl->metrics.total_bytes_transferred > 0) {
        pImpl->metrics.compression_ratio =
            static_cast<double>(pImpl->metrics.compressed_bytes) / pImpl->metrics.total_bytes_transferred;
    }
}

MigrationMetrics MigrationOptimizer::getMetrics() const { return pImpl->metrics; }

MigrationStrategy MigrationOptimizer::selectOptimalStrategy(size_t state_size, double network_bandwidth,
                                                            double cpu_utilization) {
    // Simple heuristics for strategy selection
    if (state_size < 1024 * 1024) { // < 1MB
        return MigrationStrategy::COMPRESSION;
    } else if (network_bandwidth < 100.0) { // < 100 MB/s
        return MigrationStrategy::DELTA_ENCODING;
    } else if (cpu_utilization > 0.8) { // High CPU usage
        return MigrationStrategy::LAZY_LOADING;
    } else if (pImpl->dirty_page_count < pImpl->dirty_page_threshold) {
        return MigrationStrategy::INCREMENTAL;
    }

    return MigrationStrategy::ADAPTIVE;
}

uint32_t MigrationOptimizer::calculateChecksum(const void *data, size_t size) {
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    uint32_t checksum = 0;
    for (size_t i = 0; i < size; i++) {
        checksum = ((checksum << 1) | (checksum >> 31)) ^ bytes[i];
    }
    return checksum;
}

void MigrationOptimizer::updatePageTracking(void *addr, size_t size) {
    uint64_t start_page = reinterpret_cast<uint64_t>(addr) / Impl::PAGE_SIZE;
    uint64_t end_page = (reinterpret_cast<uint64_t>(addr) + size - 1) / Impl::PAGE_SIZE;

    for (uint64_t page = start_page; page <= end_page; page++) {
        markPageDirty(page);
    }
}

bool MigrationOptimizer::shouldUseDeltaEncoding(size_t delta_size, size_t full_size) {
    return delta_size < full_size * 0.7; // Use delta if it saves >30%
}

// ZeroCopyTransfer implementation

// Define RDMAContext struct
struct ZeroCopyTransfer::RDMAContext {
    void *context;
    void *pd;
    void *mr;
    void *qp;
    void *cq;
    bool initialized = false;
};

ZeroCopyTransfer::ZeroCopyTransfer() : shared_mem_base(nullptr), shared_mem_size(0) {}

ZeroCopyTransfer::~ZeroCopyTransfer() {
    if (shared_mem_base) {
        munmap(shared_mem_base, shared_mem_size);
    }
}

bool ZeroCopyTransfer::setupSharedMemory(size_t size) {
    shared_mem_size = size;
    shared_mem_base = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return shared_mem_base != MAP_FAILED;
}

void *ZeroCopyTransfer::getSharedMemoryPtr() { return shared_mem_base; }

void ZeroCopyTransfer::transferDirect(int fd, const void *data, size_t size) {
    // Use sendfile for zero-copy transfer when possible
    // For now, fallback to regular write
    ssize_t written = write(fd, data, size);
    if (written < 0 || static_cast<size_t>(written) != size) {
        SPDLOG_ERROR("Failed to write data: {}", strerror(errno));
    }
}

// MigrationPredictor implementation
MigrationPredictor::MigrationPredictor() {}

std::chrono::milliseconds MigrationPredictor::predictNextMigration() {
    // Simple prediction based on past migration intervals
    // In real implementation, would use ML models
    return std::chrono::milliseconds(5000); // Default 5 seconds
}

void MigrationPredictor::recordPageAccess(uint64_t page_number) {
    auto now = std::chrono::steady_clock::now();
    access_history[page_number].push_back(now);
    access_sequence.push_back(page_number);

    // Keep only recent history
    if (access_sequence.size() > 10000) {
        access_sequence.erase(access_sequence.begin());
    }
}

std::vector<uint64_t> MigrationPredictor::predictNextAccesses(size_t count) {
    std::vector<uint64_t> predictions;

    // Simple frequency-based prediction
    std::unordered_map<uint64_t, size_t> frequency;
    for (uint64_t page : access_sequence) {
        frequency[page]++;
    }

    // Sort by frequency
    std::vector<std::pair<uint64_t, size_t>> freq_pairs(frequency.begin(), frequency.end());
    std::sort(freq_pairs.begin(), freq_pairs.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

    // Return top pages
    for (size_t i = 0; i < std::min(count, freq_pairs.size()); i++) {
        predictions.push_back(freq_pairs[i].first);
    }

    return predictions;
}

} // namespace mvvm