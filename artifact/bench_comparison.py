import csv
import common_util
from common_util import plot, calculate_averages_comparison
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict

cmd = [
    # "linpack",
    # "llama",
    # "rgbd_tum",
    "bc",
    "bfs",
    "cc",
    "cc_sv",
    "pr",
    "pr_spmv",
    "sssp",
    "bt",
    "cg",
    "ft",
    "lu",
    "mg",
    # "redis",
    # "hdastar",
]
folder = [
    # "linpack",
    # "llama",
    # "ORB_SLAM2",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "nas",
    "nas",
    "nas",
    "nas",
    "nas",
    # "redis",
    # "hdastar",
]
arg = [
    # [],
    # ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    # ["./ORBvoc.txt,", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
    ["-g20", "-n1000"],
    ["-g20", "-n1000"],
    ["-g20", "-n1000"],
    ["-g20", "-n1000"],
    ["-g20", "-n1000"],
    ["-g20", "-n1000"],
    ["-g20", "-n1000"],
    [],
    [],
    [],
    [],
    [],
    # [],
    # ["maze-6404.txt", "8"],
]
# envs = [
#     # "a=b",
#     # "OMP_NUM_THREADS=64",
#     # "a=b",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     "OMP_NUM_THREADS=96",
#     # "a=b",
#     # "a=b",
# ]
envs = [
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=16",
    "OMP_NUM_THREADS=64",
    "OMP_NUM_THREADS=96",
]
pool = Pool(processes=1)



def run_mvvm():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for env in envs:
            for i in range(len(cmd)):
                aot = cmd[i] + ".aot"
                results1.append(pool.apply_async(common_util.run, (aot, arg[i], env)))
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


def run_hcontainer():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_hcontainer,
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
                        print(time_object)
                    except:
                        exec_time = float(line.split()[0].replace("user", ""))
                results.append((exec, exec_time))
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
                        print(time_object)
                    except:
                        exec_time = float(line.split()[0].replace("user", ""))
                results.append((exec, exec_time))
    return results


def run_native():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for env in envs:
            for i in range(len(cmd)):
                aot = cmd[i]
                results1.append(
                    pool.apply_async(
                        common_util.run_native,
                        (aot, folder[i], arg[i], env),
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
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
        writer.writerow(["name", "native"])

        # Write the data
        for idx, row in enumerate(native_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    # native_results[idx][1],
                ]
            )


def read_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append(
                (
                    row[0],
                    float(row[1]),
                    # float(row[2]),
                )
            )
        return results


def plot(results):
    font = {"size": 40}

    plt.rc("font", **font)
    workloads = defaultdict(list)
    for (
        workload,
        mvvm_values,
        native_values,
    ) in results:
        workloads[workload.split(" ")[1].replace(".aot", "")].append(
            (
                mvvm_values,
                native_values,
            )
        )

def plot(results, results1):
    font = {"size": 20}
    plt.tight_layout()
    plt.rc("font", **font)
    workloads = defaultdict(list)
    workloads1 = defaultdict(list)
    for (
        workload,
        mvvm_values,
        hcontainer_values,
        qemu_x86_64_values,
        qemu_aarch64_values,
        native_values,
    ) in results:
        if not workload.__contains__("sp") and not workload.__contains__("lu") and not workload.__contains__("tc"):
            workloads[workload.split(" ")[1].replace(".aot", "")].append(
                (
                    hcontainer_values,
                    mvvm_values,
                    qemu_x86_64_values,
                    qemu_aarch64_values,
                    native_values,
                )
            )
    statistics = {}
    print(workloads)
    for workload, i in workloads.items():
        (
            mvvm_values,
            native_values,
        ) = zip(*i)
        statistics[workload] = {
            "mvvm_median": np.median(mvvm_values),
            "native_median": np.median(native_values),
            "mvvm_std": np.std(mvvm_values),
            "native_std": np.std(native_values),
        }
        print(workload, np.mean(mvvm_values) / np.mean(native_values))

    fig, ax = plt.subplots(figsize=(15, 10))
    index = np.arange(len(statistics))
    bar_width = 0.7 / 5

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i] - bar_width,
            stats["mvvm_median"],
            bar_width,
            # yerr=stats["mvvm_std"],
            capsize=5,
            color="purple",
            label="mvvm" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width * 2,
            stats["native_median"],
            bar_width,
            # yerr=stats["native_std"],
            capsize=5,
            color="purple",
        )

        # ax.set_xlabel(workload)
    ticklabel = (x for x in list(statistics.keys()))
    print(statistics.keys())
    ax.set_xticks(index + bar_width * 4)

    ax.set_xticklabels(ticklabel)
    ax.set_ylabel("Execution time (s)")
    ax.legend()

    # add text at upper left
    ax.legend(loc="upper right")

    plt.savefig("performance_comparison.pdf")


def calculate_averages_comparison(result):
    averages = defaultdict(list)
    for (
        workload,
        mvvm_values,
        native_values,
    ) in result:
        averages[workload] = {
            "mvvm": np.mean(mvvm_values) / native_values if native_values else 0,
            "native": np.mean(native_values) / native_values if native_values else 0,
        }
        print(workload, np.mean(mvvm_values))
    total_averages = defaultdict(float)
    for workload, policies in averages.items():
        for policy, values in policies.items():
            total_averages[policy] += values

    # Divide by the number of workloads to get the average
    num_workloads = len(averages)
    for policy in total_averages:
        total_averages[policy] /= num_workloads

    return dict(total_averages)


if __name__ == "__main__":
    # mvvm_results = run_mvvm()
    # print(mvvm_results)
    # mvvm_results  = [('a=b linpack.aot', '30.772065'), ('OMP_NUM_THREADS=4 llama.aot stories110M.bin -z tokenizer.bin -t 0.0', '10.28226'), ('a=b rgbd_tum.aot ./ORBvoc.txt, ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '497.993924'), ('OMP_NUM_THREADS=4 bc.aot -g20 -n1', '12.271252'), ('OMP_NUM_THREADS=4 bfs.aot -g20 -n1', '12.755606'), ('OMP_NUM_THREADS=4 cc.aot -g20 -n1', '13.075651'), ('OMP_NUM_THREADS=4 cc_sv.aot -g20 -n1', '12.921706'), ('OMP_NUM_THREADS=4 pr.aot -g20 -n1', '11.773185'), ('OMP_NUM_THREADS=4 pr_spmv.aot -g20 -n1', '12.357876'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -n1', '3.244041'), ('OMP_NUM_THREADS=4 bt.aot', '99.464271'), ('OMP_NUM_THREADS=4 cg.aot', '42.430665'), ('OMP_NUM_THREADS=4 ft.aot', '12.006954'), ('OMP_NUM_THREADS=4 lu.aot', '0.060153'), ('OMP_NUM_THREADS=4 mg.aot', '37.5819'), ('a=b redis.aot', '218.961516'), ('a=b hdastar.aot maze-6404.txt 8', '13.002049')]
    # native_results = run_native()
    # print(native_results)
    # # qemu_x86_64_results = run_qemu_x86_64()
    # # print(qemu_x86_64_results)
    # # # print the results
    # # qemu_aarch64_results = run_qemu_aarch64()
    # # print(qemu_aarch64_results)
    # # hcontainer_results = run_hcontainer()
    # # print(hcontainer_results)
    # write_to_csv("comparison.csv")

    results = read_from_csv("comparison.csv")
    plot(results)
    print(calculate_averages_comparison(results))
