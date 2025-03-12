import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict

cmd = [
    "bc",
    "bfs",
    "cc",
    "cc_sv",
    "pr",
    "pr_spmv",
    "sssp",
    "tc",
]
folder = [
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",

]
arg = [
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-n1"],

]
envs = [
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
]
pool = Pool(processes=20)

def run_mvvm():
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


def run_qemu_x86_64():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_qemu_x86_64,
                    (aot, folder[i], arg[i], envs[i]),
                )
            )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("elapsed"):
                try:
                    minutes, seconds = line.split()[2].replace("elapsed", "").split(":")
                    seconds, milliseconds = seconds.split(".")

                    # Convert each part to seconds (note that milliseconds are converted and added as a fraction of a second)
                    total_seconds = (
                        int(minutes) * 60 + int(seconds) + int(milliseconds) / 1000
                    )

                    print(total_seconds)
                    exec_time = total_seconds
                except:
                    try:
                        from datetime import datetime

                        time_object = datetime.strptime(line.split()[2].replace("elapsed", ""), "%H:%M:%S").time()
                        print(time_object)
                    except:
                        exec_time = float(line.split()[0].replace("user", ""))
                results.append((exec, exec_time))
    return results


def run_qemu_aarch64():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_qemu_aarch64,
                    (aot, folder[i], arg[i], envs[i]),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("elapsed"):
                try:
                    minutes, seconds = line.split()[2].replace("elapsed", "").split(":")
                    seconds, milliseconds = seconds.split(".")

                    # Convert each part to seconds (note that milliseconds are converted and added as a fraction of a second)
                    total_seconds = (
                        int(minutes) * 60 + int(seconds) + int(milliseconds) / 1000
                    )

                    print(total_seconds)
                    exec_time = total_seconds
                except:
                    try:
                        from datetime import datetime

                        time_object = datetime.strptime(line.split()[2].replace("elapsed", ""), "%H:%M:%S").time()
                        print(time_object)
                    except:
                        exec_time = float(line.split()[0].replace("user", ""))
                results.append((exec, exec_time))
    return results


def run_native():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_native,
                    (aot, folder[i], arg[i], envs[i]),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("elapsed"):
                try:
                    minutes, seconds = line.split()[2].replace("elapsed", "").split(":")
                    seconds, milliseconds = seconds.split(".")

                    # Convert each part to seconds (note that milliseconds are converted and added as a fraction of a second)
                    total_seconds = (
                        int(minutes) * 60 + int(seconds) + int(milliseconds) / 1000
                    )

                    print(total_seconds)
                    exec_time = total_seconds
                except:
                    try:
                        from datetime import datetime

                        time_object = datetime.strptime(line.split()[2].replace("elapsed", ""), "%H:%M:%S").time()
                        print(time_object)
                    except:
                        exec_time = float(line.split()[0].replace("user", ""))
                results.append((exec, exec_time))
    return results


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            ["name", "mvvm", "qemu_x86_64", "qemu_aach64", "native"]
        )

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    qemu_x86_64_results[idx][1],
                    qemu_aarch64_results[idx][1],
                    native_results[idx][1],
                ]
            )
def read_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append((row[0], float(row[1]), float(row[2]), float(row[3]), float(row[4])))
        return results

def plot(results):
    font = {'size': 25}
 
    plt.rc('font', **font)
    workloads = defaultdict(list)
    for workload, mvvm_values, qemu_x86_64_values,qemu_aarch64_values,native_values in results:
            workloads[
                workload.split(" ")[1].replace(".aot", "")
            ].append((  mvvm_values, qemu_x86_64_values,qemu_aarch64_values,native_values))

    statistics = {}
    for workload, times in workloads.items():
        mvvm_values, qemu_x86_64_values,qemu_aarch64_values,native_values= zip(*times)
        statistics[workload] = {
            "mvvm_median": np.median(mvvm_values),
            "qemu_x86_64_median" :np.median(qemu_x86_64_values),
            "qemu_aarch64_median" :np.median(qemu_aarch64_values),
            "native_median" :np.median(native_values),
            "mvvm_std": np.std(mvvm_values),
            "qemu_x86_64_std" :np.std(qemu_x86_64_values),
            "qemu_aarch64_std" :np.std(qemu_aarch64_values),
            "native_std" :np.std(native_values),
        }

    fig, ax = plt.subplots(figsize=(20, 10))
    index = np.arange(len(statistics))
    bar_width = 0.7/5

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["mvvm_median"],
            bar_width,
            yerr=stats["mvvm_std"],
            capsize=5,
            color="purple",
            label="mvvm" if i == 0 else "",
        )
        ax.bar(
            index[i]+ bar_width *1,
            stats["qemu_x86_64_median"],
            bar_width,
            yerr=stats["qemu_x86_64_std"],
            capsize=5,
            color="brown",
            label="qemu_x86_64" if i == 0 else "",
        )
        ax.bar(
            index[i]+ bar_width*2,
            stats["qemu_aarch64_median"],
            bar_width,
            yerr=stats["qemu_aarch64_std"],
            capsize=5,
            color="red",
            label="qemu_aarch64" if i == 0 else "",
        )
        ax.bar(
            index[i]+ bar_width*3,
            stats["native_median"],
            bar_width,
            yerr=stats["native_std"],
            capsize=5,
            color="cyan",
            label="native" if i == 0 else "",
        )
        # ax.set_xlabel(workload)
    ticklabel = (x for x in list(statistics.keys()))
    print(statistics.keys())
    ax.set_xticks(index)

    ax.set_xticklabels(ticklabel,fontsize =10)
    ax.set_ylabel("Execution time (s)")
    ax.legend()

    # add text at upper left
    ax.legend(loc="upper right")

    plt.savefig("performance_comparison_gapbs.pdf")


if __name__ == "__main__":
    mvvm_results = run_mvvm()
    native_results = run_native()
    qemu_x86_64_results = run_qemu_x86_64()
    # # print the results
    qemu_aarch64_results = run_qemu_aarch64()

    write_to_csv("comparison_gapbs.csv")
    
    results = read_from_csv("comparison_gapbs.csv")
    plot(results)
