import subprocess
import os
import asyncio
import time
import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict
import csv

pwd_mac = "/Users/victoryang00/Documents/project/MVVM-bench/"
pwd = "/mnt1/MVVM"
slowtier = "epyc"
burst = "mac"


def calculate_averages_comparison(results):
    workload_normalized = defaultdict(list)

    # Step 1: Normalize values for each workload
    for (
        workload,
        mvvm_values,
        hcontainer_values,
        qemu_x86_64_values,
        qemu_aarch64_values,
        native_values,
    ) in results:
        if not workload.__contains__("sp") and not workload.__contains__("lu") and not workload.__contains__("tc"):
            # Assuming 'pure' is always non-zero
            workload_normalized[workload].append(
                {
                    "native": 1,  # Baseline
                    "mvvm": mvvm_values / native_values if native_values else 0,
                    "hcontainer": hcontainer_values / native_values if native_values else 0,
                    "qemu_x86_64": qemu_x86_64_values / native_values if native_values else 0,
                    "qemu_aarch64": qemu_aarch64_values / native_values if native_values else 0,
                }
            )
            print(workload,workload_normalized[workload])

    # Step 2 and 3: Calculate total average for each policy
    total_averages = defaultdict(float)
    for workload, policies in workload_normalized.items():
        for policy, values in policies[0].items():
            total_averages[policy] += values

    # Divide by the number of workloads to get the average
    num_workloads = len(workload_normalized)
    for policy in total_averages:
        total_averages[policy] /= num_workloads

    return dict(total_averages)


def calculate_averages(results):
    workload_normalized = defaultdict(list)

    # Step 1: Normalize values for each workload
    for workload, aot,pure, stack, loop, loop_dirty in results:
        # Assuming 'pure' is always non-zero
        workload_normalized[workload].append(
            {
                "pure": 1,  # Baseline
                "aot": aot / pure if pure else 0,
                "stack": stack / pure if pure else 0,
                "loop": loop / pure if pure else 0,
                "loop_dirty": loop_dirty / pure if pure else 0,
            }
        )

    # Step 2 and 3: Calculate total average for each policy
    total_averages = defaultdict(float)
    for workload, policies in workload_normalized.items():
        for policy, values in policies[0].items():
            total_averages[policy] += values

    # Divide by the number of workloads to get the average
    num_workloads = len(workload_normalized)
    for policy in total_averages:
        total_averages[policy] /= num_workloads

    return dict(total_averages)

def calculate_loop_counter_averages(results):
    workload_normalized = defaultdict(list)

    # Step 1: Normalize values for each workload
    for workload, ckptloopcounter1, ckptloopcounter4, ckptloopcounter8, ckptloopcounter16, ckptloopcounter20,ckptloopcounter30,ckptlooppgo,aot,pure in results:
        # Assuming 'pure' is always non-zero
        workload_normalized[workload].append(
            {
            "ckptloopcounter1": ckptloopcounter1 / pure if pure else 0, 
            "ckptloopcounter4": ckptloopcounter4 / pure if pure else 0, 
            "ckptloopcounter8": ckptloopcounter8 / pure if pure else 0, 
            "ckptloopcounter16": ckptloopcounter16 / pure if pure else 0, 
            "ckptloopcounter20": ckptloopcounter20 / pure if pure else 0, 
            "ckptloopcounter30": ckptloopcounter30 / pure if pure else 0, 
            "ckptlooppgo": ckptlooppgo / pure if pure else 0, 
            "aot": aot / pure if pure else 0, 
            "pure": 1
            }
        )

    # Step 2 and 3: Calculate total average for each policy
    total_averages = defaultdict(float)
    for workload, policies in workload_normalized.items():
        for policy, values in policies[0].items():
            total_averages[policy] += values

    # Divide by the number of workloads to get the average
    num_workloads = len(workload_normalized)
    for policy in total_averages:
        total_averages[policy] /= num_workloads

    return dict(total_averages)

