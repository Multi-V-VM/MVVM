//
// Created by victoryang00 on 5/6/23.
//

#ifndef MVVM_WAMR_H
#define MVVM_WAMR_H

#include "aot_runtime.h"
#include "bh_read_file.h"
#include "logging.h"
#include "wamr_exec_env.h"
#include "wamr_export.h"
#include "wamr_read_write.h"
#include "wasm_runtime.h"
#include <ranges>

enum ArchType { x86_64, arm64 };

struct MVVMAotMetadata {
    std::vector<std::size_t> nops;
};

class WAMRInstance {
    WASMExecEnv *exec_env{};
    WASMExecEnv *cur_env{};
    WASMModuleInstanceCommon *module_inst{};
    WASMModuleCommon *module;
    WASMFunctionInstanceCommon *func{};

public:
    std::string aot_file_path;
    std::string wasm_file_path;
    std::map<ArchType, MVVMAotMetadata> mvvm_aot_metadatas;
    std::vector<const char *> dir_;
    std::vector<const char *> map_dir_;
    std::vector<const char *> env_;
    std::vector<const char *> arg_;
    std::vector<const char *> addr_;
    std::vector<const char *> ns_pool_;
    std::map<int, std::pair<std::string, int>> fd_map_;
    bool is_jit;
    bool is_aot{};
    char error_buf[128]{};
    uint32 buf_size{}, stack_size = 8092, heap_size = 8092;
    typedef struct ThreadArgs {
        wasm_exec_env_t exec_env;
        wasm_thread_callback_t callback;
        void *arg;
    } ThreadArgs;
    std::vector<ThreadArgs> thread_arg;
    std::vector<wasm_thread_t> tid;
    void (*thread_callback)(wasm_exec_env_t, void *){};

    explicit WAMRInstance(const char *wasm_path, bool is_jit);

    void instantiate();
    void recover(std::vector<std::unique_ptr<WAMRExecEnv>> *execEnv);
    bool load_wasm_binary(const char *wasm_path, char** buffer_ptr);
    bool load_mvvm_aot_metadata(const char *file_path);
    WASMFunction *get_func();
    void set_func(WASMFunction *);
#if WASM_ENABLE_AOT != 0
    std::vector<uint32> get_args();
    AOTFunctionInstance *get_func(int index);
#endif
    WASMExecEnv *get_exec_env();
    WASMModuleInstance *get_module_instance();
    AOTModule *get_module();
    void set_wasi_args(WAMRWASIContext &addrs);
    void set_wasi_args(const std::vector<std::string> &dir_list, const std::vector<std::string> &map_dir_list,
                       const std::vector<std::string> &env_list, const std::vector<std::string> &arg_list,
                       const std::vector<std::string> &addr_list, const std::vector<std::string> &ns_lookup_pool);

    int invoke_main();
    int invoke_open(uint32 fd, const std::string &path, uint32 option);
    int invoke_preopen(uint32 fd, const std::string &path);
    ~WAMRInstance();
};
#endif // MVVM_WAMR_H
