//
// Created by victoryang00 on 5/6/23.
//

#include "wamr.h"
#include "platform_common.h"
#include "wamr_export.h"
#include "wamr_native.h"
#include "wasm_export.h"
#include "wasm_interp.h"
#include "wasm_runtime.h"
#include <condition_variable>
#include <mutex>
#include <regex>
#include <semaphore>
#if !defined(_WIN32)
#include "thread_manager.h"
#include <unistd.h>
#endif

WAMRInstance::ThreadArgs **argptr;
std::counting_semaphore<100> wakeup(0);
std::counting_semaphore<100> thread_init(0);

static auto string_vec_to_cstr_array = [](const std::vector<std::string> &vecStr) {
    std::vector<const char *> cstrArray(vecStr.size());
    if (vecStr.data() == nullptr || vecStr[0].empty())
        return std::vector<const char *>(0);
    LOGV(DEBUG) << "vecStr[0]:" << vecStr[0];
    std::transform(vecStr.begin(), vecStr.end(), cstrArray.begin(), [](const std::string &str) { return str.c_str(); });
    return cstrArray;
};

WAMRInstance::WAMRInstance(const char *wasm_path, bool is_jit) : is_jit(is_jit) {
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
        LOGV(ERROR) << "Init runtime environment failed.\n";
        throw;
    }
    // Not working initialiseWAMRNatives();
    char *buffer{};
    if (!load_wasm_binary(wasm_path, &buffer)) {
        LOGV(ERROR) << "Load wasm binary failed.\n";
        throw;
    }
    module = wasm_runtime_load((uint8_t *)buffer, buf_size, error_buf, sizeof(error_buf));
    if (!module) {
        LOGV(ERROR) << fmt::format("Load wasm module failed. error: {}", error_buf);
        throw;
    }
#if !defined(_WIN32)
    struct ifaddrs *ifaddr, *ifa;
    int family, s;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        LOGV(ERROR) << "getifaddrs";
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
        LOGV(ERROR) << "Open wasm app file failed.\n";
        return false;
    }
    if ((get_package_type((const uint8_t *)*buffer_ptr, buf_size) != Wasm_Module_Bytecode) &&
        (get_package_type((const uint8_t *)*buffer_ptr, buf_size) != Wasm_Module_AoT)) {
        LOGV(ERROR) << "WASM bytecode or AOT object is expected but other file format";

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

int WAMRInstance::invoke_main() {
    if (!(func = wasm_runtime_lookup_wasi_start_function(module_inst))) {
        LOGV(ERROR) << "The wasi mode main function is not found.";
        return -1;
    }

    return wasm_runtime_call_wasm(exec_env, func, 0, nullptr);
}
void WAMRInstance::invoke_init_c() {
    auto name = "__wasm_init_memory";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi " << name << " function is not found.";
    } else {
        wasm_runtime_call_wasm(exec_env, func, 0, nullptr);
    }
    auto name1 = "__wasm_call_ctors";
    if (!(func = wasm_runtime_lookup_function(module_inst, name1, nullptr))) {
        LOGV(ERROR) << "The wasi " << name1 << " function is not found.";
    } else {
        wasm_runtime_call_wasm(exec_env, func, 0, nullptr);
    }
    return;
}
int WAMRInstance::invoke_fopen(std::string &path, uint32 option) {
    auto name = "o_";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi " << name << " function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;

    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, path.size(), (void **)&buffer_);
    if (buffer_for_wasm != 0) {
        uint32 argv[2];
        argv[0] = buffer_for_wasm; // pass the buffer_ address for WASM space
        argv[1] = option; // the size of buffer_
        strncpy(buffer_, path.c_str(), path.size()); // use native address for accessing in runtime
        wasm_runtime_call_wasm(exec_env, func, 2, argv);
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
        return ((int)argv[0]);
    }
    // auto name1 = "o_";
    // uint32 argv[0];
    // if (!(func = wasm_runtime_lookup_function(module_inst, name1, nullptr))) {
    //     LOGV(ERROR) << "The wasi " << name1 << " function is not found.";
    // }
    // wasm_runtime_call_wasm(exec_env, func, 0, argv);
    return -1;
};
int WAMRInstance::invoke_frenumber(uint32 fd, uint32 to) {
    auto name = "__wasi_fd_renumber";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name << " " << i;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name << " " << i;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    uint32 argv[2] = {fd, to};
    wasm_runtime_call_wasm(exec_env, func, 2, argv);
    return argv[0];
};

