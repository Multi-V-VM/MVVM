import csv
import common_util
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
    results_0 = []
    results_1 = []
    results_2 = []
    results_3 = []
    results_4 = []
    results_5 = []
    results_6 = []
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
                elif a == "-ckpt.aot":
                    results_3.append(exec_time)
                elif a == "-ckpt-br.aot":
                    results_4.append(exec_time)
                elif a == "-ckpt-loop.aot":
                    results_5.append(exec_time)
                elif a == "-ckpt-loop-dirty.aot":
                    results_6.append(exec_time)
                else:
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
            results_6,
        )
    )
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
                "ckpt.aot",
                "ckpt-br.aot",
                "ckpt-loop.aot",
                "ckpt-loop-dirty.aot",
            ]
        )

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [row[0], row[1], row[2], row[3], row[4], row[5], row[6], row[7]]
            )

import matplotlib.pyplot as plt
import numpy as np
def plot(results):
    keys = []
    weights = {
        "aot": [],
        "stack": [],
        "ckpt-br": []
    }
    for k, v in results.items():
        keys.append(k.replace("-g15","").replace("-n300","").replace(" -f ","").replace("-vn300","").replace("maze-6404.txt","").replace("stories15M.bin","").strip())
        for w in weights:
            weights[w].append(v[w])
    width = 0.5

    fig, ax = plt.subplots(figsize=(20, 10))

    bottom = np.zeros(len(keys))

    for boolean, weight_count in weights.items():
        p = ax.bar(keys, weight_count, width, label=boolean, bottom=bottom)
        bottom += weight_count


    ax.set_xticklabels(keys, rotation=45)
    ax.set_ylabel('Execution time (s)')
    # add note at aot
    # for i in range(len(keys)):
    #     ax.text(i, 0, "{:.2f}".format(weights["aot"][i]), ha='center', va='bottom')
    # add note at stack
    # for i in range(len(keys)):
    #     ax.text(i, weights["aot"][i], "{:.2f}".format(weights["stack"][i]), ha='center', va='bottom')
    # add note at ckpt
    # for i in range(len(keys)):
    #     ax.text(i, weights["aot"][i] + weights["stack"][i], "{:.2f}".format(weights["ckpt"][i]), ha='center', va='bottom')
    # add note at ckpt-br
    # for i in range(len(keys)):
    #     ax.text(i, weights["aot"][i] + weights["stack"][i] + weights["ckpt"][i], "{:.2f}".format(weights["ckpt-br"][i]), ha='center', va='bottom')
    # add note at total
    # for i in range(len(keys)):
    #     ax.text(i, weights["aot"][i] + weights["stack"][i] + weights["ckpt"][i] + weights["ckpt-br"][i], "total: {:.2f}".format(weights["aot"][i] + weights["stack"][i] + weights["ckpt"][i] + weights["ckpt-br"][i]), ha='center', va='bottom')
    # add text at upper left
    ax.legend(loc="upper right")

    # plt.show()

    plt.savefig("performance_multithread.pdf")


if __name__ == "__main__":
    mvvm_results = run_mvvm()

    write_to_csv("policy_multithread.csv")
