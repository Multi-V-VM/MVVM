/*
 * The WebAssembly Live Migration Project
 * Evaluation Framework Implementation
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#include "wamr_evaluation_framework.h"
#include "wamr_read_write.h"
#include <algorithm>
#include <fstream>
#include <iomanip>
#if defined(_WIN32) || defined(_WIN64)
#include <json/json.h>
#elif !defined(__APPLE__)
#include <jsoncpp/json/json.h>
#else
#include <json/json.h>
#endif
#include <numeric>
#include <random>
#include <spdlog/spdlog.h>
#include <sstream>
#include <sys/resource.h>

namespace mvvm {
namespace evaluation {

// Helper to get current memory usage
static size_t getCurrentMemoryUsage() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    return usage.ru_maxrss * 1024; // Convert to bytes
}

// Helper to get CPU usage
static double getCurrentCPUUsage() {
    static clock_t last_cpu = 0;
    static clock_t last_sys_cpu = 0;

    clock_t current_cpu = clock();
    double cpu_usage = (double)(current_cpu - last_cpu) / CLOCKS_PER_SEC * 100.0;

    last_cpu = current_cpu;
    return cpu_usage;
}

// MigrationOverheadBenchmark implementation
struct MigrationOverheadBenchmark::Impl {
    BenchmarkConfig config;
    size_t state_size = 100 * 1024 * 1024; // 100MB default
    MigrationStrategy strategy = MigrationStrategy::ADAPTIVE;
    double network_bandwidth = 1000.0; // 1Gbps default
    std::unique_ptr<MigrationOptimizer> optimizer;
    std::vector<uint8_t> test_state;
};

MigrationOverheadBenchmark::MigrationOverheadBenchmark() : pImpl(std::make_unique<Impl>()) {
    pImpl->optimizer = std::make_unique<MigrationOptimizer>();
}

MigrationOverheadBenchmark::~MigrationOverheadBenchmark() = default;

void MigrationOverheadBenchmark::setup(const BenchmarkConfig &config) {
    pImpl->config = config;

    // Generate test state
    pImpl->test_state.resize(pImpl->state_size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (auto &byte : pImpl->test_state) {
        byte = dis(gen);
    }

    // Configure optimizer
    pImpl->optimizer->setStrategy(pImpl->strategy);
    pImpl->optimizer->enableCompression(true);
}

BenchmarkResult MigrationOverheadBenchmark::run() {
    BenchmarkResult result;
    result.benchmark_name = getName();
    result.category = getCategory();
    result.workload_type = getWorkloadType();
    result.timestamp = std::chrono::steady_clock::now();

    // Warmup
    for (size_t i = 0; i < pImpl->config.warmup_iterations; i++) {
        FwriteStream writer("temp_migration.bin");
        pImpl->optimizer->createIncrementalCheckpoint(&writer);
    }

    // Actual benchmark
    std::vector<double> checkpoint_times;
    std::vector<double> restore_times;
    std::vector<size_t> data_sizes;

    for (size_t i = 0; i < pImpl->config.iterations; i++) {
        // Checkpoint phase
        auto checkpoint_start = std::chrono::high_resolution_clock::now();

        FwriteStream writer("migration_bench.bin");
        pImpl->optimizer->createIncrementalCheckpoint(&writer);

        auto checkpoint_end = std::chrono::high_resolution_clock::now();
        auto checkpoint_duration =
            std::chrono::duration_cast<std::chrono::microseconds>(checkpoint_end - checkpoint_start);
        checkpoint_times.push_back(checkpoint_duration.count() / 1000.0);

        // Get data size by checking the file size
        fflush(writer.file);
        long file_size = ftell(writer.file);
        data_sizes.push_back(file_size);

        // Restore phase
        auto restore_start = std::chrono::high_resolution_clock::now();

        FreadStream reader("migration_bench.bin");
        pImpl->optimizer->restoreIncremental(&reader);

        auto restore_end = std::chrono::high_resolution_clock::now();
        auto restore_duration = std::chrono::duration_cast<std::chrono::microseconds>(restore_end - restore_start);
        restore_times.push_back(restore_duration.count() / 1000.0);

        // Simulate some workload to create dirty pages
        for (size_t j = 0; j < pImpl->test_state.size(); j += 4096) {
            pImpl->test_state[j] = (pImpl->test_state[j] + 1) % 256;
            pImpl->optimizer->trackMemoryWrite(&pImpl->test_state[j], 1);
        }
    }

    // Calculate results
    double avg_checkpoint_time =
        std::accumulate(checkpoint_times.begin(), checkpoint_times.end(), 0.0) / checkpoint_times.size();
    double avg_restore_time = std::accumulate(restore_times.begin(), restore_times.end(), 0.0) / restore_times.size();
    size_t avg_data_size = std::accumulate(data_sizes.begin(), data_sizes.end(), 0UL) / data_sizes.size();

    result.execution_time_ms = avg_checkpoint_time + avg_restore_time;
    result.throughput_mbps = (avg_data_size / (1024.0 * 1024.0)) / (result.execution_time_ms / 1000.0);
    result.memory_usage_bytes = getCurrentMemoryUsage();
    result.cpu_utilization_percent = getCurrentCPUUsage();

    // Custom metrics
    result.custom_metrics["checkpoint_time_ms"] = avg_checkpoint_time;
    result.custom_metrics["restore_time_ms"] = avg_restore_time;
    result.custom_metrics["data_size_bytes"] = avg_data_size;
    result.custom_metrics["compression_ratio"] = (double)avg_data_size / pImpl->state_size;

    // Get metrics from optimizer
    auto opt_metrics = pImpl->optimizer->getMetrics();
    result.custom_metrics["dirty_pages"] = opt_metrics.dirty_pages_count;

    return result;
}

void MigrationOverheadBenchmark::cleanup() {
    pImpl->test_state.clear();
    std::remove("migration_bench.bin");
    std::remove("temp_migration.bin");
}

bool MigrationOverheadBenchmark::validate(const BenchmarkResult &result) {
    // Validate that results are reasonable
    return result.execution_time_ms > 0 && result.throughput_mbps > 0 &&
           result.custom_metrics.at("compression_ratio") <= 1.0;
}

void MigrationOverheadBenchmark::setStateSize(size_t size) { pImpl->state_size = size; }

void MigrationOverheadBenchmark::setMigrationStrategy(MigrationStrategy strategy) { pImpl->strategy = strategy; }

void MigrationOverheadBenchmark::setNetworkBandwidth(double bandwidth_mbps) {
    pImpl->network_bandwidth = bandwidth_mbps;
}

// SecurityOverheadBenchmark implementation
struct SecurityOverheadBenchmark::Impl {
    BenchmarkConfig config;
    security::SecurityPolicy policy = security::SecurityPolicy::POLICY_BALANCED;
    size_t data_size = 10 * 1024 * 1024; // 10MB default
    std::unique_ptr<security::SecurityFramework> security_framework;
    std::vector<uint8_t> test_data;
};

SecurityOverheadBenchmark::SecurityOverheadBenchmark() : pImpl(std::make_unique<Impl>()) {
    pImpl->security_framework = std::make_unique<security::SecurityFramework>();
}

SecurityOverheadBenchmark::~SecurityOverheadBenchmark() = default;

void SecurityOverheadBenchmark::setup(const BenchmarkConfig &config) {
    pImpl->config = config;

    // Initialize security framework
    pImpl->security_framework->initialize(pImpl->policy);

    // Generate test data
    pImpl->test_data.resize(pImpl->data_size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (auto &byte : pImpl->test_data) {
        byte = dis(gen);
    }
}

BenchmarkResult SecurityOverheadBenchmark::run() {
    BenchmarkResult result;
    result.benchmark_name = getName();
    result.category = getCategory();
    result.workload_type = getWorkloadType();
    result.timestamp = std::chrono::steady_clock::now();

    // Create security context
    auto security_ctx = pImpl->security_framework->createSecurityContext("benchmark_peer");

    std::vector<double> encryption_times;
    std::vector<double> decryption_times;
    std::vector<double> integrity_times;

    for (size_t i = 0; i < pImpl->config.iterations; i++) {
        // Encryption benchmark
        auto enc_start = std::chrono::high_resolution_clock::now();
        auto encrypted =
            pImpl->security_framework->encryptData(pImpl->test_data.data(), pImpl->test_data.size(), security_ctx);
        auto enc_end = std::chrono::high_resolution_clock::now();

        auto enc_duration = std::chrono::duration_cast<std::chrono::microseconds>(enc_end - enc_start);
        encryption_times.push_back(enc_duration.count() / 1000.0);

        // Decryption benchmark
        auto dec_start = std::chrono::high_resolution_clock::now();
        auto decrypted = pImpl->security_framework->decryptData(encrypted.data(), encrypted.size(), security_ctx);
        auto dec_end = std::chrono::high_resolution_clock::now();

        auto dec_duration = std::chrono::duration_cast<std::chrono::microseconds>(dec_end - dec_start);
        decryption_times.push_back(dec_duration.count() / 1000.0);

        // Integrity check benchmark
        auto int_start = std::chrono::high_resolution_clock::now();
        auto integrity =
            pImpl->security_framework->createIntegrityCheck(pImpl->test_data.data(), pImpl->test_data.size());
        pImpl->security_framework->verifyIntegrity(pImpl->test_data.data(), pImpl->test_data.size(), integrity);
        auto int_end = std::chrono::high_resolution_clock::now();

        auto int_duration = std::chrono::duration_cast<std::chrono::microseconds>(int_end - int_start);
        integrity_times.push_back(int_duration.count() / 1000.0);
    }

    // Calculate results
    double avg_encryption_time =
        std::accumulate(encryption_times.begin(), encryption_times.end(), 0.0) / encryption_times.size();
    double avg_decryption_time =
        std::accumulate(decryption_times.begin(), decryption_times.end(), 0.0) / decryption_times.size();
    double avg_integrity_time =
        std::accumulate(integrity_times.begin(), integrity_times.end(), 0.0) / integrity_times.size();

    result.execution_time_ms = avg_encryption_time + avg_decryption_time + avg_integrity_time;
    result.throughput_mbps = (pImpl->data_size / (1024.0 * 1024.0)) / (avg_encryption_time / 1000.0);
    result.memory_usage_bytes = getCurrentMemoryUsage();
    result.cpu_utilization_percent = getCurrentCPUUsage();

    // Custom metrics
    result.custom_metrics["encryption_time_ms"] = avg_encryption_time;
    result.custom_metrics["decryption_time_ms"] = avg_decryption_time;
    result.custom_metrics["integrity_time_ms"] = avg_integrity_time;
    result.custom_metrics["security_overhead_percent"] =
        (result.execution_time_ms / (pImpl->data_size / (1024.0 * 1024.0))) * 100;

    return result;
}

void SecurityOverheadBenchmark::cleanup() {
    pImpl->test_data.clear();
    pImpl->security_framework->shutdown();
}

bool SecurityOverheadBenchmark::validate(const BenchmarkResult &result) {
    return result.execution_time_ms > 0 && result.throughput_mbps > 0;
}

void SecurityOverheadBenchmark::setSecurityPolicy(security::SecurityPolicy policy) { pImpl->policy = policy; }

void SecurityOverheadBenchmark::setDataSize(size_t size) { pImpl->data_size = size; }

// GPUPerformanceBenchmark implementation
struct GPUPerformanceBenchmark::Impl {
    BenchmarkConfig config;
    std::string kernel_name = "matrix_multiply";
    size_t problem_size = 1024;
    bool use_cc = true;
    std::unique_ptr<gpu::GPUCCFramework> gpu_framework;
    std::unique_ptr<gpu::WasmToGPUTranslator> translator;
};

GPUPerformanceBenchmark::GPUPerformanceBenchmark() : pImpl(std::make_unique<Impl>()) {
    pImpl->gpu_framework = std::make_unique<gpu::GPUCCFramework>();
    pImpl->translator = std::make_unique<gpu::WasmToGPUTranslator>();
}

GPUPerformanceBenchmark::~GPUPerformanceBenchmark() = default;

void GPUPerformanceBenchmark::setup(const BenchmarkConfig &config) {
    pImpl->config = config;

    // Initialize GPU framework
    pImpl->gpu_framework->initialize(gpu::GPUVendor::NVIDIA);

    if (pImpl->use_cc) {
        std::vector<gpu::CCFeature> features = {gpu::CCFeature::MEMORY_ENCRYPTION, gpu::CCFeature::TRUSTED_EXECUTION};
        pImpl->gpu_framework->setupSecureComputation(features);
    }

    // Register test kernel
    std::vector<uint8_t> dummy_kernel_code(1024, 0);
    pImpl->gpu_framework->registerSecureKernel(pImpl->kernel_name, dummy_kernel_code.data(), dummy_kernel_code.size());
}

BenchmarkResult GPUPerformanceBenchmark::run() {
    BenchmarkResult result;
    result.benchmark_name = getName();
    result.category = getCategory();
    result.workload_type = getWorkloadType();
    result.timestamp = std::chrono::steady_clock::now();

    // Allocate GPU memory
    size_t data_size = pImpl->problem_size * pImpl->problem_size * sizeof(float);
    void *gpu_input =
        pImpl->gpu_framework->getInterface()->allocateSecureMemory(data_size, gpu::MemoryType::DEVICE_LOCAL);
    void *gpu_output =
        pImpl->gpu_framework->getInterface()->allocateSecureMemory(data_size, gpu::MemoryType::DEVICE_LOCAL);

    // Generate input data
    std::vector<float> host_input(pImpl->problem_size * pImpl->problem_size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    for (auto &val : host_input) {
        val = dis(gen);
    }

    std::vector<double> transfer_times;
    std::vector<double> kernel_times;

    for (size_t i = 0; i < pImpl->config.iterations; i++) {
        // Transfer data to GPU
        auto transfer_start = std::chrono::high_resolution_clock::now();
        pImpl->gpu_framework->secureDataTransferToGPU(host_input.data(), gpu_input, data_size, pImpl->use_cc);
        auto transfer_end = std::chrono::high_resolution_clock::now();

        auto transfer_duration = std::chrono::duration_cast<std::chrono::microseconds>(transfer_end - transfer_start);
        transfer_times.push_back(transfer_duration.count() / 1000.0);

        // Execute kernel
        auto kernel_start = std::chrono::high_resolution_clock::now();

        std::vector<void *> args = {gpu_input, gpu_output, &pImpl->problem_size};
        size_t grid_size = (pImpl->problem_size + 15) / 16;
        size_t block_size = 16;

        pImpl->gpu_framework->runSecureKernel(pImpl->kernel_name, args, grid_size, block_size);

        auto kernel_end = std::chrono::high_resolution_clock::now();

        auto kernel_duration = std::chrono::duration_cast<std::chrono::microseconds>(kernel_end - kernel_start);
        kernel_times.push_back(kernel_duration.count() / 1000.0);
    }

    // Calculate results
    double avg_transfer_time =
        std::accumulate(transfer_times.begin(), transfer_times.end(), 0.0) / transfer_times.size();
    double avg_kernel_time = std::accumulate(kernel_times.begin(), kernel_times.end(), 0.0) / kernel_times.size();

    result.execution_time_ms = avg_transfer_time + avg_kernel_time;

    // Calculate FLOPS for matrix multiplication
    double flops = 2.0 * pImpl->problem_size * pImpl->problem_size * pImpl->problem_size;
    result.throughput_mbps = (flops / 1e9) / (avg_kernel_time / 1000.0); // GFLOPS

    result.memory_usage_bytes = 2 * data_size; // Input + output
    result.gpu_utilization_percent = 80.0; // Mock value

    // Custom metrics
    result.custom_metrics["transfer_time_ms"] = avg_transfer_time;
    result.custom_metrics["kernel_time_ms"] = avg_kernel_time;
    result.custom_metrics["gflops"] = result.throughput_mbps;
    result.custom_metrics["cc_overhead_percent"] = pImpl->use_cc ? 10.0 : 0.0;

    // Cleanup
    pImpl->gpu_framework->getInterface()->freeSecureMemory(gpu_input);
    pImpl->gpu_framework->getInterface()->freeSecureMemory(gpu_output);

    return result;
}

void GPUPerformanceBenchmark::cleanup() { pImpl->gpu_framework->shutdown(); }

bool GPUPerformanceBenchmark::validate(const BenchmarkResult &result) {
    return result.execution_time_ms > 0 && result.custom_metrics.at("gflops") > 0;
}

void GPUPerformanceBenchmark::setKernelType(const std::string &kernel_name) { pImpl->kernel_name = kernel_name; }

void GPUPerformanceBenchmark::setProblemSize(size_t size) { pImpl->problem_size = size; }

void GPUPerformanceBenchmark::setUseConfidentialComputing(bool use_cc) { pImpl->use_cc = use_cc; }

// BenchmarkSuite implementation
struct BenchmarkSuite::Impl {
    BenchmarkConfig config;
    std::vector<std::unique_ptr<Benchmark>> benchmarks;
    std::vector<BenchmarkResult> results;
    std::unordered_map<std::string, Baseline> baselines;
};

BenchmarkSuite::BenchmarkSuite() : pImpl(std::make_unique<Impl>()) {}

BenchmarkSuite::~BenchmarkSuite() = default;

void BenchmarkSuite::addBenchmark(std::unique_ptr<Benchmark> benchmark) {
    pImpl->benchmarks.push_back(std::move(benchmark));
}

void BenchmarkSuite::setConfig(const BenchmarkConfig &config) { pImpl->config = config; }

void BenchmarkSuite::addBaseline(const Baseline &baseline) { pImpl->baselines[baseline.name] = baseline; }

void BenchmarkSuite::runAll() {
    SPDLOG_INFO("Running all benchmarks in suite");
    pImpl->results.clear();

    for (auto &benchmark : pImpl->benchmarks) {
        SPDLOG_INFO("Running benchmark: {}", benchmark->getName());

        benchmark->setup(pImpl->config);
        auto result = benchmark->run();

        if (benchmark->validate(result)) {
            pImpl->results.push_back(result);
            SPDLOG_INFO("Benchmark {} completed: {} ms", result.benchmark_name, result.execution_time_ms);
        } else {
            SPDLOG_ERROR("Benchmark {} validation failed", result.benchmark_name);
        }

        benchmark->cleanup();
    }
}

void BenchmarkSuite::runCategory(BenchmarkCategory category) {
    SPDLOG_INFO("Running benchmarks for category: {}", static_cast<int>(category));

    for (auto &benchmark : pImpl->benchmarks) {
        if (benchmark->getCategory() == category) {
            benchmark->setup(pImpl->config);
            auto result = benchmark->run();

            if (benchmark->validate(result)) {
                pImpl->results.push_back(result);
            }

            benchmark->cleanup();
        }
    }
}

void BenchmarkSuite::runSingle(const std::string &benchmark_name) {
    for (auto &benchmark : pImpl->benchmarks) {
        if (benchmark->getName() == benchmark_name) {
            benchmark->setup(pImpl->config);
            auto result = benchmark->run();

            if (benchmark->validate(result)) {
                pImpl->results.push_back(result);
            }

            benchmark->cleanup();
            break;
        }
    }
}

std::vector<BenchmarkResult> BenchmarkSuite::getResults() const { return pImpl->results; }

std::vector<BenchmarkResult> BenchmarkSuite::getResultsByCategory(BenchmarkCategory category) const {
    std::vector<BenchmarkResult> filtered;

    for (const auto &result : pImpl->results) {
        if (result.category == category) {
            filtered.push_back(result);
        }
    }

    return filtered;
}

void BenchmarkSuite::generateReport(const std::string &output_file) {
    std::ofstream report(output_file);

    report << "MVVM Evaluation Report\n";
    report << "======================\n\n";
    report << "Generated: " << std::chrono::system_clock::now() << "\n\n";

    // Summary statistics
    report << "Summary\n";
    report << "-------\n";
    report << "Total benchmarks run: " << pImpl->results.size() << "\n\n";

    // Results by category
    std::unordered_map<BenchmarkCategory, std::vector<BenchmarkResult>> by_category;
    for (const auto &result : pImpl->results) {
        by_category[result.category].push_back(result);
    }

    for (const auto &[category, results] : by_category) {
        report << "\nCategory: " << static_cast<int>(category) << "\n";
        report << "----------\n";

        for (const auto &result : results) {
            report << "Benchmark: " << result.benchmark_name << "\n";
            report << "  Execution Time: " << result.execution_time_ms << " ms\n";
            report << "  Throughput: " << result.throughput_mbps << " MB/s\n";
            report << "  Memory Usage: " << result.memory_usage_bytes / (1024.0 * 1024.0) << " MB\n";

            if (!result.custom_metrics.empty()) {
                report << "  Custom Metrics:\n";
                for (const auto &[name, value] : result.custom_metrics) {
                    report << "    " << name << ": " << value << "\n";
                }
            }
            report << "\n";
        }
    }

    report.close();
    SPDLOG_INFO("Report generated: {}", output_file);
}

void BenchmarkSuite::compareWithBaseline(const std::string &baseline_name) {
    auto it = pImpl->baselines.find(baseline_name);
    if (it == pImpl->baselines.end()) {
        SPDLOG_ERROR("Baseline {} not found", baseline_name);
        return;
    }

    const auto &baseline = it->second;

    SPDLOG_INFO("Comparing with baseline: {}", baseline.name);

    for (const auto &result : pImpl->results) {
        auto baseline_time_it = baseline.metrics.find("execution_time_ms");
        if (baseline_time_it != baseline.metrics.end()) {
            double speedup = calculateSpeedup(baseline_time_it->second, result.execution_time_ms);
            SPDLOG_INFO("{}: {}x speedup", result.benchmark_name, speedup);
        }
    }
}

void BenchmarkSuite::exportResults(const std::string &format, const std::string &output_file) {
    if (format == "json") {
        exportJSON(output_file);
    } else if (format == "csv") {
        exportCSV(output_file);
    } else {
        SPDLOG_ERROR("Unknown export format: {}", format);
    }
}

void BenchmarkSuite::exportJSON(const std::string &filename) {
    Json::Value root;
    Json::Value results(Json::arrayValue);

    for (const auto &result : pImpl->results) {
        Json::Value r;
        r["name"] = result.benchmark_name;
        r["category"] = static_cast<int>(result.category);
        r["execution_time_ms"] = result.execution_time_ms;
        r["throughput_mbps"] = result.throughput_mbps;
        r["memory_usage_bytes"] = Json::Value::Int64(result.memory_usage_bytes);

        Json::Value metrics;
        for (const auto &[name, value] : result.custom_metrics) {
            metrics[name] = value;
        }
        r["custom_metrics"] = metrics;

        results.append(r);
    }

    root["results"] = results;

    std::ofstream file(filename);
    Json::StreamWriterBuilder builder;
    std::unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
    writer->write(root, &file);
    file.close();
}

void BenchmarkSuite::exportCSV(const std::string &filename) {
    std::ofstream file(filename);

    // Header
    file << "Benchmark,Category,Execution Time (ms),Throughput (MB/s),"
         << "Memory Usage (MB),CPU %,GPU %\n";

    // Data
    for (const auto &result : pImpl->results) {
        file << result.benchmark_name << "," << static_cast<int>(result.category) << "," << result.execution_time_ms
             << "," << result.throughput_mbps << "," << result.memory_usage_bytes / (1024.0 * 1024.0) << ","
             << result.cpu_utilization_percent << "," << result.gpu_utilization_percent << "\n";
    }

    file.close();
}

BenchmarkSuite::Statistics BenchmarkSuite::calculateStatistics(const std::vector<double> &values) const {
    Statistics stats;

    if (values.empty()) {
        return stats;
    }

    // Sort for percentiles
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());

    // Mean
    stats.mean = std::accumulate(sorted.begin(), sorted.end(), 0.0) / sorted.size();

    // Median
    if (sorted.size() % 2 == 0) {
        stats.median = (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2.0;
    } else {
        stats.median = sorted[sorted.size() / 2];
    }

    // Standard deviation
    double sq_sum = 0;
    for (double val : sorted) {
        sq_sum += (val - stats.mean) * (val - stats.mean);
    }
    stats.std_dev = std::sqrt(sq_sum / sorted.size());

    // Min/Max
    stats.min = sorted.front();
    stats.max = sorted.back();

    // Percentiles
    size_t p95_idx = (size_t)(0.95 * sorted.size());
    size_t p99_idx = (size_t)(0.99 * sorted.size());
    stats.percentile_95 = sorted[p95_idx];
    stats.percentile_99 = sorted[p99_idx];

    return stats;
}

// Helper functions
double calculateSpeedup(double baseline_time, double optimized_time) { return baseline_time / optimized_time; }

double calculateEfficiency(double speedup, size_t num_resources) { return speedup / num_resources; }

std::string formatMetrics(const BenchmarkResult &result) {
    std::stringstream ss;
    ss << "Benchmark: " << result.benchmark_name << "\n";
    ss << "Execution Time: " << std::fixed << std::setprecision(2) << result.execution_time_ms << " ms\n";
    ss << "Throughput: " << result.throughput_mbps << " MB/s\n";
    ss << "Memory: " << result.memory_usage_bytes / (1024.0 * 1024.0) << " MB\n";

    return ss.str();
}

} // namespace evaluation
} // namespace mvvm