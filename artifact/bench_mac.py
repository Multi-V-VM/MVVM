import csv
import common_util
import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict

cmd = [
    "linpack",
    "rgbd_tum",
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
    "ft",
    "lu",
    "mg",
    "sp",
    "redis",
]
folder = [
    "linpack",
    "ORB_SLAM2",
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
    "nas",
    "nas",
    "nas",
    "nas",
    "redis",
]
arg = [
    [],
    ["./ORBvoc.txt,", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-n1"],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
]
envs = [
    "a=b",
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
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "a=b",
]


def run_mvvm():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(common_util.run(aot, arg[i], envs[i]))
    # print the results

    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split()[-2]
                print(exec, exec_time)
                results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def run_native():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                common_util.run_native(aot, folder[i], arg[i], envs[i]),
            )
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                try:
                    exec_time = float(line.split()[0].replace("user", ""))
                except:
                    exec_time = float(line.split()[2].replace("user", ""))
        print(exec, exec_time)
        results.append((exec, exec_time))

    return results


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(["name", "mvvm", "native"])

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    native_results[idx][1],
                ]
            )


def read_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append((row[0], float(row[1]), float(row[2])))
        return results

# print the results
def plot(result, file_name="mac.pdf"):
    workloads = defaultdict(list)
    for workload, mvvm, native in result:
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
        ].append((mvvm, native))

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        mvvms, native = zip(*times)
        statistics[workload] = {
            "mvvm_median": np.median(mvvms),
            "native_median": np.median(native),
            "mvvm_std": np.std(mvvms),
            "native_std": np.std(native),
        }
    font = {"size": 14}

    # using rc function
    plt.rc("font", **font)
    # Plotting
    fig, ax = plt.subplots(figsize=(15, 7))
    # Define the bar width and positions
    bar_width = 0.7 / 2
    index = np.arange(len(statistics))

    # Plot the bars for each workload
    # for i, (workload, stats) in enumerate(statistics.items()):
    #     ax.bar(index[i], stats['mvvm_median'], bar_width, yerr=stats['mvvm_std'], capsize=5, label=f'mvvm')
    #     ax.bar(index[i] + bar_width, stats['wamr_median'], bar_width, yerr=stats['wamr_std'], capsize=5, label=f'wamr')
    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["mvvm_median"],
            bar_width,
            yerr=stats["mvvm_std"],
            capsize=5,
            color="blue",
            label="MVVM" if i == 0 else "",
        )
        ax.bar(
            index[i] +  bar_width,
            stats["native_median"],
            bar_width,
            yerr=stats["native_std"],
            capsize=5,
            color="green",
            label="Native" if i == 0 else "",
        )
    # Labeling and formatting
    ax.set_ylabel("Time(s)")
    ax.set_xticks(index + bar_width )
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, fontsize=10)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    # plt.show()
    plt.savefig(file_name)
    # %%



if __name__ == "__main__":
    mvvm_results = run_mvvm()
    native_results = run_native()

    write_to_csv("mac.csv")
    results = read_from_csv("mac.csv")
    plot(results, "mac.pdf")
