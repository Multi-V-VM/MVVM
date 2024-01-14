#!/bin/python3
import os
import common_util

# LOGV=0 ./MVVM_checkpoint -t ./test/read-file.wasm -f write -c 0
# LOGV=0 ./MVVM_restore -t ./test/read-file.wasm


def run_fd_once(funcs):
    for i in funcs:
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/read-file.aot -f {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/read-file.aot -c 100000000")


if __name__ == "__main__":
    funcs = ["open", "read", "write", "close"]
    func_idxs = [common_util.get_func_index(x, "test/read-file.wasm") for x in funcs]
    print(func_idxs)
    run_fd_once(func_idxs)
