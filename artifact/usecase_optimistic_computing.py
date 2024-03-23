import csv
import common_util
from common_util import parse_time, parse_time_no_msec,get_avg_99percent
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict
import sys

ip = ["128.114.53.32", "128.114.59.234"]
port = 12346
new_port = 12353

cmd = [
    "bc",  # low priority task
    "bfs",  # high priority task
]
folder = [
    "gapbs",
    "gapbs",
]
arg = [
    ["-g10", "-n100000"],
    ["-g10", "-n1000000"],
]
envs = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
]

pool = Pool(processes=1)


def get_fasttier_result():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(pool.apply_async(common_util.run, (aot, arg[i], envs[i])))
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def get_slowtier_result():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(
                pool.apply_async(
                    common_util.run_slowtier,
                    (aot, arg[i], envs[i]),
                )
            )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def get_snapshot_overhead():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1 = common_util.run_checkpoint_restore_slowtier_overhead(
                aot,
                arg[i],
                envs[i],
                f"-o {ip[1]} -s {port}",
                f"-i {ip[1]} -e {port}",
            )

            for exec, output in results1:
                lines = output.split("\n")
                print(lines)
                for line in lines:
                    if line.__contains__("Snapshot time:"):
                        exec_time = line.split(" ")[-2]
                        print(exec, exec_time)
                results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def get_optimiztic_compute_overhead():
    results = []
    results1 = []
    aot = cmd[0] + ".aot"
    aot1 = cmd[1] + ".aot"
    results1 = common_util.run_checkpoint_restore_slowtier(
        aot,
        arg[0],
        aot1,
        arg[1],
        envs[0],
        f"-o {ip[1]} -s {port}",
        f"-i {ip[1]} -e {port} -o {ip[0]} -s {new_port}",
        f"-i {ip[0]} -e {new_port}",
    )
    return results1


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]
    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            [
                "name",
                "fasttier",
                "slowtier",
                "snapshot Time",
            ]
        )

        # Write the data
        for idx, row in enumerate(fasttier):
            writer.writerow([row[0], row[1], slowtier[idx][1], snapshot[idx][1]])


def read_from_csv(filename):
    results = []
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        for idx, row in enumerate(reader):
            if idx == 0:
                continue
            results.append(row)
    return results


def plot(file_name):
    workloads = defaultdict(list)
    for workload, fasttier, slowtier, snapshot in results:
        workloads[
            workload.replace("OMP_NUM_THREADS=", "")
            .replace("-g15", "")
            .replace("-n300", "")
            .replace(" -f ", "")
            .replace("-vn300", "")
            .replace("maze-6404.txt", "")
            .replace("stories110M.bin", "")
            .replace("-z tokenizer.bin -t 0.0", "")
            .replace("ORBvoc.txt", "")
            .replace("TUM3.yaml", "")
            .replace("./associations/fr1_xyz.txt", "")
            .replace("./", "")
            .strip()
        ].append((fasttier, slowtier, snapshot))
    # print(workloads)
    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        fasttiers, slowtier, snapshots = zip(*times)
        fasttiers = np.array(fasttiers).astype(float)
        slowtier = np.array(slowtier).astype(float)
        snapshots = np.array(snapshots).astype(float)
        statistics[workload] = {
            "fasttier_median": np.median(fasttiers),
            "snapshot_median": np.median(snapshots),
            "slowtier_median": np.median(slowtier),
            "fasttier_std": np.std(fasttiers),
            "snapshot_std": np.std(snapshots),
            "slowtier_std": np.std(slowtier),
        }
    font = {"size": 14}

    # using rc function
    plt.rc("font", **font)
    # Plotting
    fig, ax = plt.subplots(figsize=(15, 7))
    # Define the bar width and positions
    bar_width = 0.7 / 3
    index = np.arange(len(statistics))

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["fasttier_median"],
            bar_width,
            yerr=stats["fasttier_std"],
            capsize=5,
            color="blue",
            label="fasttier" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["slowtier_median"],
            bar_width,
            yerr=stats["slowtier_std"],
            capsize=5,
            color="green",
            label="slowtier" if i == 0 else "",
        )
        ax.bar(
            index[i] + 2*bar_width,
            stats["snapshot_median"],
            bar_width,
            yerr=stats["snapshot_std"],
            capsize=5,
            color="red",
            label="snapshot" if i == 0 else "",
        )
    # Labeling and formatting
    ax.set_ylabel("Time(s)")
    ax.set_xticks(index + bar_width)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, fontsize=10)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig("optimistic_computing.pdf")
    # %%



