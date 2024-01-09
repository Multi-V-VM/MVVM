#include "wamr_native.h"
#include "wasm_export.h"
#include <wasm_native.h>
#if defined(_WIN32)
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
#if defined(_WIN32)
    cudaSetDevice(0);
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);
    double *a_gpu, *b_gpu, *c_gpu;

    gpuErrchk(cudaMalloc((void **)&a_gpu, n * m * sizeof(float)));
    gpuErrchk(cudaMemcpy(a_gpu, a, n * m * sizeof(float), cudaMemcpyHostToDevice));

    gpuErrchk(cudaMalloc((void **)&b_gpu, m * k * sizeof(float)));

    gpuErrchk(cudaMemcpy(b_gpu, b, m * k * sizeof(float), cudaMemcpyHostToDevice));

    gpuErrchk(cudaMalloc((void **)&c_gpu, m * k * sizeof(float)));

    cudaEventRecord(start);
    cublasHandle_t handle;
    cublasCreate(&handle);
    cublasDgemm(handle, CUBLAS_OP_N, CUBLAS_OP_N, m, n, k, &alpha, a_gpu, m, b_gpu, k, &beta, c_gpu, m);

    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    float miliseconds = 0.0;
    gpuErrchk(cudaMemcpy(c, c_gpu, k * n * sizeof(float), cudaMemcpyDeviceToHost));

    cudaEventElapsedTime(&miliseconds, start, stop);

    fprintf(stderr, "CUBLAS matrix mul took: %f [s]\n", miliseconds / 1000);
#else
    auto begin = clock();
    cblas_dgemm(CblasColMajor, CblasNoTrans, CblasNoTrans, m, n, k, alpha, a, lda, b, ldb, beta, c, ldc);
    auto end = clock();
    auto time_spent = (double)(end - begin) / CLOCKS_PER_SEC;
    fprintf(stderr, "CPU matrix mul took: %f [s]\n", time_spent);
#endif
}
static NativeSymbol ns1[] = {
    REG_NATIVE_FUNC(dgemm, "(iiiFF*iF*iFF*i)"),
};

uint32_t getMVVMDemoApi(NativeSymbol **nativeSymbols) {
    *nativeSymbols = ns1;
    return sizeof(ns1) / sizeof(NativeSymbol);
}

void doSymbolRegistration(uint32_t (*f)(NativeSymbol **ns)) {
    NativeSymbol *symbols;
    uint32_t nSymbols = f(&symbols);
    wasm_native_register_natives("env", symbols, nSymbols);
}

void initialiseWAMRNatives() {
    // Register native symbols
    doSymbolRegistration(getMVVMDemoApi);
}
