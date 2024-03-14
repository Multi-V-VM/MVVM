import subprocess
import os
import asyncio
import time

pwd = "/Users/victoryang00/Documents/project/MVVM-bench/"
# pwd = "/mnt/MVVM"
slowtier = "epyc"
burst = "mac"

def parse_time(time_string):
    # Split the time string into components
    components = time_string.split(":")
    hours = int(components[0])
    minutes = int(components[1])
    seconds, milliseconds = map(int, components[2].split("."))

    # Calculate the total seconds
    total_seconds = hours * 3600 + minutes * 60 + seconds + milliseconds / 1000

    return total_seconds


def parse_time_no_msec(time_string):
    # Split the time string into components
    components = time_string.split(":")
    hours = int(components[0])
    minutes = int(components[1])
    seconds = int(components[2])

    # Calculate the total seconds
    total_seconds = hours * 3600 + minutes * 60 + seconds
    if total_seconds < 10000:
        print(time_string)
        raise ValueError
    return total_seconds

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
trial = 2


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


def run(aot_file: str, arg: list[str], env: str, extra: str = "") -> tuple[str, str]:
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


def run_checkpoint_restore_slowtier_overhead(
    aot_file: str,
    arg: list[str],
    env: str,
    extra1: str = "",
    extra2: str = "",
):
    # Execute run_checkpoint and capture its result
    res = []
    for i in range(trial):
        # Execute run_restore with the same arguments (or modify as needed)
        os.system(
            f"ssh -t {slowtier} {pwd}/build/MVVM_restore -t {pwd}/build/bench/{aot_file} {extra2} &"
        )
        print(
            f"ssh -t {slowtier} {pwd}/build/MVVM_restore -t {pwd}/build/bench/{aot_file} {extra2} &"
        )
        os.system("sleep 15")

        os.system(
            f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra1} >> ./bench/{aot_file}{i}.log &"
        )
        print(
            f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra1} > ./bench/{aot_file}{i}.log &"
        )
        os.system("sleep 10")
        os.system("pkill -SIGINT -f MVVM_checkpoint")
        os.system("pkill -SIGINT -f MVVM_checkpoint")
        os.system(f"ssh {slowtier} pkill -f MVVM_restore")
        # os.system(f"ssh {slowtier} tcpkill -i eno2 port 12346")
        # print(checkpoint_result, restore_result)
        # Return a combined result or just the checkpoint result as needed
        os.system("sleep 5")
        cmd = f"cat ./bench/{aot_file}{i}.log"
        cmd = cmd.split()
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            output = result.stdout.decode("utf-8")
        except:
            output = result.stdout
        # print(output)
        exec = " ".join([env] + [aot_file] + arg)
        res.append((exec, output))
    return res


