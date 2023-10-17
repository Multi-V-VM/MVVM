//
// Created by victoryang00 on 5/6/23.
//

#ifndef MVVM_WAMR_H
#define MVVM_WAMR_H

#include "bh_read_file.h"
#include "logging.h"
#include "wamr_exec_env.h"
#include "wamr_export.h"
#include "wamr_read_write.h"
#include "wasm_runtime.h"


struct WasiSockOpenData {
    uint32_t poolfd;
    int af;
    int socktype;
    uint32_t* sockfd;
};

struct WasiSockSendToData {
    uint32_t sock;
    const iovec_app1_t* si_data;
    uint32 si_data_len;
    uint16_t si_flags;
    const __wasi_addr_t* dest_addr;
    uint32* so_data_len;
};

struct WasiSockRecvFromData {
    uint32_t sock;
    iovec_app1_t* ri_data;
    uint32 ri_data_len;
    uint16_t ri_flags;
    __wasi_addr_t* src_addr;
    uint32* ro_data_len;
};

class WAMRInstance {
    WASMExecEnv *exec_env{};
    WASMExecEnv *cur_env{};
    WASMModuleInstanceCommon *module_inst{};
    WASMModuleCommon *module;
    WASMFunctionInstanceCommon *func{};

public:
    std::vector<const char *> dir_;
    std::vector<const char *> map_dir_;
    std::vector<const char *> env_;
    std::vector<const char *> arg_;
    std::vector<const char *> addr_;
    std::vector<const char *> ns_pool_;
    std::map<int, std::tuple<std::string, int, int>> fd_map_;
    // add offset to pair->tuple, 3rd param 'int'

    WasiSockOpenData sock_open_data;
    WasiSockSendToData sock_sendto_data;
    WasiSockRecvFromData sock_recvfrom_data;

    bool is_jit;
    char *buffer{};
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
    bool load_wasm_binary(const char *wasm_path);
    WASMFunction *get_func();
    void set_func(WASMFunction *);
    WASMExecEnv *get_exec_env();
    WASMModuleInstance *get_module_instance();
    WASMModule *get_module();
    void set_wasi_args(WAMRWASIContext &addrs);
    void set_wasi_args(const std::vector<std::string> &dir_list, const std::vector<std::string> &map_dir_list,
                       const std::vector<std::string> &env_list, const std::vector<std::string> &arg_list,
                       const std::vector<std::string> &addr_list, const std::vector<std::string> &ns_lookup_pool);

    int invoke_main();
    int invoke_fopen(std::string &path, uint32 option);
    int invoke_frenumber(uint32 fd, uint32 to);
    int invoke_fseek(uint32 fd, uint32 offset);
    int invoke_preopen(uint32 fd, const std::string &path);
    int invoke_socket(uint32 domain, const std::string &path);
    int invoke_(uint32 domain, const std::string &path);
    int invoke_sock_sendto(uint32_t sock, const iovec_app1_t* si_data, uint32 si_data_len, uint16_t si_flags, const __wasi_addr_t* dest_addr, uint32* so_data_len);
    int invoke_sock_recvfrom(uint32_t sock, iovec_app1_t* ri_data, uint32 ri_data_len, uint16_t ri_flags, __wasi_addr_t* src_addr, uint32* ro_data_len);
    int invoke_sock_open(uint32_t poolfd, int af, int socktype, uint32_t* sockfd);
    ~WAMRInstance();
};
#endif // MVVM_WAMR_H
