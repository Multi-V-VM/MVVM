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
        if not x.__contains__("(type (;") and not x.__contains__("(table (;") and not x.__contains__("global.get")
    ]
    for i in range(len(output1)):
        if func in output1[i]:
            return i


list_of_arg = [
    "OMP_NUM_THREADS=1",
]
aot_variant = [".aot"]
# aot_variant = ["-ckpt-every-dirty.aot"]

trial = 1


def contains_result(output: str, result: str) -> bool:
    return result in output


def run_checkpoint_restore(
    aot_file: str, arg: list[str], env: str
) -> tuple[str, str, str, str]:
    # Execute run_checkpoint and capture its result
    for i in range(trial):
        checkpoint_result = run_checkpoint(aot_file, arg, env)

        # Execute run_restore with the same arguments (or modify as needed)
        restore_result = run_restore(aot_file, arg, env)
        print(checkpoint_result, restore_result)
    # Return a combined result or just the checkpoint result as needed
    return checkpoint_result, restore_result


def run_checkpoint(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    cmd = f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} -c 10000"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # try:
    output = result.stdout.decode("utf-8")
    # except:
    # output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_restore(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    cmd = f"./MVVM_restore -t ./bench/{aot_file}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    # try:
    output = result.stdout.decode("utf-8")
    # except:
    # output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    return (exec, output)


def run_criu_checkpoint_restore(
    aot_file: str, arg: list[str], env: str
) -> tuple[str, str, str, str]:
    # Execute run_checkpoint and capture its result
    checkpoint_result = run_criu_checkpoint(aot_file, arg, env)

    # Execute run_criu_restore with the same arguments (or modify as needed)
    restore_result = run_criu_restore(aot_file, arg, env)

    # Return a combined result or just the checkpoint result as needed
    return checkpoint_result, restore_result


def run_criu_checkpoint(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    file = aot_file.replace(".aot", "")
    cmd = f"../build/bench/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.Popen(
        cmd, env={env}, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    try:
        output = result.stdout.decode("utf-8")
    except:
        output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_criu_restore(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    file = aot_file.replace(".aot", "")

    cmd = f"../build/bench/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.Popen(
        cmd, env={env}, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    try:
        output = result.stdout.decode("utf-8")
    except:
        output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)

def run_qemu_checkpoint_restore(
    aot_file: str, arg: list[str], env: str
) -> tuple[str, str, str, str]:
    # Execute run_checkpoint and capture its result
    checkpoint_result = run_qemu_checkpoint(aot_file, arg, env)

    # Execute run_qemu_restore with the same arguments (or modify as needed)
    restore_result = run_qemu_restore(aot_file, arg, env)

    # Return a combined result or just the checkpoint result as needed
    return checkpoint_result, restore_result


def run_qemu_checkpoint(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    file = aot_file.replace(".aot", "")
    cmd = f"./qemu_migrate_client.sh ../build/bench/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.Popen(
        cmd, env={env}, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    try:
        output = result.stdout.decode("utf-8")
    except:
        output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_qemu_restore(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    file = aot_file.replace(".aot", "")

    cmd = f"./qemu_migrate_server.sh ../build/bench/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.Popen(
        cmd, env={env}, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )

    try:
        output = result.stdout.decode("utf-8")
    except:
        output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)

def run(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    cmd = f"./MVVM_checkpoint -t ../build/bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        output = result.stdout.decode("utf-8")
    except:
        output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_hcontainer(file: str, folder: str, arg: list[str], env: str) -> tuple[str, str]:
    cmd = f"time /mnt1/MVVM/bench/{folder}/build_x86/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        output = result.stderr.decode("utf-8")
    except:
        output = result.stderr
    exec = " ".join([env] + [file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_native(file: str, folder: str, arg: list[str], env: str) -> tuple[str, str]:
    cmd = f"time /mnt/MVVM/bench/{folder}/build/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        output = result.stderr.decode("utf-8")
    except:
        output = result.stderr
    exec = " ".join([env] + [file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_qemu_x86_64(
    file: str, folder: str, arg: list[str], env: str
) -> tuple[str, str]:
    cmd = f"time qemu-x86_64 /mnt/MVVM/bench/{folder}/build/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        output = result.stderr.decode("utf-8")
    except:
        output = result.stderr
    exec = " ".join([env] + [file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_qemu_aarch64(
    file: str, folder: str, arg: list[str], env: str
) -> tuple[str, str]:
    cmd = f"time qemu-aarch64 /mnt/MVVM/bench/{folder}/build/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        output = result.stderr.decode("utf-8")
    except:
        output = result.stderr
    exec = " ".join([env] + [file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


if __name__ == "__main__":
    print(get_func_index("$recv ", "./test/tcp_client.wasm"))
    print(get_func_index("__wasi_fd_read", "./test/read-file.wasm"))
    print(get_func_index("sendto", "./test/client.wasm"))
    print(get_func_index("atomic_wait", "./bench/hdastar.wasm"))
