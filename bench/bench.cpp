#include <benchmark/benchmark.h>
#include <wamr.h>

static void BM_gapbs(benchmark::State& state) {
    for (auto _ : state) {
        // Your code to benchmark goes here
    }
}
BENCHMARK(BM_gapbs);

BENCHMARK_MAIN();