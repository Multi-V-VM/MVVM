//
// Created by victoryang00 on 10/25/2023.
//
#if WASM_ENABLE_WASI_NN != 0
#include "wamr_wasi_nn_context.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRWASINNContext::dump_impl(WASINNContext *env) {}
void WAMRWASINNContext::restore_impl(WASINNContext *env) {}
#endif