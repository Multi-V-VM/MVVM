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

#if WASM_ENABLE_WASI_NN != 0
#include "wamr_wasi_nn_context.h"
#include "wamr.h"
extern WAMRInstance *wamr;
void WAMRWASINNContext::dump_impl(WASINNContext *env) {}
void WAMRWASINNContext::restore_impl(WASINNContext *env) {}
#endif