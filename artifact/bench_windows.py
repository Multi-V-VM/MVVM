import csv
import common_util
import numpy as np
import matplotlib.pyplot as plt
from collections import defaultdict

cmd = [
    "linpack",
    "llama",
    "rgbd_tum",
    "bt",
    "cg",
    "ep",
    "ft",
    "lu",
    "mg",
    "sp",
    "redis",
]
folder = [
    "linpack",
    "llama",
    "ORB_SLAM2",
    "nas",
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
    ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["./ORBvoc.txt,", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
    [],
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
    "OMP_NUM_THREADS=1",
    "a=b",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
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
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
                results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def run_wamr():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + "-pure.aot"
            results1.append(common_util.run(aot, arg[i], envs[i]))
    # print the results
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
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
            if line.__contains__("elapsed"):
                # Split the time string into minutes, seconds, and milliseconds
                minutes, seconds = line.split(" ")[2].replace("elapsed", "").split(":")
                seconds, milliseconds = seconds.split(".")

                # Convert each part to seconds (note that milliseconds are converted and added as a fraction of a second)
                total_seconds = (
                    int(minutes) * 60 + int(seconds) + int(milliseconds) / 1000
                )

                print(total_seconds)
                exec_time = total_seconds
                results.append((exec, exec_time))
    return results


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(["name", "mvvm", "wamr", "native"])

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    wamr_results[idx][1],
                    native_results[idx][1],
                ]
            )


def read_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append((row[0], float(row[1]), float(row[2]), float(row[3])))
        return results


# print the results
def plot(result, file_name="windows.pdf"):
    workloads = defaultdict(list)
    for workload, mvvm, wamr, native in result:
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
        ].append((mvvm, wamr, native))

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        mvvms, wamr,native = zip(*times)
        statistics[workload] = {
            "mvvm_median": np.median(mvvms),
            "wamr_median": np.median(wamr),
            "native_median": np.median(native),
            "mvvm_std": np.std(mvvms),
            "wamr_std": np.std(wamr),
            "native_std": np.std(native),
        }
    font = {"size": 14}

    # using rc function
    plt.rc("font", **font)
    # Plotting
    fig, ax = plt.subplots(figsize=(15, 7))
    # Define the bar width and positions
    bar_width = 0.7 / 3
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
            index[i] + bar_width,
            stats["wamr_median"],
            bar_width,
            yerr=stats["wamr_std"],
            capsize=5,
            color="red",
            label="WAMR" if i == 0 else "",
        )
        ax.bar(
            index[i] + 2 * bar_width,
            stats["native_median"],
            bar_width,
            yerr=stats["native_std"],
            capsize=5,
            color="green",
            label="Native" if i == 0 else "",
        )
    # Labeling and formatting
    ax.set_ylabel("Time(s)")
    ax.set_xticks(index + bar_width / 2)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, fontsize=10)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig(file_name)
    # %%


if __name__ == "__main__":
    mvvm_results = run_mvvm()
    wamr_results = run_wamr()
    native_results = run_native()
    # print the results
    print(mvvm_results)
    print(wamr_results)
    print(native_results)
    write_to_csv("windows.csv")
    # results = read_from_csv("windows.csv")
    # plot(results)
