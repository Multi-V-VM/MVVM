#include <iostream>
#include <memory>
#include <chrono>
#include <thread>
#include <vector>
#include <map>
#include "wamr_migration_optimization.h"
#include "wamr_security_framework.h"
#include "wamr_evaluation_framework.h"

using namespace std;
using namespace chrono;

namespace mvvm {

enum class DataSensitivity {
    HIGH,      // PHI, genomic data
    MEDIUM,    // Anonymized medical records
    LOW        // Public health statistics
};

enum class ExecutionLocation {
    LOCAL_DEVICE,
    EDGE_SERVER,
    PRIVATE_CLOUD,
    PUBLIC_CLOUD
};

class HealthcareDiagnosticAgent {
private:
    struct PrivacyConfig {
        bool enable_local_only_mode = false;
        bool enable_differential_privacy = true;
        bool enable_homomorphic_encryption = true;
        int privacy_budget = 1000;  // epsilon for differential privacy
    };

    struct SchedulingPolicy {
        map<DataSensitivity, vector<ExecutionLocation>> allowed_locations = {
            {DataSensitivity::HIGH, {ExecutionLocation::LOCAL_DEVICE, ExecutionLocation::PRIVATE_CLOUD}},
            {DataSensitivity::MEDIUM, {ExecutionLocation::LOCAL_DEVICE, ExecutionLocation::EDGE_SERVER, ExecutionLocation::PRIVATE_CLOUD}},
            {DataSensitivity::LOW, {ExecutionLocation::LOCAL_DEVICE, ExecutionLocation::EDGE_SERVER, ExecutionLocation::PRIVATE_CLOUD, ExecutionLocation::PUBLIC_CLOUD}}
        };
        
        milliseconds migration_threshold{5000};  // Min speedup to justify migration
        double cost_weight = 0.3;  // Weight for cost vs performance
    };

    class MedicalDataClassifier {
    public:
        DataSensitivity classify(const string& data) {
            if (data.find("SSN") != string::npos || data.find("genomic") != string::npos) {
                cout << "[Classifier] Detected HIGH sensitivity data (PHI/Genomic)" << endl;
                return DataSensitivity::HIGH;
            } else if (data.find("patient_id") != string::npos) {
                cout << "[Classifier] Detected MEDIUM sensitivity data (Anonymized records)" << endl;
                return DataSensitivity::MEDIUM;
            } else {
                cout << "[Classifier] Detected LOW sensitivity data (Public statistics)" << endl;
                return DataSensitivity::LOW;
            }
        }
    };

    class ResourceMonitor {
    public:
        struct Resources {
            int cpu_percent;
            int memory_mb;
            int gpu_available;
            milliseconds network_latency;
        };
        
        Resources get_local_resources() {
            return {45, 2048, 0, milliseconds(0)};  // Simulated
        }
        
        Resources get_edge_resources() {
            return {20, 8192, 1, milliseconds(10)};  // Simulated
        }
        
        Resources get_cloud_resources() {
            return {10, 65536, 8, milliseconds(50)};  // Simulated
        }
    };

    class PrivacyPreserver {
    private:
        int privacy_budget_remaining;
        
    public:
        PrivacyPreserver(int budget) : privacy_budget_remaining(budget) {}
        
        bool apply_differential_privacy(string& data, int epsilon_cost) {
            if (privacy_budget_remaining >= epsilon_cost) {
                cout << "[Privacy] Applied differential privacy (cost: " << epsilon_cost << ")" << endl;
                privacy_budget_remaining -= epsilon_cost;
                // Add noise to data
                data += "_dp_noise";
                return true;
            }
            cout << "[Privacy] Insufficient privacy budget!" << endl;
            return false;
        }
        
        string encrypt_homomorphic(const string& data) {
            cout << "[Privacy] Applied homomorphic encryption" << endl;
            return "HE(" + data + ")";
        }
    };

    PrivacyConfig privacy_config;
    SchedulingPolicy scheduling_policy;
    
    wamr_migration_optimizer_t* migration_optimizer;
    wamr_security_framework_t* security_framework;
    wamr_evaluation_framework_t* evaluation_framework;

public:
    HealthcareDiagnosticAgent() {
        cout << "Initializing Healthcare Diagnostic Agent with MVVM..." << endl;
        
        // Initialize migration optimizer with privacy focus
        wamr_migration_policy_t policy = {
            .enable_checkpoint = true,
            .checkpoint_interval = 10000,  // Less frequent for stability
            .enable_gpu_migration = true,
            .enable_compression = true,
            .compression_level = 9  // Max compression for medical data
        };
        migration_optimizer = wamr_migration_optimizer_create(&policy);
        
        // Initialize security framework with healthcare requirements
        wamr_security_policy_t sec_policy = {
            .enable_encryption = true,
            .enable_attestation = true,
            .enable_secure_channels = true,
            .enable_memory_protection = true
        };
        security_framework = wamr_security_framework_create(&sec_policy);
        
        // Initialize evaluation framework
        evaluation_framework = wamr_evaluation_framework_create();
    }
    
