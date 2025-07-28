/*
 * The WebAssembly Live Migration Project
 * MVVM Demo Application
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#include "wamr_evaluation_framework.h"
#include "wamr_gpu_cc_framework.h"
#include "wamr_migration_optimization.h"
#include "wamr_security_framework.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

using namespace mvvm;

// Demo application showcasing MVVM features
int main(int argc, char *argv[]) {
    std::cout << "MVVM - Migratable WebAssembly Virtual Machine Demo\n";
    std::cout << "==================================================\n\n";

    // 1. Demonstrate migration optimization
    std::cout << "1. Migration Optimization Demo\n";
    std::cout << "------------------------------\n";
    {
        MigrationOptimizer optimizer;
        optimizer.setStrategy(MigrationStrategy::INCREMENTAL);
        optimizer.enableCompression(true);

        // Simulate VM state
        size_t state_size = 50 * 1024 * 1024; // 50MB
        std::vector<uint8_t> vm_state(state_size, 0xAB);

        // Track some memory writes
        for (size_t i = 0; i < 10; i++) {
            size_t offset = i * 4096;
            vm_state[offset] = i;
            optimizer.trackMemoryWrite(&vm_state[offset], 1);
        }

        // Create checkpoint
        FwriteStream writer("demo_checkpoint.bin");
        auto start = std::chrono::high_resolution_clock::now();
        optimizer.createIncrementalCheckpoint(&writer);
        auto end = std::chrono::high_resolution_clock::now();

        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        std::cout << "Checkpoint created in " << duration.count() << " ms\n";

        auto metrics = optimizer.getMetrics();
        std::cout << "Data transferred: " << metrics.total_bytes_transferred / 1024.0 << " KB\n";
        std::cout << "Compression ratio: " << metrics.compression_ratio << "\n\n";
    }

    // 2. Demonstrate security framework
    std::cout << "2. Security Framework Demo\n";
    std::cout << "--------------------------\n";
    {
        security::SecurityFramework security;
        security.initialize(security::SecurityPolicy::BALANCED);

        // Authenticate peer
        if (security.authenticatePeer("demo_peer", security::AuthMethod::MUTUAL_TLS)) {
            std::cout << "Peer authenticated successfully\n";
        }

        // Create security context
        auto ctx = security.createSecurityContext("demo_peer");
        std::cout << "Security context created: " << ctx.session_id << "\n";

        // Encrypt some data
        std::string sensitive_data = "This is sensitive VM state data";
        auto encrypted = security.encryptData(sensitive_data.data(), sensitive_data.size(), ctx);
        std::cout << "Data encrypted: " << encrypted.size() << " bytes\n";

        // Decrypt data
        auto decrypted = security.decryptData(encrypted.data(), encrypted.size(), ctx);
        std::cout << "Data decrypted successfully\n\n";
    }

    // 3. Demonstrate GPU CC framework
    std::cout << "3. GPU Confidential Computing Demo\n";
    std::cout << "----------------------------------\n";
    {
        gpu::GPUCCFramework gpu_framework;
        if (gpu_framework.initialize(gpu::GPUVendor::NVIDIA)) {
            std::cout << "GPU framework initialized\n";

            // Setup secure computation
            std::vector<gpu::CCFeature> features = {gpu::CCFeature::MEMORY_ENCRYPTION,
                                                    gpu::CCFeature::TRUSTED_EXECUTION};

            if (gpu_framework.setupSecureComputation(features)) {
                std::cout << "Secure computation environment established\n";

                // Register a kernel
                std::vector<uint8_t> dummy_kernel(1024, 0);
                gpu_framework.registerSecureKernel("demo_kernel", dummy_kernel.data(), dummy_kernel.size());
                std::cout << "Secure kernel registered\n";

                // Get performance metrics
                auto perf = gpu_framework.getPerformanceMetrics();
                std::cout << "GPU memory usage: " << perf.memory_usage_bytes / 1024.0 << " KB\n\n";
            }
        }
    }

    // 4. Run evaluation benchmarks
    std::cout << "4. Running Evaluation Benchmarks\n";
    std::cout << "--------------------------------\n";
    {
        evaluation::BenchmarkSuite suite;
        evaluation::BenchmarkConfig config;
        config.iterations = 3;
        config.warmup_iterations = 1;
        config.problem_sizes = {1024, 2048};

        suite.setConfig(config);

        // Add benchmarks
        auto migration_bench = std::make_unique<evaluation::MigrationOverheadBenchmark>();
        migration_bench->setStateSize(10 * 1024 * 1024); // 10MB
        suite.addBenchmark(std::move(migration_bench));

        auto security_bench = std::make_unique<evaluation::SecurityOverheadBenchmark>();
        security_bench->setDataSize(5 * 1024 * 1024); // 5MB
        suite.addBenchmark(std::move(security_bench));

        // Run benchmarks
        suite.runAll();

        // Get results
        auto results = suite.getResults();
        for (const auto &result : results) {
            std::cout << "\nBenchmark: " << result.benchmark_name << "\n";
            std::cout << "  Execution time: " << result.execution_time_ms << " ms\n";
            std::cout << "  Throughput: " << result.throughput_mbps << " MB/s\n";
            std::cout << "  Memory usage: " << result.memory_usage_bytes / (1024.0 * 1024.0) << " MB\n";
        }

        // Generate report
        suite.generateReport("mvvm_benchmark_report.txt");
        std::cout << "\nBenchmark report saved to mvvm_benchmark_report.txt\n";
    }

    std::cout << "\nMVVM Demo completed successfully!\n";
    return 0;
}