//
// Created by victoryang00 on 5/6/23.
//

#include "wamr.h"
#include "thread_manager.h"
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
int WAMRInstance::invoke_open(uint32 fd, const std::string& path, uint32 option) {
    if (!(func = wasm_runtime_lookup_function(module_inst, "open", "($i)i"))) {
        LOGV(ERROR) << "The wasi open function is not found.";
        return -1;
    }
    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;

    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, 100, reinterpret_cast<void **>(&buffer_));
    if (buffer_for_wasm != 0) {
        uint32 argv[2];
        strncpy(buffer_, path.c_str(), path.size()); // use native address for accessing in runtime
        argv[0] = buffer_for_wasm; // pass the buffer_ address for WASM space
        argv[1] = option; // the size of buffer_
        auto res = wasm_runtime_call_wasm(exec_env, func, 2, argv);
        wasm_runtime_module_free(module_inst, buffer_for_wasm);
        return res;
    }
    return -1;
};
int WAMRInstance::invoke_preopen(uint32 fd, const std::string& path) {
    if (!(func = wasm_runtime_lookup_function(module_inst, "__wasilibc_register_preopened_fd", "(i$)i"))) {
        LOGV(ERROR) << "The __wasilibc_register_preopened_fd function is not found.";
        return -1;
    }
    char *buffer_ = nullptr;
    uint32_t buffer_for_wasm;

    buffer_for_wasm = wasm_runtime_module_malloc(module_inst, 100, reinterpret_cast<void **>(&buffer_));
    if (buffer_for_wasm != 0) {
        uint32 argv[2];
        strncpy(buffer_, path.c_str(), path.size()); // use native address for accessing in runtime
        argv[0] = fd; // pass the buffer_ address for WASM space
        argv[1] = buffer_for_wasm; // the size of buffer_
        auto res = wasm_runtime_call_wasm(exec_env, func, 2, argv);
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