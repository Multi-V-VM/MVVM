//
// Created by victoryang00 on 5/6/23.
//

#include "wamr.h"
#include "platform_common.h"
#include "thread_manager.h"
#include "wasm_export.h"
#include "wasm_interp.h"
#include <regex>

WAMRInstance::WAMRInstance(const char *wasm_path, bool is_jit) : is_jit(is_jit) {
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

    if (!wasm_runtime_full_init(&wasm_args)) {
        LOGV(ERROR) << "Init runtime environment failed.\n";
        throw;
    }
    if (!load_wasm_binary(wasm_path)) {
        LOGV(ERROR) << "Load wasm binary failed.\n";
        throw;
    }
    module = wasm_runtime_load((uint8_t *)buffer, buf_size, error_buf, sizeof(error_buf));
    if (!module) {
        LOGV(ERROR) << fmt::format("Load wasm module failed. error: {}", error_buf);
        throw;
    }
}

bool WAMRInstance::load_wasm_binary(const char *wasm_path) {
    buffer = bh_read_file_to_buffer(wasm_path, &buf_size);
    if (!buffer) {
        LOGV(ERROR) << "Open wasm app file failed.\n";
        return false;
    }
    if ((get_package_type((const uint8_t *)buffer, buf_size) != Wasm_Module_Bytecode) &&
        (get_package_type((const uint8_t *)buffer, buf_size) != Wasm_Module_AoT)) {
        LOGV(ERROR) << "WASM bytecode or AOT object is expected but other file format";

        BH_FREE(buffer);
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
int WAMRInstance::invoke_fopen(std::string &path, uint32 option) {
    auto name = "__wasilibc_open_nomode";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
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
    uint32 argv[2] = {fd, to};
    return wasm_runtime_call_wasm(exec_env, func, 2, argv);
};

int WAMRInstance::invoke_sock_open(uint32_t poolfd, int af, int socktype, uint32_t *sockfd) {
    auto name = "__wasi_sock_open";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
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
    void *buffer_ = nullptr;
    uint32_t buffer_for_wasm;

    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, sizeof(uint32), reinterpret_cast<void **>(&buffer_));
    if (buffer_for_wasm != 0) {
        memcpy(buffer_, ((void *)sockfd), sizeof(uint32)); // use native address for accessing in runtime
        uint32 argv[4] = {poolfd, static_cast<uint32>(af), static_cast<uint32>(socktype), buffer_for_wasm};
        auto res = wasm_runtime_call_wasm(exec_env, func, 4, argv);
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
        return res;
    }
    return -1;
}

int WAMRInstance::invoke_sock_sendto(uint32_t sock, const iovec_app_t *si_data, uint32 si_data_len, uint16_t si_flags,
                                     const __wasi_addr_t *dest_addr, uint32 *so_data_len) {
    auto name = "__wasi_sock_send_to";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
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

    void *si_data_ = nullptr;
    uint32_t si_data_for_wasm;
    void *dest_addr_ = nullptr;
    uint32_t dest_addr_for_wasm;
    void *so_data_len_ = nullptr;
    uint32_t so_data_len_for_wasm;
    int res = -1;

    si_data_for_wasm =
        wasm_runtime_module_malloc(module_inst, sizeof(iovec_app_t), reinterpret_cast<void **>(&si_data_));
    if (si_data_for_wasm != 0) {
        memcpy(si_data_, ((void *)si_data), sizeof(iovec_app_t)); // use native address for accessing in runtime

        dest_addr_for_wasm =
            wasm_runtime_module_malloc(module_inst, sizeof(__wasi_addr_t), reinterpret_cast<void **>(&dest_addr_));
        if (dest_addr_for_wasm != 0) {
            memcpy(dest_addr_, ((void *)dest_addr),
                   sizeof(__wasi_addr_t)); // use native address for accessing in runtime

            so_data_len_for_wasm =
                wasm_runtime_module_malloc(module_inst, sizeof(uint32), reinterpret_cast<void **>(&so_data_len_));
            if (so_data_len_for_wasm != 0) {
                memcpy(so_data_len_, ((void *)so_data_len),
                       sizeof(uint32)); // use native address for accessing in runtime
                uint32 argv[6] = {sock,     si_data_for_wasm,   si_data_len,
                                  si_flags, dest_addr_for_wasm, so_data_len_for_wasm};

                wasm_runtime_call_wasm(exec_env, func, 6, argv);
                res = argv[0];
                wasm_runtime_module_free(module_inst, si_data_for_wasm);
            }
            wasm_runtime_module_free(module_inst, dest_addr_for_wasm);
        }
        wasm_runtime_module_free(module_inst, so_data_len_for_wasm);
        return res;
    }
    return -1;
}

int WAMRInstance::invoke_sock_recvfrom(uint32_t sock, iovec_app_t *ri_data, uint32 ri_data_len, uint16_t ri_flags,
                                       __wasi_addr_t *src_addr, uint32 *ro_data_len) {
    auto name = "__wasi_sock_recv_from";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
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

    void *ri_data_ = nullptr;
    uint32_t ri_data_for_wasm;
    void *src_addr_ = nullptr;
    uint32_t src_addr_for_wasm;
    void *ro_data_len_ = nullptr;
    uint32_t ro_data_len_for_wasm;
    int res = -1;

    ri_data_for_wasm =
        wasm_runtime_module_malloc(module_inst, sizeof(iovec_app_t), reinterpret_cast<void **>(&ri_data_));
    if (ri_data_for_wasm != 0) {
        memcpy(ri_data_, ((void *)ri_data), sizeof(iovec_app_t)); // use native address for accessing in runtime

        src_addr_for_wasm =
            wasm_runtime_module_malloc(module_inst, sizeof(__wasi_addr_t), reinterpret_cast<void **>(&src_addr_));
        if (src_addr_for_wasm != 0) {
            memcpy(src_addr_, ((void *)src_addr), sizeof(__wasi_addr_t)); // use native address for accessing in runtime

            ro_data_len_for_wasm =
                wasm_runtime_module_malloc(module_inst, sizeof(uint32), reinterpret_cast<void **>(&ro_data_len_));
            if (ro_data_len_for_wasm != 0) {
                memcpy(ro_data_len_, ((void *)ro_data_len),
                       sizeof(uint32)); // use native address for accessing in runtime
                uint32 argv[6] = {sock,     ri_data_for_wasm,  ri_data_len,
                                  ri_flags, src_addr_for_wasm, ro_data_len_for_wasm};

                wasm_runtime_call_wasm(exec_env, func, 6, argv);
                res = argv[0];
                wasm_runtime_module_free(module_inst, ri_data_for_wasm);
            }
            wasm_runtime_module_free(module_inst, src_addr_for_wasm);
        }
        wasm_runtime_module_free(module_inst, ro_data_len_for_wasm);
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
    uint32 argv[2] = {fd, offset};
    return wasm_runtime_call_wasm(exec_env, func, 2, argv);
};
int WAMRInstance::invoke_preopen(uint32 fd, const std::string &path) {
    auto name = "__wasilibc_register_preopened_fd";
    if (!(func = wasm_runtime_lookup_function(module_inst, name, nullptr))) {
        LOGV(ERROR) << "The wasi fopen function is not found.";
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

    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, 100, reinterpret_cast<void **>(&buffer_));
    if (buffer_for_wasm != 0) {
        uint32 argv[2];
        strncpy(buffer_, path.c_str(), path.size()); // use native address for accessing in runtime
        argv[0] = fd; // pass the buffer_ address for WASM space
        argv[1] = buffer_for_wasm; // the size of buffer_
        wasm_runtime_call_wasm(exec_env, func, 2, argv);
        int res = argv[0];
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
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

WASMModule *WAMRInstance::get_module() {
    return reinterpret_cast<WASMModule *>(reinterpret_cast<WASMModuleInstance *>(exec_env->module_inst)->module);
}
void WAMRInstance::recover(
    std::vector<std::unique_ptr<WAMRExecEnv>> *execEnv) { // will call pthread create wrapper if needed?
    std::sort(execEnv->begin(), execEnv->end(),
              [](const std::unique_ptr<WAMRExecEnv> &a, const std::unique_ptr<WAMRExecEnv> &b) {
                  return a->cur_count > b->cur_count;
              });

    for (auto &&exec_ : *execEnv) {
        if (exec_->cur_count != 0) {
            cur_env = wasm_cluster_spawn_exec_env(exec_env); // look into the pthread create wrapper how it worked.
        }
        this->set_wasi_args(exec_->module_inst.wasi_ctx);
        //  first get the deserializer message, here just hard code
        this->instantiate();
        restore(exec_.get(), cur_env);
        cur_env->is_restore = true;
        if (exec_->cur_count != 0) {
            auto thread_arg =
                ThreadArgs{cur_env, nullptr, nullptr}; // requires to record the args and callback for the pthread.
        }
        get_exec_env()->is_restore = true;
        wasm_interp_call_func_bytecode(get_module_instance(), get_exec_env(), get_exec_env()->cur_frame->function,
                                       get_exec_env()->cur_frame->prev_frame);
    } // every pthread has a semaphore for main thread to set all break point to start.
}
WASMFunction *WAMRInstance::get_func() { return static_cast<WASMFunction *>(func); }
void WAMRInstance::set_func(WASMFunction *f) { func = static_cast<WASMFunction *>(f); }
void WAMRInstance::set_wasi_args(const std::vector<std::string> &dir_list, const std::vector<std::string> &map_dir_list,
                                 const std::vector<std::string> &env_list, const std::vector<std::string> &arg_list,
                                 const std::vector<std::string> &addr_list,
                                 const std::vector<std::string> &ns_lookup_pool) {
    auto string_vec_to_cstr_array = [](const std::vector<std::string> &vecStr) {
        std::vector<const char *> cstrArray(vecStr.size());
        if (vecStr.data() == nullptr || vecStr[0].empty())
            return std::vector<const char *>(0);
        LOGV(DEBUG) << "vecStr[0]:" << vecStr[0];
        std::transform(vecStr.begin(), vecStr.end(), cstrArray.begin(),
                       [](const std::string &str) { return str.c_str(); });
        return cstrArray;
    };

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
void restart_execution(uint32 id) {}
void WAMRInstance::set_wasi_args(WAMRWASIContext &context) {
    // some handmade directory after recovery dir
    // std::vector<int> to_close;
    // for (auto [fd, stat] : context.fd_map) {
    //     while (true) {
    //         // if the time is not socket
    //         int fd_;
    //         if (stat.second == 0) {
    //             auto dir_name = opendir(stat.first.c_str());
    //             fd_ = dirfd(dir_name);
    //         } else {
    //             fd_ = open(stat.first.c_str(), O_RDWR);
    //         }
    //         if (fd > fd_) {
    //             //                close(fd_);
    //             to_close.emplace_back(fd_);
    //             continue;
    //         }
    //         if (fd < fd_) {
    //             throw std::runtime_error("restore fd overflow");
    //         }
    //         // remain socket.
    //         break;
    //     }
    // }
    // for (auto fd : to_close) {
    //     close(fd);
    // }
    /** refer to wasm bpf call function */

    //    auto get_dir_from_context = [](const WAMRWASIContext &wasiContext, bool is_map_dir = false) {
    //        auto cstrArray = std::vector<std::string>(wasiContext.prestats.size);
    //        std::regex dir_replacer(".+?/");
    //        std::transform(wasiContext.prestats.prestats.begin(), wasiContext.prestats.prestats.end(),
    //        cstrArray.begin(),
    //                       [&is_map_dir, &dir_replacer](const WAMRFDPrestat &str) {
    //                           return is_map_dir ? std::regex_replace(str.dir, dir_replacer, "") : str.dir;
    //                       });
    //        return cstrArray;
    //    };
    auto get_addr_from_context = [](const WAMRWASIContext &wasiContext) {
        auto addr_pool = std::vector<std::string>(wasiContext.addr_pool.size());
        std::transform(wasiContext.addr_pool.begin(), wasiContext.addr_pool.end(), addr_pool.begin(),
                       [](const WAMRAddrPool &addrs) {
                           std::string addr_str;
                           if (addrs.is_4) {
                               addr_str = fmt::format("{}.{}.{}.{}/{}", addrs.ip4[0], addrs.ip4[1], addrs.ip4[2],
                                                      addrs.ip4[3], addrs.mask);
                               if (addrs.mask != UINT8_MAX) {
                                   addr_str += fmt::format("/{}", addrs.mask);
                               }
                           } else {
                               addr_str =

                                   fmt::format("{:#}:{:#}:{:#}:{:#}:{:#}:{:#}:{:#}:{:#}", addrs.ip6[0], addrs.ip6[1],
                                               addrs.ip6[2], addrs.ip6[3], addrs.ip6[4], addrs.ip6[5], addrs.ip6[6],
                                               addrs.ip6[7], addrs.mask);
                               if (addrs.mask != UINT8_MAX) {
                                   addr_str += fmt::format("/{}", addrs.mask);
                               }
                           }
                           return addr_str;
                       });

        return addr_pool;
    };
    set_wasi_args(context.dir, context.map_dir, context.argv_environ.env_list, context.argv_environ.argv_list,
                  get_addr_from_context(context), context.ns_lookup_list);
}
void WAMRInstance::instantiate() {
    module_inst = wasm_runtime_instantiate(module, stack_size, heap_size, error_buf, sizeof(error_buf));
    if (!module_inst) {
        LOGV(ERROR) << fmt::format("Instantiate wasm module failed. error: {}", error_buf);
        throw;
    }
    cur_env = exec_env = wasm_runtime_create_exec_env(module_inst, stack_size);
}