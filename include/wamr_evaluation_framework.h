/*
 * The WebAssembly Live Migration Project
 * Evaluation Framework and Benchmarks
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#ifndef WAMR_EVALUATION_FRAMEWORK_H
#define WAMR_EVALUATION_FRAMEWORK_H

#include "wamr_gpu_cc_framework.h"
#include "wamr_migration_optimization.h"
#include "wamr_security_framework.h"
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mvvm {
namespace evaluation {

// Benchmark categories
enum class BenchmarkCategory {
    MIGRATION_OVERHEAD, // Migration latency and throughput
    SECURITY_OVERHEAD, // Security feature performance impact
    GPU_PERFORMANCE, // GPU acceleration effectiveness
    MEMORY_EFFICIENCY, // Memory usage and optimization
    NETWORK_PERFORMANCE, // Network transfer efficiency
    SCALABILITY // Scaling with workload size
};

// Workload types for testing
enum class WorkloadType {
    CPU_INTENSIVE, // Computation-heavy workloads
    MEMORY_INTENSIVE, // Memory access patterns
    IO_INTENSIVE, // I/O bound workloads
    GPU_COMPUTE, // GPU computation workloads
    MIXED, // Balanced workloads
    SYNTHETIC // Synthetic benchmarks
};

// Benchmark result
struct BenchmarkResult {
    std::string benchmark_name;
    BenchmarkCategory category;
    WorkloadType workload_type;
    double execution_time_ms;
    double throughput_mbps;
    size_t memory_usage_bytes;
    double cpu_utilization_percent;
    double gpu_utilization_percent;
    std::unordered_map<std::string, double> custom_metrics;
    std::chrono::time_point<std::chrono::steady_clock> timestamp;
};

// Comparison baseline
struct Baseline {
    std::string name;
    std::string description;
    std::unordered_map<std::string, double> metrics;
};

// Benchmark configuration
struct BenchmarkConfig {
    size_t iterations = 10;
    size_t warmup_iterations = 2;
    bool enable_profiling = true;
    bool enable_validation = true;
    std::vector<size_t> problem_sizes = {1024, 4096, 16384, 65536};
    std::vector<MigrationStrategy> migration_strategies = {
        MigrationStrategy::INCREMENTAL, MigrationStrategy::COMPRESSION, MigrationStrategy::DELTA_ENCODING};
    std::vector<security::SecurityPolicy> security_policies = {security::SecurityPolicy::POLICY_MINIMAL,
                                                               security::SecurityPolicy::POLICY_BALANCED,
                                                               security::SecurityPolicy::POLICY_STRICT};
};

// Abstract benchmark interface
class Benchmark {
public:
    virtual ~Benchmark() = default;

    virtual std::string getName() const = 0;
    virtual BenchmarkCategory getCategory() const = 0;
    virtual WorkloadType getWorkloadType() const = 0;

    virtual void setup(const BenchmarkConfig &config) = 0;
    virtual BenchmarkResult run() = 0;
    virtual void cleanup() = 0;

    virtual bool validate(const BenchmarkResult &result) = 0;
};

// Migration overhead benchmark
class MigrationOverheadBenchmark : public Benchmark {
public:
    MigrationOverheadBenchmark();
    ~MigrationOverheadBenchmark() override;

    std::string getName() const override { return "Migration Overhead"; }
    BenchmarkCategory getCategory() const override { return BenchmarkCategory::MIGRATION_OVERHEAD; }
    WorkloadType getWorkloadType() const override { return WorkloadType::MIXED; }

    void setup(const BenchmarkConfig &config) override;
    BenchmarkResult run() override;
    void cleanup() override;
    bool validate(const BenchmarkResult &result) override;

    // Configure specific parameters
    void setStateSize(size_t size);
    void setMigrationStrategy(MigrationStrategy strategy);
    void setNetworkBandwidth(double bandwidth_mbps);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Security overhead benchmark
class SecurityOverheadBenchmark : public Benchmark {
public:
    SecurityOverheadBenchmark();
    ~SecurityOverheadBenchmark() override;

    std::string getName() const override { return "Security Overhead"; }
    BenchmarkCategory getCategory() const override { return BenchmarkCategory::SECURITY_OVERHEAD; }
    WorkloadType getWorkloadType() const override { return WorkloadType::CPU_INTENSIVE; }

    void setup(const BenchmarkConfig &config) override;
    BenchmarkResult run() override;
    void cleanup() override;
    bool validate(const BenchmarkResult &result) override;

    void setSecurityPolicy(security::SecurityPolicy policy);
    void setDataSize(size_t size);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// GPU performance benchmark
class GPUPerformanceBenchmark : public Benchmark {
public:
    GPUPerformanceBenchmark();
    ~GPUPerformanceBenchmark() override;

    std::string getName() const override { return "GPU Performance"; }
    BenchmarkCategory getCategory() const override { return BenchmarkCategory::GPU_PERFORMANCE; }
    WorkloadType getWorkloadType() const override { return WorkloadType::GPU_COMPUTE; }

    void setup(const BenchmarkConfig &config) override;
    BenchmarkResult run() override;
    void cleanup() override;
    bool validate(const BenchmarkResult &result) override;

    void setKernelType(const std::string &kernel_name);
    void setProblemSize(size_t size);
    void setUseConfidentialComputing(bool use_cc);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Benchmark suite for running multiple benchmarks
class BenchmarkSuite {
public:
    BenchmarkSuite();
    ~BenchmarkSuite();

    // Add benchmarks to suite
    void addBenchmark(std::unique_ptr<Benchmark> benchmark);

    // Configure suite
    void setConfig(const BenchmarkConfig &config);
    void addBaseline(const Baseline &baseline);

    // Run benchmarks
    void runAll();
    void runCategory(BenchmarkCategory category);
    void runSingle(const std::string &benchmark_name);

    // Get results
    std::vector<BenchmarkResult> getResults() const;
    std::vector<BenchmarkResult> getResultsByCategory(BenchmarkCategory category) const;

    // Analysis and reporting
    void generateReport(const std::string &output_file);
    void compareWithBaseline(const std::string &baseline_name);
    void exportResults(const std::string &format, const std::string &output_file);

    // Export methods
    void exportJSON(const std::string &filename);
    void exportCSV(const std::string &filename);

    // Statistical analysis
    struct Statistics {
        double mean;
        double median;
        double std_dev;
        double min;
        double max;
        double percentile_95;
        double percentile_99;
    };

    Statistics calculateStatistics(const std::vector<double> &values) const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Performance profiler
class PerformanceProfiler {
public:
    PerformanceProfiler();
    ~PerformanceProfiler();

    // Profiling control
    void startProfiling();
    void stopProfiling();
    void reset();

    // Add measurement points
    void recordEvent(const std::string &event_name);
    void recordMetric(const std::string &metric_name, double value);
    void recordMemoryUsage();
    void recordCPUUsage();
    void recordGPUUsage();

    // Get profiling data
    struct ProfilingData {
        std::unordered_map<std::string, std::vector<double>> event_timings;
        std::unordered_map<std::string, std::vector<double>> metrics;
        std::vector<std::pair<std::chrono::time_point<std::chrono::steady_clock>, size_t>> memory_samples;
        std::vector<std::pair<std::chrono::time_point<std::chrono::steady_clock>, double>> cpu_samples;
        std::vector<std::pair<std::chrono::time_point<std::chrono::steady_clock>, double>> gpu_samples;
    };

    ProfilingData getData() const;
    void exportData(const std::string &filename);

    // Analysis
    void generateFlameGraph(const std::string &output_file);
    void generateTimeline(const std::string &output_file);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Workload generator for testing
class WorkloadGenerator {
public:
    WorkloadGenerator();
    ~WorkloadGenerator();

    // Generate different workload patterns
    std::vector<uint8_t> generateCPUIntensiveWorkload(size_t size);
    std::vector<uint8_t> generateMemoryIntensiveWorkload(size_t size);
    std::vector<uint8_t> generateIOIntensiveWorkload(size_t size);
    std::vector<uint8_t> generateGPUWorkload(size_t size);

    // Synthetic workloads
    void *generateMatrixMultiplication(size_t matrix_size);
    void *generateSortingWorkload(size_t array_size);
    void *generateGraphTraversal(size_t num_nodes, size_t num_edges);
    void *generateMLInference(const std::string &model_type, size_t batch_size);

    // Configure workload characteristics
    void setSeed(uint32_t seed);
    void setDistribution(const std::string &distribution_type);
    void setAccessPattern(const std::string &pattern);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Result analyzer and visualizer
class ResultAnalyzer {
public:
    ResultAnalyzer();
    ~ResultAnalyzer();

    // Load results
    void loadResults(const std::vector<BenchmarkResult> &results);
    void loadFromFile(const std::string &filename);

    // Analysis functions
    void analyzeOverhead();
    void analyzeScalability();
    void analyzeBottlenecks();
    void compareStrategies();

    // Visualization
    void plotPerformanceComparison(const std::string &output_file);
    void plotScalabilityGraph(const std::string &output_file);
    void plotOverheadBreakdown(const std::string &output_file);
    void generateHeatmap(const std::string &metric, const std::string &output_file);

    // Export formats
    void exportCSV(const std::string &filename);
    void exportJSON(const std::string &filename);
    void exportLatex(const std::string &filename);

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Real-world application benchmarks
class ApplicationBenchmark {
public:
    ApplicationBenchmark();
    ~ApplicationBenchmark();

    // Configure application
    void loadApplication(const std::string &wasm_file);
    void setInputData(const void *data, size_t size);
    void setMigrationPoints(const std::vector<size_t> &instruction_counts);

    // Run application with migration
    struct ApplicationResult {
        double total_execution_time_ms;
        std::vector<double> migration_latencies_ms;
        double application_throughput;
        size_t total_migrations;
        double average_state_size_mb;
    };

    ApplicationResult runWithMigration();

    // Specific application benchmarks
    ApplicationResult runRedis();
    ApplicationResult runNginx();
    ApplicationResult runSQLite();
    ApplicationResult runTensorFlowLite();
    ApplicationResult runFFmpeg();

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Helper functions
double calculateSpeedup(double baseline_time, double optimized_time);
double calculateEfficiency(double speedup, size_t num_resources);
std::string formatMetrics(const BenchmarkResult &result);

} // namespace evaluation
} // namespace mvvm

#endif // WAMR_EVALUATION_FRAMEWORK_H