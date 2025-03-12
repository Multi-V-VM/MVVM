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

#include "wamr.h"
#include "platform_api_vmcore.h"
#include "platform_common.h"
#include "wamr_export.h"
#include "wamr_native.h"
#include "wamr_read_write.h"
#include "wasm_export.h"
#include "wasm_interp.h"
#include "wasm_runtime.h"
#include "wasm_runtime_common.h"
#include <condition_variable>
#include <mutex>
#include <regex>
#include <semaphore>
#include <spdlog/spdlog.h>
#if WASM_ENABLE_LIB_PTHREAD != 0
#include "thread_manager.h"
#endif
#if defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#else
#include <psapi.h>
#include <windows.h>
#endif

WAMRInstance::ThreadArgs **argptr;
std::counting_semaphore<100> wakeup(0);
std::counting_semaphore<100> thread_init(0);
extern WriteStream *writer;
extern std::vector<std::unique_ptr<WAMRExecEnv>> as;
extern std::string offload_addr;
extern int offload_port;
extern std::string target;

std::string removeExtension(std::string &filename) {
    size_t dotPos = filename.find_last_of('.');
    std::string res;
    if (dotPos != std::string::npos) {
        // Extract the substring before the period
        res = filename.substr(0, dotPos);
    } else {
        // If there's no period in the string, it means there's no extension.
        SPDLOG_ERROR("No extension found.");
    }
    return res;
};
static auto string_vec_to_cstr_array = [](const std::vector<std::string> &vecStr) {
    std::vector<const char *> cstrArray(vecStr.size());
    if (vecStr.data() == nullptr || vecStr[0].empty())
        return std::vector<const char *>(0);
    SPDLOG_DEBUG("vecStr[0]: {}", vecStr[0]);
    std::transform(vecStr.begin(), vecStr.end(), cstrArray.begin(), [](const std::string &str) { return str.c_str(); });
    return cstrArray;
};

WAMRInstance::WAMRInstance(const char *wasm_path, bool is_jit, std::string policy) : is_jit(is_jit), policy(policy) {
    {
        std::string path(wasm_path);

        if (path.substr(path.length() - 5) == ".wasm") {
            is_aot = false;
            wasm_file_path = path;
            aot_file_path = path.substr(0, path.length() - 5) + ".aot";
        } else if (path.substr(path.length() - 4) == ".aot") {
            is_aot = true;
            wasm_file_path = path.substr(0, path.length() - 4) + ".wasm";
            aot_file_path = path;
        } else {
            std::cout << "Invalid file extension. Please provide a path ending in either '.wasm' or '.aot'."
                      << std::endl;
            throw;
        }
    }

    RuntimeInitArgs wasm_args;
    memset(&wasm_args, 0, sizeof(RuntimeInitArgs));
    wasm_args.mem_alloc_type = Alloc_With_Allocator;
    wasm_args.mem_alloc_option.allocator.malloc_func = ((void *)malloc);
    wasm_args.mem_alloc_option.allocator.realloc_func = ((void *)realloc);
    wasm_args.mem_alloc_option.allocator.free_func = ((void *)free);
    wasm_args.max_thread_num = 16;
    if (!is_jit)
        wasm_args.running_mode = RunningMode::Mode_Interp;
    else
        wasm_args.running_mode = RunningMode::Mode_LLVM_JIT;
    //	static char global_heap_buf[512 * 1024];// what is this?
    //    wasm_args.mem_alloc_type = Alloc_With_Pool;
    //    wasm_args.mem_alloc_option.pool.heap_buf = global_heap_buf;
    //    wasm_args.mem_alloc_option.pool.heap_size = sizeof(global_heap_buf);
    bh_log_set_verbose_level(0);
    if (!wasm_runtime_full_init(&wasm_args)) {
        SPDLOG_ERROR("Init runtime environment failed.");
        throw;
    }
    initialiseWAMRNatives();
    char *buffer{};
    if (!load_wasm_binary(wasm_path, &buffer)) {
        SPDLOG_ERROR("Load wasm binary failed.\n");
        throw;
    }
    module = wasm_runtime_load((uint8_t *)buffer, buf_size, error_buf, sizeof(error_buf));
    if (!module) {
        SPDLOG_ERROR("Load wasm module failed. error: {}", error_buf);
        throw;
    }
#if !defined(_WIN32)
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        SPDLOG_ERROR("getifaddrs");
        exit(EXIT_FAILURE);
    }

    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr)
            continue;

        if (ifa->ifa_addr->sa_family == AF_INET) {
            // IPv4
            auto *ipv4 = (struct sockaddr_in *)ifa->ifa_addr;
            uint32_t ip = ntohl(ipv4->sin_addr.s_addr);
            if (is_ip_in_cidr(MVVM_SOCK_ADDR, MVVM_SOCK_MASK, ip)) {
                // Extract IPv4 address
                local_addr.ip4[0] = (ip >> 24) & 0xFF;
                local_addr.ip4[1] = (ip >> 16) & 0xFF;
                local_addr.ip4[2] = (ip >> 8) & 0xFF;
                local_addr.ip4[3] = ip & 0xFF;
                if (local_addr.ip4[1] == 17) {
                    break;
                }
            }

        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
            // IPv6
            auto *ipv6 = (struct sockaddr_in6 *)ifa->ifa_addr;
            // Extract IPv6 address
            const auto *bytes = (const uint8_t *)ipv6->sin6_addr.s6_addr;
            if (is_ipv6_in_cidr(MVVM_SOCK_ADDR6, MVVM_SOCK_MASK6, &ipv6->sin6_addr)) {
                for (int i = 0; i < 16; i += 2) {
                    local_addr.ip6[i / 2] = (bytes[i] << 8) + bytes[i + 1];
                }
            }
        }
    }
    local_addr.is_4 = true;

    freeifaddrs(ifaddr);

