/*
 * The WebAssembly Live Migration Project
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 * Copyright 2025 Regents of the University of California
 * UC Santa Cruz Sluglab.
 */

#include "wamr_gpu_cc_framework.h"
#ifdef MVVM_ENABLE_TENSOR_WASM_JIT
#include "wamr_tensor_wasm_jit_bridge.h"
#endif

#include <cstring>
#include <limits>
#include <string>

namespace mvvm::gpu {

struct WasmToGPUTranslator::Impl {
    std::string last_error;
};

WasmToGPUTranslator::WasmToGPUTranslator() : pImpl(std::make_unique<Impl>()) {}

WasmToGPUTranslator::~WasmToGPUTranslator() = default;

namespace {

GPUKernel translationError(const std::string &name, const std::string &message) {
    GPUKernel kernel;
    kernel.name = name;
    kernel.translation_error = message;
    return kernel;
}

#ifdef MVVM_ENABLE_TENSOR_WASM_JIT
GPUKernel translate(const void *wasm_module, size_t module_size, const char *function_name,
                    const WasmToGPUTranslator::TranslationOptions &options) {
    const std::string requested_name = function_name == nullptr ? std::string{} : function_name;
    if (wasm_module == nullptr || module_size < 8) {
        return translationError(requested_name, "WebAssembly input is empty or truncated");
    }
    static constexpr uint8_t wasm_magic[] = {0x00, 0x61, 0x73, 0x6d};
    if (std::memcmp(wasm_module, wasm_magic, sizeof(wasm_magic)) != 0) {
        return translationError(requested_name, "input does not have WebAssembly magic");
    }
    if (!options.enable_vectorization) {
        return translationError(requested_name, "tensor-wasm-jit requires vectorization to emit PTX");
    }
    if (options.enable_shared_memory) {
        return translationError(requested_name,
                                "tensor-wasm-jit 0.3.8 does not expose verified shared-memory lowering");
    }
    if (options.target_compute_capability > std::numeric_limits<uint32_t>::max()) {
        return translationError(requested_name, "CUDA compute capability is out of range");
    }

    MVVMTensorWasmJITTranslation result{};
    const auto status =
        mvvm_tensor_wasm_jit_translate(static_cast<const uint8_t *>(wasm_module), module_size, function_name,
                                       static_cast<uint32_t>(options.target_compute_capability), options.tenant_id,
                                       options.v128_ratio_threshold, options.min_loop_trip_count, &result);
    if (status != 0) {
        const std::string error = result.error != nullptr ? result.error : "tensor-wasm-jit translation failed";
        mvvm_tensor_wasm_jit_translation_free(&result);
        return translationError(requested_name, error);
    }

    GPUKernel kernel;
    kernel.name = result.kernel_name != nullptr ? result.kernel_name : requested_name;
    kernel.requested_function = requested_name;
    kernel.format = GPUKernelFormat::NVIDIA_PTX;
    kernel.binary_code.assign(result.ptx, result.ptx + result.ptx_len);
    kernel.signature.assign(result.signature, result.signature + sizeof(result.signature));
    kernel.rewritten_module.assign(result.rewritten_wasm, result.rewritten_wasm + result.rewritten_wasm_len);
    kernel.function_index = result.function_index;
    kernel.fingerprint = result.fingerprint;
    kernel.grid_size = result.grid_size;
    kernel.block_size = result.block_size;
    kernel.target_compute_capability = static_cast<uint32_t>(options.target_compute_capability);
    kernel.tenant_id = options.tenant_id;
    kernel.v128_ratio_threshold = options.v128_ratio_threshold;
    kernel.min_loop_trip_count = options.min_loop_trip_count;
    mvvm_tensor_wasm_jit_translation_free(&result);
    return kernel;
}
#endif

} // namespace

GPUKernel WasmToGPUTranslator::translateModule(const void *wasm_module, size_t module_size,
                                               const TranslationOptions &options) {
#ifdef MVVM_ENABLE_TENSOR_WASM_JIT
    auto kernel = translate(wasm_module, module_size, nullptr, options);
#else
    auto kernel = translationError({}, "MVVM was built without tensor-wasm-jit support");
#endif
    pImpl->last_error = kernel.translation_error;
    return kernel;
}

GPUKernel WasmToGPUTranslator::translateFunction(const void *wasm_module, size_t module_size,
                                                 const std::string &function_name, const TranslationOptions &options) {
#ifdef MVVM_ENABLE_TENSOR_WASM_JIT
    auto kernel = translate(wasm_module, module_size, function_name.c_str(), options);
#else
    auto kernel = translationError(function_name, "MVVM was built without tensor-wasm-jit support");
#endif
    pImpl->last_error = kernel.translation_error;
    return kernel;
}

bool WasmToGPUTranslator::verifyTranslation(const GPUKernel &kernel, const void *wasm_module, size_t module_size) {
#ifdef MVVM_ENABLE_TENSOR_WASM_JIT
    if (!kernel.valid() || kernel.format != GPUKernelFormat::NVIDIA_PTX || kernel.signature.size() != 32 ||
        wasm_module == nullptr) {
        return false;
    }
    const char *requested = kernel.requested_function.empty() ? nullptr : kernel.requested_function.c_str();
    return mvvm_tensor_wasm_jit_verify(static_cast<const uint8_t *>(wasm_module), module_size, requested,
                                       kernel.target_compute_capability, kernel.tenant_id, kernel.v128_ratio_threshold,
                                       kernel.min_loop_trip_count, kernel.fingerprint, kernel.binary_code.data(),
                                       kernel.binary_code.size(), kernel.signature.data()) == 1;
#else
    (void)kernel;
    (void)wasm_module;
    (void)module_size;
    return false;
#endif
}

} // namespace mvvm::gpu
