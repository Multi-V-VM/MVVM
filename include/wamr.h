//
// Created by victoryang00 on 5/6/23.
//

#ifndef MVVM_WAMR_H
#define MVVM_WAMR_H

#if WASM_ENABLE_AOT != 0
#include "aot_runtime.h"
#endif
#include "bh_read_file.h"
#include "logging.h"
#include "wamr_exec_env.h"
#include "wamr_export.h"
#include "wamr_read_write.h"
#include "wamr_wasi_context.h"
#include "wasm_runtime.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iterator>
#include <mutex>
#include <numeric>
#include <ranges>
#include <semaphore>
#include <tuple>

class WAMRInstance {
public:
    WASMExecEnv *cur_env{};
    WASMExecEnv *exec_env{};
    WASMModuleInstanceCommon *module_inst{};
    WASMModuleCommon *module;
    WASMFunctionInstanceCommon *func{};

    std::string aot_file_path{};
    std::string wasm_file_path{};
    std::vector<std::size_t> int3_addr{};
    std::vector<std::pair<std::size_t, std::size_t>> switch_addr{};
    std::vector<const char *> dir_{};
    std::vector<const char *> map_dir_{};
    std::vector<const char *> env_{};
    std::vector<const char *> arg_{};
    std::vector<const char *> addr_{};
    std::vector<const char *> ns_pool_{};
    std::vector<WAMRExecEnv *> execEnv{};
    std::map<int, std::tuple<std::string, std::vector<std::tuple<int, int, fd_op>>>> fd_map_{};
    // add offset to pair->tuple, 3rd param 'int'
    std::map<int, int> new_sock_map_{};
    std::map<int, SocketMetaData, std::greater<>> socket_fd_map_{};
    SocketAddrPool local_addr{};
    // lwcp is LightWeight CheckPoint
    std::map<uint64, int> lwcp_list;
    size_t ready = 0;
    std::mutex as_mtx{};
    std::vector<struct sync_op_t> sync_ops;
    bool should_snapshot{};
    WASMMemoryInstance **tmp_buf;
    uint32 tmp_buf_size{};
    std::vector<struct sync_op_t>::iterator sync_iter;
    std::mutex sync_op_mutex;
    std::condition_variable sync_op_cv;
    std::map<uint64, uint64> tid_map;
    std::map<uint64, uint64> child_tid_map;
    std::map<uint64, std::pair<int, int>> tid_start_arg_map;
    uint32 id{};
    size_t cur_thread;
    std::chrono::time_point<std::chrono::high_resolution_clock> time;
    std::vector<long long> latencies;
    bool is_jit{};
    bool is_aot{};
    char error_buf[128]{};
    struct mvvm_op_data op_data {};
    uint32 buf_size{}, stack_size = 65536, heap_size = 3355443200;
    typedef struct ThreadArgs {
        wasm_exec_env_t exec_env;
    } ThreadArgs;

    explicit WAMRInstance(const char *wasm_path, bool is_jit);

    void instantiate();
    void recover(std::vector<std::unique_ptr<WAMRExecEnv>> *);
    bool load_wasm_binary(const char *wasm_path, char **buffer_ptr);
    bool get_int3_addr();
    bool replace_int3_with_nop();
    bool replace_mfence_with_nop();
    bool replace_nop_with_int3();
    void replay_sync_ops(bool, wasm_exec_env_t);
    WASMFunction *get_func();
    void set_func(WASMFunction *);
#if WASM_ENABLE_AOT != 0
    std::vector<uint32> get_args();
    AOTFunctionInstance *get_func(int index);
    [[nodiscard]] AOTModule *get_module() const;
#endif
    WASMExecEnv *get_exec_env();
    [[nodiscard]] WASMModuleInstance *get_module_instance() const;

    void set_wasi_args(WAMRWASIContext &addrs);
    void set_wasi_args(const std::vector<std::string> &dir_list, const std::vector<std::string> &map_dir_list,
                       const std::vector<std::string> &env_list, const std::vector<std::string> &arg_list,
                       const std::vector<std::string> &addr_list, const std::vector<std::string> &ns_lookup_pool);
    void spawn_child(WASMExecEnv *main_env, bool);

    int invoke_main();
    void invoke_init_c();
    int invoke_fopen(std::string &path, uint32 option);
    int invoke_frenumber(uint32 fd, uint32 to);
    int invoke_fseek(uint32 fd, uint32 offset);
    int invoke_ftell(uint32 fd, uint32 offset, uint32 whench);
    int invoke_preopen(uint32 fd, const std::string &path);
    int invoke_sock_open(uint32_t domain, uint32_t socktype, uint32_t protocol, uint32_t sockfd);
    int invoke_sock_connect(uint32_t sockfd, struct sockaddr *sock, socklen_t sock_size);
    int invoke_sock_accept(uint32_t sockfd, struct sockaddr *sock, socklen_t sock_size);
    int invoke_sock_getsockname(uint32_t sockfd, struct sockaddr **sock, socklen_t *sock_size);
    int invoke_recv(int sockfd, uint8 **buf, size_t len, int flags);
    int invoke_recvfrom(int sockfd, uint8 **buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);
    ~WAMRInstance();
};
#endif // MVVM_WAMR_H