#endif
}

bool WAMRInstance::load_wasm_binary(const char *wasm_path, char **buffer_ptr) {
    *buffer_ptr = bh_read_file_to_buffer(wasm_path, &buf_size);
    if (!*buffer_ptr) {
        SPDLOG_ERROR("Open wasm app file failed.\n");
        return false;
    }
    if ((get_package_type((const uint8_t *)*buffer_ptr, buf_size) != Wasm_Module_Bytecode) &&
        (get_package_type((const uint8_t *)*buffer_ptr, buf_size) != Wasm_Module_AoT)) {
        SPDLOG_ERROR("WASM bytecode or AOT object is expected but other file format");

        BH_FREE(*buffer_ptr);
        return false;
    }

    return true;
}

WAMRInstance::~WAMRInstance() {
    if (!exec_env)
        wasm_runtime_destroy_exec_env(exec_env);
    if (!module_inst)
        wasm_runtime_deinstantiate(module_inst);
    if (!module)
        wasm_runtime_unload(module);
    wasm_runtime_destroy();
}
void WAMRInstance::find_func(const char *name) {
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        SPDLOG_ERROR("The wasi\"{}\"function is not found.", name);
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                SPDLOG_DEBUG("{} {}", cur_func->u.func_import->field_name, i);

                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                SPDLOG_DEBUG("{} {}", cur_func->u.func->field_name, i);

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
}
int WAMRInstance::invoke_main() {
    if (!(func = wasm_runtime_lookup_wasi_start_function(module_inst))) {
        SPDLOG_ERROR("The wasi mode main function is not found.");
        return -1;
    }

    return wasm_runtime_call_wasm(exec_env, func, 0, nullptr);
}
void WAMRInstance::invoke_init_c() {
    auto name1 = "__wasm_call_ctors";
    if (!(func = wasm_runtime_lookup_function(module_inst, name1, nullptr))) {
        SPDLOG_ERROR("The wasi ", name1, " function is not found.");
    } else {
        wasm_runtime_call_wasm(exec_env, func, 0, nullptr);
    }
}
int WAMRInstance::invoke_fopen(std::string &path, uint32 option) {
    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;
    find_func("o_");
    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, path.size(), (void **)&buffer_);
    if (buffer_for_wasm != 0) {
        uint32 argv[3];
        argv[0] = buffer_for_wasm; // pass the buffer_ address for WASM space
        argv[1] = option; // the size of buffer_
        strncpy(buffer_, path.c_str(), path.size()); // use native address for accessing in runtime
        wasm_runtime_call_wasm(exec_env, func, 2, argv);
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
        return ((int)argv[0]);
    }
    return -1;
};
int WAMRInstance::invoke_frenumber(uint32 fd, uint32 to) {
    uint32 argv[2] = {fd, to};
    find_func("__wasi_fd_renumber");
    wasm_runtime_call_wasm(exec_env, func, 2, argv);
    return argv[0];
};

