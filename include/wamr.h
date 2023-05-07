//
// Created by yiwei yang on 5/6/23.
//

#ifndef MVVM_WAMR_H
#define MVVM_WAMR_H


#include "wasm_runtime.h"

class WAMRInstance {
    WASMExecEnv execEnv;
    char *buffer;
    char *error_buf[128];
    explicit WAMRInstance();
};


#endif //MVVM_WAMR_H
