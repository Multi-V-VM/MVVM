import csv
import common_util
from common_util import (
    plot_loop_counter,
    plot_loop_counter_snapshot,
    calculate_loop_counter_averages,
    aot_variant_freq,
    calculate_loop_counter_snapshot_averages,
)
from multiprocessing import Pool

cmd = [
    "llama",
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
    # "hdastar",
]
folder = [
    "llama",
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
    # "hdastar",
]
arg = [
    ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
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
    [],
    # ["maze-6404.txt", "8"],
]
envs = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "a=b",
    # "a=b",
]

pool = Pool(processes=16)


def run_mvvm():
    results = [[] for _ in range(len(aot_variant_freq))]
    name = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            for j in range(len(common_util.aot_variant_freq)):
                aot = cmd[i] + common_util.aot_variant_freq[j]
                results1.append(
                    pool.apply_async(common_util.run, (aot, arg[i], envs[i]))
                )
    # print the results
    results1 = [x.get() for x in results1]
    exec_time = ""
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
                # print(exec, exec_time)

        for a in common_util.aot_variant_freq:
            if exec.__contains__(a):
                if a == "-ckpt-loop-counter-1.aot":
                    results[0].append(exec_time)
                elif a == "-ckpt-loop-counter-4.aot":
                    results[1].append(exec_time)
                elif a == "-ckpt-loop-counter-8.aot":
                    results[2].append(exec_time)
                elif a == "-ckpt-loop-counter-16.aot":
                    results[3].append(exec_time)
                elif a == "-ckpt-loop-counter-20.aot":
                    results[4].append(exec_time)
                elif a == "-ckpt-loop-counter-30.aot":
                    results[5].append(exec_time)
                elif a == "-ckpt-loop-pgo.aot":
                    results[6].append(exec_time)
                elif a == "-pure.aot":
                    results[8].append(exec_time)
                elif (
                    a == ".aot"
                    and not exec.__contains__("-ckpt-loop-counter-1.aot")
                    and not exec.__contains__("-ckpt-loop-counter-4.aot")
                    and not exec.__contains__("-ckpt-loop-counter-8.aot")
                    and not exec.__contains__("-ckpt-loop-counter-16.aot")
                    and not exec.__contains__("-ckpt-loop-counter-20.aot")
                    and not exec.__contains__("-ckpt-loop-counter-30.aot")
                    and not exec.__contains__("-ckpt-loop-pgo.aot")
                    and not exec.__contains__("-pure.aot")
                ):
                    results[7].append(exec_time)
                    name.append(exec)
    final_results = list(zip(name, *results))
    print(results)
    return final_results


def run_mvvm_profile():
    results1 = []
    # for _ in range(common_util.trial):
    for i in range(len(cmd)):
        # for j in range(len(common_util.aot_variant_freq)):
        aot = cmd[i] + "-ckpt-loop-pgo.aot"
        results1.append(
            pool.apply_async(common_util.run_profile, (aot, arg[i], envs[i]))
        )
    results1 = [x.get() for x in results1]


def run_mvvm_snapshot():
    results = [[] for _ in range(len(aot_variant_freq))]
    name = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            for j in range(len(common_util.aot_variant_freq)):
                aot = cmd[i] + common_util.aot_variant_freq[j]
                results1.append(
                    pool.apply_async(common_util.run_checkpoint, (aot, arg[i], envs[i]))
                )
    # print the results
    results1 = [x.get() for x in results1]
    exec_time = ""
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Snapshot Overhead:"):
                exec_time = line.split(" ")[-2]
                # print(exec, exec_time)

        for a in common_util.aot_variant_freq:
            if exec.__contains__(a):
                if a == "-ckpt-loop-counter-1.aot":
                    results[0].append(exec_time)
                elif a == "-ckpt-loop-counter-4.aot":
                    results[1].append(exec_time)
                elif a == "-ckpt-loop-counter-8.aot":
                    results[2].append(exec_time)
                elif a == "-ckpt-loop-counter-16.aot":
                    results[3].append(exec_time)
                elif a == "-ckpt-loop-counter-20.aot":
                    results[4].append(exec_time)
                elif a == "-ckpt-loop-counter-30.aot":
                    results[5].append(exec_time)
                elif a == "-ckpt-loop-pgo.aot":
                    results[6].append(exec_time)
                elif a == "-pure.aot":
                    results[8].append(exec_time)
                elif (
                    a == ".aot"
                    and not exec.__contains__("-ckpt-loop-counter-1.aot")
                    and not exec.__contains__("-ckpt-loop-counter-4.aot")
                    and not exec.__contains__("-ckpt-loop-counter-8.aot")
                    and not exec.__contains__("-ckpt-loop-counter-16.aot")
                    and not exec.__contains__("-ckpt-loop-counter-20.aot")
                    and not exec.__contains__("-ckpt-loop-counter-30.aot")
                    and not exec.__contains__("-ckpt-loop-pgo.aot")
                    and not exec.__contains__("-pure.aot")
                ):
                    results[7].append(exec_time)
                    name.append(exec)
    final_results = list(zip(name, *results))
    print(results)
    return final_results


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]
    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(["name", *aot_variant_freq])

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    row[2],
                    row[3],
                    row[4],
                    row[5],
                    row[6],
                    row[7],
                    row[8],
                    row[9],
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
                    float(row[2]),
                    float(row[3]),
                    float(row[4]),
                    float(row[5]),
                    float(row[6]),
                    float(row[7]),
                    float(row[8]),
                    float(row[9]),
                )
            )
        return results


def read_from_csv_snapshot(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append(
                (
                    row[0],
                    float(row[1]),
                    float(row[2]),
                    float(row[3]),
                    float(row[4]),
                    float(row[5]),
                    float(row[6]),
                )
            )
        return results


if __name__ == "__main__":
    # run_mvvm_profile()
    # mvvm_results = run_mvvm()
    # write_to_csv("policy_loop_counter2.csv")
    mvvm_results = read_from_csv("policy_loop_counter2.csv")
    plot_loop_counter(mvvm_results, "policy_loop_counter.pdf")
    print(calculate_loop_counter_averages(mvvm_results))
    # mvvm_results = run_mvvm_snapshot()
    # write_to_csv("policy_loop_snapshot.csv")
    mvvm_results = read_from_csv_snapshot("policy_loop_snapshot.csv")
    # mvvm_results = read_from_csv("policy_loop_snapshot.csv")
    plot_loop_counter_snapshot(mvvm_results, "policy_loop_counter_snapshot.pdf")
    print(calculate_loop_counter_snapshot_averages(mvvm_results))