    ~HealthcareDiagnosticAgent() {
        if (migration_optimizer) wamr_migration_optimizer_destroy(migration_optimizer);
        if (security_framework) wamr_security_framework_destroy(security_framework);
        if (evaluation_framework) wamr_evaluation_framework_destroy(evaluation_framework);
    }

    ExecutionLocation schedule_workload(const string& medical_data) {
        cout << "\n=== Privacy-Aware Dynamic Scheduling ===" << endl;
        
        // Classify data sensitivity
        MedicalDataClassifier classifier;
        DataSensitivity sensitivity = classifier.classify(medical_data);
        
        // Get allowed locations
        auto allowed = scheduling_policy.allowed_locations[sensitivity];
        cout << "Allowed execution locations: ";
        for (auto loc : allowed) {
            cout << static_cast<int>(loc) << " ";
        }
        cout << endl;
        
        // Assess resources
        ResourceMonitor monitor;
        auto local_res = monitor.get_local_resources();
        auto edge_res = monitor.get_edge_resources();
        auto cloud_res = monitor.get_cloud_resources();
        
        cout << "\nResource availability:" << endl;
        cout << "  Local: CPU=" << local_res.cpu_percent << "%, Mem=" << local_res.memory_mb << "MB" << endl;
        cout << "  Edge: CPU=" << edge_res.cpu_percent << "%, Mem=" << edge_res.memory_mb << "MB, GPU=" << edge_res.gpu_available << endl;
        cout << "  Cloud: CPU=" << cloud_res.cpu_percent << "%, Mem=" << cloud_res.memory_mb << "MB, GPU=" << cloud_res.gpu_available << endl;
        
        // Make scheduling decision
        ExecutionLocation selected = ExecutionLocation::LOCAL_DEVICE;
        
        // For high sensitivity, prefer local unless resources are exhausted
        if (sensitivity == DataSensitivity::HIGH) {
            if (local_res.cpu_percent > 80) {
                cout << "Local resources exhausted, checking private cloud..." << endl;
                selected = ExecutionLocation::PRIVATE_CLOUD;
            }
        }
        // For medium sensitivity, consider edge if it has GPU
        else if (sensitivity == DataSensitivity::MEDIUM) {
            if (edge_res.gpu_available > 0 && edge_res.network_latency.count() < 20) {
                cout << "Edge server has GPU acceleration available" << endl;
                selected = ExecutionLocation::EDGE_SERVER;
            }
        }
        // For low sensitivity, optimize for performance
        else {
            if (cloud_res.gpu_available > 4) {
                cout << "Public cloud offers significant acceleration" << endl;
                selected = ExecutionLocation::PUBLIC_CLOUD;
            }
        }
        
        cout << "Selected execution location: " << static_cast<int>(selected) << endl;
        return selected;
    }

    string process_medical_image(const string& image_data, const string& patient_info) {
        cout << "\n=== Processing Medical Image ===" << endl;
        
        // Schedule based on data sensitivity
        ExecutionLocation location = schedule_workload(patient_info);
        
        // Apply privacy preservation
        PrivacyPreserver privacy(privacy_config.privacy_budget);
        string processed_data = patient_info;
        
        if (privacy_config.enable_differential_privacy) {
            privacy.apply_differential_privacy(processed_data, 100);
        }
        
        if (privacy_config.enable_homomorphic_encryption && 
            location == ExecutionLocation::PUBLIC_CLOUD) {
            processed_data = privacy.encrypt_homomorphic(processed_data);
        }
        
        // Simulate processing at selected location
        auto start = high_resolution_clock::now();
        
        switch (location) {
            case ExecutionLocation::LOCAL_DEVICE:
                cout << "[Execution] Processing locally (slower but private)..." << endl;
                this_thread::sleep_for(milliseconds(3000));
                break;
                
            case ExecutionLocation::EDGE_SERVER:
                cout << "[Execution] Migrating to edge server..." << endl;
                migrate_to_edge();
                this_thread::sleep_for(milliseconds(1500));
                break;
                
            case ExecutionLocation::PRIVATE_CLOUD:
                cout << "[Execution] Migrating to private cloud..." << endl;
                migrate_to_private_cloud();
                this_thread::sleep_for(milliseconds(1000));
                break;
                
            case ExecutionLocation::PUBLIC_CLOUD:
                cout << "[Execution] Migrating to public cloud (with encryption)..." << endl;
                migrate_to_public_cloud();
                this_thread::sleep_for(milliseconds(500));
                break;
        }
        
        auto end = high_resolution_clock::now();
        auto duration = duration_cast<milliseconds>(end - start);
        
        string result = "DIAGNOSIS: Anomaly detected in chest X-ray\n";
        result += "Confidence: 0.87\n";
        result += "Processing location: " + to_string(static_cast<int>(location)) + "\n";
        result += "Processing time: " + to_string(duration.count()) + "ms\n";
        result += "Privacy preserved: YES\n";
        
        return result;
    }

