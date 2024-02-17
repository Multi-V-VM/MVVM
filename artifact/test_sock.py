#!/bin/python3
import os
import common_util

#  ./MVVM_checkpoint -t ./test/client.aot -f {i} -x {func_times[idx]} -i -c 0
#  ./MVVM_restore -t ./test/client.aot


def run_sock_client_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"./MVVM_checkpoint -t ./test/server.aot &"
        )

        os.system(f"./MVVM_checkpoint -t test/client.aot -f {i} -x {func_times[idx]} -i")
        os.system(f"./MVVM_restore -t test/client.aot")
        os.system("pkill MVVM_checkpoint")
        os.system("pkill MVVM_restore")

def run_sock_server_once(funcs, func_times):
    for idx,i in enumerate(funcs):
        os.system(f"./MVVM_checkpoint -t test/server.aot -f {i} -x {func_times[idx]} -i &")
        os.system(f"./MVVM_checkpoint -t ./test/client.aot &")
        os.system(f"./MVVM_restore -t test/server.aot")

    

def run_sock_migrate_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"docker exec -it f9d MVVM_checkpoint -t test/server.aot -f {i} -x {func_times[idx]} -i &"
        )
    os.system("./gateway &")

    for i in range(0, funcs):
        os.system(f"MVVM_checkpoint -t test/client.aot")
        os.system(
            f"docker exec -it mvvm ./MVVM_restore -t ./test/server.aot -f {i} -x {func_times[idx]} -i &"
        )


def run_tcp_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"./MVVM_checkpoint -t ./test/server.aot -f {i} -x {func_times[idx]} -i"
        )

    for i in range(0, funcs):
        os.system(f" ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f" ./MVVM_restore -t ./test/client.aot")


def run_tcp_migrate_client_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"./MVVM_checkpoint -t ./test/server.aot -f {i} -x {func_times[idx]} -i"
        )
    os.system("./gateway &")

    for i in range(0, funcs):
        os.system(f" ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f" ./MVVM_restore -t ./test/client.aot")


def run_tcp_migrate_server_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            " ./MVVM_checkpoint -t ./test/server.aot -f {i} -x {func_times[idx]} -i"
        )
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f" ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f" ./MVVM_restore -t ./test/client.aot")


def run_tcp_migrate_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            "./MVVM_checkpoint -t ./test/server.aot -f {i} -x {func_times[idx]} -i"
        )
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f"./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"./MVVM_restore -t ./test/client.aot")


def get_tcp_latency(output: str):
    pass


def get_tcp_bandwidth():
    pass


if __name__ == "__main__":
    funcs = ["socket", "sendto", "recvfrom", "close"]
    func_times = [0, 0, 0, 0]
    func_idxs = [common_util.get_func_index(x, "test/client.wasm") for x in funcs]
    print(func_idxs)
    run_sock_client_once(func_idxs, func_times)
    func_idxs = [common_util.get_func_index(x, "test/server.wasm") for x in funcs]
    run_sock_server_once(func_idxs, func_times)
    print("test_sock.py passed")
