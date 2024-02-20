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


pool = Pool(processes=1)

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
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
    hcontainer_results.append((exec, exec_time))


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
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
    qemu_x86_64_results.append((exec, exec_time))


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
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
    qemu_aarch64_results.append((exec, exec_time))


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
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
    native_results.append((exec, exec_time))


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



if __name__ == "__main__":
    run_native()
    run_qemu_x86_64()
    # print the results
    run_qemu_aarch64()
    run_hcontainer()

    write_to_csv("linpack_result.csv")
