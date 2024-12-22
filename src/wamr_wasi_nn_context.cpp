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
#include "wasi_nn.h"
#include <cstdio>
#include <cstdlib>

#define MAX_MODEL_SIZE 85000000
#define MAX_OUTPUT_TENSOR_SIZE 1000000
#define INPUT_TENSOR_DIMS 4
#define EPSILON 1e-8
extern WAMRInstance *wamr;
void WAMRWASINNContext::dump_impl(WASINNContext *env) {
    fprintf(stderr, "dump_impl\n");
    // take the model
    // get target
}
void WAMRWASINNContext::restore_impl(WASINNContext *env) {
    fprintf(stderr, "restore_impl\n");
    // replay the graph initialization
    if (!is_initialized) {
        for (const auto &model : models) {
            FILE *pFile = fopen(model.model_name.c_str(), "r");
            if (pFile == nullptr)
                return;

            uint8_t *buffer;
            size_t result;

            // allocate memory to contain the whole file:
            buffer = (uint8_t *)malloc(sizeof(uint8_t) * MAX_MODEL_SIZE);
            if (buffer == nullptr) {
                fclose(pFile);
                return;
            }

            result = fread(buffer, 1, MAX_MODEL_SIZE, pFile);
            if (result <= 0) {
                fclose(pFile);
                free(buffer);
                return;
            }

            graph_builder_array arr;

            arr.size = 1;
            arr.buf = (graph_builder *)malloc(sizeof(graph_builder));
            if (arr.buf == nullptr) {
                fclose(pFile);
                free(buffer);
                return;
            }

            arr.buf[0].size = result;
            arr.buf[0].buf = buffer;
            graph g;
            error res = load(&arr, tensorflowlite, execution_target::gpu, &g);
            if (res != error::success) {
                res = load(&arr, tensorflowlite, execution_target::tpu, &g);
            }
            if (res != error::success) {
                res = load(&arr, tensorflowlite, execution_target::cpu, &g);
            }

            graph_execution_context ctx;
            if (init_execution_context(g, &ctx) != success) {
                return;
            }
            tensor_dimensions dims;
            dims.size = INPUT_TENSOR_DIMS;
            dims.buf = (uint32_t *)malloc(dims.size * sizeof(uint32_t));
            if (dims.buf == NULL)
                return;


            tensor tensor;
            tensor.dimensions = &dims;
            for (int i = 0; i < tensor.dimensions->size; ++i)
                tensor.dimensions->buf[i] = model.dims[i];
            tensor.type = fp32;
            tensor.data = (uint8_t *)model.input_tensor.data();
            error err = set_input(ctx, 0, &tensor);

            free(dims.buf);

            fclose(pFile);
            free(buffer);
            free(arr.buf);
        }
    }
}
#endif