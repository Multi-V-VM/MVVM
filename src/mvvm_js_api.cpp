/*
 * The WebAssembly Live Migration Project
 *
 * SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
 */

#include "mvvm_js_api.h"
#include "wamr.h"
#include "wamr_export.h"
#include "wamr_read_write.h"
#include "ylt/struct_pack.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef MVVM_JS_BACKEND_NAME
#define MVVM_JS_BACKEND_NAME "default"
#endif

extern WAMRInstance *wamr;
extern std::string offload_addr;
extern int offload_port;
extern std::string target;
extern ReadStream *reader;
extern WriteStream *writer;

namespace {

thread_local std::string last_error;
constexpr uint32_t js_api_heap_size = 64 * 1024;

std::string str(const char *value) { return value == nullptr ? std::string{} : std::string(value); }

std::vector<std::string> list_or_empty(const char *value) {
    auto item = str(value);
    if (item.empty())
        return {};
    return {item};
}

std::vector<std::string> list_or_default(const char *value, const char *fallback) {
    auto item = str(value);
    if (!item.empty())
        return {item};
    return {fallback};
}

void set_error(const std::string &message) { last_error = message; }

bool path_exists(const char *file) {
    if (file == nullptr)
        return false;
    std::error_code error;
    return std::filesystem::exists(file, error) && !error;
}

bool checkpoint_ready(const char *file) {
    if (file == nullptr)
        return false;
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error) || error)
        return false;
    return std::filesystem::file_size(file, error) > 0 && !error;
}

std::string checkpoint_log_path(const char *checkpoint_path) {
    auto directory = std::filesystem::path(str(checkpoint_path)).parent_path();
    if (directory.empty())
        return "checkpoint.log";
    return (directory / "checkpoint.log").string();
}

int normalize_child_status(int status, const char *checkpoint_path, bool expect_checkpoint) {
    if (expect_checkpoint && checkpoint_ready(checkpoint_path))
        return 0;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
}

