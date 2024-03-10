import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict

ip = ["128.114.53.32", "192.168.0.1"]
port = 1234
new_port = 1235
cmd = [
    "redis",  # low priority task
    "rgbd_tum",  # high priority task
]
folder = [
    "redis",
    "ORB_SLAM2",  # networkbound?
]
arg = [
    [],
    ["./ORBvoc.txt", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
]
envs = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
]

pool = Pool(processes=20)


def get_cloud_result():
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


def get_edge_result():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(
                pool.apply_async(
                    common_util.run_slowtier,
                    (
                        aot,
                        arg[i],
                        envs[i],
                        f"-i {ip[0]} -e {port}",
                        f"-o {ip[1]} -s {port}",
                    ),
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


def get_snapshot_overhead():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(
                pool.apply_async(
                    common_util.run_checkpoint_restore_slowtier,
                    (aot, arg[i], envs[i]),
                    f"-o {ip[0]} -s {port}",
                    f"-i {ip[1]} -e {port} -o {ip[0]} -s {new_port}",
                    f"-i {ip[1]} -e {new_port}",
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Snapshot time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant


def get_burst_compute():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(
                pool.apply_async(
                    common_util.run_checkpoint_restore_burst,
                    (
                        aot,
                        arg[i],
                        envs[i],
                        f"-o {ip[0]} -s {port}",
                        f"-i {ip[1]} -e {port} -o {ip[0]} -s {new_port}",
                        f"-i {ip[1]} -e {new_port}",
                    ),  # energy
                )
            )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Snapshot time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant


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
            writer.writerow([row[0], row[1], slowtier[1], snapshot[1]])


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

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        fasttiers, slowtier, snapshots = zip(*times)
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
    bar_width = 0.7 / 2
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
            index[i] + bar_width,
            stats["snapshot_median"],
            bar_width,
            yerr=stats["snapshot_std"],
            capsize=5,
            color="green",
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
    plt.savefig(file_name)
    # %%


def plog_time():
    pass


if __name__ == "__main__":
    fasttier = get_cloud_result()
    slowtier = get_edge_result()
    snapshot = get_snapshot_overhead()
    reu = get_burst_compute()
    # plot skew
    write_to_csv("burst_computing.csv")

    results = read_from_csv("burst_computing.csv")

    plot(results)
    plog_time(results)
