#!/bin/python3
import os
import common_util


def run_fd_once(funcs):
    for i in funcs:
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/multi-thread.wasm -f {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/multi-thread.wasm -c 100000000")


if __name__ == "__main__":
    funcs = ["pthread_create","pthread_join","pthread_mutex_lock","pthread_mutex_unlock"]
    func_idxs = [common_util.get_func_index(x, "./test/multi-thread.wasm") for x in funcs]
    print(func_idxs)
    run_fd_once(func_idxs)
