//
// Created by victoryang00 on 6/17/23.
//

#ifdef __cplusplus
extern "C" {
#endif
#ifdef MVVM_WASI
#include "wasm_runtime.h"
typedef struct iovec_app1 {
    uint32 buf_offset;
    uint32 buf_len;
} iovec_app1_t;

void serialize_to_file(struct WASMExecEnv *);
#endif
void insert_sock_send_to_data(uint32_t sock, const iovec_app1_t* si_data, uint32 si_data_len, uint16_t si_flags, const __wasi_addr_t* dest_addr, uint32* so_data_len);
void insert_sock_open_data(uint32_t, int, int, uint32_t *);
void insert_sock_recv_from_data(uint32_t sock, iovec_app1_t* ri_data, uint32 ri_data_len, uint16_t ri_flags, __wasi_addr_t* src_addr, uint32* ro_data_len);
void insert_fd(int, char const *, int, int);
void remove_fd(int);
void insert_socket(char const *, int);
void remove_socket(char const *); // see whether there's socket maintainance impl in wasi?
void insert_lock(char const *, int);
void insert_sem(char const *, int);
void remove_lock(char const *);
void remove_sem(char const *);
void restart_execution(uint32 targs);
#ifdef __cplusplus
}
#endif