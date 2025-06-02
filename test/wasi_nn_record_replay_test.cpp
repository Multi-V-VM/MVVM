/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 *  Copyright 2025 Regents of the University of California
 *  UC Santa Cruz Sluglab.
 */

#include "wamr_wasi_nn_context.h"
#include "wamr_wasi_nn_recorder.h"
#include "wasi_nn.h"
#include <iostream>
#include <vector>
#include <cassert>

// Mock WASI-NN context for testing
static WASINNContext test_wasi_nn_ctx;

// Test function to verify record and replay functionality
void test_wasi_nn_record_replay() {
    std::cout << "Testing WASI-NN Record and Replay functionality..." << std::endl;
    
    // Create a test MVVM context
    WAMRWASINNContext mvvm_ctx;
    
    // Test 1: Record a LOAD_MODEL operation
    std::cout << "Test 1: Recording LOAD_MODEL operation..." << std::endl;
    
    WAMRWASINNOperation load_op;
    load_op.type = WASINNOperationType::LOAD_MODEL;
    load_op.sequence_id = 0;
    load_op.model_name = "/path/to/test_model.tflite";
    load_op.encoding = graph_encoding::tensorflowlite;
    load_op.target = execution_target::cpu;
    load_op.graph_id = 123;
    
    mvvm_ctx.record_operation(load_op);
    assert(mvvm_ctx.recorded_operations.size() == 1);
    std::cout << "✓ LOAD_MODEL operation recorded successfully" << std::endl;
    
    // Test 2: Record an INIT_EXECUTION_CONTEXT operation
    std::cout << "Test 2: Recording INIT_EXECUTION_CONTEXT operation..." << std::endl;
    
    WAMRWASINNOperation init_ctx_op;
    init_ctx_op.type = WASINNOperationType::INIT_EXECUTION_CONTEXT;
    init_ctx_op.sequence_id = 1;
    init_ctx_op.graph_id = 123;
    init_ctx_op.ctx_id = 456;
    
    mvvm_ctx.record_operation(init_ctx_op);
    assert(mvvm_ctx.recorded_operations.size() == 2);
    std::cout << "✓ INIT_EXECUTION_CONTEXT operation recorded successfully" << std::endl;
    
    // Test 3: Record a SET_INPUT operation
    std::cout << "Test 3: Recording SET_INPUT operation..." << std::endl;
    
    WAMRWASINNOperation set_input_op;
    set_input_op.type = WASINNOperationType::SET_INPUT;
    set_input_op.sequence_id = 2;
    set_input_op.ctx_id = 456;
    set_input_op.input_index = 0;
    set_input_op.tensor_dims = {1, 224, 224, 3};
    set_input_op.data_type = tensor_type::fp32;
    
    // Mock tensor data (1*224*224*3 = 150528 float32 values)
    size_t tensor_size = 1 * 224 * 224 * 3 * sizeof(float);
    set_input_op.tensor_data.resize(tensor_size);
    
    // Fill with some test data
    float* data_ptr = reinterpret_cast<float*>(set_input_op.tensor_data.data());
    for (size_t i = 0; i < tensor_size / sizeof(float); ++i) {
        data_ptr[i] = static_cast<float>(i % 256) / 255.0f; // Normalized test data
    }
    
    mvvm_ctx.record_operation(set_input_op);
    assert(mvvm_ctx.recorded_operations.size() == 3);
    std::cout << "✓ SET_INPUT operation recorded successfully" << std::endl;
    
    // Test 4: Record a COMPUTE operation
    std::cout << "Test 4: Recording COMPUTE operation..." << std::endl;
    
    WAMRWASINNOperation compute_op;
    compute_op.type = WASINNOperationType::COMPUTE;
    compute_op.sequence_id = 3;
    compute_op.ctx_id = 456;
    
    mvvm_ctx.record_operation(compute_op);
    assert(mvvm_ctx.recorded_operations.size() == 4);
    std::cout << "✓ COMPUTE operation recorded successfully" << std::endl;
    
    // Test 5: Record a GET_OUTPUT operation
    std::cout << "Test 5: Recording GET_OUTPUT operation..." << std::endl;
    
    WAMRWASINNOperation get_output_op;
    get_output_op.type = WASINNOperationType::GET_OUTPUT;
    get_output_op.sequence_id = 4;
    get_output_op.ctx_id = 456;
    get_output_op.output_index = 0;
    get_output_op.output_size = 1000 * sizeof(float); // 1000 classes
    get_output_op.output_data.resize(get_output_op.output_size);
    
    // Fill with mock classification results
    float* output_ptr = reinterpret_cast<float*>(get_output_op.output_data.data());
    for (size_t i = 0; i < 1000; ++i) {
        output_ptr[i] = 1.0f / 1000.0f; // Uniform distribution
    }
    
    mvvm_ctx.record_operation(get_output_op);
    assert(mvvm_ctx.recorded_operations.size() == 5);
    std::cout << "✓ GET_OUTPUT operation recorded successfully" << std::endl;
    
    // Test 6: Test replay functionality
    std::cout << "Test 6: Testing replay functionality..." << std::endl;
    
    // Enable replay mode
    mvvm_ctx.enable_replay(true);
    mvvm_ctx.enable_recording(false);
    mvvm_ctx.reset_replay();
    
    // Test getting operations in sequence
    WAMRWASINNOperation* op1 = mvvm_ctx.get_next_operation();
    assert(op1 != nullptr);
    assert(op1->type == WASINNOperationType::LOAD_MODEL);
    assert(op1->sequence_id == 0);
    assert(op1->model_name == "/path/to/test_model.tflite");
    std::cout << "✓ Replayed LOAD_MODEL operation correctly" << std::endl;
    
    WAMRWASINNOperation* op2 = mvvm_ctx.get_next_operation();
    assert(op2 != nullptr);
    assert(op2->type == WASINNOperationType::INIT_EXECUTION_CONTEXT);
    assert(op2->sequence_id == 1);
    std::cout << "✓ Replayed INIT_EXECUTION_CONTEXT operation correctly" << std::endl;
    
    WAMRWASINNOperation* op3 = mvvm_ctx.get_next_operation();
    assert(op3 != nullptr);
    assert(op3->type == WASINNOperationType::SET_INPUT);
    assert(op3->sequence_id == 2);
    assert(op3->tensor_dims.size() == 4);
    assert(op3->tensor_dims[0] == 1 && op3->tensor_dims[1] == 224);
    std::cout << "✓ Replayed SET_INPUT operation correctly" << std::endl;
    
    WAMRWASINNOperation* op4 = mvvm_ctx.get_next_operation();
    assert(op4 != nullptr);
    assert(op4->type == WASINNOperationType::COMPUTE);
    assert(op4->sequence_id == 3);
    std::cout << "✓ Replayed COMPUTE operation correctly" << std::endl;
    
    WAMRWASINNOperation* op5 = mvvm_ctx.get_next_operation();
    assert(op5 != nullptr);
    assert(op5->type == WASINNOperationType::GET_OUTPUT);
    assert(op5->sequence_id == 4);
    assert(op5->output_size == 1000 * sizeof(float));
    std::cout << "✓ Replayed GET_OUTPUT operation correctly" << std::endl;
    
    // Test end of replay
    WAMRWASINNOperation* op6 = mvvm_ctx.get_next_operation();
    assert(op6 == nullptr);
    std::cout << "✓ Replay correctly reached end of operations" << std::endl;
    
    // Test 7: Test dump and restore functionality
    std::cout << "Test 7: Testing dump and restore..." << std::endl;
    
    // Create another context for restoration test
    WAMRWASINNContext restore_ctx;
    
    // Simulate dump operation
    test_wasi_nn_ctx.is_model_loaded = true;
    test_wasi_nn_ctx.current_encoding = graph_encoding::tensorflowlite;
    
    mvvm_ctx.dump_impl(&test_wasi_nn_ctx);
    
    // Copy recorded operations to restore context
    restore_ctx.recorded_operations = mvvm_ctx.recorded_operations;
    restore_ctx.is_initialized = mvvm_ctx.is_initialized;
    restore_ctx.current_encoding = mvvm_ctx.current_encoding;
    
    std::cout << "✓ Dump operation completed successfully" << std::endl;
    std::cout << "✓ Context contains " << restore_ctx.recorded_operations.size() << " recorded operations" << std::endl;
    
    std::cout << "\n🎉 All WASI-NN Record and Replay tests passed successfully!" << std::endl;
}

int main() {
    try {
        test_wasi_nn_record_replay();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Test failed with unknown exception" << std::endl;
        return 1;
    }
}