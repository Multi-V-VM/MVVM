import subprocess
import sys
import os
import time


def get_func_index(func, file):
    cmd = ["wasm2wat", "--enable-all", file, "|", "grep", "func"]
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output = result.stdout.decode("utf-8")
    output = output.split("\n")
    for i in range(len(output)):
        if func in output[i]:
            return i


list_of_arg = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=2",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=8",
]
# aot_variant = [".aot", "-pure.aot", "-stack.aot", "-ckpt.aot", "-ckpt-br.aot"]
aot_variant = ["-ckpt-every-dirty.aot"]


def run(aot_file: str, arg: list[str]) -> tuple[str, str]:
    cmd = f"../MVVM_checkpoint -t {aot_file} {' '.join(['-a ' + str(x) for x in arg])}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output = result.stdout.decode("utf-8")
    exec = " ".join([aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


if __name__ == "__main__":
    get_func_index("poll", "./counter.wasm")