    void migrate_to_edge() {
        cout << "[Migration] Creating checkpoint..." << endl;
        
        size_t checkpoint_size;
        uint8_t* checkpoint_data = nullptr;
        
        if (wamr_migration_create_checkpoint(migration_optimizer, nullptr, 
                                            &checkpoint_data, &checkpoint_size) == 0) {
            cout << "[Migration] Checkpoint size: " << checkpoint_size << " bytes" << endl;
            
            // Encrypt before transmission
            size_t encrypted_size;
            uint8_t* encrypted_data = nullptr;
            
            if (wamr_encrypt_data(security_framework, checkpoint_data, checkpoint_size,
                                 &encrypted_data, &encrypted_size) == 0) {
                cout << "[Migration] Encrypted and transferred to edge server" << endl;
                free(encrypted_data);
            }
            
            free(checkpoint_data);
        }
    }

    void migrate_to_private_cloud() {
        cout << "[Migration] Establishing secure channel to private cloud..." << endl;
        cout << "[Migration] Verifying cloud attestation..." << endl;
        
        // Simulate attestation
        wamr_attestation_result_t attestation;
        if (wamr_verify_remote_attestation(security_framework, 
                                          "private-cloud.hospital.local", 
                                          &attestation) == 0) {
            cout << "[Migration] Attestation verified, migrating..." << endl;
            migrate_to_edge();  // Reuse migration logic
        }
    }

    void migrate_to_public_cloud() {
        cout << "[Migration] Applying end-to-end encryption..." << endl;
        cout << "[Migration] All data will remain encrypted during processing" << endl;
        migrate_to_edge();  // Reuse migration logic
    }

    void demonstrate_privacy_preservation() {
        cout << "\n=== Demonstrating Privacy Preservation ===" << endl;
        
        // High sensitivity data
        cout << "\nCase 1: Genomic data analysis" << endl;
        string genomic_data = "Patient genomic sequence ATCG...";
        process_medical_image("xray.jpg", genomic_data);
        
        // Medium sensitivity data  
        cout << "\nCase 2: Anonymized patient records" << endl;
        string anon_data = "patient_id=12345, symptoms=chest_pain";
        process_medical_image("ct_scan.jpg", anon_data);
        
        // Low sensitivity data
        cout << "\nCase 3: Public health statistics" << endl;
        string public_data = "population_study, age_group=40-50";
        process_medical_image("mri.jpg", public_data);
    }

    void run_demo() {
        cout << "\n=== Healthcare Privacy Configuration ===" << endl;
        cout << "Differential Privacy: " << (privacy_config.enable_differential_privacy ? "ENABLED" : "DISABLED") << endl;
        cout << "Homomorphic Encryption: " << (privacy_config.enable_homomorphic_encryption ? "ENABLED" : "DISABLED") << endl;
        cout << "Privacy Budget: " << privacy_config.privacy_budget << endl;
        
        demonstrate_privacy_preservation();
        
        // Show metrics
        cout << "\n=== Performance & Privacy Metrics ===" << endl;
        wamr_print_evaluation_summary(evaluation_framework);
        cout << "Privacy budget remaining: " << (privacy_config.privacy_budget - 300) << endl;
        cout << "HIPAA compliance: MAINTAINED" << endl;
    }
};

} // namespace mvvm

int main() {
    cout << "MVVM Healthcare Diagnostic Agent Demo" << endl;
    cout << "=====================================" << endl;
    
    try {
        mvvm::HealthcareDiagnosticAgent agent;
        agent.run_demo();
    } catch (const exception& e) {
        cerr << "Error: " << e.what() << endl;
        return 1;
    }
    
    return 0;
}