#if !defined(_WIN32)
int run_child(const std::function<int()> &fn, const char *checkpoint_path, bool expect_checkpoint,
              uint32_t signal_after_ms, uint32_t timeout_ms) {
    pid_t pid = fork();
    if (pid < 0) {
        set_error("fork failed");
        return 1;
    }
    if (pid == 0) {
        try {
            std::exit(fn());
        } catch (const std::exception &err) {
            fprintf(stderr, "mvvm js api child error: %s\n", err.what());
            std::exit(90);
        } catch (...) {
            fprintf(stderr, "mvvm js api child error: unknown exception\n");
            std::exit(91);
        }
    }

    int status = 0;
    bool signaled = false;
    bool timed_out = false;
    const auto start = std::chrono::steady_clock::now();
    pid_t waited = 0;
    while ((waited = waitpid(pid, &status, WNOHANG)) == 0) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (signal_after_ms > 0 && !signaled) {
            if (elapsed.count() >= signal_after_ms) {
                kill(pid, SIGINT);
                signaled = true;
            }
        }
        if (timeout_ms > 0 && elapsed.count() >= timeout_ms) {
            kill(pid, SIGKILL);
            timed_out = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (waited < 0) {
        set_error("waitpid failed");
        return 1;
    }

    const int code = normalize_child_status(status, checkpoint_path, expect_checkpoint);
    if (timed_out && code != 0)
        set_error("MVVM child timed out after " + std::to_string(timeout_ms) + "ms");
    else if (code != 0)
        set_error("MVVM child exited with status " + std::to_string(code));
    return code;
}
#else
int run_child(const std::function<int()> &, const char *, bool, uint32_t, uint32_t) {
    set_error("MVVM JS shared library API requires fork and is not supported on Windows");
    return 1;
}
#endif

int checkpoint_child(const char *target_path, const char *checkpoint_path, const char *dir, const char *map_dir,
                     const char *env, const char *arg, const char *addr, const char *ns_pool, int jit, uint64_t count,
                     int function_index, int function_count, int debug) {
    target = str(target_path);
    offload_addr.clear();
    offload_port = 0;
    reader = nullptr;
    writer = new FwriteStream(checkpoint_path);
    snapshot_threshold = static_cast<size_t>(count);
    stop_func_threshold = function_count;
    is_debug = debug != 0;
    stop_func_index = function_index;

    auto dirs = list_or_default(dir, "./");
    auto map_dirs = list_or_empty(map_dir);
    auto envs = list_or_default(env, "a=b");
    auto argv = list_or_empty(arg);
    argv.insert(argv.begin(), target);
    auto addrs = list_or_default(addr, "0.0.0.0/36");
    auto ns_pools = list_or_empty(ns_pool);

    register_sigtrap();
    register_sigint();
    const auto log_path = checkpoint_log_path(checkpoint_path);
    for (int i = 0; i < 10; i++) {
        FILE *file = fopen(log_path.c_str(), "w");
        if (file != nullptr)
            fclose(file);
    }

    wamr = new WAMRInstance(target.c_str(), jit != 0);
    wamr->heap_size = js_api_heap_size;
    wamr->set_wasi_args(dirs, map_dirs, envs, argv, addrs, ns_pools);
    wamr->instantiate();
    wamr->get_int3_addr();
    wamr->replace_int3_with_nop();
    wamr->replace_mfence_with_nop();
    const int result = wamr->invoke_main();
    delete writer;
    writer = nullptr;
    delete wamr;
    wamr = nullptr;
    return result;
}

int restore_child(const char *target_path, const char *checkpoint_path, int jit, uint64_t count) {
    target = str(target_path);
    offload_addr.clear();
    offload_port = 0;
    writer = nullptr;
    snapshot_threshold = static_cast<size_t>(count);

    register_sigtrap();
    register_sigint();
    wamr = new WAMRInstance(target.c_str(), jit != 0);
    wamr->heap_size = js_api_heap_size;
    wamr->instantiate();
    wamr->get_int3_addr();
    wamr->replace_int3_with_nop();
    reader = new FreadStream(checkpoint_path);
    auto state = struct_pack::deserialize<std::vector<std::unique_ptr<WAMRExecEnv>>>(*reader).value();
    wamr->recover(&state);
    delete reader;
    reader = nullptr;
    delete wamr;
    wamr = nullptr;
    return 0;
}

int validate_paths(const char *target_path, const char *checkpoint_path, bool checkpoint_must_exist) {
    if (target_path == nullptr || str(target_path).empty()) {
        set_error("target is required");
        return 1;
    }
    if (!path_exists(target_path)) {
        set_error("target does not exist: " + str(target_path));
        return 1;
    }
    if (checkpoint_path == nullptr || str(checkpoint_path).empty()) {
        set_error("checkpoint_path is required");
        return 1;
    }
    if (checkpoint_must_exist && !checkpoint_ready(checkpoint_path)) {
        set_error("checkpoint does not exist: " + str(checkpoint_path));
        return 1;
    }
    return 0;
}

} // namespace

const char *mvvm_backend_name(void) { return MVVM_JS_BACKEND_NAME; }

const char *mvvm_last_error(void) { return last_error.c_str(); }

int mvvm_checkpoint(const char *target_path, const char *checkpoint_path, const char *dir, const char *map_dir,
                    const char *env, const char *arg, const char *addr, const char *ns_pool, int jit, uint64_t count,
                    int function_index, int function_count, int debug, uint32_t signal_after_ms, uint32_t timeout_ms) {
    last_error.clear();
    if (const int invalid = validate_paths(target_path, checkpoint_path, false); invalid != 0)
        return invalid;
    if (count != 0 && function_index != 0) {
        set_error("count and function_index are mutually exclusive");
        return 1;
    }
    std::error_code error;
    std::filesystem::remove(checkpoint_path, error);
    if (error) {
        set_error("failed to remove existing checkpoint: " + error.message());
        return 1;
    }
    return run_child(
        [&]() {
            return checkpoint_child(target_path, checkpoint_path, dir, map_dir, env, arg, addr, ns_pool, jit, count,
                                    function_index, function_count, debug);
        },
        checkpoint_path, true, signal_after_ms, timeout_ms);
}

int mvvm_restore(const char *target_path, const char *checkpoint_path, int jit, uint64_t count, uint32_t timeout_ms) {
    last_error.clear();
    if (const int invalid = validate_paths(target_path, checkpoint_path, true); invalid != 0)
        return invalid;
    return run_child([&]() { return restore_child(target_path, checkpoint_path, jit, count); }, checkpoint_path, false,
                     0, timeout_ms);
}
