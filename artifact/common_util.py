import subprocess
import sys
import os
import time


def get_func_index(func, file):
    cmd = ["wasm2wat", "--enable-all", file]
    grep_cmd = ["grep", "func"]
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    grep_process = subprocess.Popen(
        grep_cmd, stdin=process.stdout, stdout=subprocess.PIPE
    )
    process.stdout.close()
    output = grep_process.communicate()[0].decode("utf-8")
    output = output.split("\n")
    output1 = [
        x
        for x in output
        if not x.__contains__("(type (;") and not x.__contains__("(table (;")
    ]
    for i in range(len(output1)):
        if func in output1[i]:
            return i


list_of_arg = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=2",
]
aot_variant = [".aot", "-pure.aot", "-stack.aot", "-ckpt.aot", "-ckpt-br.aot"]
# aot_variant = ["-ckpt-every-dirty.aot"]


def run(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    cmd = f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    output = result.stdout.decode("utf-8")
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


if __name__ == "__main__":
    print(get_func_index("recvfrom", "./test/server.wasm"))
    print(get_func_index("poll_oneoff", "./test/counter.wasm"))
    print(get_func_index("sendto", "./test/client.wasm"))