int WAMRInstance::invoke_sock_open(uint32_t domain, uint32_t socktype, uint32_t protocol, uint32_t sockfd) {
    auto name = "s_";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi " << name << " function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    uint32 argv[4] = {domain, socktype, protocol, sockfd};
    auto res = wasm_runtime_call_wasm(exec_env, func, 4, argv);
    return argv[0];
}
int WAMRInstance::invoke_sock_connect(uint32_t sockfd, struct sockaddr *sock, socklen_t sock_size) {
    auto name = "init_connect";

    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi " << name << " function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }

    uint32 argv[1];
    argv[0] = sockfd;
    wasm_runtime_call_wasm(exec_env, func, 1, argv);
    int res = argv[0];
    return -1;
}
int WAMRInstance::invoke_sock_accept(uint32_t sockfd, struct sockaddr *sock, socklen_t sock_size) {
    auto name = "accept";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi\"" << name << "\"function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name << " " << i;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name << " " << i;
                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    char *buffer1_ = nullptr;
    char *buffer2_ = nullptr;
    uint32_t buffer1_for_wasm;
    uint32_t buffer2_for_wasm;

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
    auto name = "getsockname";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi " << name << " function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }

    char *buffer1_ = nullptr;
    uint32_t buffer1_for_wasm;
    char *buffer2_ = nullptr;
    uint32_t buffer2_for_wasm;

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
int WAMRInstance::invoke_fseek(uint32 fd, uint32 offset) {
    auto name = "__wasi_fd_seek";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name << " " << i;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name << " " << i;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    uint32 argv[2] = {fd, offset};
    return wasm_runtime_call_wasm(exec_env, func, 2, argv);
};
int WAMRInstance::invoke_ftell(uint32 fd, uint32 offset, uint32 whench) {
    auto name = "__wasi_fd_tell";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name << " " << i;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name << " " << i;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    uint32 argv[2] = {fd, offset};
    return wasm_runtime_call_wasm(exec_env, func, 2, argv);
};
int WAMRInstance::invoke_preopen(uint32 fd, const std::string &path) {
    auto name = "__wasilibc_nocwd_openat_nomode";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi\"" << name << "\"function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name << " " << i;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name << " " << i;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;

    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, 100, reinterpret_cast<void **>(&buffer_));
    if (buffer_for_wasm != 0) {
        uint32 argv[3];
        strncpy(buffer_, path.c_str(), path.size()); // use native address for accessing in runtime
        argv[0] = fd; // pass the buffer_ address for WASM space
        argv[1] = buffer_for_wasm; // the size of buffer_
        argv[2] = 2; // O_RW | O_CREATE
        wasm_runtime_call_wasm(exec_env, func, 3, argv);
        int res = argv[0];
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
        return res;
    }
    return -1;
};
int WAMRInstance::invoke_recv(int sockfd, uint8 **buf, size_t len, int flags) {
    auto name = "recv";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi\"" << name << "\"function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name << " " << i;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {

                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name << " " << i;

                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;

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
};
int WAMRInstance::invoke_recvfrom(int sockfd, uint8 **buf, size_t len, int flags, struct sockaddr *src_addr,
                                  socklen_t *addrlen) {
    auto name = "recvfrom";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi\"" << name << "\"function is not found.";
        auto target_module = get_module_instance()->e;
        for (int i = 0; i < target_module->function_count; i++) {
            auto cur_func = &target_module->functions[i];
            if (cur_func->is_import_func) {
                LOGV(DEBUG) << cur_func->u.func_import->field_name << " " << i;
                if (!strcmp(cur_func->u.func_import->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }

            } else {
                LOGV(DEBUG) << cur_func->u.func->field_name << " " << i;
                if (!strcmp(cur_func->u.func->field_name, name)) {
                    func = ((WASMFunctionInstanceCommon *)cur_func);
                    break;
                }
            }
        }
    }
    char *buffer1_ = nullptr;
    char *buffer2_ = nullptr;
    char *buffer3_ = nullptr;
    uint32_t buffer1_for_wasm;
    uint32_t buffer2_for_wasm;
    uint32_t buffer3_for_wasm;

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
};
WASMExecEnv *WAMRInstance::get_exec_env() {
    return cur_env; // should return the current thread's
}