int WAMRInstance::invoke_sock_open(uint32_t domain, uint32_t socktype, uint32_t protocol, uint32_t sockfd) {
    uint32 argv[4] = {domain, socktype, protocol, sockfd};
    find_func("s_");
    auto res = wasm_runtime_call_wasm(exec_env, func, 4, argv);
    return argv[0];
}
int WAMRInstance::invoke_sock_client_connect(uint32_t sockfd, struct sockaddr *sock, socklen_t sock_size) {
    uint32 argv[1] = {sockfd};
    find_func("ci_");
    wasm_runtime_call_wasm(exec_env, func, 1, argv);
    int res = argv[0];
    return -1;
}
int WAMRInstance::invoke_sock_server_connect(uint32_t sockfd, struct sockaddr *sock, socklen_t sock_size) {
    uint32 argv[1] = {sockfd};
    find_func("si_");
    wasm_runtime_call_wasm(exec_env, func, 1, argv);
    int res = argv[0];
    return -1;
}
int WAMRInstance::invoke_sock_accept(uint32_t sockfd, struct sockaddr *sock, socklen_t sock_size) {
    char *buffer1_ = nullptr;
    char *buffer2_ = nullptr;
    uint32_t buffer1_for_wasm;
    uint32_t buffer2_for_wasm;
    find_func("accept");

    buffer1_for_wasm =
        wasm_runtime_module_malloc(module_inst, sizeof(struct sockaddr), reinterpret_cast<void **>(&buffer1_));
    buffer2_for_wasm =
        wasm_runtime_module_malloc(module_inst, sizeof(struct sockaddr), reinterpret_cast<void **>(&buffer2_));
    if (buffer1_for_wasm != 0 && buffer2_for_wasm != 0) {
        uint32 argv[3];
        memcpy(buffer1_, sock, sizeof(struct sockaddr)); // use native address for accessing in runtime
        memcpy(buffer2_, &sock_size, sizeof(socklen_t)); // use native address for accessing in runtime
        argv[0] = sockfd; // pass the buffer_ address for WASM space
        argv[1] = buffer1_for_wasm;
        argv[2] = buffer2_for_wasm;
        wasm_runtime_call_wasm(exec_env, func, 3, argv);
        int res = argv[0];
        wasm_runtime_module_free(module_inst, buffer1_for_wasm);
        wasm_runtime_module_free(module_inst, buffer2_for_wasm);
        return res;
    }
    return -1;
}
int WAMRInstance::invoke_sock_getsockname(uint32_t sockfd, struct sockaddr **sock, socklen_t *sock_size) {
    char *buffer1_ = nullptr;
    char *buffer2_ = nullptr;
    uint32_t buffer1_for_wasm;
    uint32_t buffer2_for_wasm;
    find_func("getsockname");
    buffer1_for_wasm = wasm_runtime_module_malloc(module_inst, *sock_size, reinterpret_cast<void **>(&buffer1_));
    buffer2_for_wasm = wasm_runtime_module_malloc(module_inst, sizeof(socklen_t), reinterpret_cast<void **>(&buffer2_));
    if (buffer1_for_wasm != 0) {
        uint32 argv[3];
        memcpy(buffer1_, *sock, sizeof(struct sockaddr));
        argv[0] = sockfd;
        argv[1] = buffer1_for_wasm;
        argv[2] = buffer2_for_wasm;
        wasm_runtime_call_wasm(exec_env, func, 3, argv);
        memcpy(*sock, buffer1_, sizeof(struct sockaddr));
        int res = argv[0];
        wasm_runtime_module_free(module_inst, buffer1_for_wasm);
        return res;
    }
    return -1;
}
int WAMRInstance::invoke_fseek(uint32 fd, uint32 flags, uint32 offset) {
    // return 0;
    find_func("__wasi_fd_seek");
    uint32 argv[5] = {fd, offset, 0, flags, 0};
    auto res = wasm_runtime_call_wasm(exec_env, func, 5, argv);
    return argv[0];
}
int WAMRInstance::invoke_preopen(uint32 fd, const std::string &path) {

    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;
    find_func("__wasilibc_nocwd_openat_nomode");
    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, 100, reinterpret_cast<void **>(&buffer_));
    if (buffer_for_wasm != 0) {
        uint32 argv[3];
        strncpy(buffer_, path.c_str(), path.size()); // use native address for accessing in runtime
        buffer_[path.size()] = '\0';
        argv[0] = fd; // pass the buffer_ address for WASM space
        argv[1] = buffer_for_wasm; // the size of buffer_
        argv[2] = 102; // O_RW | O_CREATE
        wasm_runtime_call_wasm(exec_env, func, 3, argv);
        int res = argv[0];
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
        return res;
    }
    return -1;
}
int WAMRInstance::invoke_recv(int sockfd, uint8 **buf, size_t len, int flags) {
    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;
    find_func("recv");

    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, len, reinterpret_cast<void **>(&buffer_));
    if (buffer_for_wasm != 0) {
        uint32 argv[4];
        memcpy(buffer_, *buf, len); // use native address for accessing in runtime
        argv[0] = sockfd; // pass the buffer_ address for WASM space
        argv[1] = buffer_for_wasm; // the size of buffer_
        argv[2] = len;
        argv[3] = flags;
        wasm_runtime_call_wasm(exec_env, func, 4, argv);
        int res = argv[0];
        memcpy(*buf, buffer_, len);
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
        return res;
    }
    return -1;
}
int WAMRInstance::invoke_recvfrom(int sockfd, uint8 **buf, size_t len, int flags, struct sockaddr *src_addr,
                                  socklen_t *addrlen) {
    char *buffer1_ = nullptr;
    char *buffer2_ = nullptr;
    char *buffer3_ = nullptr;
    uint32_t buffer1_for_wasm;
    uint32_t buffer2_for_wasm;
    uint32_t buffer3_for_wasm;
    find_func("recvfrom");

    buffer1_for_wasm = wasm_runtime_module_malloc(module_inst, len, reinterpret_cast<void **>(&buffer1_));
    buffer2_for_wasm =
        wasm_runtime_module_malloc(module_inst, sizeof(struct sockaddr), reinterpret_cast<void **>(&buffer2_));
    buffer3_for_wasm = wasm_runtime_module_malloc(module_inst, sizeof(socklen_t), reinterpret_cast<void **>(&buffer3_));
    if (buffer1_for_wasm != 0 && buffer2_for_wasm != 0 && buffer3_for_wasm != 0) {
        uint32 argv[6];
        memcpy(buffer1_, *buf, len); // use native address for accessing in runtime
        memcpy(buffer2_, src_addr, sizeof(struct sockaddr)); // use native address for accessing in runtime
        memcpy(buffer3_, addrlen, sizeof(socklen_t)); // use native address for accessing in runtime
        argv[0] = sockfd; // pass the buffer_ address for WASM space
        argv[1] = buffer1_for_wasm; // the size of buffer_
        argv[2] = len;
        argv[3] = flags;
        argv[4] = buffer2_for_wasm;
        argv[5] = buffer3_for_wasm;
        wasm_runtime_call_wasm(exec_env, func, 6, argv);
        int res = argv[0];
        memcpy(*buf, buffer1_, len);
        wasm_runtime_module_free(module_inst, buffer1_for_wasm);
        wasm_runtime_module_free(module_inst, buffer2_for_wasm);
        wasm_runtime_module_free(module_inst, buffer3_for_wasm);
        return res;
    }
    return -1;
}
WASMExecEnv *WAMRInstance::get_exec_env() {
    return cur_env; // should return the current thread's
}

WASMModuleInstance *WAMRInstance::get_module_instance() const {
    return reinterpret_cast<WASMModuleInstance *>(exec_env->module_inst);
}

#if WASM_ENABLE_AOT != 0
AOTModule *WAMRInstance::get_module() const {
    return reinterpret_cast<AOTModule *>(reinterpret_cast<WASMModuleInstance *>(exec_env->module_inst)->module);
}
#endif

