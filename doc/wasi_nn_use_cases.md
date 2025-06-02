# WASI-NN Record and Replay Use Cases

## Table of Contents
1. [Edge Computing Scenarios](#edge-computing-scenarios)
2. [Cloud and Serverless Applications](#cloud-and-serverless-applications)
3. [Research and Development](#research-and-development)
4. [Production Deployment Patterns](#production-deployment-patterns)
5. [Security and Compliance](#security-and-compliance)
6. [IoT and Embedded Systems](#iot-and-embedded-systems)

## Edge Computing Scenarios

### 1. Smart City Traffic Management

**Background**: A smart city deploys AI-powered traffic cameras at intersections. Each camera runs a WASM application with vehicle detection and traffic flow analysis models.

**Challenge**: Cameras need maintenance, hardware upgrades, or load balancing without interrupting traffic monitoring.

**Solution with WASI-NN Record/Replay**:

```cpp
// Traffic Camera AI Module
class TrafficAnalyzer {
private:
    WAMRWASINNContext* ctx;
    graph vehicle_detection_model;
    graph traffic_flow_model;
    
public:
    void initialize() {
        // Load vehicle detection model
        wamr_wasi_nn_load_with_recording(exec_env, builder, size,
            tensorflowlite, gpu, &vehicle_detection_model, "yolo_v5_traffic.tflite");
            
        // Load traffic flow analysis model
        wamr_wasi_nn_load_with_recording(exec_env, builder, size,
            tensorflowlite, cpu, &traffic_flow_model, "traffic_flow_lstm.tflite");
    }
    
    TrafficData analyze_frame(const ImageFrame& frame) {
        // Detect vehicles
        auto vehicles = detect_vehicles(frame);
        
        // Analyze traffic flow
        auto flow_data = analyze_traffic_flow(vehicles);
        
        return {vehicles, flow_data, frame.timestamp};
    }
    
    void migrate_to_backup_camera(const std::string& backup_address) {
        // Create checkpoint
        ctx->dump_impl(&checkpoint_data);
        
        // Transfer to backup camera
        send_checkpoint(backup_address, checkpoint_data);
        
        // Backup camera restores and continues monitoring
    }
};

// Usage during maintenance
void perform_camera_maintenance(int camera_id) {
    auto backup_camera = find_available_backup();
    
    // Migrate workload
    cameras[camera_id]->migrate_to_backup_camera(backup_camera->address);
    
    // Perform maintenance
    maintain_camera(camera_id);
    
    // Migrate back after maintenance
    backup_camera->migrate_back(cameras[camera_id]->address);
}
```

**Benefits**:
- Zero downtime traffic monitoring
- Seamless hardware maintenance
- Load balancing during peak hours
- Fault tolerance with automatic failover

### 2. Autonomous Vehicle Fleet Management

**Background**: A fleet of autonomous vehicles uses edge AI for real-time decision making. Each vehicle runs multiple ML models for object detection, path planning, and behavior prediction.

**Challenge**: Vehicles need software updates, model updates, or temporary computational offloading without stopping operations.

**Solution**:

```cpp
class AutonomousVehicleAI {
private:
    struct ModelSet {
        graph object_detection;
        graph path_planning;
        graph behavior_prediction;
        graph_execution_context obj_ctx, path_ctx, behavior_ctx;
    };
    
    ModelSet current_models;
    WAMRWASINNContext* ctx;
    
public:
    DrivingDecision process_sensor_data(const SensorData& data) {
        // Object detection
        auto objects = detect_objects(data.camera_frames);
        
        // Path planning
        auto planned_path = plan_path(objects, data.lidar_points);
        
        // Behavior prediction
        auto behavior = predict_behavior(objects, planned_path);
        
        return make_driving_decision(objects, planned_path, behavior);
    }
    
    void update_models_seamlessly(const ModelUpdatePackage& update) {
        // Create checkpoint of current state
        ctx->dump_impl(&pre_update_state);
        
        try {
            // Load new models
            ModelSet new_models;
            load_updated_models(update, &new_models);
            
            // Test new models with recorded scenarios
            if (validate_models_with_recorded_data(new_models)) {
                current_models = new_models;
                std::cout << "Models updated successfully" << std::endl;
            } else {
                // Rollback to previous models
                ctx->restore_impl(&pre_update_state);
                std::cout << "Model update failed, rolled back" << std::endl;
            }
        } catch (...) {
            // Automatic rollback on any error
            ctx->restore_impl(&pre_update_state);
        }
    }
    
    void offload_computation_during_high_load() {
        // During heavy computational load (e.g., complex intersection)
        if (cpu_usage > 80 && edge_server_available()) {
            // Migrate complex models to edge server
            migrate_model_to_edge_server(behavior_prediction);
            
            // Continue with essential models locally
            process_with_reduced_models();
        }
    }
};
```

### 3. Industrial IoT Predictive Maintenance

**Background**: Manufacturing equipment uses AI models to predict failures and optimize maintenance schedules.

**Solution**:

```cpp
class PredictiveMaintenanceSystem {
private:
    graph vibration_analysis_model;
    graph thermal_analysis_model;
    graph failure_prediction_model;
    
    std::vector<SensorReading> sensor_history;
    WAMRWASINNContext* ctx;
    
public:
    MaintenanceRecommendation analyze_equipment_health(const SensorData& data) {
        // Record sensor data for replay
        sensor_history.push_back({data, std::chrono::system_clock::now()});
        
        // Analyze vibration patterns
        auto vibration_features = analyze_vibration(data.vibration_data);
        
        // Analyze thermal patterns  
        auto thermal_features = analyze_thermal(data.temperature_data);
        
        // Predict failure probability
        auto failure_risk = predict_failure(vibration_features, thermal_features);
        
        return generate_maintenance_recommendation(failure_risk);
    }
    
    void handle_equipment_upgrade() {
        // Equipment needs hardware upgrade but can't stop production
        
        // 1. Create complete system state checkpoint
        ctx->dump_impl(&equipment_state);
        
        // 2. Transfer monitoring to backup system
        auto backup_system = get_backup_monitoring_system();
        backup_system->restore_from_checkpoint(equipment_state);
        
        // 3. Perform upgrade while backup monitors
        perform_hardware_upgrade();
        
        // 4. Restore state to upgraded system
        ctx->restore_impl(&equipment_state);
        
        // 5. Validate system integrity
        run_system_validation();
    }
    
    void create_digital_twin() {
        // Create a digital twin for testing scenarios
        DigitalTwin twin;
        
        // Copy all recorded operations to twin
        twin.load_historical_data(ctx->recorded_operations);
        
        // Test different maintenance strategies
        test_maintenance_strategy(twin, "aggressive_schedule");
        test_maintenance_strategy(twin, "predictive_only");
        test_maintenance_strategy(twin, "hybrid_approach");
    }
};
```

## Cloud and Serverless Applications

### 1. Auto-Scaling ML Inference Service

**Background**: A cloud service provides ML inference APIs that need to scale based on demand while maintaining state consistency.

**Challenge**: Scale instances up/down without losing model state or causing cold starts.

**Solution**:

```cpp
class ScalableMLService {
private:
    WAMRWASINNContext* ctx;
    std::string instance_id;
    std::shared_ptr<LoadBalancer> load_balancer;
    
public:
    class WarmInstancePool {
    private:
        std::queue<PrewarmedInstance> available_instances;
        std::mutex pool_mutex;
        
    public:
        PrewarmedInstance get_instance() {
            std::lock_guard<std::mutex> lock(pool_mutex);
            
            if (available_instances.empty()) {
                return create_cold_instance();
            }
            
            auto instance = available_instances.front();
            available_instances.pop();
            return instance;
        }
        
        void return_instance(PrewarmedInstance instance) {
            // Save instance state for reuse
            instance.ctx->dump_impl(&instance.warm_state);
            
            std::lock_guard<std::mutex> lock(pool_mutex);
            available_instances.push(instance);
        }
        
        void prewarm_instances(int count) {
            for (int i = 0; i < count; i++) {
                auto instance = create_instance();
                
                // Load common models
                instance.load_popular_models();
                
                // Save prewarmed state
                instance.ctx->dump_impl(&instance.warm_state);
                
                std::lock_guard<std::mutex> lock(pool_mutex);
                available_instances.push(instance);
            }
        }
    };
    
    InferenceResult process_request(const InferenceRequest& request) {
        // Check if we need to scale up
        if (load_balancer->current_load() > 0.8) {
            scale_up();
        }
        
        // Process the actual request
        return run_inference(request);
    }
    
    void scale_up() {
        // Get a prewarmed instance
        auto new_instance = warm_pool.get_instance();
        
        // If model already loaded in warm instance, use it
        if (new_instance.has_model(required_model)) {
            new_instance.ctx->restore_impl(&new_instance.warm_state);
        } else {
            // Load required model
            load_model_on_instance(new_instance, required_model);
        }
        
        // Add to active pool
        load_balancer->add_instance(new_instance);
    }
    
    void scale_down() {
        // Remove least used instance
        auto instance = load_balancer->remove_least_used_instance();
        
        // Save state and return to warm pool
        warm_pool.return_instance(instance);
    }
    
private:
    static WarmInstancePool warm_pool;
};

// Usage in Kubernetes/Docker environment
class K8sMLDeployment {
public:
    void handle_pod_scaling(const ScalingEvent& event) {
        if (event.type == ScalingEvent::SCALE_UP) {
            // Create new pod with prewarmed state
            auto checkpoint = get_warm_checkpoint(event.model_type);
            
            PodSpec pod_spec;
            pod_spec.image = "ml-inference:latest";
            pod_spec.env_vars["CHECKPOINT_DATA"] = checkpoint.serialize();
            
            k8s_client.create_pod(pod_spec);
        }
        
        if (event.type == ScalingEvent::SCALE_DOWN) {
            // Save pod state before termination
            auto pod = k8s_client.get_pod(event.pod_id);
            auto checkpoint = extract_checkpoint_from_pod(pod);
            
            save_checkpoint_for_reuse(checkpoint);
            k8s_client.delete_pod(event.pod_id);
        }
    }
};
```

### 2. Multi-Model Serverless Functions

**Background**: Serverless functions that need to switch between different ML models based on request type.

**Solution**:

```cpp
class MultiModelServerlessFunction {
private:
    std::unordered_map<std::string, ModelCheckpoint> model_checkpoints;
    WAMRWASINNContext* current_ctx;
    std::string current_model;
    
public:
    FunctionResponse handle_request(const FunctionRequest& request) {
        std::string required_model = determine_required_model(request);
        
        // Switch model if necessary
        if (current_model != required_model) {
            switch_to_model(required_model);
        }
        
        // Process request with current model
        return process_with_current_model(request);
    }
    
private:
    void switch_to_model(const std::string& model_name) {
        // Save current state if we have an active model
        if (!current_model.empty()) {
            WASINNContext state;
            current_ctx->dump_impl(&state);
            model_checkpoints[current_model] = {state, std::chrono::system_clock::now()};
        }
        
        // Load target model
        if (model_checkpoints.find(model_name) != model_checkpoints.end()) {
            // Restore from checkpoint
            auto& checkpoint = model_checkpoints[model_name];
            current_ctx->restore_impl(&checkpoint.state);
            checkpoint.last_used = std::chrono::system_clock::now();
        } else {
            // Cold start - load model from scratch
            load_model_from_scratch(model_name);
            
            // Save initial state
            WASINNContext initial_state;
            current_ctx->dump_impl(&initial_state);
            model_checkpoints[model_name] = {initial_state, std::chrono::system_clock::now()};
        }
        
        current_model = model_name;
    }
    
    void cleanup_old_checkpoints() {
        auto now = std::chrono::system_clock::now();
        
        for (auto it = model_checkpoints.begin(); it != model_checkpoints.end();) {
            auto age = std::chrono::duration_cast<std::chrono::minutes>(now - it->second.last_used);
            
            if (age.count() > 30) {  // Remove checkpoints older than 30 minutes
                it = model_checkpoints.erase(it);
            } else {
                ++it;
            }
        }
    }
};

// AWS Lambda integration example
extern "C" {
    const char* lambda_handler(const char* event_json, const char* context_json) {
        static MultiModelServerlessFunction function;
        
        auto request = parse_lambda_request(event_json);
        auto response = function.handle_request(request);
        
        return serialize_lambda_response(response);
    }
}
```

## Research and Development

### 1. ML Model A/B Testing Framework

**Background**: Researchers need to compare different model versions with identical input conditions.

**Solution**:

```cpp
class ModelABTestingFramework {
private:
    struct TestConfiguration {
        std::string model_a_path;
        std::string model_b_path;
        std::vector<TestCase> test_cases;
        WAMRWASINNContext* ctx_a;
        WAMRWASINNContext* ctx_b;
    };
    
public:
    struct ABTestResults {
        std::string model_a_id;
        std::string model_b_id;
        std::vector<ComparisonResult> results;
        StatisticalSignificance significance;
        double performance_diff_percent;
    };
    
    ABTestResults run_ab_test(const TestConfiguration& config) {
        ABTestResults results;
        
        // Load both models
        setup_model_a(config);
        setup_model_b(config);
        
        for (const auto& test_case : config.test_cases) {
            // Run identical test on both models
            auto result_a = run_test_case_on_model_a(test_case);
            auto result_b = run_test_case_on_model_b(test_case);
            
            // Record operations for reproducibility
            save_test_case_operations(test_case.id, config.ctx_a, config.ctx_b);
            
            results.results.push_back({result_a, result_b, test_case.id});
        }
        
        // Analyze results
        results.significance = calculate_statistical_significance(results.results);
        results.performance_diff_percent = calculate_performance_difference(results.results);
        
        return results;
    }
    
    void replay_test_for_debugging(const std::string& test_case_id) {
        // Load recorded operations for specific test case
        auto operations_a = load_operations("model_a_" + test_case_id + ".ops");
        auto operations_b = load_operations("model_b_" + test_case_id + ".ops");
        
        // Replay with debugging enabled
        ctx_a->enable_debug_mode(true);
        ctx_b->enable_debug_mode(true);
        
        ctx_a->recorded_operations = operations_a;
        ctx_b->recorded_operations = operations_b;
        
        WASINNContext debug_state_a, debug_state_b;
        ctx_a->restore_impl(&debug_state_a);
        ctx_b->restore_impl(&debug_state_b);
        
        // Analyze step-by-step differences
        compare_execution_traces(ctx_a, ctx_b);
    }
    
    void generate_reproducibility_report(const ABTestResults& results) {
        ReproducibilityReport report;
        
        report.test_timestamp = std::chrono::system_clock::now();
        report.model_a_hash = calculate_model_hash(config.model_a_path);
        report.model_b_hash = calculate_model_hash(config.model_b_path);
        
        // Include all operation traces
        for (const auto& result : results.results) {
            auto ops_a = load_operations("model_a_" + result.test_case_id + ".ops");
            auto ops_b = load_operations("model_b_" + result.test_case_id + ".ops");
            
            report.operation_traces[result.test_case_id] = {ops_a, ops_b};
        }
        
        // Save report for future reproduction
        save_reproducibility_report(report);
    }
};
```

### 2. Hyperparameter Optimization with State Preservation

**Background**: Researchers need to efficiently explore hyperparameter spaces while preserving intermediate states.

**Solution**:

```cpp
class HyperparameterOptimizer {
private:
    struct ExperimentState {
        Hyperparameters params;
        WAMRWASINNContext checkpoint;
        double performance_score;
        std::chrono::time_point<std::chrono::system_clock> timestamp;
    };
    
    std::vector<ExperimentState> experiment_history;
    BayesianOptimizer optimizer;
    
public:
    OptimizationResult optimize_hyperparameters(const OptimizationConfig& config) {
        for (int iteration = 0; iteration < config.max_iterations; iteration++) {
            // Get next hyperparameter configuration
            auto params = optimizer.suggest_next_params(experiment_history);
            
            // Check if we've tested similar parameters before
            auto similar_experiment = find_similar_experiment(params);
            
            if (similar_experiment) {
                // Resume from similar checkpoint
                auto score = resume_from_checkpoint(*similar_experiment, params);
                record_experiment_result(params, score);
            } else {
                // Run full experiment
                auto score = run_full_experiment(params);
                record_experiment_result(params, score);
            }
            
            // Early stopping if we find good enough result
            if (is_satisfactory_result(score, config.target_score)) {
                break;
            }
        }
        
        return compile_optimization_results();
    }
    
private:
    double resume_from_checkpoint(const ExperimentState& base_experiment, 
                                const Hyperparameters& new_params) {
        // Restore base state
        WASINNContext base_state;
        base_experiment.checkpoint.restore_impl(&base_state);
        
        // Apply parameter differences incrementally
        auto param_diff = calculate_parameter_difference(base_experiment.params, new_params);
        
        if (param_diff.requires_model_reload) {
            // Need to reload model with new architecture parameters
            reload_model_with_params(new_params);
        } else {
            // Can modify existing model
            modify_model_parameters(param_diff);
        }
        
        // Continue training/evaluation from checkpoint
        return continue_experiment_from_checkpoint();
    }
    
    void save_experiment_checkpoint(const Hyperparameters& params, double score) {
        ExperimentState state;
        state.params = params;
        state.performance_score = score;
        state.timestamp = std::chrono::system_clock::now();
        
        // Save model state
        current_ctx->dump_impl(&state.checkpoint);
        
        experiment_history.push_back(state);
        
        // Prune old experiments to save memory
        if (experiment_history.size() > max_checkpoints) {
            prune_old_experiments();
        }
    }
};
```

## Production Deployment Patterns

### 1. Blue-Green Deployment with State Migration

**Background**: Production ML services need zero-downtime deployments with the ability to rollback.

**Solution**:

```cpp
class BlueGreenMLDeployment {
private:
    enum Environment { BLUE, GREEN };
    
    struct DeploymentEnvironment {
        Environment type;
        std::vector<MLServiceInstance> instances;
        LoadBalancer load_balancer;
        HealthChecker health_checker;
        WAMRWASINNContext* shared_context;
    };
    
    DeploymentEnvironment blue_env;
    DeploymentEnvironment green_env;
    Environment active_env = BLUE;
    
public:
    DeploymentResult deploy_new_version(const ModelPackage& new_model) {
        auto inactive_env = (active_env == BLUE) ? &green_env : &blue_env;
        
        try {
            // 1. Deploy to inactive environment
            deploy_to_environment(*inactive_env, new_model);
            
            // 2. Copy active state to new environment
            migrate_state_to_environment(*inactive_env);
            
            // 3. Run health checks
            if (!run_comprehensive_health_checks(*inactive_env)) {
                throw std::runtime_error("Health checks failed");
            }
            
            // 4. Run canary tests with real traffic
            if (!run_canary_tests(*inactive_env)) {
                throw std::runtime_error("Canary tests failed");
            }
            
            // 5. Switch traffic
            switch_traffic_to_environment(inactive_env->type);
            
            // 6. Monitor for issues
            if (!monitor_post_deployment(inactive_env->type)) {
                // Automatic rollback
                rollback_to_previous_environment();
                throw std::runtime_error("Post-deployment monitoring failed");
            }
            
            return DeploymentResult::SUCCESS;
            
        } catch (const std::exception& e) {
            // Cleanup failed deployment
            cleanup_failed_deployment(*inactive_env);
            return DeploymentResult::FAILED;
        }
    }
    
private:
    void migrate_state_to_environment(DeploymentEnvironment& target_env) {
        auto active_env_ptr = (active_env == BLUE) ? &blue_env : &green_env;
        
        // Create comprehensive state snapshot
        StateSnapshot snapshot;
        
        for (auto& instance : active_env_ptr->instances) {
            InstanceState state;
            instance.ctx->dump_impl(&state.wasi_nn_context);
            state.instance_metadata = instance.get_metadata();
            snapshot.instance_states.push_back(state);
        }
        
        // Apply state to target environment
        for (size_t i = 0; i < target_env.instances.size(); i++) {
            if (i < snapshot.instance_states.size()) {
                target_env.instances[i].ctx->restore_impl(
                    &snapshot.instance_states[i].wasi_nn_context
                );
            }
        }
    }
    
    bool run_canary_tests(const DeploymentEnvironment& env) {
        // Route small percentage of traffic to new environment
        auto canary_router = create_canary_router(env, 0.05);  // 5% traffic
        
        CanaryTestResults results;
        auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - start_time < canary_duration) {
            // Collect metrics
            auto metrics = canary_router.collect_metrics();
            results.add_metrics(metrics);
            
            // Check for issues
            if (metrics.error_rate > acceptable_error_rate ||
                metrics.latency_p99 > acceptable_latency) {
                return false;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(30));
        }
        
        return results.is_successful();
    }
    
    void rollback_to_previous_environment() {
        auto previous_env = (active_env == BLUE) ? BLUE : GREEN;
        
        // Immediate traffic switch
        switch_traffic_to_environment(previous_env);
        
        // Log rollback reason
        log_rollback_event("Automatic rollback due to deployment issues");
    }
};
```

### 2. Multi-Region Active-Active Deployment

**Background**: Global ML service with active-active deployment across multiple regions.

**Solution**:

```cpp
class MultiRegionMLService {
private:
    struct RegionConfig {
        std::string region_name;
        std::vector<MLServiceInstance> instances;
        WAMRWASINNContext* region_context;
        std::unique_ptr<RegionSynchronizer> synchronizer;
    };
    
    std::vector<RegionConfig> regions;
    GlobalLoadBalancer global_lb;
    
public:
    void deploy_global_model_update(const ModelPackage& model) {
        // Coordinate deployment across all regions
        std::vector<std::future<DeploymentResult>> deployment_futures;
        
        for (auto& region : regions) {
            auto future = std::async(std::launch::async, [&region, &model]() {
                return deploy_to_region(region, model);
            });
            deployment_futures.push_back(std::move(future));
        }
        
        // Wait for all deployments to complete
        std::vector<DeploymentResult> results;
        for (auto& future : deployment_futures) {
            results.push_back(future.get());
        }
        
        // Check if any deployment failed
        bool any_failed = std::any_of(results.begin(), results.end(),
            [](const DeploymentResult& r) { return r != DeploymentResult::SUCCESS; });
        
        if (any_failed) {
            // Rollback all successful deployments
            rollback_all_regions();
        } else {
            // All deployments successful, enable global traffic
            global_lb.enable_all_regions();
        }
    }
    
    void handle_region_failure(const std::string& failed_region) {
        // Remove failed region from load balancer
        global_lb.disable_region(failed_region);
        
        // Find healthy region with most similar state
        auto backup_region = find_best_backup_region(failed_region);
        
        // Scale up backup region to handle additional load
        scale_up_region(backup_region);
        
        // Synchronize state from failed region if possible
        if (can_recover_state(failed_region)) {
            auto recovered_state = recover_region_state(failed_region);
            merge_state_into_region(backup_region, recovered_state);
        }
    }
    
private:
    DeploymentResult deploy_to_region(RegionConfig& region, const ModelPackage& model) {
        try {
            // Create snapshot of current region state
            RegionSnapshot snapshot;
            create_region_snapshot(region, &snapshot);
            
            // Deploy new model
            for (auto& instance : region.instances) {
                // Deploy model to instance
                deploy_model_to_instance(instance, model);
                
                // Restore state to maintain continuity
                instance.ctx->restore_impl(&snapshot.instance_states[instance.id]);
            }
            
            // Verify deployment health
            if (!verify_region_health(region)) {
                // Restore from snapshot
                restore_region_from_snapshot(region, snapshot);
                return DeploymentResult::FAILED;
            }
            
            return DeploymentResult::SUCCESS;
            
        } catch (const std::exception& e) {
            log_deployment_error(region.region_name, e.what());
            return DeploymentResult::FAILED;
        }
    }
    
    std::string find_best_backup_region(const std::string& failed_region) {
        double best_similarity = 0.0;
        std::string best_region;
        
        auto failed_state = get_last_known_state(failed_region);
        
        for (const auto& region : regions) {
            if (region.region_name == failed_region) continue;
            
            RegionSnapshot current_state;
            create_region_snapshot(region, &current_state);
            
            double similarity = calculate_state_similarity(failed_state, current_state);
            
            if (similarity > best_similarity) {
                best_similarity = similarity;
                best_region = region.region_name;
            }
        }
        
        return best_region;
    }
};
```

## Security and Compliance

### 1. Auditable ML Pipeline

**Background**: Financial services ML applications requiring complete audit trails and compliance verification.

**Solution**:

```cpp
class AuditableMLPipeline {
private:
    struct AuditRecord {
        std::string operation_id;
        std::string user_id;
        std::chrono::system_clock::time_point timestamp;
        WASINNOperationType operation_type;
        std::string model_hash;
        std::vector<uint8_t> input_hash;
        std::vector<uint8_t> output_hash;
        std::string compliance_signature;
    };
    
    std::vector<AuditRecord> audit_log;
    CryptographicSigner signer;
    ComplianceValidator validator;
    
public:
    class SecureWASINNWrapper {
    private:
        AuditableMLPipeline* parent;
        std::string current_user;
        
    public:
        error secure_load_model(const std::string& model_path, 
                               const SecurityContext& sec_ctx) {
            // Verify user permissions
            if (!parent->validator.verify_model_access_permission(current_user, model_path)) {
                throw SecurityException("Insufficient permissions to load model");
            }
            
            // Verify model integrity
            auto model_hash = calculate_file_hash(model_path);
            if (!parent->validator.verify_model_integrity(model_path, model_hash)) {
                throw SecurityException("Model integrity verification failed");
            }
            
            // Record audit event
            AuditRecord record;
            record.operation_id = generate_operation_id();
            record.user_id = current_user;
            record.timestamp = std::chrono::system_clock::now();
            record.operation_type = WASINNOperationType::LOAD_MODEL;
            record.model_hash = model_hash;
            record.compliance_signature = parent->signer.sign_operation(record);
            
            parent->audit_log.push_back(record);
            
            // Perform actual operation
            return wamr_wasi_nn_load_with_recording(exec_env, builder, size,
                                                   encoding, target, graph, model_path.c_str());
        }
        
        error secure_set_input(graph_execution_context ctx, uint32_t index,
                              tensor_wasm* input_tensor, const DataClassification& classification) {
            // Check data classification compliance
            if (!parent->validator.verify_data_classification_compliance(classification)) {
                throw ComplianceException("Data classification not permitted for current context");
            }
            
            // Hash input data for audit
            auto input_hash = calculate_tensor_hash(input_tensor);
            
            // Check for sensitive data patterns
            if (parent->validator.contains_sensitive_data(input_tensor)) {
                // Apply additional security measures
                apply_sensitive_data_protection(input_tensor);
            }
            
            // Record audit event
            AuditRecord record;
            record.operation_id = generate_operation_id();
            record.user_id = current_user;
            record.timestamp = std::chrono::system_clock::now();
            record.operation_type = WASINNOperationType::SET_INPUT;
            record.input_hash = input_hash;
            record.compliance_signature = parent->signer.sign_operation(record);
            
            parent->audit_log.push_back(record);
            
            return wamr_wasi_nn_set_input_with_recording(exec_env, ctx, index, input_tensor);
        }
    };
    
    ComplianceReport generate_compliance_report(const std::string& period_start,
                                               const std::string& period_end) {
        ComplianceReport report;
        
        auto start_time = parse_time(period_start);
        auto end_time = parse_time(period_end);
        
        for (const auto& record : audit_log) {
            if (record.timestamp >= start_time && record.timestamp <= end_time) {
                // Verify signature integrity
                if (!signer.verify_signature(record)) {
                    report.integrity_violations.push_back(record.operation_id);
                }
                
                // Check compliance requirements
                auto compliance_check = validator.check_operation_compliance(record);
                if (!compliance_check.is_compliant) {
                    report.compliance_violations.push_back({record.operation_id, compliance_check.violations});
                }
                
                report.total_operations++;
            }
        }
        
        // Generate cryptographic proof of report integrity
        report.integrity_proof = signer.generate_report_proof(report);
        
        return report;
    }
    
    void replay_for_audit(const std::string& operation_id) {
        // Find operation in audit log
        auto it = std::find_if(audit_log.begin(), audit_log.end(),
            [&operation_id](const AuditRecord& record) {
                return record.operation_id == operation_id;
            });
        
        if (it == audit_log.end()) {
            throw std::runtime_error("Operation not found in audit log");
        }
        
        // Verify operation signature
        if (!signer.verify_signature(*it)) {
            throw SecurityException("Operation signature verification failed");
        }
        
        // Create isolated replay environment
        IsolatedReplayEnvironment replay_env;
        
        // Replay operation with full logging
        replay_env.enable_detailed_logging(true);
        replay_env.replay_operation(*it);
        
        // Generate audit replay report
        auto replay_report = replay_env.get_replay_report();
        save_audit_replay_report(operation_id, replay_report);
    }
};
```

### 2. Privacy-Preserving ML with Differential Privacy

**Background**: Healthcare ML applications requiring privacy protection and HIPAA compliance.

**Solution**:

```cpp
class PrivacyPreservingMLPipeline {
private:
    struct PrivacyParameters {
        double epsilon;  // Privacy budget
        double delta;    // Failure probability
        NoiseDistribution noise_type;
        bool enable_record_linkage_protection;
    };
    
    PrivacyParameters privacy_params;
    PrivacyBudgetTracker budget_tracker;
    
public:
    class DifferentiallyPrivateWASINN {
    private:
        PrivacyPreservingMLPipeline* parent;
        
    public:
        error dp_set_input(graph_execution_context ctx, uint32_t index,
                          tensor_wasm* input_tensor, const PrivacyLevel& level) {
            // Apply differential privacy noise based on privacy level
            auto noisy_tensor = apply_differential_privacy_noise(input_tensor, level);
            
            // Track privacy budget consumption
            parent->budget_tracker.consume_budget(level.epsilon_cost);
            
            // Record privacy-preserving operation
            PrivacyAuditRecord record;
            record.original_sensitivity = calculate_sensitivity(input_tensor);
            record.noise_applied = calculate_noise_level(noisy_tensor, input_tensor);
            record.privacy_guarantee = level;
            
            parent->record_privacy_operation(record);
            
            return wamr_wasi_nn_set_input_with_recording(exec_env, ctx, index, &noisy_tensor);
        }
        
        error dp_get_output(graph_execution_context ctx, uint32_t index,
                           tensor_data output_tensor, uint32_t output_tensor_len,
                           uint32_t* output_tensor_size, const PrivacyLevel& level) {
            // Get raw output
            auto result = wamr_wasi_nn_get_output_with_recording(
                exec_env, ctx, index, output_tensor, output_tensor_len, output_tensor_size
            );
            
            if (result == success) {
                // Apply output perturbation for privacy
                apply_output_privacy_protection(output_tensor, *output_tensor_size, level);
                
                // Update privacy accounting
                parent->budget_tracker.consume_budget(level.epsilon_cost);
            }
            
            return result;
        }
        
    private:
        tensor_wasm apply_differential_privacy_noise(tensor_wasm* original, 
                                                   const PrivacyLevel& level) {
            tensor_wasm noisy_tensor = *original;
            
            // Calculate required noise scale
            double noise_scale = calculate_noise_scale(level.epsilon, level.delta, 
                                                     level.sensitivity);
            
            // Apply noise based on tensor type
            switch (original->type) {
                case fp32:
                    add_gaussian_noise_fp32(&noisy_tensor, noise_scale);
                    break;
                case fp64:
                    add_gaussian_noise_fp64(&noisy_tensor, noise_scale);
                    break;
                default:
                    throw std::runtime_error("Unsupported tensor type for DP");
            }
            
            // Clip values to valid range
            clip_tensor_values(&noisy_tensor, level.clipping_bound);
            
            return noisy_tensor;
        }
    };
    
    PrivacyComplianceReport generate_privacy_report() {
        PrivacyComplianceReport report;
        
        // Check remaining privacy budget
        report.remaining_epsilon = budget_tracker.get_remaining_budget();
        
        // Verify all operations met privacy requirements
        for (const auto& operation : privacy_audit_log) {
            if (!verify_privacy_guarantee(operation)) {
                report.privacy_violations.push_back(operation);
            }
        }
        
        // Calculate composition of privacy guarantees
        report.total_privacy_cost = budget_tracker.get_total_consumed();
        report.composition_bounds = calculate_composition_bounds();
        
        return report;
    }
    
    void anonymize_checkpoint_data(WAMRWASINNContext* ctx) {
        // Remove personally identifiable information from recorded operations
        for (auto& operation : ctx->recorded_operations) {
            if (contains_sensitive_data(operation)) {
                anonymize_operation_data(&operation);
            }
        }
        
        // Apply k-anonymity to tensor data
        apply_k_anonymity_protection(ctx);
        
        // Generate anonymization certificate
        auto cert = generate_anonymization_certificate(ctx);
        attach_certificate_to_context(ctx, cert);
    }
};
```

## IoT and Embedded Systems

### 1. Resource-Constrained Edge Devices

**Background**: IoT devices with limited memory and CPU resources running ML inference.

**Solution**:

```cpp
class ResourceConstrainedMLDevice {
private:
    struct ResourceLimits {
        size_t max_memory_mb;
        size_t max_checkpoint_size_mb;
        size_t max_operations_history;
        double max_cpu_utilization;
    };
    
    ResourceLimits limits;
    ResourceMonitor monitor;
    WAMRWASINNContext* ctx;
    
public:
    class LightweightRecorder {
    private:
        std::circular_buffer<WAMRWASINNOperation> operation_buffer;
        CompressionEngine compressor;
        
    public:
        void record_operation(const WAMRWASINNOperation& op) {
            // Compress operation data to save memory
            auto compressed_op = compressor.compress_operation(op);
            
            // Use circular buffer to limit memory usage
            if (operation_buffer.full()) {
                operation_buffer.pop_front();  // Remove oldest operation
            }
            
            operation_buffer.push_back(compressed_op);
        }
        
        std::vector<WAMRWASINNOperation> get_operations_for_checkpoint() {
            std::vector<WAMRWASINNOperation> operations;
            
            for (const auto& compressed_op : operation_buffer) {
                auto decompressed = compressor.decompress_operation(compressed_op);
                operations.push_back(decompressed);
            }
            
            return operations;
        }
    };
    
    void adaptive_resource_management() {
        auto current_usage = monitor.get_current_usage();
        
        if (current_usage.memory_percent > 80) {
            // Memory pressure - reduce recording granularity
            ctx->set_recording_sample_rate(0.1);  // Record only 10% of operations
            
            // Compress existing data
            compress_recorded_operations();
            
            // Remove non-essential recorded data
            prune_non_critical_operations();
        }
        
        if (current_usage.cpu_percent > 90) {
            // CPU pressure - disable recording temporarily
            ctx->enable_recording(false);
            
            // Schedule recording re-enable when CPU usage drops
            schedule_recording_reenable();
        }
        
        if (current_usage.storage_percent > 85) {
            // Storage pressure - create remote checkpoint
            offload_checkpoint_to_cloud();
        }
    }
    
    void intelligent_checkpoint_scheduling() {
        // Consider device state and resource availability
        DeviceState state = get_device_state();
        
        bool should_checkpoint = false;
        
        // Checkpoint when device is idle
        if (state.is_idle && state.battery_level > 50) {
            should_checkpoint = true;
        }
        
        // Emergency checkpoint before low battery
        if (state.battery_level < 20 && !has_recent_checkpoint()) {
            should_checkpoint = true;
        }
        
        // Scheduled checkpoint during maintenance window
        if (is_in_maintenance_window()) {
            should_checkpoint = true;
        }
        
        if (should_checkpoint) {
            create_lightweight_checkpoint();
        }
    }
    
private:
    void create_lightweight_checkpoint() {
        // Create minimal checkpoint for constrained devices
        LightweightCheckpoint checkpoint;
        
        // Only store essential state
        checkpoint.model_metadata = extract_model_metadata();
        checkpoint.critical_operations = filter_critical_operations(ctx->recorded_operations);
        checkpoint.device_context = capture_minimal_device_context();
        
        // Compress checkpoint data
        auto compressed_checkpoint = compress_checkpoint(checkpoint);
        
        // Store locally or offload based on available resources
        if (has_sufficient_local_storage()) {
            store_checkpoint_locally(compressed_checkpoint);
        } else {
            offload_checkpoint_to_edge_server(compressed_checkpoint);
        }
    }
    
    void offload_checkpoint_to_cloud() {
        // Asynchronously upload checkpoint to cloud storage
        std::thread upload_thread([this]() {
            try {
                CloudStorage cloud;
                auto checkpoint_data = serialize_current_state();
                cloud.upload_checkpoint(device_id, checkpoint_data);
                
                // Clear local data after successful upload
                clear_local_checkpoint_data();
                
            } catch (const std::exception& e) {
                // Fallback to local storage if cloud upload fails
                store_checkpoint_locally_compressed();
            }
        });
        
        upload_thread.detach();
    }
};
```

### 2. Swarm Intelligence with Distributed ML

**Background**: A swarm of drones performing collaborative object detection and tracking.

**Solution**:

```cpp
class DroneSwarmMLSystem {
private:
    struct DroneNode {
        std::string drone_id;
        GeographicLocation location;
        WAMRWASINNContext* local_context;
        SwarmCommunicator* communicator;
        MLCapabilities capabilities;
    };
    
    std::vector<DroneNode> swarm_nodes;
    SwarmCoordinator coordinator;
    
public:
    class CollaborativeInference {
    private:
        DroneSwarmMLSystem* parent;
        
    public:
        SwarmDetectionResult detect_objects_collaboratively(const ImageData& local_image) {
            // Perform local detection
            auto local_results = perform_local_detection(local_image);
            
            // Share results with nearby drones
            auto nearby_drones = parent->coordinator.find_nearby_drones(current_location, 1000); // 1km radius
            
            std::vector<DetectionResult> swarm_results;
            swarm_results.push_back(local_results);
            
            for (const auto& drone : nearby_drones) {
                // Request detection from nearby drone
                auto remote_result = request_detection_from_drone(drone.drone_id, local_image);
                if (remote_result.confidence > confidence_threshold) {
                    swarm_results.push_back(remote_result);
                }
            }
            
            // Fuse results using ensemble method
            auto fused_result = fuse_detection_results(swarm_results);
            
            // Record collaborative operation
            CollaborativeOperation op;
            op.participating_drones = extract_drone_ids(nearby_drones);
            op.fusion_method = "weighted_confidence";
            op.local_result = local_results;
            op.fused_result = fused_result;
            
            record_collaborative_operation(op);
            
            return fused_result;
        }
        
        void share_model_updates() {
            // Check if local model has improved
            if (has_significant_model_improvement()) {
                ModelUpdate update = extract_model_update();
                
                // Share with swarm
                parent->coordinator.broadcast_model_update(update);
                
                // Record sharing operation
                SharingOperation op;
                op.update_type = "incremental_learning";
                op.improvement_metric = calculate_improvement_metric();
                op.target_drones = parent->coordinator.get_all_active_drones();
                
                record_sharing_operation(op);
            }
        }
    };
    
    void handle_drone_failure(const std::string& failed_drone_id) {
        auto failed_drone = find_drone(failed_drone_id);
        if (!failed_drone) return;
        
        // Find nearest healthy drone to take over responsibilities
        auto replacement = coordinator.find_nearest_healthy_drone(failed_drone->location);
        
        if (replacement) {
            // Transfer learned model state to replacement
            auto last_checkpoint = get_last_checkpoint(failed_drone_id);
            
            if (last_checkpoint) {
                // Restore state on replacement drone
                replacement->local_context->restore_impl(&last_checkpoint->state);
                
                // Update coordination roles
                coordinator.transfer_responsibilities(failed_drone_id, replacement->drone_id);
                
                // Notify swarm of role transfer
                coordinator.broadcast_role_transfer(failed_drone_id, replacement->drone_id);
            }
        } else {
            // No replacement available - redistribute responsibilities
            redistribute_failed_drone_responsibilities(failed_drone_id);
        }
    }
    
    void optimize_swarm_formation() {
        // Analyze current detection coverage
        auto coverage_analysis = analyze_detection_coverage();
        
        if (coverage_analysis.has_gaps) {
            // Reposition drones to improve coverage
            auto new_formation = calculate_optimal_formation(coverage_analysis);
            
            for (const auto& position_change : new_formation.changes) {
                auto drone = find_drone(position_change.drone_id);
                
                // Save current state before repositioning
                CheckpointData checkpoint;
                drone->local_context->dump_impl(&checkpoint.state);
                checkpoint.location = drone->location;
                
                save_repositioning_checkpoint(position_change.drone_id, checkpoint);
                
                // Move drone to new position
                coordinator.command_drone_movement(position_change.drone_id, 
                                                 position_change.target_location);
            }
        }
    }
    
private:
    void handle_swarm_model_consensus() {
        // Collect model states from all drones
        std::vector<ModelState> swarm_models;
        
        for (const auto& drone : swarm_nodes) {
            ModelState state;
            drone.local_context->dump_impl(&state.context);
            state.performance_metrics = get_drone_performance_metrics(drone.drone_id);
            state.data_diversity = calculate_data_diversity(drone.drone_id);
            
            swarm_models.push_back(state);
        }
        
        // Find consensus model using federated learning approach
        auto consensus_model = calculate_federated_consensus(swarm_models);
        
        // Distribute consensus model to all drones
        for (auto& drone : swarm_nodes) {
            drone.local_context->restore_impl(&consensus_model.context);
        }
        
        // Record consensus operation
        ConsensusOperation op;
        op.participating_drones = extract_all_drone_ids();
        op.consensus_method = "federated_averaging";
        op.convergence_metric = calculate_convergence_metric(swarm_models, consensus_model);
        
        record_consensus_operation(op);
    }
};
```

这个完整的用例文档展示了WASI-NN record和replay功能在各种实际场景中的应用，从边缘计算到云端服务，从研究开发到生产部署，涵盖了安全性、合规性、物联网等多个方面的需求。每个用例都提供了具体的代码示例和实现细节，帮助开发者理解如何在实际项目中应用这些功能。