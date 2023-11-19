#define _ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH 1
#include <benchmark/benchmark.h>
#include <wamr.h>

WAMRInstance *wamr;

void serialize_to_file(WASMExecEnv *instance) {
    /** Sounds like AoT/JIT is in this?*/
    // Note: insert fd
    // a fast restore interface for benchmarking
    std::ifstream stdoutput;
}
//./bfs -g10 -n0 > test/out/generate-g10.out
// \033[92mPASS\033[0m Generates g10
//./bfs -u10 -n0 > test/out/generate-u10.out
// \033[91mFAIL\033[0m Generates u10
//./bfs -f test/graphs/4.gr -n0 > test/out/load-4.gr.out
// \033[92mPASS\033[0m Load 4.gr
//./bfs -f test/graphs/4.el -n0 > test/out/load-4.el.out
// \033[92mPASS\033[0m Load 4.el
//./bfs -f test/graphs/4.wel -n0 > test/out/load-4.wel.out
// \033[92mPASS\033[0m Load 4.wel
//./bfs -f test/graphs/4.graph -n0 > test/out/load-4.graph.out
// \033[92mPASS\033[0m Load 4.graph
//./bfs -f test/graphs/4w.graph -n0 > test/out/load-4w.graph.out
// \033[92mPASS\033[0m Load 4w.graph
//./bfs -f test/graphs/4.mtx -n0 > test/out/load-4.mtx.out
// \033[92mPASS\033[0m Load 4.mtx
//./bfs -f test/graphs/4w.mtx -n0 > test/out/load-4w.mtx.out
// \033[92mPASS\033[0m Load 4w.mtx
//./bc -g10 -vn1 > test/out/verify-bc-g10.out
// \033[92mPASS\033[0m Verify bc
//./bfs -g10 -vn1 > test/out/verify-bfs-g10.out
// \033[92mPASS\033[0m Verify bfs
//./cc -g10 -vn1 > test/out/verify-cc-g10.out
// \033[92mPASS\033[0m Verify cc
//./cc_sv -g10 -vn1 > test/out/verify-cc_sv-g10.out
// \033[92mPASS\033[0m Verify cc_sv
//./pr -g10 -vn1 > test/out/verify-pr-g10.out
// \033[92mPASS\033[0m Verify pr
//./pr_spmv -g10 -vn1 > test/out/verify-pr_spmv-g10.out
// \033[92mPASS\033[0m Verify pr_spmv
//./sssp -g10 -vn1 > test/out/verify-sssp-g10.out
// \033[92mPASS\033[0m Verify sssp
//./tc -g10 -vn1 > test/out/verify-tc-g10.out
static void BM_gapbs(benchmark::State &state) {
    auto dir = {std::string("./")};
    auto map_dir = {std::string("./")};
    std::vector<string> env = {};
    std::vector<std::string> cmd = {"bfs", "bfs", "bfs", "bfs",   "bfs", "bfs",     "bfs",  "bfs", "bfs",
                                    "bc",  "bfs", "cc",  "cc_sv", "pr",  "pr_spmv", "sssp", "tc"};
    std::vector<std::vector<string>> arg = {{"-g10", "-n0"},
                                            {"-u10", "-n0"},
                                            {"-f", "test/graphs/4.gr", "-n0"},
                                            {"-f", "test/graphs/4.el", "-n0"},
                                            {"-f", "test/graphs/4.wel", "-n0"},
                                            {"-f", "test/graphs/4.graph", "-n0"},
                                            {"-f", "test/graphs/4w.graph", "-n0"},
                                            {"-f", "test/graphs/4.mtx", "-n0"},
                                            {"-f", "test/graphs/4w.mtx", "-n0"},
                                            {"-g10", "-vn1"},
                                            {"-g10", "-vn1"},
                                            {"-g10", "-vn1"},
                                            {"-g10", "-vn1"},
                                            {"-g10", "-vn1"},
                                            {"-g10", "-vn1"},
                                            {"-g10", "-vn1"},
                                            {"-g10", "-vn1"}};
    std::vector<string> addr = {};
    std::vector<string> ns_pool = {};
    auto is_jit = false;
#ifndef __APPLE__
    for (auto _ : state) {
        for (auto const &[k, v] : cmd | enumerate) {
            wamr = new WAMRInstance(v.c_str(), is_jit);
            wamr->set_wasi_args(dir, map_dir, env, arg[k], addr, ns_pool);
            wamr->instantiate();
            wamr->invoke_main();
        }
    }
#else
    for (auto _ : state) {
        for (int i = 0; i < cmd.size(); i++) {
            wamr = new WAMRInstance(cmd[i], is_jit);
            wamr->set_wasi_args(dir, map_dir, env, arg[i], addr, ns_pool);
            wamr->instantiate();
            wamr->invoke_main();
        }
    }
#endif
}
BENCHMARK(BM_gapbs);

static void BM_redis(benchmark::State &state) {
    for (auto _ : state) {
        // Your code to benchmark goes here
    }
}
BENCHMARK(BM_redis);

#if !defined(__WIN32__)

static void BM_llama(benchmark::State &state) {
    auto dir = {std::string("./")};
    auto map_dir = {std::string("./")};
    std::vector<string> env = {};
    std::vector<string> arg = {};
    std::vector<string> addr = {};
    std::vector<string> ns_pool = {};
    auto is_jit = false;
    for (auto _ : state) {
        wamr = new WAMRInstance("./run", is_jit);
        wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
        wamr->instantiate();
        wamr->invoke_main();
    }
}
BENCHMARK(BM_llama);

static void BM_llama_aot(benchmark::State &state) {
    auto dir = {std::string("./")};
    auto map_dir = {std::string("./")};
    std::vector<string> env = {};
    std::vector<string> arg = {};
    std::vector<string> addr = {};
    std::vector<string> ns_pool = {};
    auto is_jit = false;
    for (auto _ : state) {
        wamr = new WAMRInstance("./llama.aot", is_jit);
        wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
        wamr->instantiate();
        wamr->invoke_main();
    }
}
BENCHMARK(BM_llama_aot);
#endif

BENCHMARK_MAIN();