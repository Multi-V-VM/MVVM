/*
 * The WebAssembly Live Migration Project
 * Migration Overhead Optimization Module
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#ifndef WAMR_MIGRATION_OPTIMIZATION_H
#define WAMR_MIGRATION_OPTIMIZATION_H

#include "wamr_type.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <vector>

// Forward declarations for stream classes
struct WriteStream;
struct ReadStream;

namespace mvvm {

// Migration optimization strategies
enum class MigrationStrategy {
    INCREMENTAL, // Incremental checkpoint/restore
    LAZY_LOADING, // Load pages on demand
    COMPRESSION, // Compress data during transfer
    DELTA_ENCODING, // Send only changed data
    ADAPTIVE // Choose strategy based on workload
};

// Page tracking for incremental migration
struct PageTracker {
    uint64_t page_number;
    bool is_dirty;
    uint64_t last_modified;
    uint32_t checksum;
};

// Memory region metadata
struct MemoryRegion {
    void *base_addr;
    size_t size;
    bool is_readonly;
    std::vector<PageTracker> pages;
};

// Migration metrics
struct MigrationMetrics {
    size_t total_bytes_transferred;
    size_t compressed_bytes;
    std::chrono::microseconds checkpoint_time;
    std::chrono::microseconds transfer_time;
    std::chrono::microseconds restore_time;
    double compression_ratio;
    size_t dirty_pages_count;
};

class MigrationOptimizer {
public:
    MigrationOptimizer();
    ~MigrationOptimizer();

    // Configure optimization strategies
    void setStrategy(MigrationStrategy strategy);
    void enableCompression(bool enable);
    void setDirtyPageThreshold(size_t threshold);

    // Track memory modifications
    void trackMemoryWrite(void *addr, size_t size);
    void markPageDirty(uint64_t page_number);
    std::vector<uint64_t> getDirtyPages() const;

    // Incremental checkpointing
    void createIncrementalCheckpoint(WriteStream *writer);
    void restoreIncremental(ReadStream *reader);

    // Delta encoding for state transfer
    void computeStateDelta(const void *old_state, const void *new_state, size_t size, std::vector<uint8_t> &delta);
    void applyStateDelta(void *base_state, const std::vector<uint8_t> &delta);

    // Compression utilities
    size_t compressData(const void *input, size_t input_size, void *output, size_t output_capacity);
    size_t decompressData(const void *input, size_t input_size, void *output, size_t output_capacity);

    // Lazy loading support
    void setupLazyLoading(void *memory_base, size_t memory_size);
    void handlePageFault(void *fault_addr);

    // Performance monitoring
    void startMetrics();
    void endMetrics();
    MigrationMetrics getMetrics() const;

    // Adaptive strategy selection
    MigrationStrategy selectOptimalStrategy(size_t state_size, double network_bandwidth, double cpu_utilization);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;

    // Helper methods
    uint32_t calculateChecksum(const void *data, size_t size);
    void updatePageTracking(void *addr, size_t size);
    bool shouldUseDeltaEncoding(size_t delta_size, size_t full_size);
};

// Zero-copy transfer support
class ZeroCopyTransfer {
public:
    ZeroCopyTransfer();
    ~ZeroCopyTransfer();

    // Setup shared memory for zero-copy
    bool setupSharedMemory(size_t size);
    void *getSharedMemoryPtr();

    // RDMA support for high-speed networks
    bool setupRDMA(const char *remote_addr, int port);
    void transferViaRDMA(const void *data, size_t size);

    // Direct memory transfer
    void transferDirect(int fd, const void *data, size_t size);

private:
    struct RDMAContext;
    std::unique_ptr<RDMAContext> rdma_ctx;
    void *shared_mem_base;
    size_t shared_mem_size;
};

// Prefetching and prediction
class MigrationPredictor {
public:
    MigrationPredictor();

    // Predict next migration time
    std::chrono::milliseconds predictNextMigration();

    // Prefetch likely-to-be-accessed pages
    void prefetchPages(const std::vector<uint64_t> &page_list);

    // Learn access patterns
    void recordPageAccess(uint64_t page_number);
    std::vector<uint64_t> predictNextAccesses(size_t count);

private:
    std::unordered_map<uint64_t, std::vector<std::chrono::time_point<std::chrono::steady_clock>>> access_history;
    std::vector<uint64_t> access_sequence;
};

} // namespace mvvm

#endif // WAMR_MIGRATION_OPTIMIZATION_H