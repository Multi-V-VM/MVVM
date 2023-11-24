#!/bin/python3
import os
# LOGV=0 ./MVVM_checkpoint -t ./test/read-file.wasm -f write -c 0
# LOGV=0 ./MVVM_restore -t ./test/read-file.wasm

def run_sock_once(funcs):
    os.system("LOGV=0 ./MVVM_checkpoint -t ./test/read-file.wasm -f write -c 0")
    os.system("./gateway")
    for func in funcs.keys():
        for i in range(0, funcs[func]):
            os.system(f"LOGV=0 ./MVVM_checkpoint -t ./test/read-file.wasm -f {func} -c {i}")
            os.system(f"LOGV=0 ./MVVM_restore -t ./test/read-file.wasm")


if __name__ == "__main__":
    run_sock_once({"fwrite":2, "fread":1, "fclose":3,"fopen":3,"fseek":3,"ftell":1})