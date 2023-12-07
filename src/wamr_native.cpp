#include "wamr_native.h"
#include "wasm_export.h"
#include <wasm_native.h>

static void dgemm_wrapper(wasm_exec_env_t exec_env, int8_t a, int8_t b, int8_t c, int32_t d, int32_t e, int32_t f,
                          double g, double *h, int32_t i, double *j, int32_t k, double l, double *m, int32_t n) {
    cblas_dgemm(static_cast<const CBLAS_ORDER>(a), static_cast<const CBLAS_TRANSPOSE>(b),
                static_cast<const CBLAS_TRANSPOSE>(c), d, e, f, g, h, i, j, k, l, m, n);
}

static NativeSymbol ns1[] = {
    REG_NATIVE_FUNC(dgemm, "(i8i8i8iiiFF*iF*FF*i)"),
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

void doWasiSymbolRegistration(uint32_t (*f)(NativeSymbol **ns)) {
    NativeSymbol *symbols;
    uint32_t nSymbols = f(&symbols);
    wasm_native_register_natives("wasi_snapshot_preview1", symbols, nSymbols);
}

void initialiseWAMRNatives() {
    // Register native symbols
    doSymbolRegistration(getMVVMDemoApi);
#ifdef __linux__sb
    doSymbolRegistration(getMVVMMPIApi);
#endif
}
