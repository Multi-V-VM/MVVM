# for thread cound to 1, 2, 4, 8, 16
import os
import common_util
from collections import defaultdict
import numpy as np
from matplotlib import pyplot as plt
from multiprocessing import Pool

cmd = [
    "linpack",
    "llama","llama",
    "bt","bt","bt",
    "cg","cg","cg",
    "ep","ep","ep",
    "ft","ft","ft",
    "lu","lu","lu",
    "mg","mg","mg",
    "sp","sp","sp",
    "redis",
    "hdastar",
    "hdastar",
    "hdastar",
]
arg = [
    [],
    ["stories15M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["stories15M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],
    ["maze-6404.txt", "2"],
    ["maze-6404.txt", "4"],
    ["maze-6404.txt", "8"],
]
envs = [ "a=b",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "a=b","a=b","a=b","a=b",
        ]


pool = Pool(processes=10)
results = []


def run_mvvm():
    global results
    results1 = []

    for i, c in enumerate(cmd):
        for j in range(len(common_util.aot_variant)):
            aot = cmd[i] + common_util.aot_variant[j]
            results1.append(
                pool.apply_async(
                    common_util.run_checkpoint_restore,
                    (aot, arg[i], envs[i]),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            for line in lines:
                if line.__contains__("Snapshot time:"):
                    time = line.split(" ")[-2]
                    snapshot_time = float(time)
                if line.__contains__("Recover time:"):
                    time = line.split(" ")[-1]
                    recover_time = float(time)

            results += [(exec, snapshot_time, recover_time)]
    # print the results
    # results += results1


def run_criu():
    global results
    results1 = []
    for i in range(len(cmd)):
        aot = cmd[i]
        results1.append(
            pool.apply_async(
                common_util.run_criu_checkpoint_restore,
                (aot, arg[i], "OMP_NUM_THREADS=1"),
            )
        )
    # print the results
    results += [x.get() for x in results1]


def run_qemu():
    global results
    results1 = []

    for i in range(len(cmd)):
        aot = cmd[i]
        results1.append(
            pool.apply_async(
                common_util.run_qemu_checkpoint_checkpoint,
                (aot, arg[i], "OMP_NUM_THREADS=1"),
            )
        )
    # print the results
    results += [x.get() for x in results1]


# print the results
def plot(result):
    workloads = defaultdict(list)
    for workload, snapshot, recovery in result:
        workloads[workload.split("1")[0]].append((snapshot, recovery))

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        snapshots, recoveries = zip(*times)
        statistics[workload] = {
            "snapshot_median": np.median(snapshots),
            "recovery_median": np.median(recoveries),
            "snapshot_std": np.std(snapshots),
            "recovery_std": np.std(recoveries),
        }

    # Plotting
    fig, ax = plt.subplots()

    # Define the bar width and positions
    bar_width = 0.35
    index = np.arange(len(statistics))

    # Plot the bars for each workload
    # for i, (workload, stats) in enumerate(statistics.items()):
    #     ax.bar(index[i], stats['snapshot_median'], bar_width, yerr=stats['snapshot_std'], capsize=5, label=f'Snapshot')
    #     ax.bar(index[i] + bar_width, stats['recovery_median'], bar_width, yerr=stats['recovery_std'], capsize=5, label=f'Recovery')
    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["snapshot_median"],
            bar_width,
            yerr=stats["snapshot_std"],
            capsize=5,
            color="blue",
            label="Snapshot" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["recovery_median"],
            bar_width,
            yerr=stats["recovery_std"],
            capsize=5,
            color="red",
            label="Recovery" if i == 0 else "",
        )

    # Labeling and formatting
    ax.set_xlabel("Workload")
    ax.set_ylabel("Time")
    ax.set_title("Median and Variation of Snapshot and Recovery Times by Workload")
    ax.set_xticks(index + bar_width / 2)
    ax.set_xticklabels(statistics.keys())
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig("ckpt_restore_latency.pdf")
    # %%


if __name__ == "__main__":
    run_mvvm()
    print(results)
    print(len(arg),len(cmd),len(envs))
    # results = [('a=b linpack.aot', 4.147552, 2.550483), ('a=b linpack.aot', 4.164721, 2.58253), ('a=b llama.aot stories15M.bin -z tokenizer.bin -t 0.0', 0.238909, 2.58253), ('a=b llama.aot stories15M.bin -z tokenizer.bin -t 0.0', 0.238602, 2.58253)]
    plot(results)
    # run_criu()
    # run_qemu()
