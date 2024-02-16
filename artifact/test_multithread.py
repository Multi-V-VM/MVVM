#!/bin/python3
import os
import common_util

def run_mutex_once(funcs):
    for i in funcs:
        os.system(f"./MVVM_checkpoint -t ./test/mutex.aot -f {i} -x 0 -i")
        os.system(f"./MVVM_restore -t ./test/mutex.aot")
    assert int(open("test1.txt").read()) == 20000


def run_multi_thread_once(funcs):
    for i in funcs:
        os.system(f"./MVVM_checkpoint -t ./test/multi-thread.aot -f {i} -x 0 -i")
        os.system(f"./MVVM_restore -t ./test/multi-thread.aot")
    assert int(open("test1.txt").read()) == 2000

if __name__ == "__main__":
    funcs = ["pthread_create","pthread_join","printf","printf"]
    func_times = [0, 0, 100, 100]
    func_idxs = [common_util.get_func_index(x, "./test/multi-thread.wasm") for x in funcs]
    print(func_idxs)
    run_multi_thread_once(func_idxs)
    
    func_idxs = [common_util.get_func_index(x, "./test/mutex.wasm") for x in funcs]
    run_mutex_once(func_idxs)
    print("test_multithread.py passed")
    
    