WASMModuleInstance *WAMRInstance::get_module_instance() {
    return reinterpret_cast<WASMModuleInstance *>(exec_env->module_inst);
}

#if WASM_ENABLE_AOT != 0
AOTModule *WAMRInstance::get_module() {
    return reinterpret_cast<AOTModule *>(reinterpret_cast<WASMModuleInstance *>(exec_env->module_inst)->module);
}
#endif

void restart_execution(uint32 id) {
    WAMRInstance::ThreadArgs *targs = argptr[id];
    wasm_interp_call_func_bytecode((WASMModuleInstance *)targs->exec_env->module_inst, targs->exec_env,
                                   targs->exec_env->cur_frame->function, targs->exec_env->cur_frame->prev_frame);
}

void WAMRInstance::register_tid_map() { tid_map[cur_thread] = gettid(); }

extern "C" int32 pthread_mutex_lock_wrapper(wasm_exec_env_t, uint32 *);
extern "C" int32 pthread_mutex_unlock_wrapper(wasm_exec_env_t, uint32 *);
extern "C" int32 pthread_mutex_init_wrapper(wasm_exec_env_t, uint32 *, void *);
void WAMRInstance::replay_sync_ops(bool main, wasm_exec_env_t exec_env) {
    if (main) {
        for (auto &i : sync_ops) {
            // remap to new tids so that if we reserialize it'll be correct
            (i).tid = tid_map[(i).tid];
        }
        // start from the beginning
        sync_iter = sync_ops.begin();
        thread_init.release(100);
    } else {
        // wait for remap to finish
        thread_init.acquire();
    }
    size_t mytid = gettid();
    // Actually replay
    while (sync_iter != sync_ops.end()) {
        std::unique_lock l(sync_op_mutex);
        if ((*sync_iter).tid == mytid) {

            // do op
            printf("replay %ld, op %d\n", sync_iter->tid, sync_iter->sync_op);
            switch (sync_iter->sync_op) {
            case SYNC_OP_MUTEX_LOCK:
                pthread_mutex_lock_wrapper(exec_env, &(sync_iter->ref));
                break;
            case SYNC_OP_MUTEX_UNLOCK:
                pthread_mutex_unlock_wrapper(exec_env, &(sync_iter->ref));
                break;
            }
            ++sync_iter;
            // wakeup everyone
            sync_op_cv.notify_all();
        } else {
            // wait for actual recipient
            sync_op_cv.wait(l);
        }
    }
}
// End Sync Op Specific Stuff

