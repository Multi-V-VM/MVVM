//
// Created by victoryang00 on 6/17/23.
//
#include "wasm_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(MVVM_WASI)
#define MVVM_WASI
#define SNAPSHOT_DEBUG_STEP 0
#define SNAPSHOT_STEP 1e8
struct SocketAddrPool {
    uint8 ip4[4];
    uint16 ip6[8];
    bool is_4;
    uint16 port;
};
enum fd_op { MVVM_FOPEN = 0, MVVM_FWRITE = 1, MVVM_FREAD = 2, MVVM_FSEEK = 3 };

enum sync_op {
    SYNC_OP_MUTEX_LOCK,
    SYNC_OP_MUTEX_UNLOCK,
};

struct sync_op_t {
    uint32 tid;
    uint32 ref;
    enum sync_op sync_op;
};

#if !defined(_WIN32)
void insert_sock_send_to_data(uint32_t, uint8 *, uint32, uint16_t, __wasi_addr_t *);
void insert_sock_open_data(uint32_t, int, int, uint32_t);
void insert_sock_recv_from_data(uint32_t, uint8 *, uint32, uint16_t, __wasi_addr_t *);
void replay_sock_recv_from_data(uint32_t, uint8 **, unsigned long *, __wasi_addr_t *);
void insert_socket(int, int, int, int);
void update_socket_fd_address(int, struct SocketAddrPool *);
void init_gateway(struct SocketAddrPool *address);
void set_tcp();
int get_sock_fd(int);
void insert_sync_op(wasm_exec_env_t exec_env, uint32 *mutex, enum sync_op locking);
void restart_execution(uint32 targs);
extern int pthread_create_wrapper(wasm_exec_env_t exec_env, uint32 *thread, const void *attr, uint32 elem_index,
                                  uint32 arg);
#endif
void serialize_to_file(struct WASMExecEnv *);
#endif

void insert_fd(int, const char *, int, int, enum fd_op op);
void remove_fd(int);
void rename_fd(int, char const *, int, char const *);
void lightweight_checkpoint(WASMExecEnv *);
void lightweight_uncheckpoint(WASMExecEnv *);
void wamr_wait(wasm_exec_env_t);
void sigint_handler(int sig);
void register_sigtrap();
void sigtrap_handler(int sig);
#if defined(__APPLE__) || defined(_WIN32)
int gettid();
#endif
extern size_t snapshot_threshold;
extern int stop_func_threshold;
extern bool checkpoint;
extern bool is_debug;
extern int stop_func_index;
extern int cur_func_count;

#ifdef __cplusplus
}
#endif
