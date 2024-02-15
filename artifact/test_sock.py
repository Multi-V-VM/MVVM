#!/bin/python3
import os
import common_util

#  /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/client.aot -f {i} -c 0
#  /mnt/MVVM/MVVM_restore -t /mnt/MVVM/test/client.aot

def run_sock_once(funcs):
    os.system("/mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/server.aot -f {i}")

    for i in range(0, funcs):
        os.system(f"/mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/build/test/client.aot -c {i}")
        os.system(f"/mnt/MVVM/build/MVVM_restore -t /mnt/MVVM/build/test/client.aot")
    
def run_sock_migrate_once(funcs):
    os.system(f"docker exec -it f9d /mnt/MVVM/build/MVVM_checkpoint -t /mnt/MVVM/build/test/server.aot -f {i} &")
    os.system("/mnt/MVVM/build/gateway &")

    for i in range(0, funcs):
        os.system(f" /mnt/MVVM/build/MVVM_checkpoint -t /mnt/MVVM/build/test/client.aot")
        os.system(f"docker exec -it mvvm /mnt/MVVM/MVVM_restore -t /mnt/MVVM/test/server.aot -f {i} &")
    
def run_tcp_once(funcs):
    os.system(" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/server.aot -f {i}")

    for i in range(0, funcs):
        os.system(f" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/client.aot -c {i}")
        os.system(f" /mnt/MVVM/MVVM_restore -t /mnt/MVVM/test/client.aot")
    
def run_tcp_migrate_once(funcs):
    os.system(" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/server.aot -f {i}")
    os.system("/mnt/MVVM/gateway")

    for i in range(0, funcs):
        os.system(f" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/client.aot -c {i}")
        os.system(f" /mnt/MVVM/MVVM_restore -t /mnt/MVVM/test/client.aot")
    
    
def run_tcp_migrate_once(funcs):
    os.system(" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/server.aot -f {i}")
    os.system("/mnt/MVVM/gateway")

    for i in range(0, funcs):
        os.system(f" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/client.aot -c {i}")
        os.system(f" /mnt/MVVM/MVVM_restore -t /mnt/MVVM/test/client.aot")

    
def run_tcp_migrate_once(funcs):
    os.system(" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/server.aot -f {i}")
    os.system("/mnt/MVVM/gateway")

    for i in range(0, funcs):
        os.system(f" /mnt/MVVM/MVVM_checkpoint -t /mnt/MVVM/test/client.aot -c {i}")
        os.system(f" /mnt/MVVM/MVVM_restore -t /mnt/MVVM/test/client.aot")
    
def get_tcp_latency():
    pass
def get_tcp_bandwidth():
    pass

if __name__ == "__main__":
    funcs = ["socket", "sendto", "recvfrom", "close"]
    func_idxs = [common_util.get_func_index(x) for x in funcs]
    print(func_idxs)
    run_sock_migrate_once(func_idxs)

