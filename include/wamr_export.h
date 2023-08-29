//
// Created by victoryang00 on 6/17/23.
//


#ifdef __cplusplus
extern "C" {
#endif
#ifdef MVVM_WASI
#include "wasm_runtime.h"
void serialize_to_file(struct WASMExecEnv *);
#endif


void insert_fd(int, char const *, int, int);
void remove_fd(int);
void insert_socket(int, int, int, int);
void update_socket_fd_address(int, struct SocketAddrPool *);
void remove_socket(char const *); // see whether there's socket maintainance impl in wasi?
void insert_lock(char const *, int);
void insert_sem(char const *, int);
void remove_lock(char const *);
void remove_sem(char const *);
void restart_execution(uint32 targs);
#ifdef __cplusplus
}
#endif