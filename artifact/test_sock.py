#!/bin/python3
import os
import common_util
import time

#  {pwd}/MVVM_checkpoint -t {pwd}/test/client.aot -f {i} -x {func_times[idx]} -i -c 0
#  {pwd}/MVVM_restore -t {pwd}/test/client.aot
server_ip = "172.17.0.2"
client_ip = "172.17.0.3"
pwd = os.getcwd()


def run_sock_client_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(f"ssh {server_ip} {pwd}/MVVM_checkpoint -t {pwd}/test/server.aot &")
        time.sleep(1)
        os.system(
            f"{pwd}/MVVM_checkpoint -t {pwd}/test/client.aot -f {i} -x {func_times[idx]} -i"
        )
        os.system(f"{pwd}/MVVM_restore -t {pwd}/test/client.aot")
        os.system(f"ssh {server_ip} pkill MVVM_checkpoint")
        os.system("pkill MVVM_checkpoint")
        os.system("pkill MVVM_restore")


def run_sock_server_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"ssh {server_ip} {pwd}/MVVM_checkpoint -t test/server.aot -f {i} -x {func_times[idx]} -i &"
        )
        os.system(f"{pwd}/MVVM_checkpoint -t {pwd}/test/client.aot &")
        os.system(f"{pwd}/MVVM_restore -t test/server.aot")
        os.system(f"ssh {server_ip} pkill MVVM_checkpoint")
        os.system("pkill MVVM_checkpoint")
        os.system("pkill MVVM_restore")


def run_sock_server_migrate_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"ssh {server_ip} MVVM_checkpoint -t test/server.aot -f {i} -x {func_times[idx]} -i &"
        )
        os.system(f"./gateway/gateway &")

        os.system(f"{pwd}/MVVM_checkpoint -t test/client.aot")
        os.system(
            f"ssh {client_ip} {pwd}/MVVM_restore -t {pwd}/test/server.aot -f {i} -x {func_times[idx]} -i &"
        )
        os.system(f"ssh {client_ip} pkill MVVM_checkpoint")
        os.system("pkill MVVM_checkpoint")
        os.system("pkill MVVM_restore")


def run_sock_client_migrate_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"ssh {server_ip} MVVM_checkpoint -t test/server.aot -f {i} -x {func_times[idx]} -i &"
        )
        os.system(f"./gateway/gateway &")

        os.system(f"{pwd}/MVVM_checkpoint -t test/client.aot")
        os.system(
            f"ssh {client_ip} {pwd}/MVVM_restore -t {pwd}/test/server.aot -f {i} -x {func_times[idx]} -i &"
        )
        os.system(f"ssh {client_ip} pkill MVVM_checkpoint")
        os.system("pkill MVVM_checkpoint")
        os.system("pkill MVVM_restore")


def run_tcp_server_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"{pwd}/MVVM_checkpoint -t {pwd}/test/server.aot -f {i} -x {func_times[idx]} -i"
        )

        os.system(f" {pwd}/MVVM_checkpoint -t {pwd}/test/client.aot -c {i}")
        os.system(f" {pwd}/MVVM_restore -t {pwd}/test/client.aot")


def run_tcp_client_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"{pwd}/MVVM_checkpoint -t {pwd}/test/client.aot -f {i} -x {func_times[idx]} -i &"
        )

        os.system(f" {pwd}/MVVM_checkpoint -t {pwd}/test/sever.aot -c {i}")
        os.system(f" {pwd}/MVVM_restore -t {pwd}/test/client.aot")


def run_tcp_migrate_client_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            f"{pwd}/MVVM_checkpoint -t {pwd}/test/server.aot -f {i} -x {func_times[idx]} -i"
        )
        os.system("{pwd}/gateway &")

        os.system(f" {pwd}/MVVM_checkpoint -t {pwd}/test/client.aot -c {i}")
        os.system(f" {pwd}/MVVM_restore -t {pwd}/test/client.aot")


def run_tcp_migrate_server_once(funcs, func_times):
    for idx, i in enumerate(funcs):
        os.system(
            " {pwd}/MVVM_checkpoint -t {pwd}/test/server.aot -f {i} -x {func_times[idx]} -i"
        )
        os.system("{pwd}/gateway &")

        os.system(f" {pwd}/MVVM_checkpoint -t {pwd}/test/client.aot -c {i}")
        os.system(f" {pwd}/MVVM_restore -t {pwd}/test/client.aot")


def get_tcp_latency():
    # require reconstruct of the tcp to allow with gateway or not
    pass


def get_tcp_bandwidth():
    pass


if __name__ == "__main__":
    funcs = ["__wasi_sock_recv", "__wasi_sock_send"]
    func_times = [0, 0]
    func_idxs = [common_util.get_func_index(x, "test/client.wasm") for x in funcs]
    print(func_idxs)
    run_sock_client_once(func_idxs, func_times)
    run_sock_client_migrate_once(func_idxs, func_times)
    func_idxs = [common_util.get_func_index(x, "test/server.wasm") for x in funcs]
    run_sock_server_once(func_idxs, func_times)
    run_sock_server_migrate_once(func_idxs, func_times)

    funcs = ["__wasi_sock_recv_from", "__wasi_sock_send_to"]
    func_idxs = [common_util.get_func_index(x, "test/tcp_server.wasm") for x in funcs]
    run_tcp_server_once(func_idxs, func_times)
    run_tcp_migrate_server_once(func_idxs, func_times)
    
    func_idxs = [common_util.get_func_index(x, "test/tcp_client.wasm") for x in funcs]
    run_tcp_client_once(func_idxs, func_times)
    run_tcp_migrate_client_once(func_idxs, func_times)
    
    get_tcp_latency()
    get_tcp_bandwidth()
    print("test_sock.py passed")