def calculate_loop_counter_snapshot_averages(results):
    workload_normalized = defaultdict(list)

    # Step 1: Normalize values for each workload
    for workload, ckptloopcounter1, ckptloopcounter4, ckptloopcounter8, ckptloopcounter16, ckptloopcounter20,ckptloopcounter30 in results:
        # Assuming 'pure' is always non-zero
        workload_normalized[workload].append(
            {
            "ckptloopcounter1": 1,
            "ckptloopcounter4": ckptloopcounter4 / ckptloopcounter1 if ckptloopcounter1 else 0, 
            "ckptloopcounter8": ckptloopcounter8 / ckptloopcounter1 if ckptloopcounter1 else 0, 
            "ckptloopcounter16": ckptloopcounter16 / ckptloopcounter1 if ckptloopcounter1 else 0, 
            "ckptloopcounter20": ckptloopcounter20 / ckptloopcounter1 if ckptloopcounter1 else 0, 
            "ckptloopcounter30": ckptloopcounter30 / ckptloopcounter1 if ckptloopcounter1 else 0, 
            }
        )

    # Step 2 and 3: Calculate total average for each policy
    total_averages = defaultdict(float)
    for workload, policies in workload_normalized.items():
        for policy, values in policies[0].items():
            total_averages[policy] += values

    # Divide by the number of workloads to get the average
    num_workloads = len(workload_normalized)
    for policy in total_averages:
        total_averages[policy] /= num_workloads

    return dict(total_averages)


def plot(results, file_name):
    font = {"size": 18}
    plt.rc("font", **font)
    workloads = defaultdict(list)

    # Simplifying and grouping your data
    for workload, aot,pure, stack, loop, loop_dirty in results:
        workloads[workload.split(" ")[1].replace(".aot", "")].append(
            (pure, aot, stack, loop, loop_dirty)
        )

    # Calculate statistics
    statistics = {}
    for workload, times in workloads.items():
        pures, aots, stacks, loops, loop_dirtys = zip(*times)
        statistics[workload] = [
            ("pure", np.median(pures), np.std(pures)),
            ("aot", np.median(aots), np.std(aots)),
            ("stack", np.median(stacks), np.std(stacks)),
            ("loop", np.median(loops), np.std(loops)),
            ("loop_dirty", np.median(loop_dirtys), np.std(loop_dirtys)),
        ]

    fig, ax = plt.subplots(figsize=(20, 10))
    index = np.arange(len(statistics))
    bar_width = 0.7  # Adjusted for visual clarity
    color = {
        "pure": "green",
        "aot": "cyan",
        "stack": "purple",
        "loop": "brown",
        "loop_dirty": "red",
    }
    for i, (workload, stats) in enumerate(statistics.items()):
        sorted_stats = sorted(stats, key=lambda x: -x[1])  # Sort by median time

        for j, (label, median, std) in enumerate(sorted_stats):
            ax.bar(
                index[i],
                median,
                bar_width,
                yerr=std,
                color=color[label],
                capsize=5,
                label=f"{label}" if i == 0 else "",
            )

    ax.set_xticks(index)  # Adjust this based on the number of bars per group
    ax.set_xticklabels(statistics.keys(), fontsize=18)
    ax.set_ylabel("Execution Time (s)")
    plt.tight_layout()
    ax.legend(loc="upper right")

    plt.savefig(file_name)


