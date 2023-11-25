#define _ALLOW_ITERATOR_DEBUG_LEVEL_MISMATCH 1
#include "logging.h"
#include "thread_manager.h"
#include <benchmark/benchmark.h>
#include <condition_variable>
#include <wamr.h>

WAMRInstance *wamr;
FwriteStream *writer;

void serialize_to_file(WASMExecEnv *instance) {

    std::vector<std::unique_ptr<WAMRExecEnv>> as;
    std::mutex as_mtx;
    auto cluster = wasm_exec_env_get_cluster(instance);
    auto all_count = bh_list_length(&cluster->exec_env_list);
    int cur_count = 0;
    if (all_count > 1) {
        auto elem = (WASMExecEnv *)bh_list_first_elem(&cluster->exec_env_list);
        while (elem) {
            if (elem == instance) {
                break;
            }
            cur_count++;
            elem = (WASMExecEnv *)bh_list_elem_next(elem);
        }
    } // gets the element index
    auto a = new WAMRExecEnv();
    dump(a, instance);
    std::unique_lock as_ul(as_mtx);
    as.emplace_back(a);
    as.back().get()->cur_count = cur_count;
    if (as.size() == all_count) {
        struct_pack::serialize_to(*writer, as);
        LOGV(INFO) << "serialize to file " << cur_count << " " << all_count << "\n";
        return;
    }
    // Is there some better way to sleep until exit?
    std::condition_variable as_cv;
    as_cv.wait(as_ul);
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
string cmd_postfix_wasm = ".wasm";
string cmd_postfix_aot = ".aot";

static void BM_gapbs(benchmark::State &state) {
    auto dir = {string("./","../../bench/gapbs/test/graphs/")};
    auto map_dir = {string("./","./test/graphs/")};
    std::vector<string> env = {};
    std::vector<string> cmd = {"bfs", "bfs", "bfs", "bfs",   "bfs", "bfs",     "bfs",  "bfs", "bfs",
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
            wamr = new WAMRInstance((v + cmd_postfix_wasm).c_str(), is_jit);
            writer = new FwriteStream((v + ".bin").c_str());
            wamr->set_wasi_args(dir, map_dir, env, arg[k], addr, ns_pool);
            wamr->instantiate();
            wamr->invoke_main();
        }
    }
#else
    for (auto _ : state) {
        for (int i = 0; i < cmd.size(); i++) {
            wamr = new WAMRInstance((v + cmd_postfix_wasm).c_str(), is_jit);
            wamr->set_wasi_args(dir, map_dir, env, arg[i], addr, ns_pool);
            wamr->instantiate();
            wamr->invoke_main();
        }
    }
#endif
}
BENCHMARK(BM_gapbs);

static void BM_gapbs_aot(benchmark::State &state) {
    auto dir = {string("./","../../bench/gapbs/test/graphs/")};
    auto map_dir = {string("./","./test/graphs/")};
    std::vector<string> env = {};
    std::vector<string> cmd = {"bfs", "bfs", "bfs", "bfs",   "bfs", "bfs",     "bfs",  "bfs", "bfs",
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
            wamr = new WAMRInstance((v + cmd_postfix_aot).c_str(), is_jit);
            writer = new FwriteStream((v + ".bin").c_str());
            wamr->set_wasi_args(dir, map_dir, env, arg[k], addr, ns_pool);
            wamr->instantiate();
            wamr->invoke_main();
        }
    }
#else
    for (auto _ : state) {
        for (int i = 0; i < cmd.size(); i++) {
            wamr = new WAMRInstance((v + cmd_postfix_aot).c_str(), is_jit);
            wamr->set_wasi_args(dir, map_dir, env, arg[i], addr, ns_pool);
            wamr->instantiate();
            wamr->invoke_main();
        }
    }
#endif
}
BENCHMARK(BM_gapbs_aot);

static void BM_redis(benchmark::State &state) {
    auto dir = {string("./")};
    auto map_dir = {string("./")};
    std::vector<string> env = {};
    std::vector<string> arg = {};
    std::vector<string> addr = {};
    std::vector<string> ns_pool = {"0.0.0.0/32"};
    auto is_jit = false;
    for (auto _ : state) {
        wamr = new WAMRInstance(("./redis" + cmd_postfix_wasm).c_str(), is_jit);
        wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
        wamr->instantiate();
        wamr->invoke_main();
    }
}
BENCHMARK(BM_redis);

static void BM_redis_aot(benchmark::State &state) {
    auto dir = {string("./")};
    auto map_dir = {string("./")};
    std::vector<string> env = {};
    std::vector<string> arg = {};
    std::vector<string> addr = {};
    std::vector<string> ns_pool = {"0.0.0.0/32"};
    auto is_jit = false;
    for (auto _ : state) {
        wamr = new WAMRInstance(("./redis" + cmd_postfix_aot).c_str(), is_jit);
        wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
        wamr->instantiate();
        wamr->invoke_main();
    }
}
BENCHMARK(BM_redis_aot);

static void BM_llama(benchmark::State &state) {
    auto dir = {string("./")};
    auto map_dir = {string("./")};
    std::vector<string> env = {};
    std::vector<string> arg = {};
    std::vector<string> addr = {};
    std::vector<string> ns_pool = {};
    auto is_jit = false;
    for (auto _ : state) {
        wamr = new WAMRInstance(("./llama" + cmd_postfix_wasm).c_str(), is_jit);
        wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
        wamr->instantiate();
        wamr->invoke_main();
    }
}
BENCHMARK(BM_llama);

static void BM_llama_aot(benchmark::State &state) {
    auto dir = {string("./")};
    auto map_dir = {string("./")};
    std::vector<string> env = {};
    std::vector<string> arg = {};
    std::vector<string> addr = {};
    std::vector<string> ns_pool = {};
    auto is_jit = false;
    for (auto _ : state) {
        wamr = new WAMRInstance(("./llama" + cmd_postfix_aot).c_str(), is_jit);
        wamr->set_wasi_args(dir, map_dir, env, arg, addr, ns_pool);
        wamr->instantiate();
        wamr->invoke_main();
    }
}
BENCHMARK(BM_llama_aot);

BENCHMARK_MAIN();