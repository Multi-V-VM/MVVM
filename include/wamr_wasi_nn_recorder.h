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

#ifndef MVVM_WAMR_WASI_NN_RECORDER_H
#define MVVM_WAMR_WASI_NN_RECORDER_H

#include "wasi_nn.h"
#include "wamr_wasi_nn_context.h"
#include "wasm_export.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the WASI-NN recorder for a module instance
void wamr_wasi_nn_recorder_init(wasm_module_inst_t instance);

// Cleanup the WASI-NN recorder for a module instance
void wamr_wasi_nn_recorder_cleanup(wasm_module_inst_t instance);

// Get the MVVM WASI-NN context for a module instance
WAMRWASINNContext* wamr_get_wasi_nn_mvvm_context(wasm_module_inst_t instance);

// Wrapped WASI-NN functions with recording capability
error wamr_wasi_nn_load_with_recording(wasm_exec_env_t exec_env, 
                                      graph_builder_wasm *builder,
                                      uint32_t builder_wasm_size, 
                                      graph_encoding encoding,
                                      execution_target target, 
                                      graph *g,
                                      const char* model_name);

error wamr_wasi_nn_init_execution_context_with_recording(wasm_exec_env_t exec_env,
                                                        graph g,
                                                        graph_execution_context *ctx);

error wamr_wasi_nn_set_input_with_recording(wasm_exec_env_t exec_env,
                                           graph_execution_context ctx,
                                           uint32_t index,
                                           tensor_wasm *input_tensor);

error wamr_wasi_nn_compute_with_recording(wasm_exec_env_t exec_env,
                                         graph_execution_context ctx);

error wamr_wasi_nn_get_output_with_recording(wasm_exec_env_t exec_env,
                                            graph_execution_context ctx,
                                            uint32_t index,
                                            tensor_data output_tensor,
                                            uint32_t output_tensor_len,
                                            uint32_t *output_tensor_size);

#ifdef __cplusplus
}
#endif

#endif // MVVM_WAMR_WASI_NN_RECORDER_H