def plot_loop_counter(results, file_name):
    font = {"size": 18}
    plt.rc("font", **font)
    workloads = defaultdict(list)

    # Simplifying and grouping your data
    for workload, ckptloopcounter1s, ckptloopcounter4s, ckptloopcounter8s, ckptloopcounter16s, ckptloopcounter20s,ckptloopcounter30s,ckptlooppgos,aots,pures in results:
        workloads[workload.split(" ")[1].replace(".aot", "")].append(
            (float(ckptloopcounter1s), float(ckptloopcounter4s), float(ckptloopcounter8s), float(ckptloopcounter16s), float(ckptloopcounter20s),float(ckptloopcounter30s),float(ckptlooppgos),float(aots),float(pures))
        )

    # Calculate statistics
    statistics = {}
    for workload, times in workloads.items():
        ckptloopcounter1s, ckptloopcounter4s, ckptloopcounter8s, ckptloopcounter16s, ckptloopcounter20s,ckptloopcounter30s,ckptlooppgos,aots,pures = zip(*times)
        divisor =np.median(pures)
        ckptloopcounter1s = [x/divisor for x in ckptloopcounter1s]
        ckptloopcounter4s = [x/divisor for x in ckptloopcounter4s]
        ckptloopcounter8s = [x/divisor for x in ckptloopcounter8s]
        ckptloopcounter16s = [x/divisor for x in ckptloopcounter16s]

        ckptloopcounter20s = [x/divisor for x in ckptloopcounter20s]
        ckptloopcounter30s = [x/divisor for x in ckptloopcounter30s]
        ckptlooppgos = [x/divisor for x in ckptlooppgos]
        aots = [x/divisor for x in aots]
        pures = [x/divisor for x in pures]
        statistics[workload] = [
            ("ckptloopcounter1", np.median(ckptloopcounter1s), np.std(ckptloopcounter1s)),
            ("ckptloopcounter4", np.median(ckptloopcounter4s), np.std(ckptloopcounter4s)),
            ("ckptloopcounter8", np.median(ckptloopcounter8s), np.std(ckptloopcounter8s)),
            ("ckptloopcounter16", np.median(ckptloopcounter16s), np.std(ckptloopcounter16s)),
            ("ckptloopcounter20", np.median(ckptloopcounter20s), np.std(ckptloopcounter20s)),
            ("ckptloopcounter30", np.median(ckptloopcounter30s), np.std(ckptloopcounter30s)),
            ("ckptlooppgo", np.median(ckptlooppgos), np.std(ckptlooppgos)),
            ("aot", np.median(aots), np.std(aots)),
            ("pure", np.median(pures), np.std(pures)),
        ]

    fig, ax = plt.subplots(figsize=(20, 10))
    index = np.arange(len(statistics))
    bar_width = 0.7  # Adjusted for visual clarity
    color = [
        "ckptloopcounter1",
        "ckptloopcounter4",
        "ckptloopcounter8",
        "ckptloopcounter16",
        "ckptloopcounter20",
        "ckptloopcounter30",
        "ckptlooppgo",
        "pure",
        "aot",
    ]
    x_ = [1, 4, 8, 16, 20, 30,40, 50,60]
    # x_ = [1<<x for x in x_]
    for i, (workload, stats) in enumerate(statistics.items()):
        # sorted_stats = sorted(stats, key=lambda x: -x[1])  # Sort by median time
        
        x_values = []
        y_values = []
        y_errors = []
        
        for j, (label, median, std) in enumerate(stats):
            x_values.append(x_[j])
            y_values.append(median)
            y_errors.append(std)
        
        ax.errorbar(
            x_values,
            y_values,
            # yerr=y_errors,
            # color=color[workload],
            capsize=5,
            label=workload,
            marker='o',
            linestyle='-',
            linewidth=2,
        )
    ax.set_xticks(x_)  # Adjust this based on the number of bars per group
    # ax.set_xticklabels(statistics.keys(), fontsize=18)
    ax.set_ylabel("Execution Time (s)")
    plt.tight_layout()
    ax.legend(loc="upper right")

    plt.savefig(file_name)