void restart_execution(uint32 id) {
    WAMRInstance::ThreadArgs *targs = argptr[id];
    wasm_interp_call_func_bytecode((WASMModuleInstance *)targs->exec_env->module_inst, targs->exec_env,
                                   targs->exec_env->cur_frame->function, targs->exec_env->cur_frame->prev_frame);
}
#if WASM_ENABLE_LIB_PTHREAD != 0
extern "C" {
korp_mutex syncop_mutex;
korp_cond syncop_cv;
}
void WAMRInstance::replay_sync_ops(bool main, wasm_exec_env_t exec_env) {
    if (main) {
        std::map<uint32, uint32> ref_map = {};
        for (auto &i : sync_ops) {
            // remap to new tids so that if we reserialize it'll be correct
            i.tid = tid_map[i.tid];
            if (ref_map.find(i.ref) != ref_map.end()) {
                pthread_mutex_init_wrapper(exec_env, &i.ref, nullptr);
                ref_map[i.ref] = i.tid;
            }
        }
        // start from the beginning
        sync_iter = sync_ops.begin();
        thread_init.release(100);
    } else {
        // wait for remap to finish
        thread_init.acquire();
    }
    // Actually replay
    os_mutex_lock(&syncop_mutex);
    while (sync_iter != sync_ops.end()) {
        SPDLOG_INFO("test {} == {}, op {}\n", (uint64_t)exec_env->handle, (uint64_t)sync_iter->tid,
                    ((int)sync_iter->sync_op));
        if (((uint64_t)(*sync_iter).tid) == ((uint64_t)exec_env->handle)) {
            SPDLOG_INFO("replay {}, op {}\n", sync_iter->tid, ((int)sync_iter->sync_op));
            auto mysync = sync_iter;
            ++sync_iter;
            // do op
            switch (mysync->sync_op) {
            case SYNC_OP_MUTEX_LOCK:
                pthread_mutex_lock_wrapper(exec_env, &(mysync->ref));
                break;
            case SYNC_OP_MUTEX_UNLOCK:
                pthread_mutex_unlock_wrapper(exec_env, &(mysync->ref));
                break;
            case SYNC_OP_COND_WAIT:
                pthread_cond_wait_wrapper(exec_env, &(mysync->ref), nullptr);
                break;
            case SYNC_OP_COND_SIGNAL:
                pthread_cond_signal_wrapper(exec_env, &(mysync->ref));
                break;
            case SYNC_OP_COND_BROADCAST:
                pthread_cond_broadcast_wrapper(exec_env, &(mysync->ref));
                break;
            case SYNC_OP_ATOMIC_WAIT:
                wasm_runtime_atomic_wait(
                    exec_env->module_inst,
                    ((uint8_t *)((WASMModuleInstance *)exec_env->module_inst)->memories[0]->memory_data + mysync->ref),
                    mysync->expected, -1, mysync->wait64);
                break;
            case SYNC_OP_ATOMIC_NOTIFY:
                break;
            }
            // wakeup everyone
            os_cond_signal(&syncop_cv);
        } else {
            os_cond_reltimedwait(&syncop_cv, &syncop_mutex, 10);
        }
    }
    os_mutex_unlock(&syncop_mutex);
}
// End Sync Op Specific Stuff
#endif
WAMRExecEnv *child_env;
// will call pthread create wrapper if needed?
void WAMRInstance::recover(std::vector<std::unique_ptr<WAMRExecEnv>> *e_) {
    execEnv.reserve(e_->size());
    std::transform(e_->begin(), e_->end(), std::back_inserter(execEnv),
                   [](const std::unique_ptr<WAMRExecEnv> &uniquePtr) { return uniquePtr ? uniquePtr.get() : nullptr; });
    // got this done tommorrow.
    // order threads by id (descending)
    std::sort(execEnv.begin(), execEnv.end(), [](const auto &a, const auto &b) {
        return a->frames.back()->function_index < b->frames.back()->function_index;
    });

    argptr = (ThreadArgs **)malloc(sizeof(void *) * execEnv.size());
    set_wasi_args(execEnv.front()->module_inst.wasi_ctx);
    instantiate();
    this->time = std::chrono::high_resolution_clock::now();
    // invoke_init_c();
    // std::string stdout = "/dev/stdout";
    // invoke_preopen(1, stdout);

    restore(execEnv.front(), cur_env);
    if (tid_start_arg_map.find(execEnv.back()->cur_count) != tid_start_arg_map.end()) {
        std::sort(execEnv.begin() + 1, execEnv.end(), [&](const auto &a, const auto &b) {
            return tid_start_arg_map[a->cur_count].second < tid_start_arg_map[b->cur_count].second;
        });
    }

    auto main_env = cur_env;
    cur_thread = ((uint64_t)main_env->handle);

    fprintf(stderr, "main_env created %p %p\n\n", main_env, main_saved_call_chain);

    main_env->is_restore = true;

    main_env->restore_call_chain = nullptr;

//    std::string path = "./rgbd_dataset_freiburg3_long_office_household_validation/rgb/1341848156.474862.png";
//    invoke_fopen( path,4);
//    path = "./rgbd_dataset_freiburg3_long_office_household_validation/depth";
//    invoke_fopen( path,5);
#if WASM_ENABLE_LIB_PTHREAD != 0
    // SPDLOG_ERROR("no impl");
    // exit(-1);
    spawn_child(main_env, true);
#endif
    // restart main thread execution
    if (!is_aot) {
        wasm_interp_call_func_bytecode(get_module_instance(), get_exec_env(), get_exec_env()->cur_frame->function,
                                       get_exec_env()->cur_frame->prev_frame);
    } else {
        exec_env = cur_env = main_env;
        module_inst = main_env->module_inst;

        fprintf(stderr, "invoke_init_c\n");
        fprintf(stderr, "wakeup.release\n");
        wakeup.release(100);

#if WASM_ENABLE_LIB_PTHREAD != 0
        SPDLOG_ERROR("no impl");
        exit(-1);
        fprintf(stderr, "invoke main %p %u\n", cur_env, cur_env->call_chain_size);
        // replay sync ops to get OS state matching
        wamr_handle_map(execEnv.front()->cur_count, ((uint64_t)main_env->handle));

        replay_sync_ops(true, main_env);
#endif
        if (this->time != std::chrono::time_point<std::chrono::high_resolution_clock>()) {
            auto end = std::chrono::high_resolution_clock::now();
            // get duration in us
            auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - this->time);
            this->time = std::chrono::time_point<std::chrono::high_resolution_clock>();
            SPDLOG_INFO("Recover time: {}\n", dur.count() / 1000000.0);
            // put things back
        }
        invoke_main();
    }
}

