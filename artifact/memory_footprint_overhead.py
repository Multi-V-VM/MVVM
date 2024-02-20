# windows mac and linux
import os
import csv
import common_util
from multiprocessing import Pool
import numpy as np
from collections import defaultdict
from matplotlib import pyplot as plt

cmd = [
    "linpack",
    "llama","llama",
    "rgbd_tum",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bc", "bc", "bc",
    "bc", "bc", "bc",
    "bfs", "bfs", "bfs",
    "cc", "cc", "cc",
    "cc_sv", "cc_sv", "cc_sv",
    "pr", "pr", "pr",
    "pr_spmv", "pr_spmv", "pr_spmv",
    "sssp", "sssp", "sssp",
    "tc", "tc", "tc",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "redis",
    "hdastar",
    "hdastar",
    "hdastar",
]
folder = [
    "linpack",
    "llama","llama",
    "orb_slam2",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "bt","bt","bt",
    "cg","cg","cg",
    "ep","ep","ep",
    "ft","ft","ft",
    "lu","lu","lu",
    "mg","mg","mg",
    "sp","sp","sp",
    "redis",
    "hdastar",
    "hdastar",
    "hdastar",
]
arg = [
    [],
    ["stories15M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["stories15M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["./ORBvoc.txt,","./TUM3.yaml","./","./associations/fr1_xyz.txt"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-u20", "-n300"],["-u20", "-n300"],["-u20", "-n300"],
    ["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    ["-g20", "-vn300"],["-g20", "-vn300"],["-g20", "-vn300"],
    ["-g20", "-vn300"],["-g20", "-vn300"],["-g20", "-vn300"],
    ["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],
    ["-g20", "-vn300"],["-g20", "-vn300"],["-g20", "-vn300"],
    ["-g20", "-vn300"],["-g20", "-vn300"],["-g20", "-vn300"],
    ["-g20", "-vn300"],["-g20", "-vn300"],["-g20", "-vn300"],
    ["-g20", "-vn300"],["-g20", "-vn300"],["-g20", "-vn300"],
    ["-g20", "-vn300"],["-g20", "-vn300"],["-g20", "-vn300"],
    ["-g20", "-n300"],["-g20", "-n300"],["-g20", "-n300"],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],
    ["maze-6404.txt", "2"],
    ["maze-6404.txt", "4"],
    ["maze-6404.txt", "8"],
]
envs = [
    "a=b",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2",
    "a=b",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "a=b",
    "a=b","a=b","a=b",
]

pool = Pool(processes=10)
results = []


def run_mvvm():
    global results
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
    write_to_csv(results1, "memory_footprint_overhead_raw.csv")
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            for line in lines:
                if line.__contains__("Memory usage"):
                    time = line.split(" ")[-2]
                    useage = int(time)

            results += [(exec, useage)]
    # print the results
    # results += results1

def write_to_csv(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(["name", "memory usage(MB)"])

        # Write the data
        for row in data:
            writer.writerow(row)

# print the results
def plot(result, file_name="memory_footprint_overhead.pdf"):
    workloads = defaultdict(list)
    for workload, memory in result:
        workloads[workload.replace("OMP_NUM_THREADS=","").replace("-g20","").replace("a=b","").replace("./ORBvoc.txt, ./TUM3.yaml ./ ./associations/fr1_xyz.txt","").replace("-n300","").replace(" -f ","").replace("-vn300","").replace("maze-6404.txt","").replace("stories15M.bin","").replace("-z tokenizer.bin -t 0.0","").strip()].append(memory)

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, memories in workloads.items():
        statistics[workload] = {
            "memory_median": np.median(memories),
            "memory_std": np.std(memories),
        }

    # Plotting
    fig, ax = plt.subplots(figsize=(15, 7))
    bar_width = 0.35
    index = np.arange(len(statistics))

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["memory_median"],
            bar_width,
            yerr=stats["memory_std"],
            capsize=5,
            color="blue",
            label="Memory Footprint" if i == 0 else "",
        )

    # Labeling and formatting
    ax.set_xlabel("Workload")
    ax.set_ylabel("Memory Footprint (MB)")
    # ax.set_title("Median and Standard Deviation of Memory Footprint by Workload")
    ax.set_xticks(index)
    ax.set_xticklabels(statistics.keys(), rotation=45)
    ax.legend()

    plt.tight_layout()
    plt.savefig(file_name)
    plt.show()

if __name__ == "__main__":
    print(len(arg),len(cmd),len(envs),len(folder))
    run_mvvm()
    write_to_csv(results, "memmory_footprint_overhead.csv")
    plot(results, "memmory_footprint_overhead.pdf")