def plot_loop_counter_snapshot(results, file_name):
    font = {"size": 18}
    plt.rc("font", **font)
    workloads = defaultdict(list)

    # Simplifying and grouping your data
    for workload, ckptloopcounter1s, ckptloopcounter4s, ckptloopcounter8s, ckptloopcounter16s, ckptloopcounter20s,ckptloopcounter30s in results:
        workloads[workload.split(" ")[1].replace(".aot", "")].append(
            (float(ckptloopcounter1s), float(ckptloopcounter4s), float(ckptloopcounter8s), float(ckptloopcounter16s), float(ckptloopcounter20s),float(ckptloopcounter30s))
        )

    # Calculate statistics
    statistics = {}
    for workload, times in workloads.items():
        ckptloopcounter1s, ckptloopcounter4s, ckptloopcounter8s, ckptloopcounter16s, ckptloopcounter20s,ckptloopcounter30s = zip(*times)
        divisor =np.median(ckptloopcounter1s)
        ckptloopcounter1s = [x/divisor for x in ckptloopcounter1s]
        ckptloopcounter4s = [x/divisor for x in ckptloopcounter4s]
        ckptloopcounter8s = [x/divisor for x in ckptloopcounter8s]
        ckptloopcounter16s = [x/divisor for x in ckptloopcounter16s]

        ckptloopcounter20s = [x/divisor for x in ckptloopcounter20s]
        ckptloopcounter30s = [x/divisor for x in ckptloopcounter30s]
        statistics[workload] = [
            ("ckptloopcounter1", np.median(ckptloopcounter1s), np.std(ckptloopcounter1s)),
            ("ckptloopcounter4", np.median(ckptloopcounter4s), np.std(ckptloopcounter4s)),
            ("ckptloopcounter8", np.median(ckptloopcounter8s), np.std(ckptloopcounter8s)),
            ("ckptloopcounter16", np.median(ckptloopcounter16s), np.std(ckptloopcounter16s)),
            ("ckptloopcounter20", np.median(ckptloopcounter20s), np.std(ckptloopcounter20s)),
            ("ckptloopcounter30", np.median(ckptloopcounter30s), np.std(ckptloopcounter30s)),
        ]

    fig, ax = plt.subplots(figsize=(20, 10))
    index = np.arange(len(statistics))
    bar_width = 0.7  # Adjusted for visual clarity
    color = [
        "ckptloopcounter1",
        "ckptloopcounter4",
        "ckptloopcounter8",
        "ckptloopcounter16",
        "ckptloopcounter20",
        "ckptloopcounter30",
    ]
    x_ = [1, 4, 8, 16, 20, 30]
    # x_ = [1<<x for x in x_]
    for i, (workload, stats) in enumerate(statistics.items()):
        # sorted_stats = sorted(stats, key=lambda x: -x[1])  # Sort by median time
        
        x_values = []
        y_values = []
        y_errors = []
        
        for j, (label, median, std) in enumerate(stats):
            x_values.append(x_[j])
            y_values.append(median)
            y_errors.append(std)
        
        ax.errorbar(
            x_values,
            y_values,
            # yerr=y_errors,
            # color=color[workload],
            capsize=5,
            label=workload,
            marker='o',
            linestyle='-',
            linewidth=2,
        )
    ax.set_xticks(x_)  # Adjust this based on the number of bars per group
    # ax.set_xticklabels(statistics.keys(), fontsize=18)
    ax.set_ylabel("Snapshot Time (s)")
    plt.tight_layout()
    ax.legend(loc="upper right")

    plt.savefig(file_name)
    
def plot_whole(results, file_name):
    font = {"size": 20}
    plt.rc("font", **font)
    fig, ax = plt.subplots(figsize=(20, 10))

    # Simplifying and grouping your data
    for idx, result in enumerate(results):
        with open(result, "r") as csvfile:
            reader = csv.reader(csvfile)
            next(reader)
            results = []
            for row in reader:
                results.append(
                    (
                        row[0],
                        float(row[1]),
                        float(row[2]),
                        float(row[3]),
                        float(row[5]),
                        float(row[6]),
                    )
                )

        workloads = defaultdict(list)
        for workload, aot,pure, stack, loop, loop_dirty in results:
            workloads[workload.split(" ")[1].replace(".aot", "")].append(
                (pure, aot, stack, loop, loop_dirty)
            )

        # Calculate statistics
        statistics = {}
        for workload, times in workloads.items():
            pures, aots, stacks, loops, loop_dirtys = zip(*times)
            statistics[workload] = [
                ("pure", np.median(pures), np.std(pures)),
                ("aot", np.median(aots), np.std(aots)),
                ("stack", np.median(stacks), np.std(stacks)),
                ("loop", np.median(loops), np.std(loops)),
                ("loop_dirty", np.median(loop_dirtys), np.std(loop_dirtys)),
            ]

        index = np.arange(len(statistics))
        bar_width = 0.7 / 3  # Adjusted for visual clarity
        color = {
            "pure": "green",
            "aot": "cyan",
            "stack": "purple",
            "loop": "brown",
            "loop_dirty": "red",
        }
        for i, (workload, stats) in enumerate(statistics.items()):
            sorted_stats = sorted(stats, key=lambda x: -x[1])  # Sort by median time

            for j, (label, median, std) in enumerate(sorted_stats):
                ax.bar(
                    index[i] + bar_width * idx,
                    median,
                    bar_width,
                    yerr=std,
                    color=color[label],
                    capsize=5,
                    label=f"{label}" if i == 0 and idx == 0 else "",
                )

    ax.set_xticks(
        index + bar_width
    )  # Adjust this based on the number of bars per group
    ax.set_xticklabels(statistics.keys(), fontsize=20)
    ax.set_ylabel("Execution Time (s)")
    plt.tight_layout()
    ax.legend(loc="upper right")

    plt.savefig(file_name)


