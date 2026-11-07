#include "gpu_kernel.h"
#include <stdint.h>
#ifndef GPU_STATE_BYTES
#define GPU_STATE_BYTES 1048576
#endif
__attribute__((import_module("env"), import_name("mvvm_checkpoint"))) void checkpoint(void);
__attribute__((import_module("env"), import_name("mvvm_gpu_alloc"))) uint32_t alloc(uint32_t);
__attribute__((import_module("env"), import_name("mvvm_gpu_upload"))) void upload(uint32_t, const void *, uint32_t);
__attribute__((import_module("env"), import_name("mvvm_gpu_download"))) void download(uint32_t, void *, uint32_t);
__attribute__((import_module("env"), import_name("mvvm_gpu_compile"))) void compile(const void *, uint32_t);
__attribute__((import_module("env"), import_name("mvvm_gpu_execute"))) void execute(uint32_t, uint32_t);
static volatile uint32_t count;
static uint32_t values[8] = {1, 2, 3, 4, 1, 1, 1, 1};
static unsigned char state[GPU_STATE_BYTES];
__attribute__((noinline)) static uint32_t leaf(uint32_t in, uint32_t out, uint32_t i) {
    uint32_t kept = i * 7 + 11;
    ++count;
    execute(in, out);
    // The output exists only on the GPU at this exact suspension boundary.
    checkpoint();
    download(out, values, 16);
    upload(in, values, 32);
    return values[0] + values[1] + values[2] + values[3] + kept;
}
__attribute__((noinline)) static uint32_t middle(uint32_t in, uint32_t out, uint32_t i) {
    uint32_t kept = i + 29;
    return leaf(in, out, i) + kept;
}
static uint32_t (*volatile dispatch)(uint32_t, uint32_t, uint32_t) = middle;
__attribute__((export_name("run"))) uint32_t run(uint32_t iterations) {
    compile(gpu_kernel, sizeof(gpu_kernel));
    uint32_t in = alloc(32), out = alloc(16), extra = alloc(sizeof(state));
    for (uint32_t i = 0; i < sizeof(state); ++i)
        state[i] = (unsigned char)(i * 13 + 7);
    upload(extra, state, sizeof(state));
    upload(in, values, 32);
    uint32_t acc = 17;
    for (uint32_t i = 0; i < iterations; ++i)
        acc += dispatch(in, out, i);
    // Also validate the larger dormant GPU allocation after process replacement.
    for (uint32_t i = 0; i < sizeof(state); ++i)
        state[i] = 0;
    download(extra, state, sizeof(state));
    for (uint32_t i = 0; i < sizeof(state); ++i)
        if (state[i] != (unsigned char)(i * 13 + 7))
            return 0xdeadbeef;
    return acc;
}
__attribute__((export_name("effects"))) uint32_t effects(void) { return count; }
