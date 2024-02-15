#!/bin/python3
import os
import common_util

# LOGV=0 ./MVVM_checkpoint -t ./test/read-file.wasm -f write -c 0
# LOGV=0 ./MVVM_restore -t ./test/read-file.wasm


def run_fd_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"./MVVM_checkpoint -t ./test/read-file.aot -f {i} -x {func_times[idx]} -i"
        )
        os.system(f"./MVVM_restore -t ./test/read-file.aot")
        assert os.path.getsize("test4.txt") == 30


def get_fd_merge_latency():
    pass


if __name__ == "__main__":
    funcs = ["$fopen", "$fread", "$fwrite", "$fwrite", "$fwrite", "$fseek"]
    func_times = [0, 0, 2, 3, 4, 0]
    func_idxs = [common_util.get_func_index(x, "test/read-file.wasm") for x in funcs]
    print(func_idxs)
    run_fd_once(func_idxs, func_times)
