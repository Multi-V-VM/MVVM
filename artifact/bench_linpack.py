import numpy as np
import common_util
from multiprocessing import Pool
import csv
from collections import defaultdict
from matplotlib import pyplot as plt

cmd = [
    "linpack",
]
arg = [
    [],
]


pool = Pool(processes=40)

mvvm_results = []
qemu_x86_64_results = []
qemu_aarch64_results = []
hcontainer_results = []
native_results = []


def run_mvvm():
    global results
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            for j in range(len(common_util.aot_variant)):
                for env in common_util.list_of_arg:
                    aot = cmd[i] + common_util.aot_variant[j]
                    results1.append(
                        pool.apply_async(common_util.run, (aot, arg[i], env))
                    )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results:
        print(exec)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-1]
            print(exec, exec_time)
    native_results.append((exec, exec_time))

def run_hcontainer():
    global results
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_hcontainer,
                    (aot, "linpack", arg[i], "OMP_NUM_THREADS=1"),
                )
            )
    # print the results
    results += [x.get() for x in results1]


def run_qemu_x86_64():
    global results
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_qemu_x86_64,
                    (aot, "linpack", arg[i], "OMP_NUM_THREADS=1"),
                )
            )
    # print the results
    results += [x.get() for x in results1]


def run_qemu_aarch64():
    global results
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_qemu_aarch64,
                    (aot, "linpack", arg[i], "OMP_NUM_THREADS=1"),
                )
            )
    # print the results
    results += [x.get() for x in results1]


def run_native():
    global results
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_native,
                    (aot, "linpack", arg[i], "OMP_NUM_THREADS=1"),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results:
        print(exec)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("real"):
                exec_time = line.split(" ")[-1]
    native_results.append((exec, exec_time))
    # print the results


def write_to_csv(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            ["name", "mvvm", "hcontainer", "qemu_x86_64", "qemu_aach64", "native"]
        )

        # Write the data
        for row in data:
            writer.writerow(row)


# print the results
def plot(result):
    workloads = defaultdict(list)
    for workload, snapshot, recovery in result:
        workloads[workload].append((snapshot, recovery))

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
    run_native()
    run_qemu_x86_64()
    # print the results
    run_qemu_aarch64()
    run_hcontainer()

    write_to_csv(results, "linpack_result.csv")
