#include "wamr_native.h"
#include "wasm_export.h"
#include <wasm_native.h>
#ifdef __linux__
static NativeSymbol ns[] = {
    REG_NATIVE_FUNC(MPI_Abort, "(ii)i"),
    REG_NATIVE_FUNC(MPI_Allgather, "(*i**i**)i"),
    REG_NATIVE_FUNC(MPI_Allgatherv, "(*i**ii**)i"),
    REG_NATIVE_FUNC(MPI_Allreduce, "(**i***)i"),
    REG_NATIVE_FUNC(MPI_Alltoall, "(*i**i**)i"),
    REG_NATIVE_FUNC(MPI_Alltoallv, "(*ii**ii**)i"),
    REG_NATIVE_FUNC(MPI_Barrier, "(*)i"),
    REG_NATIVE_FUNC(MPI_Bcast, "(*i*i*)i"),
    REG_NATIVE_FUNC(MPI_Cart_create, "(*iiii*)i"),
    REG_NATIVE_FUNC(MPI_Cart_get, "(*i***)i"),
    REG_NATIVE_FUNC(MPI_Cart_rank, "(***)i"),
    REG_NATIVE_FUNC(MPI_Cart_shift, "(*ii**)i"),
    REG_NATIVE_FUNC(MPI_Comm_dup, "(**)i"),
    REG_NATIVE_FUNC(MPI_Comm_free, "(*)i"),
    REG_NATIVE_FUNC(MPI_Comm_rank, "(**)i"),
    REG_NATIVE_FUNC(MPI_Comm_size, "(**)i"),
    REG_NATIVE_FUNC(MPI_Comm_split, "(*ii*)i"),
    REG_NATIVE_FUNC(MPI_Finalize, "()i"),
    REG_NATIVE_FUNC(MPI_Gather, "(*i**i*i*)i"),
    REG_NATIVE_FUNC(MPI_Get_count, "(***)i"),
    REG_NATIVE_FUNC(MPI_Get_processor_name, "(*i)i"),
    REG_NATIVE_FUNC(MPI_Get_version, "(**)i"),
    REG_NATIVE_FUNC(MPI_Init, "(ii)i"),
    REG_NATIVE_FUNC(MPI_Irecv, "(*i*ii**)i"),
    REG_NATIVE_FUNC(MPI_Isend, "(*i*ii**)i"),
    REG_NATIVE_FUNC(MPI_Op_create, "(*ii)i"),
    REG_NATIVE_FUNC(MPI_Op_free, "(*)i"),
    REG_NATIVE_FUNC(MPI_Probe, "(ii**)i"),
    REG_NATIVE_FUNC(MPI_Recv, "(*i*ii**)i"),
    REG_NATIVE_FUNC(MPI_Reduce, "(**i**i*)i"),
    REG_NATIVE_FUNC(MPI_Reduce_scatter, "(**i***)i"),
    REG_NATIVE_FUNC(MPI_Request_free, "(*)i"),
    REG_NATIVE_FUNC(MPI_Rsend, "(*i*ii*)i"),
    REG_NATIVE_FUNC(MPI_Scan, "(**i***)i"),
    REG_NATIVE_FUNC(MPI_Scatter, "(*i**i*i*)i"),
    REG_NATIVE_FUNC(MPI_Send, "(*i*ii*)i"),
    REG_NATIVE_FUNC(MPI_Sendrecv, "(*i*ii*i*ii**)i"),
    REG_NATIVE_FUNC(MPI_Type_commit, "(*)i"),
    REG_NATIVE_FUNC(MPI_Type_contiguous, "(i**)i"),
    REG_NATIVE_FUNC(MPI_Type_free, "(*)i"),
    REG_NATIVE_FUNC(MPI_Type_size, "(**)i"),
    REG_NATIVE_FUNC(MPI_Wait, "(*i)i"),
    REG_NATIVE_FUNC(MPI_Waitall, "(i**)i"),
    REG_NATIVE_FUNC(MPI_Waitany, "(i*i*)i"),
    REG_NATIVE_FUNC(MPI_Wtime, "()F"),
};
uint32_t getMVVMMPIApi(NativeSymbol **nativeSymbols) {
    *nativeSymbols = ns;
    return sizeof(ns) / sizeof(NativeSymbol);
}
#endif
static void dgemm_wrapper(wasm_exec_env_t exec_env, int8_t a, int8_t b, int8_t c, int32_t d, int32_t e, int32_t f,
                          double g, double *h, int32_t i, double *j, int32_t k, double l, double *m, int32_t n) {
    cblas_dgemm(static_cast<const CBLAS_ORDER>(a), static_cast<const CBLAS_TRANSPOSE>(b),
                static_cast<const CBLAS_TRANSPOSE>(c), d, e, f, g, h, i, j, k, l, m, n);
}

static NativeSymbol ns1[] = {
    REG_NATIVE_FUNC(dgemm, "(i8i8i8iiif64f64*if64*f64f64*i)"),
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
#ifdef __linux__
    doSymbolRegistration(getMVVMMPIApi);
#endif
}