def plot_time(reu):
    # get from reu
    # start time -> end time -> start time
    reu = reu.split("\\n")
    state = 0
    time = []
    exec_time = [[], [], [], []]
    for line in reu:
        try:
            if line.__contains__("Trial"):
                to_append = float(line.split(" ")[-1].replace("\\r", ""))
                if to_append < 0.001 and to_append > 0:
                    exec_time[state].append(to_append)
                # print(exec_time)
                # print("exec_time ",exec_time[-1])
            if line.__contains__("Snapshot ") or line.__contains__("Execution "):
                time.append(parse_time(line.split(" ")[1].replace("]", "")))
                print("time ", time)
                state += 1
        except:
            print(line)
    # print(exec_time)

    # print(time)
    # record time
    fig, ax = plt.subplots()
    base = time[0] - sum(exec_time[0])
    time_spots2 = [time[1] - sum(exec_time[1]) - base]
    for i in exec_time[1]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots2[-1] + i
        # Append the new time spot to the sequence
        time_spots2.append(new_time_spot)
    time_spots2.pop(0)

    time_spots = [time[0] - sum(exec_time[0]) - base]
    for i in exec_time[0]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots[-1] + i
        # Append the new time spot to the sequence
        time_spots.append(new_time_spot)
    time_spots.pop(0)
    to_pop = len(time_spots)
    time_spots.append(time[2] - sum(exec_time[2]) - base)

    for i in exec_time[2]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots[-1] + i
        # Append the new time spot to the sequence
        time_spots.append(new_time_spot)
    time_spots.pop(to_pop - 1)

    to_pop = len(time_spots)
    time_spots.append(time[3] - sum(exec_time[3]) - base)
    for i in exec_time[3]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots[-1] + i
        # Append the new time spot to the sequence
        time_spots.append(new_time_spot)
    time_spots.pop(to_pop - 1)
    print(time[3] - sum(exec_time[3]))
    ax.plot(time_spots, exec_time[0] + exec_time[2] + exec_time[3], "blue")
    ax.plot(time_spots2, exec_time[1], "r")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Average Trial Time (s)")
    plt.savefig("optimistic.pdf")


