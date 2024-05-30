# for thread cound to 1, 2, 4, 8, 16
import csv
import common_util
from common_util import calculate_averages_latency, calculate_averages_size
from collections import defaultdict
import numpy as np
from matplotlib import pyplot as plt
from multiprocessing import Pool

cmd = [
    "linpack",
    "llama",
    "rgbd_tum",
    "bfs",
    "bc",
    "bfs",
    "cc",
    "cc_sv",
    "pr",
    "pr_spmv",
    "sssp",
    "tc",
    "bt",
    "cg",
    # "ep",
    "ft",
    # "lu",
    "mg",
    # "sp",
    "redis",
    "hdastar",
]
folder = [
    "linpack",
    "llama",
    "ORB_SLAM2",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "nas",
    "nas",
    # "nas",
    # "nas",
    "nas",
    "nas",
    # "nas",
    "redis",
    "hdastar",
]
arg = [
    [],
    ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["./ORBvoc.txt,", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
    ["-f", "./road.sg", "-n300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-f", "./road.sg", "-n300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-n300"],
    [],
    [],
    [],
    [],
    # [],
    # [],
    # [],
    [],
    ["maze-6404.txt", "8"],
]
envs = [
    "a=b",
    "OMP_NUM_THREADS=4",
    "a=b",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    # "OMP_NUM_THREADS=4",
    # "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    # "OMP_NUM_THREADS=4",
    "a=b",
    "a=b",
]


pool = Pool(processes=10)


def run_mvvm():
    results = []
    results1 = []

    for i, c in enumerate(cmd):
        aot = cmd[i] + common_util.aot_variant[0]
        results1.append(
            pool.apply_async(
                common_util.run_checkpoint_restore,
                (aot, arg[i], envs[i]),
            )
        )
    results1 = [x.get() for x in results1]
    write_to_csv(results1, "ckpt_restore_latency_raw.csv")
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            snapshot_memory = "0"
            snapshot_time = 0.0
            recover_time = 0.0
            for line in lines:
                if line.__contains__("Snapshot time:"):
                    time = line.split(" ")[-2]
                    snapshot_time = float(time)
                if line.__contains__("root root"):
                    snapshot_memory = int(
                        eval(
                            line.split(" ")[4]
                            .replace("K", "*1000")
                            .replace("M", "*1000000")
                            .replace("G", "*1000000000")
                            .replace("B", "")
                        )
                    )
                if line.__contains__("Recover time:"):
                    time = line.split(" ")[-1]
                    recover_time = float(time)
            results += [(exec, snapshot_time, recover_time, snapshot_memory)]
    return results


def run_criu():
    results = []
    results1 = []
    for i in range(len(cmd)):
        aot = cmd[i]
        results1.append(
            pool.apply_async(
                common_util.run_criu_checkpoint_restore,
                (aot, folder[i], arg[i], envs[i]),
            )
        )
    # print the results
    results1 = [x.get() for x in results1]
    write_to_csv_raw(results1, "ckpt_restore_latency_criu_raw.csv")
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            snapshot_time = 0.0
            recover_time = 0.0
            for line in lines:
                if line.__contains__("Dumping finished successfully"):
                    time = line.split(" ")[0].replace("(", "").replace(")", "")
                    snapshot_time = float(time)
                if line.__contains__("/tmp/"):
                    print(line)
                    snapshot_memory = int(
                        eval(
                            line.split("/tmp")[0]
                            .replace("K", "*1000")
                            .replace("M", "*1000000")
                            .replace("G", "*1000000000")
                            .replace("B", "")
                        )
                    )
                if line.__contains__(
                    "Restore finished successfully."
                ) or line.__contains__("Restoring FAILED."):
                    time = line.split(" ")[0].replace("(", "").replace(")", "")
                    recover_time = float(time)
            # print(output[o])
            results += [(exec, snapshot_time, recover_time, snapshot_memory)]
    return results


def run_qemu():
    results = []
    results1 = []

    # for i in range(len(cmd)):
    #     aot = cmd[i]
    #     results1.append(
    #         pool.apply_async(
    #             common_util.run_qemu_checkpoint_restore,
    #             (aot, folder[i], arg[i], envs[i]),
    #         )
    #     )
    # # print the results
    # results1 = [x.get() for x in results1]
    # write_to_csv_raw(results1, "ckpt_restore_latency_qemu_raw.csv")
    results1 = read_from_csv_raw("ckpt_restore_latency_qemu_raw.csv")
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            for line in lines:
                if line.__contains__("total time: "):
                    time = line.split(" ")[-2]
                    snapshot_time = float(time) / 1000
                if line.__contains__("downtime: "):
                    time = line.split(" ")[-2]
                    downtime = float(time) / 1000
            print(exec, snapshot_time, downtime)
            results += [(exec,snapshot_time,downtime)]
    return results


def write_to_csv_raw(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        csvfile.write("name, output\n")
        # Optionally write headers

        # Write the data
        csvfile.write(str(data))

def read_from_csv_raw(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results =list(row[0])
        # print(results)
        return results

def write_to_csv_profile(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(["name", "snapshot time(s)", "recovery time(s)", "file size"])

        # Write the data
        for row in data:
            writer.writerow(row)


def write_to_csv(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(["name", "snapshot time(s)", "recovery time(s)"])

        # Write the data
        for row in data:
            writer.writerow(row)


def read_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append((row[0], float(row[1]), float(row[2])))
        return results


def read_size_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append((row[0], float(row[3])))
        return results


# print the results
def plot_qemu(result, file_name="ckpt_restore_latency_qemu.pdf"):
    workloads = defaultdict(list)
    for workload, Total, Down in result:
        if Total != 0 or Down != 0:
            workloads[
                workload.replace("OMP_NUM_THREADS=", "")
                .replace("-g20", "")
                .replace("-n300", "")
                .replace(" -f ", "")
                .replace("-vn300", "")
                .replace("maze-6404.txt", "")
                .replace("stories110M.bin", "")
                .replace("-z tokenizer.bin -t 0.0", "")
                .strip()
            ].append((Total, Down))

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        Totals, recoveries = zip(*times)
        statistics[workload] = {
            "Total_median": np.median(Totals),
            "Down_median": np.median(recoveries),
            "Total_std": np.std(Totals),
            "Down_std": np.std(recoveries),
        }

    # Plotting
    fig, ax = plt.subplots(figsize=(15, 7))
    # Define the bar width and positions
    bar_width = 0.35
    index = np.arange(len(statistics))

    # Plot the bars for each workload
    # for i, (workload, stats) in enumerate(statistics.items()):
    #     ax.bar(index[i], stats['Total_median'], bar_width, yerr=stats['Total_std'], capsize=5, label=f'Total')
    #     ax.bar(index[i] + bar_width, stats['Down_median'], bar_width, yerr=stats['Down_std'], capsize=5, label=f'Down')
    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["Total_median"],
            bar_width,
            yerr=stats["Total_std"],
            capsize=5,
            color="blue",
            label="Total" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["Down_median"],
            bar_width,
            yerr=stats["Down_std"],
            capsize=5,
            color="red",
            label="Down" if i == 0 else "",
        )

    # Labeling and formatting
    ax.set_xlabel("Workload")
    ax.set_ylabel("Time")
    ax.set_title("Median and Variation of Total and Down Times by Workload")
    ax.set_xticks(index + bar_width / 2)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, rotation=45)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig(file_name)


# print the results
def plot(result, file_name="ckpt_restore_latency.pdf"):
    workloads = defaultdict(list)
    for workload, snapshot, recovery in result:
        workloads[
            workload.replace("OMP_NUM_THREADS=", "")
            .replace("-g15", "")
            .replace("-n300", "")
            .replace(" -f ", "")
            .replace("-vn300", "")
            .replace("maze-6404.txt", "")
            .replace("stories110M.bin", "")
            .replace("-z tokenizer.bin -t 0.0", "")
            .strip()
        ].append((snapshot, recovery))

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
    fig, ax = plt.subplots(figsize=(20, 10))
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
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, rotation=45)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig(file_name)
    # %%


# print the results
def plot_whole(
    result_mvvm, result_criu, result_qemu, file_name="ckpt_restore_latency_whole.pdf"
):
    font = {"size": 25}

    plt.rc("font", **font)
    workloads = defaultdict(list)
    for workload, snapshot, recovery in result_mvvm:
        workloads[workload.split(" ")[1].replace(".aot", "")].append(
            (snapshot, recovery)
        )
    workloads_criu = defaultdict(list)
    for workload, snapshot, recovery in result_criu:
        workloads_criu[workload.split(" ")[1].replace(".aot", "")].append(
            (snapshot, recovery)
        )

    workloads_qemu = defaultdict(list)
    for workload, snapshot, recovery in result_qemu:
        workloads_qemu[workload.split(" ")[1].replace(".aot", "")].append(
            (snapshot, recovery)
        )

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        snapshots, recoveries = zip(*times)
        print(workload, workloads_criu[workload])
        snapshots_criu, recoveries_criu = zip(*workloads_criu[workload])
        snapshots_qemu, recoveries_qemu = zip(*workloads_qemu[workload])
        statistics[workload] = {
            "snapshot_median": np.median(snapshots),
            "recovery_median": np.median(recoveries),
            "snapshot_std": np.std(snapshots),
            "recovery_std": np.std(recoveries),
            "criu_snapshot_median": np.median(snapshots_criu),
            "criu_recovery_median": np.median(recoveries_criu),
            "criu_snapshot_std": np.std(snapshots_criu),
            "criu_recovery_std": np.std(recoveries_criu),
            "qemu_snapshot_median": np.median(snapshots_qemu),
            "qemu_snapshot_std": np.std(snapshots_qemu),
        }

    # Plotting
    fig, ax = plt.subplots(figsize=(20, 10))
    # Define the bar width and positions
    bar_width = 0.7 / 3
    index = np.arange(len(statistics))

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["snapshot_median"] + stats["recovery_median"],
            bar_width,
            # yerr=stats["snapshot_std"],
            capsize=5,
            color="blue",
            label="MVVM" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["criu_snapshot_median"] + stats["criu_recovery_median"],
            bar_width,
            # yerr=stats["criu_snapshot_std"],
            capsize=5,
            color="cyan",
            label="CRIU" if i == 0 else "",
        )
        ax.bar(
            index[i] + 2 * bar_width,
            stats["qemu_snapshot_median"],
            bar_width,
            # yerr=stats["qemu_snapshot_std"],
            capsize=5,
            color="green",
            label="QEMU Total" if i == 0 else "",
        )

    # Labeling and formatting
    ax.set_ylabel("Checkpoint Restore Time (s)")
    ax.set_xticks(index + bar_width / 2)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel,fontsize=18)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig(file_name)


