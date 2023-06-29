//
// Created by victoryang00 on 6/17/23.
//

#include "wasm_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
void insert_fd(int, char const *, int);
void remove_fd(int);
void serialize_to_file(struct WASMExecEnv*);
void insert_socket(char const *, int);
void remove_socket(char const *); // see whether there's socket maintainance impl in wasi?
void insert_lock(char const *, int);
void insert_sem(char const *, int);
void remove_lock(char const *);
void remove_sem(char const *);
#ifdef __cplusplus
}
#endif