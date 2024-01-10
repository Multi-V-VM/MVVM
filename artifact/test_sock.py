#!/bin/python3
import os
import common_util

# LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -f {i} -c 0
# LOGV=0 ./MVVM_restore -t ./test/client.aot

def run_sock_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i} -c 10000000000")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    
def run_sock_migrate_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i} -c 10000000000")
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    
def run_tcp_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i} -c 10000000000")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    
def run_tcp_migrate_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i} -c 10000000000")
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    

if __name__ == "__main__":
    funcs = ["socket", "sendto", "recvfrom", "close"]
    func_idxs = [common_util.get_func_index(x) for x in funcs]
    run_sock_once(func_idxs)