def plot_size(
    result_mvvm, result_criu, file_name="ckpt_restore_latency_size.pdf"
):
    font = {"size": 25}

    plt.rc("font", **font)
    workloads = defaultdict(list)
    for workload, size in result_mvvm:
        workloads[workload.split(" ")[1].replace(".aot", "")].append((size))
    workloads_criu = defaultdict(list)
    for workload, size in result_criu:
        workloads_criu[workload.split(" ")[1].replace(".aot", "")].append((size))


    # Calculate the medians and standard deviations for each workload
    statistics = {}
    # print(workloads)
    for workload, times in workloads.items():
        size = times
        # print(workload, workloads_criu[workload])
        size_criu = workloads_criu[workload]
        statistics[workload] = {
            "snapshot_median": np.median(size),
            "snapshot_std": np.std(size),
            "criu_snapshot_median": np.median(size_criu),
            "criu_snapshot_std": np.std(size_criu),
        }

    # Plotting
    fig, ax = plt.subplots(figsize=(20, 10))
    # Define the bar width and positions
    bar_width = 0.7 / 3
    index = np.arange(len(statistics))

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["snapshot_median"],
            bar_width,
            # yerr=stats["snapshot_std"],
            capsize=5,
            color="blue",
            label="MVVM" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["criu_snapshot_median"],
            bar_width,
            # yerr=stats["criu_snapshot_std"],
            capsize=5,
            color="cyan",
            label="CRIU" if i == 0 else "",
        )

    # Labeling and formatting
    ax.set_ylabel("Checkpoint file size (B)")
    ax.set_xticks(index + bar_width / 2)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel,fontsize=18)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig(file_name)
    # %%


