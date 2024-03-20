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

#include "wamr_native.h"
#include "wasm_export.h"
#include <wasm_native.h>

#if defined(_WIN32) && defined(MVVM_BUILD_TEST)
#define gpuErrchk(ans)                                                                                                 \
    { gpuAssert((ans), __FILE__, __LINE__); }
inline void gpuAssert(cudaError_t code, const char *file, int line, bool abort = true) {
    if (code != cudaSuccess) {
        fprintf(stderr, "GPUassert: %s %s %d\n", cudaGetErrorString(code), file, line);
        if (abort)
            exit(code);
    }
}
#endif
static void dgemm_wrapper(wasm_exec_env_t exec_env, int32_t m, int32_t n, int32_t k, double alpha, double *a,
                          int32_t lda, double *b, int32_t ldb, double beta, double *c, int32_t ldc) {
//    fprintf(stderr, "CUBLAS matrix mul took: %f [s]\n", 1.);
//    return;
#if defined(MVVM_BUILD_TEST)
#if defined(_WIN32)
    cudaSetDevice(0);
    //    cudaEvent_t start, stop;
    //    cudaEventCreate(&start);
    //    cudaEventCreate(&stop);
    double *a_gpu, *b_gpu, *c_gpu;

    gpuErrchk(cudaMalloc((void **)&a_gpu, n * m * sizeof(double)));
    gpuErrchk(cudaMemcpy(a_gpu, a, n * m * sizeof(double), cudaMemcpyHostToDevice));

    gpuErrchk(cudaMalloc((void **)&b_gpu, m * k * sizeof(double)));

    gpuErrchk(cudaMemcpy(b_gpu, b, m * k * sizeof(double), cudaMemcpyHostToDevice));

    gpuErrchk(cudaMalloc((void **)&c_gpu, m * k * sizeof(double)));

    //    cudaEventRecord(start);
    cublasHandle_t handle;
    cublasCreate(&handle);
    cublasDgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, m, n, k, &alpha, a_gpu, m, b_gpu, k, &beta, c_gpu, m);

    //    cudaEventRecord(stop);
    //    cudaEventSynchronize(stop);

    float miliseconds = 0.0;
    gpuErrchk(cudaMemcpy(c, c_gpu, k * n * sizeof(float), cudaMemcpyDeviceToHost));
    gpuErrchk(cudaFree(a_gpu));
    gpuErrchk(cudaFree(b_gpu));
    gpuErrchk(cudaFree(c_gpu));
//    cudaEventElapsedTime(&miliseconds, start, stop);
//
//    fprintf(stderr, "CUBLAS matrix mul took: %f [s]\n", miliseconds / 1000);
#else
    auto begin = clock();
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, m, n, k, alpha, a, m, b, k, beta, c, m);
    auto end = clock();
    auto time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    fprintf(stderr, "CPU matrix mul took: %f [s]\n", time_spent);
#endif
#endif
}
static void lambda_read_wrapper(wasm_exec_env_t exec_env, int32_t m){
    fprintf(stderr, "lambda_read_wrapper: %d\n", m);
    
}
static void lambda_write_wrapper(wasm_exec_env_t exec_env, int32_t m){
    fprintf(stderr, "lambda_write_wrapper: %d\n", m);

}
static NativeSymbol ns1[] = {
    REG_NATIVE_FUNC(dgemm, "(iiiF*i*iF*i)"),
};

uint32_t getMVVMDemoApi(NativeSymbol **nativeSymbols) {
    *nativeSymbols = ns1;
    return sizeof(ns1) / sizeof(NativeSymbol);
}

void doSymbolRegistration(uint32_t (*f)(NativeSymbol **ns)) {
    NativeSymbol *symbols;
    uint32_t nSymbols = f(&symbols);
    wasm_runtime_register_natives("env", symbols, nSymbols);
}

void initialiseWAMRNatives() {
    // Register native symbols
    doSymbolRegistration(getMVVMDemoApi);
}
