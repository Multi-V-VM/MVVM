import subprocess
import os
import asyncio
import time

pwd = "/mnt/MVVM"


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
        if not x.__contains__("(type (;")
        and not x.__contains__("(table (;")
        and not x.__contains__("global.get")
    ]
    for i in range(len(output1)):
        if func in output1[i]:
            return i


list_of_arg = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=2",
    "OMP_NUM_THREADS=4",
    # "OMP_NUM_THREADS=8",
]
# aot_variant = [".aot"]
# aot_variant = ["-ckpt-every-dirty.aot"]
aot_variant = [
    ".aot",
    "-pure.aot",
    "-stack.aot",
    "-ckpt-every-dirty.aot",
    "-ckpt-loop.aot",
    "-ckpt-loop-dirty.aot",
]
trial = 10


def contains_result(output: str, result: str) -> bool:
    return result in output


def run_checkpoint_restore(
    aot_file: str, arg: list[str], env: str
) -> tuple[str, str, str, str]:
    # Execute run_checkpoint and capture its result
    res = []
    for _ in range(trial):
        checkpoint_result = run_checkpoint(aot_file, arg, env)

        # Execute run_restore with the same arguments (or modify as needed)
        restore_result = run_restore(aot_file, arg, env)
        # print(checkpoint_result, restore_result)
        # Return a combined result or just the checkpoint result as needed

        res.append(checkpoint_result[1] + restore_result[1])
    return (checkpoint_result[0], res)


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
    proc = subprocess.Popen(
        cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )

    # Record start time
    start_time = time.time()
    while True:
        # Check if the process has terminated
        if proc.poll() is not None:
            break  # Process has finished

        # Check for timeout
        if time.time() - start_time > 20:
            # Attempt to terminate the process
            proc.terminate()
            try:
                # Wait a bit for the process to terminate
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                # If it's still not terminated, kill it
                proc.kill()
                proc.wait()
            break  # Exit the loop

        time.sleep(0.1)  # Sleep briefly to avoid busy waiting

    # Capture any output
    output, stderr = proc.communicate()

    # output = stdout.decode("utf-8")
    # except:
    # output = result.stdout
    exec = " ".join([env] + [aot_file] + arg)
    return (exec, output)


def run_criu_checkpoint_restore(
    aot_file: str, folder, arg: list[str], env: str
) -> tuple[str, str, str, str]:
    # Execute run_checkpoint and capture its result
    res = []
    for _ in range(trial):
        checkpoint_result = run_criu_checkpoint(aot_file, folder, arg, env)

        # Execute run_restore with the same arguments (or modify as needed)
        restore_result = run_criu_restore(aot_file, arg, env)
        # print(checkpoint_result, restore_result)
        # Return a combined result or just the checkpoint result as needed

        res.append(checkpoint_result[1] + restore_result[1])
    return (checkpoint_result[0], res)


async def run_subprocess(cmd, env):
    # Create a subprocess
    process = await asyncio.create_subprocess_exec(
        *cmd, env=env, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE
    )

    return process.pid


