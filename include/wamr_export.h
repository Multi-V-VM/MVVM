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
void insert_socket(char const *, int);
void remove_socket(char const *); // see whether there's socket maintainance impl in wasi?
void insert_lock(char const *, int);
void insert_sem(char const *, int);
void remove_lock(char const *);
void remove_sem(char const *);
void wasm_thread_restart();

void restart_execution(uint32 targs);
extern int pthread_create_wrapper(wasm_exec_env_t exec_env, uint32 *thread, /* thread_handle */
                                  const void *attr, /* not supported */
                                  uint32 elem_index, /* entry function */
                                  uint32 arg) /* arguments buffer */
    ;
#ifdef __cplusplus
}
#endif
