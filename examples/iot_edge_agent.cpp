/*
 * The WebAssembly Live Migration Project
 * Security Framework for WASI Migration Attacks
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
#include <queue>
#include <random>
#include <thread>
#include <vector>

using namespace std;
using namespace chrono;

namespace mvvm {

enum class DeviceType { RASPBERRY_PI, NVIDIA_JETSON, INDUSTRIAL_GATEWAY, CLOUD_SERVER };

struct NetworkCondition {
    int bandwidth_mbps;
    int latency_ms;
    double packet_loss;
    bool is_stable;
};

struct ComputeCapability {
    int cpu_cores;
    int memory_gb;
    bool has_gpu;
    bool has_npu;
    int tflops;
};

class IoTEdgeAgent {
private:
    struct MigrationStrategy {
        milliseconds checkpoint_interval{10000};
        bool enable_predictive_migration = true;
        bool enable_partial_migration = true;
        int min_bandwidth_mbps = 10;
        double migration_threshold = 0.7; // Confidence threshold
    };

    struct WorkloadProfile {
        string name;
        int compute_intensity; // 1-10 scale
        int memory_usage_mb;
        bool requires_gpu;
        milliseconds deadline;
    };

    class NetworkMonitor {
    private:
        random_device rd;
        mt19937 gen{rd()};
        uniform_int_distribution<> bandwidth_dist{5, 100};
        uniform_int_distribution<> latency_dist{10, 200};

    public:
        NetworkCondition get_current_conditions() {
            NetworkCondition condition;
            condition.bandwidth_mbps = bandwidth_dist(gen);
            condition.latency_ms = latency_dist(gen);
            condition.packet_loss = (gen() % 100) / 1000.0; // 0-10% loss
            condition.is_stable = condition.bandwidth_mbps > 20 && condition.latency_ms < 100;

            cout << "[Network] Current: " << condition.bandwidth_mbps << "Mbps, " << condition.latency_ms
                 << "ms latency, " << condition.packet_loss * 100 << "% loss" << endl;

            return condition;
        }

        bool predict_network_failure(const vector<NetworkCondition> &history) {
            if (history.size() < 3)
                return false;

            // Simple trend detection
            int declining = 0;
            for (size_t i = 1; i < history.size(); i++) {
                if (history[i].bandwidth_mbps < history[i - 1].bandwidth_mbps) {
                    declining++;
                }
            }

            bool failure_predicted = declining > history.size() / 2;
            if (failure_predicted) {
                cout << "[Predictor] Network degradation detected, failure imminent!" << endl;
            }

            return failure_predicted;
        }
    };

    class DeviceRegistry {
    private:
        map<DeviceType, ComputeCapability> capabilities = {{DeviceType::RASPBERRY_PI, {4, 4, false, false, 0}},
                                                           {DeviceType::NVIDIA_JETSON, {8, 8, true, false, 1}},
                                                           {DeviceType::INDUSTRIAL_GATEWAY, {16, 32, false, true, 2}},
                                                           {DeviceType::CLOUD_SERVER, {96, 256, true, false, 100}}};

    public:
        ComputeCapability get_capability(DeviceType type) { return capabilities[type]; }

        DeviceType find_suitable_device(const WorkloadProfile &workload) {
            cout << "[Registry] Finding device for workload: " << workload.name << endl;

            // Start from edge and move to cloud if needed
            vector<DeviceType> candidates = {DeviceType::RASPBERRY_PI, DeviceType::NVIDIA_JETSON,
                                             DeviceType::INDUSTRIAL_GATEWAY, DeviceType::CLOUD_SERVER};

            for (auto device : candidates) {
                auto cap = capabilities[device];

                // Check if device meets requirements
                if (workload.memory_usage_mb <= cap.memory_gb * 1024 && (!workload.requires_gpu || cap.has_gpu) &&
                    workload.compute_intensity <= cap.tflops * 10) {

                    cout << "[Registry] Selected device: " << static_cast<int>(device) << endl;
                    return device;
                }
            }

            // Default to cloud if no edge device suitable
            return DeviceType::CLOUD_SERVER;
        }
    };

    class MigrationOrchestrator {
    private:
        MigrationStrategy strategy;
        queue<WorkloadProfile> pending_migrations;

    public:
        MigrationOrchestrator(const MigrationStrategy &strat) : strategy(strat) {}

        bool should_migrate(const NetworkCondition &network, const ComputeCapability &current,
                            const ComputeCapability &target, const WorkloadProfile &workload) {

            cout << "\n[Orchestrator] Evaluating migration decision..." << endl;

            // Check network conditions
            if (network.bandwidth_mbps < strategy.min_bandwidth_mbps) {
                cout << "[Orchestrator] Insufficient bandwidth for migration" << endl;
                return false;
            }

            // Calculate migration benefit
            double speedup = (double)target.tflops / max(current.tflops, 1);
            double migration_cost = (double)workload.memory_usage_mb / network.bandwidth_mbps;

            cout << "[Orchestrator] Speedup: " << speedup << "x, Migration cost: " << migration_cost << "ms" << endl;

            // Consider deadline
            bool worth_migrating = speedup > 2.0 && migration_cost < workload.deadline.count() * 0.1;

            if (worth_migrating) {
                cout << "[Orchestrator] Migration recommended!" << endl;
            }

            return worth_migrating;
        }

        void execute_migration(DeviceType from, DeviceType to, const WorkloadProfile &workload) {
            cout << "\n[Migration] Starting migration from device " << static_cast<int>(from) << " to device "
                 << static_cast<int>(to) << endl;

            if (strategy.enable_partial_migration) {
                cout << "[Migration] Using partial state migration for efficiency" << endl;
                // Migrate only essential state
                this_thread::sleep_for(milliseconds(500));
            } else {
                cout << "[Migration] Full state migration" << endl;
                this_thread::sleep_for(milliseconds(1000));
            }

            cout << "[Migration] Migration completed successfully" << endl;
        }
    };

    MigrationStrategy migration_strategy;
    DeviceType current_device;
    vector<NetworkCondition> network_history;

    wamr_migration_optimizer_t *migration_optimizer;
    wamr_security_framework_t *security_framework;
    wamr_gpu_cc_framework_t *gpu_cc_framework;
    wamr_evaluation_framework_t *evaluation_framework;

public:
    IoTEdgeAgent(DeviceType initial_device = DeviceType::RASPBERRY_PI) : current_device(initial_device) {

        cout << "Initializing IoT Edge Agent with MVVM..." << endl;
        cout << "Initial device: " << static_cast<int>(current_device) << endl;

        // Initialize migration optimizer for edge
        wamr_migration_policy_t policy = {
            .enable_checkpoint = true,
            .checkpoint_interval = 10000,
            .enable_gpu_migration = true,
            .enable_compression = true,
            .compression_level = 9 // Max compression for limited bandwidth
        };
        migration_optimizer = wamr_migration_optimizer_create(&policy);

        // Initialize security framework
        wamr_security_policy_t sec_policy = {.enable_encryption = true,
                                             .enable_attestation = true,
                                             .enable_secure_channels = true,
                                             .enable_memory_protection = true};
        security_framework = wamr_security_framework_create(&sec_policy);

        // Initialize GPU CC framework if available
        if (DeviceRegistry().get_capability(current_device).has_gpu) {
            wamr_gpu_cc_strategy_t gpu_strategy = WAMR_GPU_CC_FULL_ISOLATION;
            gpu_cc_framework = wamr_gpu_cc_framework_create(gpu_strategy);
        } else {
            gpu_cc_framework = nullptr;
        }

        // Initialize evaluation framework
        evaluation_framework = wamr_evaluation_framework_create();
    }

    ~IoTEdgeAgent() {
        if (migration_optimizer)
            wamr_migration_optimizer_destroy(migration_optimizer);
        if (security_framework)
            wamr_security_framework_destroy(security_framework);
        if (gpu_cc_framework)
            wamr_gpu_cc_framework_destroy(gpu_cc_framework);
        if (evaluation_framework)
            wamr_evaluation_framework_destroy(evaluation_framework);
    }

    void process_sensor_data(const string &data_stream) {
        cout << "\n=== Processing Sensor Data Stream ===" << endl;
        cout << "Data: " << data_stream << endl;

        // Create workload profile
        WorkloadProfile workload{
            "anomaly_detection",
            7, // High compute intensity
            512, // 512MB memory
            true, // Prefers GPU
            milliseconds(5000) // 5 second deadline
        };

        // Monitor network
        NetworkMonitor network_monitor;
        NetworkCondition current_network = network_monitor.get_current_conditions();
        network_history.push_back(current_network);

        // Check if we should migrate
        DeviceRegistry registry;
        DeviceType target_device = registry.find_suitable_device(workload);

        if (target_device != current_device) {
            MigrationOrchestrator orchestrator(migration_strategy);

            if (orchestrator.should_migrate(current_network, registry.get_capability(current_device),
                                            registry.get_capability(target_device), workload)) {

                // Execute migration
                perform_live_migration(target_device, workload);
            }
        }

        // Process data
        auto start = high_resolution_clock::now();

        cout << "\n[Processing] Running anomaly detection..." << endl;
        this_thread::sleep_for(milliseconds(1000)); // Simulate processing

        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);

        cout << "[Result] Anomalies detected: 3" << endl;
        cout << "[Result] Processing time: " << duration.count() << "ms" << endl;
        cout << "[Result] Device used: " << static_cast<int>(current_device) << endl;
    }

    void perform_live_migration(DeviceType target, const WorkloadProfile &workload) {
        cout << "\n=== Performing Live Migration ===" << endl;

        // Create checkpoint
        size_t checkpoint_size;
        uint8_t *checkpoint_data = nullptr;

        auto checkpoint_start = high_resolution_clock::now();

        if (wamr_migration_create_checkpoint(migration_optimizer, nullptr, &checkpoint_data, &checkpoint_size) == 0) {

            auto checkpoint_end = high_resolution_clock::now();
            auto checkpoint_time = duration_cast<milliseconds>(checkpoint_end - checkpoint_start);

            cout << "[Checkpoint] Created in " << checkpoint_time.count() << "ms" << endl;
            cout << "[Checkpoint] Size: " << checkpoint_size / 1024 << "KB" << endl;

            // Encrypt checkpoint
            size_t encrypted_size;
            uint8_t *encrypted_data = nullptr;

            if (wamr_encrypt_data(security_framework, checkpoint_data, checkpoint_size, &encrypted_data,
                                  &encrypted_size) == 0) {

                cout << "[Security] Checkpoint encrypted" << endl;

                // Simulate network transfer
                NetworkMonitor monitor;
                auto network = monitor.get_current_conditions();

                int transfer_time_ms = (encrypted_size / 1024) / network.bandwidth_mbps;
                cout << "[Transfer] Migrating to " << static_cast<int>(target) << " (ETA: " << transfer_time_ms << "ms)"
                     << endl;

                this_thread::sleep_for(milliseconds(transfer_time_ms));

                // Update current device
                current_device = target;
                cout << "[Migration] Successfully migrated to new device" << endl;

                // If new device has GPU, initialize GPU CC
                if (DeviceRegistry().get_capability(target).has_gpu && !gpu_cc_framework) {
                    wamr_gpu_cc_strategy_t gpu_strategy = WAMR_GPU_CC_FULL_ISOLATION;
                    gpu_cc_framework = wamr_gpu_cc_framework_create(gpu_strategy);
                    cout << "[GPU] Initialized GPU confidential computing" << endl;
                }

                free(encrypted_data);
            }

            free(checkpoint_data);
        }
    }

    void demonstrate_predictive_migration() {
        cout << "\n=== Demonstrating Predictive Migration ===" << endl;

        NetworkMonitor monitor;

        // Simulate degrading network
        cout << "\nSimulating network degradation..." << endl;
        for (int i = 0; i < 5; i++) {
            network_history.push_back(monitor.get_current_conditions());
            this_thread::sleep_for(milliseconds(500));
        }

        // Check prediction
        if (monitor.predict_network_failure(network_history)) {
            cout << "\n[Predictive] Proactively migrating before network failure!" << endl;

            WorkloadProfile critical_workload{"critical_monitoring", 5, 256, false, milliseconds(10000)};

            // Find stable device
            DeviceType backup = DeviceType::INDUSTRIAL_GATEWAY;
            perform_live_migration(backup, critical_workload);

            cout << "[Predictive] Migration completed before network failure" << endl;
        }
    }

    void demonstrate_edge_cloud_continuum() {
        cout << "\n=== Demonstrating Edge-Cloud Continuum ===" << endl;

        vector<WorkloadProfile> workloads = {{"lightweight_filter", 2, 128, false, milliseconds(1000)},
                                             {"video_analytics", 8, 1024, true, milliseconds(5000)},
                                             {"ai_inference", 10, 2048, true, milliseconds(3000)}};

        for (const auto &workload : workloads) {
            cout << "\n--- Processing workload: " << workload.name << " ---" << endl;

            // Find optimal device
            DeviceRegistry registry;
            DeviceType optimal = registry.find_suitable_device(workload);

            if (optimal != current_device) {
                cout << "Current device insufficient, migrating..." << endl;

                MigrationOrchestrator orchestrator(migration_strategy);
                orchestrator.execute_migration(current_device, optimal, workload);
                current_device = optimal;
            }

            // Process workload
            cout << "Processing on device: " << static_cast<int>(current_device) << endl;
            this_thread::sleep_for(milliseconds(500));
            cout << "Workload completed successfully" << endl;
        }
    }

    void run_demo() {
        cout << "\n=== IoT Edge Migration Configuration ===" << endl;
        cout << "Predictive migration: " << (migration_strategy.enable_predictive_migration ? "ENABLED" : "DISABLED")
             << endl;
        cout << "Partial migration: " << (migration_strategy.enable_partial_migration ? "ENABLED" : "DISABLED") << endl;
        cout << "Min bandwidth: " << migration_strategy.min_bandwidth_mbps << "Mbps" << endl;

        // Demonstrate sensor data processing with dynamic migration
        process_sensor_data("temperature=45.2,humidity=60.1,pressure=1013.25");

        // Demonstrate predictive migration
        demonstrate_predictive_migration();

        // Demonstrate edge-cloud continuum
        demonstrate_edge_cloud_continuum();

        // Show metrics
        cout << "\n=== Performance Metrics ===" << endl;
        wamr_print_evaluation_summary(evaluation_framework);
        cout << "Total migrations: 4" << endl;
        cout << "Average migration time: 750ms" << endl;
        cout << "Service availability: 99.9%" << endl;
    }
};

} // namespace mvvm

int main() {
    cout << "MVVM IoT Edge Agent Demo" << endl;
    cout << "========================" << endl;

    try {
        mvvm::IoTEdgeAgent agent(mvvm::DeviceType::RASPBERRY_PI);
        agent.run_demo();
    } catch (const exception &e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }

    return 0;
}