#if WASM_ENABLE_LIB_PTHREAD != 0
void WAMRInstance::spawn_child(WASMExecEnv *cur_env, bool main) {
    static std::vector<WAMRExecEnv *>::iterator iter;
    static uint64 parent;
    if (main) {
        iter = ++(execEnv.begin());
        parent = 0;
    }
    //  Each thread needs it's own thread arg
    auto thread_arg = ThreadArgs{cur_env};
    static std::mutex mtx;
    static std::condition_variable cv;
    std::unique_lock ul(mtx);

    while (iter != execEnv.end()) {
        // Get parent's virtual TID from child's OS TID
        if (parent == 0) {
            child_env = *iter;
            parent = child_env->cur_count;
            if (tid_start_arg_map.find(child_env->cur_count) != tid_start_arg_map.end()) {
                parent = tid_start_arg_map[parent].second;
            }
            parent = child_tid_map[parent];
            for (auto &[tid, vtid] : tid_start_arg_map) {
                if (vtid.second == parent) {
                    parent = tid;
                    break;
                }
            }
            SPDLOG_ERROR("{} {}", parent, child_env->cur_count);
        } // calculate parent TID once
        if (parent != ((uint64_t)cur_env->handle) && (parent != !main)) {
            cv.wait(ul);
            continue;
        }
        // requires to record the args and callback for the pthread.
        argptr[id] = &thread_arg;
        // restart thread execution
        SPDLOG_DEBUG("pthread_create_wrapper, func {}\n", child_env->cur_count);
        // module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
        if (tid_start_arg_map.find(child_env->cur_count) != tid_start_arg_map.end()) {
            SPDLOG_ERROR("no impl");
            exit(-1);
            // find the parent env
            auto saved_call_chain_size = cur_env->call_chain_size;
            auto saved_stack_size = cur_env->wasm_stack.top_boundary - cur_env->wasm_stack.bottom;
            auto saved_stack = (char *)malloc(saved_stack_size);
            memcpy(saved_stack, cur_env->wasm_stack.bottom, saved_stack_size);
            exec_env->is_restore = true;
            // main thread
            thread_spawn_wrapper(cur_env, tid_start_arg_map[child_env->cur_count].first);
            cur_env->call_chain_size = saved_call_chain_size;
            memcpy(cur_env->wasm_stack.bottom, saved_stack, saved_stack_size);
            free(saved_stack);
        } else {
            exec_env->is_restore = true;
            pthread_create_wrapper(cur_env, nullptr, nullptr, id, id); // tid_map
        }
        fprintf(stderr, "child spawned %p %p\n\n", cur_env, child_env);
        // sleep(1);
        thread_init.acquire();
        // advance ptr
        ++iter;
        parent = 0;
        cv.notify_all();
    }
}
#endif

WASMFunction *WAMRInstance::get_func() { return static_cast<WASMFunction *>(func); }
void WAMRInstance::set_func(WASMFunction *f) { func = static_cast<WASMFunction *>(f); }
void WAMRInstance::set_wasi_args(const std::vector<std::string> &dir_list, const std::vector<std::string> &map_dir_list,
                                 const std::vector<std::string> &env_list, const std::vector<std::string> &arg_list,
                                 const std::vector<std::string> &addr_list,
                                 const std::vector<std::string> &ns_lookup_pool) {

    dir_ = string_vec_to_cstr_array(dir_list);
    map_dir_ = string_vec_to_cstr_array(map_dir_list);
    env_ = string_vec_to_cstr_array(env_list);
    arg_ = string_vec_to_cstr_array(arg_list);
    addr_ = string_vec_to_cstr_array(addr_list);
    ns_pool_ = string_vec_to_cstr_array(ns_lookup_pool);

    wasm_runtime_set_wasi_args_ex(this->module, dir_.data(), dir_.size(), map_dir_.data(), map_dir_.size(), env_.data(),
                                  env_.size(), const_cast<char **>(arg_.data()), arg_.size(), 0, 1, 2);

    wasm_runtime_set_wasi_addr_pool(module, addr_.data(), addr_.size());
    wasm_runtime_set_wasi_ns_lookup_pool(module, ns_pool_.data(), ns_pool_.size());
}
void WAMRInstance::set_wasi_args(WAMRWASIContext &context) {
    set_wasi_args(context.dir, context.map_dir, context.env_list, context.argv_list, context.addr_pool,
                  context.ns_lookup_list);
}
extern WAMRInstance *wamr;
extern "C" { // stop name mangling so it can be linked externally
void wamr_wait(wasm_exec_env_t exec_env) {
    SPDLOG_DEBUG("child getting ready to wait {}", fmt::ptr(exec_env));
    thread_init.release(1);
    wamr->spawn_child(exec_env, false);
    SPDLOG_DEBUG("finish child restore");
    wakeup.acquire();
#if WASM_ENABLE_LIB_PTHREAD != 0
    SPDLOG_DEBUG("go child!! {}", ((uint64_t)exec_env->handle));
    wamr->replay_sync_ops(false, exec_env);
    SPDLOG_DEBUG("finish syncing");
#endif
    if (wamr->time != std::chrono::time_point<std::chrono::high_resolution_clock>()) {
        auto end = std::chrono::high_resolution_clock::now();
        // get duration in us
        auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - wamr->time);
        wamr->time = std::chrono::time_point<std::chrono::high_resolution_clock>();
        SPDLOG_INFO("Recover time: {}\n", dur.count() / 1000000.0);
        // put things back
    }
    // finished restoring
    exec_env->is_restore = true;
    fprintf(stderr, "invoke side%p\n", ((WASMModuleInstance *)exec_env->module_inst)->global_data);
}