WAMRExecEnv *child_env;
// will call pthread create wrapper if needed?
void WAMRInstance::recover(std::vector<std::unique_ptr<WAMRExecEnv>> *execEnv) {
    // order threads by id (descending)
    std::sort(execEnv->begin(), execEnv->end(),
              [](const std::unique_ptr<WAMRExecEnv> &a, const std::unique_ptr<WAMRExecEnv> &b) {
                  return a->frames.back()->function_index > b->frames.back()->function_index;
                  ;
              });

    argptr = (ThreadArgs **)malloc(sizeof(void *) * execEnv->size());
    uint32 id = 0;
    set_wasi_args(execEnv->back()->module_inst.wasi_ctx);

    instantiate();
    auto mi = module_inst;

    get_int3_addr();
    replace_int3_with_nop();

    restore(execEnv->back().get(), cur_env);
    auto main_env = cur_env;
    auto main_saved_call_chain = main_env->restore_call_chain;
    cur_thread = main_env->cur_count;
    register_tid_map();

    fprintf(stderr, "main_env created %p %p\n\n", main_env, main_saved_call_chain);

    main_env->is_restore = true;

    main_env->restore_call_chain = nullptr;

    invoke_init_c();
    // invoke_preopen(1, "/dev/stdout");

#if !defined(_WIN32)
    for (auto [idx, exec_] : *execEnv | enumerate) {
        if (idx + 1 == execEnv->size()) {
            // the last one should be the main thread
            break;
        }
        child_env = exec_.get();

        // requires to record the args and callback for the pthread.
        auto thread_arg = ThreadArgs{main_env};

        argptr[id] = &thread_arg;

        // restart thread execution
        fprintf(stderr, "pthread_create_wrapper, func %d\n", id);
        /*    module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
        exec_env->is_restore = false;
        auto s = exec_env->restore_call_chain;
        exec_env->restore_call_chain = NULL;
            invoke_init_c();
            invoke_preopen(1, "/dev/stdout");
        exec_env->is_restore = true;
        exec_env->restore_call_chain =s; // */
        pthread_create_wrapper(main_env, nullptr, nullptr, id, id);

        thread_init.acquire();
        id++;
    }
    module_inst = mi;
    fprintf(stderr, "child spawned %p\n\n", main_env);
    // restart main thread execution
#endif
    if (!is_aot) {
        wasm_interp_call_func_bytecode(get_module_instance(), get_exec_env(), get_exec_env()->cur_frame->function,
                                       get_exec_env()->cur_frame->prev_frame);
    } else {
        exec_env = cur_env = main_env;
        module_inst = main_env->module_inst;

        fprintf(stderr, "invoke_init_c\n");
        // invoke_init_c();
        //  invoke_preopen(1, "/dev/stdout");
        fprintf(stderr, "wakeup.release\n");
        wakeup.release(100);

        cur_env->is_restore = true;
        cur_env->restore_call_chain = main_saved_call_chain;

        fprintf(stderr, "invoke main %p %p\n", cur_env, cur_env->restore_call_chain);
        // replay sync ops to get OS state matching
        replay_sync_ops(true, main_env);
        invoke_main();
    }
}

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
    fprintf(stderr, "child getting ready to wait\n");
    // register thread id mapping
    wamr->register_tid_map();
    thread_init.release(1);
    fprintf(stderr, "finish child restore\n");
    wakeup.acquire();
    fprintf(stderr, "go child!! %d\n", gettid());
    wamr->replay_sync_ops(false, exec_env);
    fprintf(stderr, "finish syncing\n");
}
WASMExecEnv *restore_env() {
    auto exec_env = wasm_exec_env_create_internal(wamr->module_inst, wamr->stack_size);
    restore(child_env, exec_env);

    auto s = exec_env->restore_call_chain;
    /*
    exec_env->is_restore = false;
    exec_env->restore_call_chain = NULL;

    auto name = "__wasm_init_memory";
    auto func = wasm_runtime_lookup_function(wamr->module_inst, name, nullptr);
    wasm_runtime_call_wasm(exec_env, func, 0, nullptr);
    auto name1 = "__wasm_call_ctors";
    func = wasm_runtime_lookup_function(wamr->module_inst, name1, nullptr);
    wasm_runtime_call_wasm(exec_env, func, 0, nullptr);

    exec_env->restore_call_chain = s;
// */
    wamr->cur_thread = exec_env->cur_count;
    exec_env->is_restore = true;
    fprintf(stderr, "restore_env: %p %p\n", exec_env, s);

    return exec_env;
}
}

void WAMRInstance::instantiate() {
    module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
    if (!module_inst) {
        LOGV(ERROR) << fmt::format("Instantiate wasm module failed. error: {}", error_buf);
        throw;
    }
    cur_env = exec_env = wasm_runtime_create_exec_env(module_inst, stack_size);
}