def get_avg_99percent(data):
    group_size = 1000
    num_groups = len(data) // group_size
    grouped_data = np.reshape(data[: num_groups * group_size], (num_groups, group_size))
    avg_values = np.mean(grouped_data, axis=1)
    percentile99_values = np.percentile(grouped_data, 99, axis=1)

    # Extend the calculated values to match the original size of the data
    avg_extended = np.repeat(avg_values, group_size)
    percentile99_extended = np.repeat(percentile99_values, group_size)

    # Append the remaining data points to the extended arrays
    avg_extended = np.concatenate(
        (avg_extended, [avg_values[-1]] * (len(data) - len(avg_extended)))
    )
    percentile99_extended = np.concatenate(
        (
            percentile99_extended,
            [percentile99_values[-1]] * (len(data) - len(percentile99_extended)),
        )
    )
    return avg_extended, percentile99_extended


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
    "-ckpt-loop-counter.aot",
]
aot_variant_freq = [
    "-ckpt-loop-counter-1.aot",
    "-ckpt-loop-counter-4.aot",
    "-ckpt-loop-counter-8.aot",
    "-ckpt-loop-counter-16.aot",
    "-ckpt-loop-counter-20.aot",
    "-ckpt-loop-counter-30.aot",
    "-ckpt-loop-pgo.aot",
    ".aot",
    "-pure.aot"
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
    cmd = f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} -c 100000"
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
    cmd = f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra}"
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


def run_profile(aot_file: str, arg: list[str], env: str, extra: str = "") -> tuple[str, str]:
    wasm_file = aot_file.replace("aot","wasm")
    cmd = f"./MVVM_profile -w ./bench/{wasm_file} -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra}"
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


def run_checkpoint_restore_burst_overhead(
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
            f"script -f -q /dev/null -c 'ssh -t {burst} ./MVVM_restore -t ./bench/{aot_file} {extra2}' &"
        )
        print(
            f"script -f -q /dev/null -c 'ssh -t {burst} ./MVVM_restore -t ./bench/{aot_file} {extra2}' &"
        )
        os.system("sleep 40")

        os.system(
            f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra1} > ./bench/{aot_file}{i}.log &"
        )
        print(
            f"./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra1} > ./bench/{aot_file}{i}.log &"
        )
        os.system("sleep 20")
        os.system("pkill -SIGINT -f MVVM_checkpoint")
        # os.system(f"ssh {burst} tcpkill -i eno2 port 12346")
        # print(checkpoint_result, restore_result)
        # Return a combined result or just the checkpoint result as needed
        os.system("sleep 60")
        os.system(f"ssh {burst} pkill -f MVVM_restore")
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
    os.system(
        f"ssh -t {slowtier} {pwd}/artifact/run_with_cpu_monitoring_nocommand.sh MVVM_restore &"
    )
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
    os.system(
        f"scp -r {slowtier}:{pwd}/build/MVVM_restore.ps.out ./MVVM_restore.ps.1.out"
    )

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