WASMExecEnv *restore_env(WASMModuleInstanceCommon *module_inst) {
    auto exec_env = wasm_exec_env_create_internal(module_inst, wamr->stack_size);
    restore(child_env, exec_env);

    wamr->cur_thread = ((uint64_t)exec_env->handle);
    exec_env->is_restore = true;

    return exec_env;
}
}

void WAMRInstance::instantiate() {
    module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
    if (!module_inst) {
        SPDLOG_ERROR("Instantiate wasm module failed. error: {}", error_buf);
        throw;
    }
    cur_env = exec_env = wasm_runtime_create_exec_env(module_inst, stack_size);
}

bool is_ip_in_cidr(const char *base_ip, int subnet_mask_len, uint32_t ip) {
    uint32_t base_ip_bin, subnet_mask, network_addr, broadcast_addr;
    SPDLOG_DEBUG("base_ip: {} subnet_mask_len: {}", base_ip, subnet_mask_len);
    SPDLOG_DEBUG("ip: {}.{}.{}.{}", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);

    // Convert base IP to binary
    if (inet_pton(AF_INET, base_ip, &base_ip_bin) != 1) {
        fprintf(stderr, "Error converting base IP to binary\n");
        return false;
    }

    // Ensure that the subnet mask length is valid
    if (subnet_mask_len < 0 || subnet_mask_len > 32) {
        fprintf(stderr, "Invalid subnet mask length\n");
        return false;
    }

    // Calculate subnet mask in binary
    subnet_mask = htonl(~((1 << (32 - subnet_mask_len)) - 1));

    // Calculate network and broadcast addresses
    network_addr = base_ip_bin & subnet_mask;
    broadcast_addr = network_addr | ~subnet_mask;

    // Ensure ip is in network byte order
    uint32_t ip_net_order = htonl(ip);

    // Check if IP is within range
    return ip_net_order >= network_addr && ip_net_order <= broadcast_addr;
}
bool is_ipv6_in_cidr(const char *base_ip_str, int subnet_mask_len, struct in6_addr *ip) {
    struct in6_addr base_ip {
    }, subnet_mask{}, network_addr{}, ip_min{}, ip_max{};
    unsigned char mask;

    // Convert base IP to binary
    inet_pton(AF_INET6, base_ip_str, &base_ip);

    // Clear subnet_mask and network_addr
    memset(&subnet_mask, 0, sizeof(subnet_mask));
    memset(&network_addr, 0, sizeof(network_addr));

    // Create the subnet mask and network address
    for (int i = 0; i < subnet_mask_len / 8; i++) {
        subnet_mask.s6_addr[i] = 0xff;
    }
    if (subnet_mask_len % 8) {
        mask = (0xff << (8 - (subnet_mask_len % 8)));
        subnet_mask.s6_addr[subnet_mask_len / 8] = mask;
    }

    // Apply the subnet mask to the base IP to get the network address
    for (int i = 0; i < 16; i++) {
        network_addr.s6_addr[i] = base_ip.s6_addr[i] & subnet_mask.s6_addr[i];
    }

    // Calculate the first and last IPs in the range
    ip_min = network_addr;
    ip_max = network_addr;
    for (int i = 15; i >= subnet_mask_len / 8; i--) {
        ip_max.s6_addr[i] = 0xff;
    }

    // Check if IP is within range
    for (int i = 0; i < 16; i++) {
        if (ip->s6_addr[i] < ip_min.s6_addr[i] || ip->s6_addr[i] > ip_max.s6_addr[i]) {
            return false;
        }
    }
    return true;
}
long get_rss() {
#if defined(__linux__)
    long rss = 0;
    std::ifstream statm("/proc/self/statm");
    if (statm.is_open()) {
        long dummy; // to skip other fields
        statm >> dummy >> rss; // skip the first field (size) and read rss
        statm.close();
    } else {
        std::cerr << "Unable to open /proc/self/statm" << std::endl;
    }
    return rss * sysconf(_SC_PAGESIZE); // mul
// multiply by page size to get bytes
#elif defined(__APPLE__)

    mach_msg_type_number_t infoCount = TASK_BASIC_INFO_COUNT;
    task_basic_info_data_t taskInfo;

    if (KERN_SUCCESS != task_info(mach_task_self(), TASK_BASIC_INFO, (task_info_t)&taskInfo, &infoCount)) {
        std::cerr << "Failed to get task info" << std::endl;
        return -1; // Indicate failure
    } else {
        // taskInfo.resident_size is in bytes
        return taskInfo.resident_size;
    }
#else
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize; // RSS in bytes
    } else {
        std::cerr << "Failed to get process memory info" << std::endl;
        return -1; // Indicate failure
    }