def run_checkpoint_restore_slowtier(
    aot_file: str,
    arg: list[str],
    aot_file1: str,
    arg1: list[str],
    env: str,
    extra1: str = "",
    extra2: str = "",
    extra3: str = "",
):
    # Execute run_checkpoint and capture its result
    res = []
    os.system("rm ./*.out")
    os.system(f"ssh -t {slowtier} rm {pwd}/build/*.out")
    # Execute run_restore with the same arguments (or modify as needed)
    os.system(
        f"script -q /dev/null -c 'ssh -t {slowtier} {pwd}/build/MVVM_restore -t {pwd}/build/bench/{aot_file1} {extra2}' >> MVVM_restore.1.out &"
    )
    os.system(f"ssh -t {slowtier} {pwd}/artifact/run_with_cpu_monitoring_nocommand.sh MVVM_restore &")
    # print(f"ssh -t {slowtier} bash -c 'cd {pwd}/build && {pwd}/artifact/run_with_cpu_monitoring_nocommand.sh MVVM_restore' &")
    os.system(
        f"script -q /dev/null -c './MVVM_restore -t ./bench/{aot_file1} {extra3}' >> MVVM_restore.out &"
    )
    os.system(f"../artifact/run_with_cpu_monitoring_nocommand.sh MVVM_restore &")
    
    os.system("sleep 15")
    os.system(
        f"../artifact/run_with_cpu_monitoring.sh ./MVVM_checkpoint -t ./bench/{aot_file1} {' '.join(['-a ' + str(x) for x in arg1])} -e {env} {extra1} &"
    )
    os.system("sleep 10")
    os.system(f"pkill -SIGINT -f MVVM_checkpoint")
    os.system("mv MVVM_checkpoint.out MVVM_checkpoint.1.out")
    os.system("mv MVVM_checkpoint.ps.out MVVM_checkpoint.ps.1.out")
    os.system(
        f"../artifact/run_with_cpu_monitoring.sh ./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env}"
    )
    os.system(f"ssh -t {slowtier} pkill -SIGINT -f MVVM_restore")
    
    # print(checkpoint_result, restore_result)
    # Return a combined result or just the checkpoint result as needed
    os.system("sleep 100")
    os.system(f"scp -r {slowtier}:{pwd}/build/MVVM_restore.ps.out ./MVVM_restore.ps.1.out")
    
    cmd = f"cat ./MVVM_checkpoint.out ./MVVM_checkpoint.1.out ./MVVM_restore.1.out ./MVVM_restore.out"
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        output = result.stdout.decode("utf-8")
    except:
        output = result.stdout
    
    exec = " ".join([env] + [aot_file] + arg)
    res.append((exec, output))
    return res


def run_checkpoint_restore_burst(
    aot_file: str,
    arg: list[str],
    aot_file1: str,
    arg1: list[str],
    env: str,
    extra1: str = "",
    extra2: str = "",
    extra3: str = "",
    extra4: str = "",
    extra5: str = "",
    extra6: str = "",
    extra7: str = "",
    extra8: str = "",
):
    # Execute run_checkpoint and capture its result
    res = []
    os.system("rm ./*.out")
    os.system(f"ssh -t {burst} rm {pwd}/build/*.out")
    # Execute run_restore with the same arguments (or modify as needed)
    os.system(
        f"script -q /dev/null -c 'ssh -t {burst} {pwd}/build/MVVM_restore -t {pwd}/build/bench/{aot_file1} {extra2}' >> MVVM_restore.1.out &"
    )
    os.system(f"ssh -t {burst} bash -c ../artifact/run_with_energy_monitoring.sh MVVM_restore 1 &")
    os.system(
        f"script -q /dev/null -c './MVVM_restore -t ./bench/{aot_file1} {extra3}' >> MVVM_restore.out &"
    )
    os.system(f"../artifact/run_with_energy_monitoring.sh MVVM_restore 2 &")
    
    os.system("sleep 15")
    os.system(
        f"./MVVM_checkpoint -t ./bench/{aot_file1} {' '.join(['-a ' + str(x) for x in arg1])} -e {env} {extra1} &"
    )
    os.system("../artifact/run_with_energy_monitoring.sh MVVM_checkpoint 1 &")

    os.system("sleep 10")
    os.system(f"pkill -SIGINT -f MVVM_checkpoint")
    os.system(
        f"../artifact/run_with_energy_monitoring_mac.sh ./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra4}"
    ) #redis
    os.system(f"ssh -t {burst} pkill -SIGINT -f MVVM_restore")
    os.system(f"")
    # print(checkpoint_result, restore_result)
    # Return a combined result or just the checkpoint result as needed
    os.system("sleep 100")
    
    cmd = f"cat ./MVVM_checkpoint.out ./MVVM_checkpoint.1.out ./MVVM_restore.1.out ./MVVM_restore.out"
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        output = result.stdout.decode("utf-8")
    except:
        output = result.stdout
    
    exec = " ".join([env] + [aot_file] + arg)
    res.append((exec, output))
    return res



def run_slowtier(
    aot_file: str, arg: list[str], env: str, extra: str = ""
) -> tuple[str, str]:
    cmd = f"ssh {slowtier} {pwd}/build/MVVM_checkpoint -t {pwd}/build/bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra}"
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
