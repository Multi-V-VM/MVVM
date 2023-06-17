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
#ifdef __cplusplus
}
#endif