def exec_with_log(cmd):
    print(cmd)
    os.system(cmd)


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
    extra9: str = "",
):
    # Execute run_checkpoint and capture its result
    res = []
    exec_with_log("rm ./*.out")
    exec_with_log("sudo pkill MVVM_checkpoint")
    exec_with_log("sudo pkill MVVM_restore")
    exec_with_log(f"ssh -t {burst} pkill MVVM_checkpoint")
    exec_with_log(f"ssh -t {burst} pkill MVVM_restore")
    exec_with_log(f"ssh -t {burst} rm {pwd_mac}/build/*.out")
    # Execute run_restore with the same arguments (or modify as needed)
    exec_with_log(
        f"script -f -q /dev/null -c 'ssh -t {burst} ./MVVM_restore -t ./bench/{aot_file1} {extra2}' >> MVVM_restore.0.out &"
    )
    exec_with_log(
        f"ssh -t {burst} {pwd_mac}/artifact/run_with_energy_monitoring_mac.sh MVVM_restore 0 {aot_file} &"
    )

    exec_with_log(
        f"script -f -q /dev/null -c './MVVM_restore -t ./bench/{aot_file1} {extra3}' >> MVVM_restore.1.out &"
    )
    exec_with_log(
        f"sudo ../artifact/run_with_energy_monitoring.sh MVVM_restore 1 {aot_file} &"
    )
    exec_with_log(
        f"script -f -q /dev/null -c './MVVM_restore -t ./bench/{aot_file} {extra7}' >> MVVM_restore.4.out &"
    )
    exec_with_log(
        f"sudo ../artifact/run_with_energy_monitoring.sh MVVM_restore 4 {aot_file1} &"
    )
    exec_with_log("sleep 100")

    exec_with_log(
        f"./MVVM_checkpoint -t ./bench/{aot_file1} {' '.join(['-a ' + str(x) for x in arg1])} -e {env} {extra1} > MVVM_checkpoint.0.out &"
    )
    exec_with_log(
        f"sudo ../artifact/run_with_energy_monitoring.sh MVVM_checkpoint 0 {aot_file} &"
    )
    
    exec_with_log("sleep 640")

    exec_with_log(
        f"script -f -q /dev/null -c 'ssh -t {burst}  ./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra6}' > MVVM_checkpoint.1.out &"
    )
    exec_with_log(
        f"ssh -t {burst} {pwd_mac}/artifact/run_with_energy_monitoring_mac.sh MVVM_checkpoint 1 {aot_file1} &"
    )
    # exec_with_log(f"ssh -t mac ../artifact/run_with_energy_monitoring_mac.sh MVVM_checkpoint 1 {aot_file} &")
    exec_with_log(f"pkill -SIGINT MVVM_checkpoint")
    sleep(1)
    exec_with_log(f"pkill -SIGINT MVVM_checkpoint")

    exec_with_log("sleep 50")

    exec_with_log(f"ssh -t {burst} pkill -SIGINT MVVM_restore")
    exec_with_log(f"ssh -t {burst} pkill -SIGINT MVVM_checkpoint")
    exec_with_log(
        f"script -f -q /dev/null -c 'ssh -t {burst} ./MVVM_restore -t ./bench/{aot_file1} {extra4}' >> MVVM_restore.2.out &"
    )
    exec_with_log(
        f"ssh -t {burst} {pwd_mac}/artifact/run_with_energy_monitoring_mac.sh MVVM_restore 2 {aot_file} &"
    )
    exec_with_log(
        f"script -f -q /dev/null -c 'ssh -t {burst} ./MVVM_restore -t ./bench/{aot_file} {extra8}' >> MVVM_restore.5.out &"
    )
    exec_with_log(
        f"ssh -t {burst} {pwd_mac}/artifact/run_with_energy_monitoring_mac.sh MVVM_restore 5 {aot_file1} &"
    )
    exec_with_log("sleep 50")
    exec_with_log(f"pkill -SIGINT MVVM_restore")
    exec_with_log(
        f"script -f -q /dev/null -c './MVVM_restore -t ./bench/{aot_file} {extra9}' >> MVVM_restore.6.out &"
    )
    exec_with_log(
    f"sudo ../artifact/run_with_energy_monitoring.sh MVVM_restore 6 {aot_file1} &"
    )
    exec_with_log(
        f"script -f -q /dev/null -c './MVVM_restore -t ./bench/{aot_file1} {extra5}' >> MVVM_restore.3.out &"
    )
    exec_with_log(
        f"sudo ../artifact/run_with_energy_monitoring.sh MVVM_restore 3 {aot_file} &"
    )
    # Return a combined result or just the checkpoint result as needed

    exec_with_log("sleep 50")
    exec_with_log(f"ssh -t {burst} pkill -SIGINT MVVM_restore")
    exec_with_log(f"sleep 1000")
    exec_with_log(f"scp {burst}:{pwd_mac}/build/*.*.out ./")
    cmd = f"cat ./MVVM_checkpoint.0.out ./MVVM_checkpoint.1.out ./MVVM_restore.0.out ./MVVM_restore.1.out ./MVVM_restore.2.out ./MVVM_restore.3.out ./MVVM_restore.4.out ./MVVM_restore.5.out ./MVVM_restore.6.out"
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


def run_burst(
    aot_file: str, arg: list[str], env: str, extra: str = ""
) -> tuple[str, str]:
    os.system(
        f"script -q /dev/null -c 'ssh -t {burst} ./MVVM_checkpoint -t ./bench/{aot_file} {' '.join(['-a ' + str(x) for x in arg])} -e {env} {extra}' > burst.out"
    )
    cmd = "cat burst.out"
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
    plot_whole(["policy.csv", "policy_multithread.csv", "policy_mac.csv"], "policy.pdf")
