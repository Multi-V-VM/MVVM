/*
 * The WebAssembly Live Migration Project
 *
 *  By: Aibo Hu
 *      Yiwei Yang
 *      Brian Zhao
 *      Andrew Quinn
 *
 *  Copyright 2024 Regents of the Univeristy of California
 *  UC Santa Cruz Sluglab.
 */

#ifndef MVVM_WAMR_NATIVE_H
#define MVVM_WAMR_NATIVE_H
#pragma once

#if defined(MVVM_BUILD_TEST)
#if defined(_WIN32)
#include <cublas_api.h>
#include <cublas_v2.h>
#include <cuda_runtime_api.h>
#else
#include <cblas.h>
#endif
#endif
#include <cstddef>
#include <cstdint>
#include <lib_export.h>

#define REG_NATIVE_FUNC(func_name, signature)                                                                          \
    { #func_name, (void *)func_name##_wrapper, signature, nullptr }

#define REG_WASI_NATIVE_FUNC(func_name, signature)                                                                     \
    { #func_name, (void *)wasi_##func_name, signature, nullptr }

/*
 * -- WAMR native signatures --
 *
 * When defining WAMR native functions you have to specify the function
 * signature. This uses the following scheme, where capitals mean a 64-bit
 * version:
 *
 * - $ = string
 * - * = pointer
 * - F,f = float
 * - I,i = integer
 *
 * For example:
 *
 * int32_t myFunc(int32_t i, char* s) = "(i$)i"
 * int32_t myBigIntFunc(int64_t i, char* s) = "(I$)i"
 * void fooBar(*int32_t i, char* s, float32_t f) = "(*$f)"
 * void nothing() = "()"
 *
 * Note that, when using `*`, `~`, or `$`, the WASM runtime checks that the
 * offset is a pointer within the WASM linear memory, and translates it into a
 * native pointer that we can use. I.e. you could also use `i` to indicate a
 * offset into WASM memory, but it would not be bound-checked, nor translated
 * to a native pointer.
 *
 * Link to WAMR docs:
 * https://github.com/bytecodealliance/wasm-micro-runtime/blob/main/doc/export_native_api.md#buffer-address-conversion-and-boundary-check
 */

void initialiseWAMRNatives();

#endif /* MVVM_WAMR_NATIVE_H */
