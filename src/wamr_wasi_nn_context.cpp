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
#include <wasi-nn.h>

#define MAX_MODEL_SIZE 85000000
#define MAX_OUTPUT_TENSOR_SIZE 1000000
#define INPUT_TENSOR_DIMS 4
#define EPSILON 1e-8
extern WAMRInstance *wamr;
void WAMRWASINNContext::dump_impl(WASINNContext *env) {
    fprintf(stderr, "dump_impl\n");
    // take the model
}
void WAMRWASINNContext::restore_impl(WASINNContext *env) {
    fprintf(stderr, "restore_impl\n");
    // replay the graph initialization  
    wasm_get_output(env->wasm_instance, env->wasm_instance->output_index, env->wasm_instance->output_size);
        FILE *pFile = fopen(model_name, "r");
    if (pFile == NULL)
        return invalid_argument;

    uint8_t *buffer;
    size_t result;

    // allocate memory to contain the whole file:
    buffer = (uint8_t *)malloc(sizeof(uint8_t) * MAX_MODEL_SIZE);
    if (buffer == NULL) {
        fclose(pFile);
        return missing_memory;
    }

    result = fread(buffer, 1, MAX_MODEL_SIZE, pFile);
    if (result <= 0) {
        fclose(pFile);
        free(buffer);
        return missing_memory;
    }

    graph_builder_array arr;

    arr.size = 1;
    arr.buf = (graph_builder *)malloc(sizeof(graph_builder));
    if (arr.buf == NULL) {
        fclose(pFile);
        free(buffer);
        return missing_memory;
    }

    arr.buf[0].size = result;
    arr.buf[0].buf = buffer;

    error res = load(&arr, tensorflowlite, target, g);

    fclose(pFile);
    free(buffer);
    free(arr.buf);
    return res;
}
#endif