#endif
}
void serialize_to_file(WASMExecEnv *instance) {
    // gateway
    auto start = std::chrono::high_resolution_clock::now();
    if (writer == nullptr) {
        if (offload_addr.empty())
            writer = new FwriteStream((removeExtension(target) + ".bin").c_str());
#if __linux__
        else
            writer = new SocketWriteStream(offload_addr.c_str(), offload_port);
#endif
    }
#if WASM_ENABLE_LIB_PTHREAD != 0
    auto cluster = wasm_exec_env_get_cluster(instance);
    auto all_count = bh_list_length(&cluster->exec_env_list);
    // fill vector

    std::unique_lock as_ul(wamr->as_mtx);
    SPDLOG_DEBUG("get lock");
    wamr->ready++;
    wamr->lwcp_list[((uint64_t)instance->handle)]++;
    if (wamr->ready == all_count) {
        wamr->should_snapshot = true;
    }
    // If we're not all ready
    SPDLOG_DEBUG("thread {}, with {} ready out of {} total", ((uint64_t)instance->handle), wamr->ready, all_count);
#endif
#if !defined(_WIN32)
    if (!wamr->socket_fd_map_.empty() && wamr->should_snapshot) {
        // tell gateway to keep alive the server
        struct sockaddr_in addr {};
        int fd = 0;
        ssize_t rc;
        SocketAddrPool src_addr{};
        bool is_server = false;
        for (auto [tmp_fd, sock_data] : wamr->socket_fd_map_) {
            if (sock_data.is_server) {
                is_server = true;
                break;
            }
        }
        wamr->op_data.op = is_server ? MVVM_SOCK_SUSPEND_TCP_SERVER : MVVM_SOCK_SUSPEND;

        for (auto [tmp_fd, sock_data] : wamr->socket_fd_map_) {
            int idx = wamr->op_data.size;
            src_addr = sock_data.socketAddress;
            auto tmp_ip4 =
                fmt::format("{}.{}.{}.{}", src_addr.ip4[0], src_addr.ip4[1], src_addr.ip4[2], src_addr.ip4[3]);
            auto tmp_ip6 =
                fmt::format("{}:{}:{}:{}:{}:{}:{}:{}", src_addr.ip6[0], src_addr.ip6[1], src_addr.ip6[2],
                            src_addr.ip6[3], src_addr.ip6[4], src_addr.ip6[5], src_addr.ip6[6], src_addr.ip6[7]);
            if (src_addr.is_4 && tmp_ip4 == "0.0.0.0" || !src_addr.is_4 && tmp_ip6 == "0:0:0:0:0:0:0:0") {
                src_addr = wamr->local_addr;
                src_addr.port = sock_data.socketAddress.port;
            }
            SPDLOG_INFO("addr: {} {}.{}.{}.{}  port: {}", tmp_fd, src_addr.ip4[0], src_addr.ip4[1], src_addr.ip4[2],
                        src_addr.ip4[3], src_addr.port);
            // make the rest coroutine?
            tmp_ip4 = fmt::format("{}.{}.{}.{}", sock_data.socketSentToData.dest_addr.ip.ip4[0],
                                  sock_data.socketSentToData.dest_addr.ip.ip4[1],
                                  sock_data.socketSentToData.dest_addr.ip.ip4[2],
                                  sock_data.socketSentToData.dest_addr.ip.ip4[3]);
            tmp_ip6 = fmt::format(
                "{}:{}:{}:{}:{}:{}:{}:{}", sock_data.socketSentToData.dest_addr.ip.ip6[0],
                sock_data.socketSentToData.dest_addr.ip.ip6[1], sock_data.socketSentToData.dest_addr.ip.ip6[2],
                sock_data.socketSentToData.dest_addr.ip.ip6[3], sock_data.socketSentToData.dest_addr.ip.ip6[4],
                sock_data.socketSentToData.dest_addr.ip.ip6[5], sock_data.socketSentToData.dest_addr.ip.ip6[6],
                sock_data.socketSentToData.dest_addr.ip.ip6[7]);
            if (tmp_ip4 == "0.0.0.0" || tmp_ip6 == "0:0:0:0:0:0:0:0") {
                if (!wamr->op_data.is_tcp) {
                    if (sock_data.socketSentToData.dest_addr.ip.is_4 && tmp_ip4 == "0.0.0.0" ||
                        !sock_data.socketSentToData.dest_addr.ip.is_4 && tmp_ip6 == "0:0:0:0:0:0:0:0") {
                        wamr->op_data.addr[idx][1].is_4 = sock_data.socketRecvFromDatas[0].src_addr.ip.is_4;
                        std::memcpy(wamr->op_data.addr[idx][1].ip4, sock_data.socketRecvFromDatas[0].src_addr.ip.ip4,
                                    sizeof(sock_data.socketRecvFromDatas[0].src_addr.ip.ip4));
                        std::memcpy(wamr->op_data.addr[idx][1].ip6, sock_data.socketRecvFromDatas[0].src_addr.ip.ip6,
                                    sizeof(sock_data.socketRecvFromDatas[0].src_addr.ip.ip6));
                        wamr->op_data.addr[idx][1].port = sock_data.socketRecvFromDatas[0].src_addr.port;
                    } else {
                        wamr->op_data.addr[idx][1].is_4 = sock_data.socketSentToData.dest_addr.ip.is_4;
                        std::memcpy(wamr->op_data.addr[idx][1].ip4, sock_data.socketSentToData.dest_addr.ip.ip4,
                                    sizeof(sock_data.socketSentToData.dest_addr.ip.ip4));
                        std::memcpy(wamr->op_data.addr[idx][1].ip6, sock_data.socketSentToData.dest_addr.ip.ip6,
                                    sizeof(sock_data.socketSentToData.dest_addr.ip.ip6));
                        wamr->op_data.addr[idx][1].port = sock_data.socketSentToData.dest_addr.port;
                    }
                } else {
                    // if it's not socket
                    if (!is_server) {
                        int tmp_fd = 0;
                        unsigned int size_ = sizeof(sockaddr_in);
                        sockaddr_in *ss = (sockaddr_in *)malloc(size_);
                        wamr->invoke_sock_getsockname(tmp_fd, (sockaddr **)&ss, &size_);
                        if (ss->sin_family == AF_INET) {
                            auto *ipv4 = (struct sockaddr_in *)ss;
                            uint32_t ip = ntohl(ipv4->sin_addr.s_addr);
                            wamr->op_data.addr[idx][1].is_4 = true;
                            wamr->op_data.addr[idx][1].ip4[0] = (ip >> 24) & 0xFF;
                            wamr->op_data.addr[idx][1].ip4[1] = (ip >> 16) & 0xFF;
                            wamr->op_data.addr[idx][1].ip4[2] = (ip >> 8) & 0xFF;
                            wamr->op_data.addr[idx][1].ip4[3] = ip & 0xFF;
                            wamr->op_data.addr[idx][1].port = ntohs(ipv4->sin_port);
                        } else {
                            auto *ipv6 = (struct sockaddr_in6 *)ss;
                            wamr->op_data.addr[idx][1].is_4 = false;
                            const auto *bytes = (const uint8_t *)ipv6->sin6_addr.s6_addr;
                            for (int i = 0; i < 16; i += 2) {
                                wamr->op_data.addr[idx][1].ip6[i / 2] = (bytes[i] << 8) + bytes[i + 1];
                            }
                            wamr->op_data.addr[idx][1].port = ntohs(ipv6->sin6_port);
                        }
                        free(ss);
                    } else if (sock_data.is_server) {
                        wamr->op_data.size--;
                    }
                }
            }
            SPDLOG_DEBUG("dest_addr: {}.{}.{}.{}:{}", wamr->op_data.addr[idx][1].ip4[0],
                         wamr->op_data.addr[idx][1].ip4[1], wamr->op_data.addr[idx][1].ip4[2],
                         wamr->op_data.addr[idx][1].ip4[3], wamr->op_data.addr[idx][1].port);
            wamr->op_data.size += 1;
        }
        // Create a socket
        if ((fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
            SPDLOG_ERROR("socket error");
            throw std::runtime_error("socket error");
        }

        addr.sin_family = AF_INET;
        addr.sin_port = htons(MVVM_SOCK_PORT);

        // Convert IPv4 and IPv6 addresses from text to binary form
        if (inet_pton(AF_INET, MVVM_SOCK_ADDR, &addr.sin_addr) <= 0) {
            SPDLOG_ERROR("AF_INET not supported");
            exit(EXIT_FAILURE);
        }
        // Connect to the server
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            SPDLOG_ERROR("Connection Failed {}", errno);
            exit(EXIT_FAILURE);
        }

        SPDLOG_DEBUG("Connected successfully");
        rc = send(fd, &wamr->op_data, sizeof(struct mvvm_op_data), 0);
        if (rc == -1) {
            SPDLOG_ERROR("send error");
            exit(EXIT_FAILURE);
        }

        // Clean up
        close(fd);
    }
#endif
#if WASM_ENABLE_LIB_PTHREAD != 0
    if (wamr->ready < all_count) {
        // Then wait for someone else to get here and finish the job
        std::condition_variable as_cv;
        as_cv.wait(as_ul);
    }
    wasm_cluster_suspend_all_except_self(cluster, instance);
    auto elem = (WASMExecEnv *)bh_list_first_elem(&cluster->exec_env_list);
    while (elem) {
        instance = elem;
#endif // windows has no threads so only does it once
        auto a = new WAMRExecEnv();
        dump(a, instance);
        as.emplace_back(a);
#if WASM_ENABLE_LIB_PTHREAD != 0
        elem = (WASMExecEnv *)bh_list_elem_next(elem);
    }
    // finish filling vector
#endif
    auto end1 = std::chrono::high_resolution_clock::now();
    // get duration in us
    auto dur1 = std::chrono::duration_cast<std::chrono::microseconds>(end1 - start);
    SPDLOG_INFO("Snapshot Overhead: {} s", dur1.count() / 1000000.0);
#if __linux__
    if (dynamic_cast<RDMAWriteStream *>(writer)) {
        auto buffer = struct_pack::serialize(as);
        ((RDMAWriteStream *)writer)->buffer = buffer;
        ((RDMAWriteStream *)writer)->position = buffer.size();
        SPDLOG_DEBUG("Snapshot size: {}\n", buffer.size());
        delete ((RDMAWriteStream *)writer);

    } else
#endif
        struct_pack::serialize_to(*writer, as);

    auto end = std::chrono::high_resolution_clock::now();
    // get duration in us
    auto dur = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    SPDLOG_INFO("Snapshot time: {} s", dur.count() / 1000000.0);
    SPDLOG_INFO("Max Interval: {}", wamr->max_diff);
    SPDLOG_INFO("Memory usage: {} MB", get_rss() / 1024 / 1024);
    exit(EXIT_SUCCESS);
}