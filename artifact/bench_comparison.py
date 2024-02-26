import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np

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
    "ep",
    "ft",
    "lu",
    "mg",
    "sp",
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
    "nas",
    "nas",
    "nas",
    "nas",
    "nas",
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
    [],
    [],
    [],
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
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=4",
    "a=b",
    "a=b",
]

pool = Pool(processes=1)


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


def run_hcontainer():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_hcontainer,
                    (aot, "linpack", arg[i], envs[i]),
                )
            )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
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
                    (aot, "linpack", arg[i], envs[i]),
                )
            )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
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
                    (aot, "linpack", arg[i], envs[i]),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
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
                    (aot, "linpack", arg[i], envs[i]),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
        results.append((exec, exec_time))
    return results


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            ["name", "mvvm", "hcontainer", "qemu_x86_64", "qemu_aach64", "native"]
        )

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    hcontainer_results[idx][1],
                    qemu_x86_64_results[idx][1],
                    qemu_aarch64_results[idx][1],
                    native_results[idx][1],
                ]
            )

def plot(results):
    # items = ["bt", "cg", "ep", "ft", "lu", "mg", "sp", "linpack", "llama"]
    # hcontainer_values = [261.77, 111.80, 0.0035, 205.29, 29.10, 62.92, 0.28, 27.15, 12.00]
    # mvvm_values = [85.05, 27.64, 0.000179, 39.12, 8.83, 18.80, 0.118, 35.48, 3.54]
    # native_values = [46.84, 27.88, 0.00, 28.96, 8.56, 9.34, 0.07, 35.0, 2.86]
    # qemu_values = [1936.74, 473.24, 0.02, 1376.74, 373.91, 646.23, 2.75, 24.48, 45.18]

    # Number of groups and bar width
    n = len(items)
    bar_width = 0.2

    # Setting the positions of the bars on the x-axis
    index = np.arange(n)

    # Creating figure
    fig, ax = plt.subplots(figsize=(12, 6))

    # Plotting
    for i in range(len(results)):
        bar1 = ax.bar(index, zip(* ), bar_width, label='HContainer')
        bar2 = ax.bar(index + bar_width, mvvm_values, bar_width, label='MVVM')
        bar3 = ax.bar(index + 2 * bar_width, native_values, bar_width, label='Native')
        bar4 = ax.bar(index + 3 * bar_width, qemu_values, bar_width, label='QEMU')

    # Adding labels, title, and legend
    ax.set_ylabel('Latency (s)')
    # ax.set_title('Performance comparison between different techniques')
    ax.set_xticks(index + bar_width * 1.5)
    ax.set_xticklabels(items)
    ax.legend()

    # Display the plot
    # plt.show()
    plt.savefig("performance_comparison.pdf")
if __name__ == "__main__":
    mvvm_results = run_mvvm()
    native_results = run_native()
    qemu_x86_64_results = run_qemu_x86_64()
    # print the results
    qemu_aarch64_results = run_qemu_aarch64()
    hcontainer_results = run_hcontainer()

    write_to_csv("comparison.csv")