def run_criu_checkpoint(
    aot_file: str, folder, arg: list[str], env: str
) -> tuple[str, str]:
    file = aot_file.replace(".aot", "")
    cmd = f"{pwd}/bench/{folder}/build/{file} {' '.join(arg)}"
    env_arg = dict([env.split("=")])
    print(cmd)
    cmd = cmd.split()
    pid = asyncio.run(run_subprocess(cmd, env_arg))
    time.sleep(1)
    os.system(f"mkdir -p /tmp/{file}{env}")
    criu_cmd = f"/usr/sbin/criu dump -t {pid} -D /tmp/{file}{env} --shell-job -v"
    criu_cmd = criu_cmd.split()
    proc = subprocess.Popen(
        criu_cmd, env=env_arg, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )

    # Record start time
    start_time = time.time()
    while True:
        # Check if the process has terminated
        if proc.poll() is not None:
            break  # Process has finished

        # Check for timeout
        if time.time() - start_time > 20:
            # Attempt to terminate the process
            proc.terminate()
            try:
                # Wait a bit for the process to terminate
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                # If it's still not terminated, kill it
                proc.kill()
                proc.wait()
            break  # Exit the loop

        time.sleep(0.1)  # Sleep briefly to avoid busy waiting

    # Capture any output
    _, output = proc.communicate()
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_criu_restore(aot_file: str, arg: list[str], env: str) -> tuple[str, str]:
    file = aot_file.replace(".aot", "")

    cmd = f"/usr/sbin/criu restore -D /tmp/{file}{env} --shell-job -v"
    print(cmd)
    cmd = cmd.split()
    env_arg = dict([env.split("=")])

    proc = subprocess.Popen(
        cmd, env=env_arg, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True
    )

    # Record start time
    start_time = time.time()
    while True:
        # Check if the process has terminated
        if proc.poll() is not None:
            break  # Process has finished

        # Check for timeout
        if time.time() - start_time > 20:
            # Attempt to terminate the process
            proc.terminate()
            try:
                # Wait a bit for the process to terminate
                proc.wait(timeout=1)
            except subprocess.TimeoutExpired:
                # If it's still not terminated, kill it
                proc.kill()
                proc.wait()
            break  # Exit the loop

        time.sleep(0.1)  # Sleep briefly to avoid busy waiting

    # Capture any output
    _, output = proc.communicate()

    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run_qemu_checkpoint_restore(
    aot_file: str, folder: str, arg: list[str], env: str
) -> tuple[str, str, str, str]:
    res = []
    for _ in range(trial):
        os.system(f"nohup ../artifact/qemu_migrate_client.sh &")
        os.system(f"nohup ../artifact/qemu_migrate_server.sh &")
        time.sleep(20)

        checkpoint_result = run_qemu_checkpoint(aot_file, folder, arg, env)
        # Return a combined result or just the checkpoint result as needed

        res.append(checkpoint_result[1])
        os.system("pkill -f qemu-system-x86_64")
    return (checkpoint_result[0], res)


def run_qemu_checkpoint(
    aot_file: str, folder: str, arg: list[str], env: str
) -> tuple[str, str]:
    file = aot_file.replace(".aot", "")
    cmd = f"../artifact/prepare_checkpoint.sh env {env} {pwd}/bench/{folder}/build/{file} {' '.join(arg)}"
    print(cmd)
    cmd = cmd.split()
    env_arg = dict([env.split("=")])

    result = subprocess.Popen(
        cmd, env=env_arg, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    stdout, stderr = result.communicate()

    try:
        output = stdout.decode("utf-8")
    except:
        output = stdout
    exec = " ".join([env] + [aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)


def run(aot_file: str, arg: list[str], env: str, extra:str="") -> tuple[str, str]:
    cmd = f"./MVVM_checkpoint -t ../build/bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra}"
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

def run_checkpoint_restore_slowtier(
    aot_file: str, folder, arg: list[str], env: str, extra:str=""
) -> tuple[str, str, str, str]:
    # Execute run_checkpoint and capture its result
    res = []
    for _ in range(trial):
        checkpoint_result = run(aot_file, folder, arg, env,extra)

        # Execute run_restore with the same arguments (or modify as needed)
        restore_result = run_criu_restore(aot_file, arg, env)
        # print(checkpoint_result, restore_result)
        # Return a combined result or just the checkpoint result as needed

        res.append(checkpoint_result[1] + restore_result[1])
    return (checkpoint_result[0], res)


def run_slowtier(aot_file: str, arg: list[str], env: str, extra:str="") -> tuple[str, str]:
    cmd = f"ssh epyc {pwd}/MVVM_checkpoint -t {pwd}/build/bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra}"
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
    cmd = f"/usr/bin/time {pwd}/bench/{folder}/build_x86/{file} {' '.join(arg)}"
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
    cmd = f"/usr/bin/time {pwd}/bench/{folder}/build/{file} {' '.join(arg)}".strip()
    print(cmd)
    cmd = cmd.split()
    env_arg = dict([env.split("=")])

    result = subprocess.run(
        cmd, env=env_arg, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
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
    cmd = f"/usr/bin/time /usr/bin/qemu-x86_64 -E {env} {pwd}/bench/{folder}/build/{file} {' '.join(arg)}"
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
    cmd = f"/usr/bin/time /usr/bin/qemu-aarch64 -E {env} {pwd}/bench/{folder}/build_aarch64_native/{file} {' '.join(arg)}"
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
