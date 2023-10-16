#include <benchmark/benchmark.h>
#include <wamr.h>

static void BM_gapbs(benchmark::State& state) {
    for (auto _ : state) {
        // Your code to benchmark goes here
    }
}
BENCHMARK(BM_gapbs);

static void BM_clickhouse(benchmark::State& state) {
    for (auto _ : state) {
        // Your code to benchmark goes here
    }
}
BENCHMARK(BM_clickhouse);

static void BM_llama(benchmark::State& state) {
    for (auto _ : state) {
        // Your code to benchmark goes here
    }
}
BENCHMARK(BM_llama);

BENCHMARK_MAIN();