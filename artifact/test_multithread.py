#!/bin/python3
import os
import common_util


def run_mutex_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"./MVVM_checkpoint -t ./test/mutex.aot -f {i} -x {func_times[idx]} -i"
        )
        os.system(f"./MVVM_restore -t ./test/mutex.aot")
    assert int(open("test1.txt").read()) == 40000


def run_multi_thread_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"./MVVM_checkpoint -t ./test/multi-thread.aot -f {i} -x {func_times[idx]} -i"
        )
        os.system(f"./MVVM_restore -t ./test/multi-thread.aot")
    assert int(open("test1.txt").read()) == 4000


if __name__ == "__main__":
    funcs = ["$printf", "$printf", "$printf"]
    func_times = [10, 11, 12]
    func_idxs = [
        common_util.get_func_index(x, "./test/multi-thread.wasm") for x in funcs
    ]
    print(func_idxs)
    run_multi_thread_once(func_idxs, func_times)

    func_idxs = [common_util.get_func_index(x, "./test/mutex.wasm") for x in funcs]
    run_mutex_once(func_idxs, func_times)
    print("test_multithread.py passed")