if __name__ == "__main__":
    # mvvm_result = run_mvvm()
    # write_to_csv(mvvm_result, "ckpt_restore_latency_profile.csv")

    # print(len(arg), len(cmd), len(envs))
    # criu_result = run_criu()
    # write_to_csv(criu_result, "ckpt_restore_latency_criu.csv")
    # plot(criu_result, "ckpt_restore_latency_criu.pdf")
    # qemu_result = run_qemu()
    # write_to_csv(qemu_result, "ckpt_restore_latency_qemu.csv")
    # plot_qemu(results, "ckpt_restore_latency_qemu.pdf")

    mvvm_result = read_from_csv("ckpt_restore_latency_profile.csv")
    criu_result = read_from_csv("ckpt_restore_latency_criu.csv")
    qemu_result = read_from_csv("ckpt_restore_latency_qemu.csv")
    plot_whole(mvvm_result, criu_result, qemu_result)
    print(calculate_averages_latency(mvvm_result,criu_result,qemu_result))
    mvvm_size_result = read_size_from_csv("ckpt_restore_latency_profile.csv")
    criu_size_result = read_size_from_csv("ckpt_restore_latency_criu.csv")
    plot_size(mvvm_size_result, criu_size_result)
    print(calculate_averages_size(mvvm_size_result,criu_size_result))
