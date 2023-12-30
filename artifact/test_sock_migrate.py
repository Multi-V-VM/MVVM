#!/bin/python3
import os

# LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -f write -c 0
# LOGV=0 ./MVVM_restore -t ./test/client.aot


def run_sock_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/server.aot -f write -c 10000000000")
    os.system("./gateway")

    for i in range(0, funcs):
        os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/client.aot -c {i}")
        os.system(f"LOGV=0 ./MVVM_restore -t ./test/client.aot")


if __name__ == "__main__":
    run_sock_once([88])
