import csv
import common_util
from common_util import plot, calculate_averages
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
    "hdastar",
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
    "hdastar",
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
    ["maze-6404.txt", "8"],
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

pool = Pool(processes=16)


def run_mvvm():
    results_0 = []
    results_1 = []
    results_2 = []
    results_3 = []
    results_4 = []
    results_5 = []
    results = []
    name = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            for j in range(len(common_util.aot_variant)):
                aot = cmd[i] + common_util.aot_variant[j]
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

        for a in common_util.aot_variant:
            if exec.__contains__(a):
                if a == "-pure.aot":
                    results_1.append(exec_time)
                elif a == "-stack.aot":
                    results_2.append(exec_time)
                elif a == "-ckpt-loop-counter.aot":
                    results_3.append(exec_time)
                elif a == "-ckpt-loop.aot":
                    results_4.append(exec_time)
                elif a == "-ckpt-loop-dirty.aot":
                    results_5.append(exec_time)
                elif (
                    a == ".aot"
                    and not exec.__contains__("-pure.aot")
                    and not exec.__contains__("-stack.aot")
                    and not exec.__contains__("-ckpt-loop-counter.aot")
                    and not exec.__contains__("-ckpt-loop.aot")
                    and not exec.__contains__("-ckpt-loop-dirty.aot")
                ):
                    results_0.append(exec_time)
                    name.append(exec)
    results = list(
        zip(
            name,
            results_0,
            results_1,
            results_2,
            results_3,
            results_4,
            results_5,
        )
    )
    print(results)
    print("results_0", results_0)
    print("results_1", results_1)
    return results


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]
    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            [
                "name",
                "aot",
                "pure.aot",
                "stack.aot",
                "ckpt-loop.aot",
                "ckpt-loop-dirty.aot",
            ]
        )

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow([row[0], row[1], row[2], row[3], row[4], row[5]])


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
                    float(row[5]),
                    float(row[6]),
                )
            )
        return results
    
if __name__ == "__main__":
    mvvm_results = run_mvvm()
    write_to_csv("policy_multithread.csv")
    mvvm_results = read_from_csv("policy_multithread.csv")
    # plot(mvvm_results, "policy_multithread.pdf")
    # print(calculate_averages(mvvm_results))