// SPDX-License-Identifier: Apache-2.0
#ifndef MVVM_TENSOR_WASM_JIT_BRIDGE_H
#define MVVM_TENSOR_WASM_JIT_BRIDGE_H

#include <cstddef>
#include <cstdint>

extern "C" {

struct MVVMTensorWasmJITTranslation {
    char *error;
    uint8_t *rewritten_wasm;
    size_t rewritten_wasm_len;
    char *kernel_name;
    uint8_t *ptx;
    size_t ptx_len;
    uint8_t signature[32];
    uint32_t function_index;
    uint64_t fingerprint;
    size_t original_op_count;
    uint32_t grid_size;
    uint32_t block_size;
};

int32_t mvvm_tensor_wasm_jit_translate(const uint8_t *wasm, size_t wasm_len, const char *function_name,
                                       uint32_t sm_version, uint64_t tenant_id, float v128_ratio_threshold,
                                       uint64_t min_trip_count, MVVMTensorWasmJITTranslation *out);
int32_t mvvm_tensor_wasm_jit_verify(const uint8_t *wasm, size_t wasm_len, const char *function_name,
                                    uint32_t sm_version, uint64_t tenant_id, float v128_ratio_threshold,
                                    uint64_t min_trip_count, uint64_t fingerprint, const uint8_t *ptx, size_t ptx_len,
                                    const uint8_t signature[32]);
void mvvm_tensor_wasm_jit_translation_free(MVVMTensorWasmJITTranslation *result);

} // extern "C"

#endif
