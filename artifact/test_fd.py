#!/bin/python3
import os

# LOGV=0 ./MVVM_checkpoint -t ./test/read-file.wasm -f write -c 0
# LOGV=0 ./MVVM_restore -t ./test/read-file.wasm


def run_fd_once(funcs):
    for i in funcs:
        os.system(
            f"LOGV=0 ./MVVM_checkpoint -t ./test/read-file.aot -c {i}"
        )
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/read-file.aot -c 100000000")


if __name__ == "__main__":
    run_fd_once(
        [77,109]
    )