def plot_time(reu, checkpoint, checkpoint1, restore, restore1):
    # get from reu
    # start time -> end time -> start time
    reu = reu.split("\\n")
    state = 0
    time = []
    exec_time = [[], [], [], []]
    for line in reu:
        try:
            if line.__contains__("Trial"):
                to_append = float(line.split(" ")[-1].replace("\\r", ""))
                if to_append < 0.001 and to_append > 0:
                    exec_time[state].append(to_append)
                # print(exec_time)
                # print("exec_time ",exec_time[-1])
            if line.__contains__("Snapshot ") or line.__contains__("Execution "):
                time.append(parse_time(line.split(" ")[1].replace("]", "")))
                print("time ", time)
                state += 1
        except:
            print(line)
    # print(exec_time)
    # print(time)
    # record time
    fig, ax = plt.subplots()
    base = time[1] - sum(exec_time[1])

    time_spots2 = [time[0] - sum(exec_time[0]) - base]

    for i in exec_time[0]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots2[-1] + i
        # Append the new time spot to the sequence
        time_spots2.append(new_time_spot)
    time_spots2.pop(0)

    time_spots = [time[1] - sum(exec_time[1]) - base]
    for i in exec_time[1]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots[-1] + i
        # Append the new time spot to the sequence
        time_spots.append(new_time_spot)
    time_spots.pop(0)
    to_pop = len(time_spots)
    time_spots.append(time[2] - sum(exec_time[2]) - base)
    for i in exec_time[2]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots[-1] + i
        # Append the new time spot to the sequence
        time_spots.append(new_time_spot)
    time_spots.pop(len(time_spots) - 1)

    to_pop = len(time_spots)
    time_spots.append(time[3] - sum(exec_time[3]) - base)
    for i in exec_time[3]:
        # Add the current increment to the last time spot
        new_time_spot = time_spots[-1] + i
        # Append the new time spot to the sequence
        time_spots.append(new_time_spot)
    time_spots.pop(to_pop - 1)
    avg_extended, percentile99_extended = get_avg_99percent(exec_time[1] + exec_time[2] + exec_time[3])
    avg_exec_time1, percentile_99_exec_time1 = get_avg_99percent(exec_time[0])
    ax.plot(time_spots, avg_extended, "blue")
    ax.plot(time_spots, percentile99_extended, color="purple", linestyle="--")
    ax.plot(time_spots2, avg_exec_time1, "r")
    ax.plot(time_spots2, percentile_99_exec_time1, color="pink", linestyle="--")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Average Trial Time (s)")
    plt.savefig("optimistic.pdf")

    cpu = []
    memory = []
    exec_time_checkpoint = []
    cpu1 = []
    memory1 = []
    exec_time_checkpoint1 = []
    checkpoint = checkpoint.split("\n")
    checkpoint1 = checkpoint1.split("\n")
    restore = restore.split("\n")
    restore1 = restore1.split("\n")

    for line in checkpoint1:
        try:
            if line.__contains__("2024"):
                exec_time_checkpoint1.append(parse_time_no_msec(line.split(" ")[3]))
                cpu1.append(0)
                memory1.append(0)
                # print(exec_time)
                # print("exec_time ",exec_time[-1])
            else:
                if float(line.split(" ")[2]) > 10:
                    cpu1.append(float(line.split(" ")[2]))
                    memory1.append(float(line.split(" ")[5]))
                    exec_time_checkpoint1.append(exec_time_checkpoint1[-1] + 0.5)
                else:
                    exec_time_checkpoint[-1] = exec_time_checkpoint[-1] + 0.5
        except:
            print(line)
    for line in checkpoint:
        try:
            if line.__contains__("2024"):
                exec_time_checkpoint.append(
                    parse_time_no_msec(line.split(" ")[3])
                )
                cpu.append(0)
                memory.append(0)
                # print(exec_time)
                # print("exec_time ",exec_time[-1])
            else:
                if float(line.split(" ")[2]) > 10:
                    cpu.append(float(line.split(" ")[2]))
                    memory.append(float(line.split(" ")[5]))
                    exec_time_checkpoint.append(exec_time_checkpoint[-1] + 0.5)
                else:
                    exec_time_checkpoint[-1] = exec_time_checkpoint[-1] + 0.5
        except:
            print(line)
    print(len(exec_time_checkpoint), len(cpu))
    for line in restore1:
        try:
            if line.__contains__("2024"):
                exec_time_checkpoint.append(parse_time_no_msec(line.split(" ")[3]))
                cpu.append(0)
                memory.append(0)
                # print(exec_time)
                # print("exec_time ",exec_time[-1])
            else:
                if float(line.split(" ")[2]) > 10:
                    cpu.append(float(line.split(" ")[2]))
                    memory.append(float(line.split(" ")[5]))
                    exec_time_checkpoint.append(exec_time_checkpoint[-1] + 0.5)
                else:
                    exec_time_checkpoint[-1] = exec_time_checkpoint[-1] + 0.5
        except:
            print(line)
    print(exec_time_checkpoint)
    for line in restore:
        try:
            if line.__contains__("2024"):
                exec_time_checkpoint.append(parse_time_no_msec(line.split(" ")[3]))
                cpu.append(0)
                memory.append(0)
                # print(exec_time)
                # print("exec_time ",exec_time[-1])
            else:
                if float(line.split(" ")[2]) > 10:
                    cpu.append(float(line.split(" ")[2]))
                    memory.append(float(line.split(" ")[5]))
                    exec_time_checkpoint.append(exec_time_checkpoint[-1] + 0.5)
                else:
                    exec_time_checkpoint[-1] = exec_time_checkpoint[-1] + 0.5
        except:
            print(line)
    print(len(exec_time_checkpoint), len(cpu))

    fig, ax = plt.subplots()

    ax.plot(exec_time_checkpoint, cpu, "b")
    ax.plot(exec_time_checkpoint1, cpu1, "r")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Average CPU (percentage)")
    plt.savefig("optimistic_cpu.pdf")
    fig, ax = plt.subplots()
    ax.plot(exec_time_checkpoint, memory, "b")
    ax.plot(exec_time_checkpoint1, memory1, "r")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Average Memory (B)")
    plt.savefig("optimistic_memory.pdf")


if __name__ == "__main__":
    # fasttier = get_fasttier_result()
    # slowtier = get_slowtier_result()
    # snapshot = get_snapshot_overhead()
    # print("fasttier = ", fasttier)
    # print("slowtier = ", slowtier)
    # print("snapshot = ", snapshot)
    # # plot skew
    # write_to_csv("optimisitc_computing.csv")

    # results = read_from_csv("optimistic_computing.csv")

    # plot(results)
    # reu = get_optimiztic_compute_overhead()
    # with open("optimistic.txt", "w") as f:
    #     f.write(str(reu))
    reu = ""
    with open("optimistic.txt", "r") as f:
        reu = f.read()
    with open("MVVM_checkpoint.ps.1.out") as f:
        checkpoint1 = f.read()
    with open("MVVM_checkpoint.ps.out") as f:
        checkpoint = f.read()
    with open("MVVM_restore.ps.1.out") as f:
        restore1 = f.read()
    with open("MVVM_restore.ps.out") as f:
        restore = f.read()
    # print(reu)
    plot_time(reu, checkpoint, checkpoint1, restore, restore1)
