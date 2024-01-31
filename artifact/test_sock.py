#!/bin/python3
import os
import common_util

# LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -f {i} -c 0
# LOGV=0 ./MVVM_restore -t ./test/client.aot

def run_sock_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i}")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    
def run_sock_migrate_once(funcs):
    os.system(f"docker exec -it f9d ./MVVM_checkpoint -t ./test/server.aot -f {i} &")
    os.system("./gateway &")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot")
        os.system(f"docker exec -it f9d ./MVVM_restore -t ./test/server.aot -f {i} &")
    
def run_tcp_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i}")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    
def run_tcp_migrate_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i}")
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    
    
def run_tcp_migrate_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i}")
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")

    
def run_tcp_migrate_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f {i}")
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")
    
def get_tcp_latency():
    pass
def get_tcp_bandwidth():
    pass

if __name__ == "__main__":
    funcs = ["socket", "sendto", "recvfrom", "close"]
    func_idxs = [common_util.get_func_index(x) for x in funcs]
    print(func_idxs)
    run_sock_migrate_once(func_idxs)

