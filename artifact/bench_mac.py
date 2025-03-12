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
    ["./ORBvoc.txt", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
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
            index[i] + bar_width,
            stats["native_median"],
            bar_width,
            yerr=stats["native_std"],
            capsize=5,
            color="green",
            label="Native" if i == 0 else "",
        )
    # Labeling and formatting
    ax.set_ylabel("Time(s)")
    ax.set_xticks(index + bar_width)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, fontsize=10)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    # plt.show()
    plt.savefig(file_name)
    # %%


if __name__ == "__main__":
#    mvvm_results = run_mvvm()
#     native_results = run_native()
# #    print("mvvm_results=",mvvm_results)
#     print("native_results=",native_results)
#     mvvm_results= [('a=b linpack.aot', '20.35297'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1756.614537'), ('OMP_NUM_THREADS=4 cc_sv.aot -g20 -vn300', '337.642205'), ('OMP_NUM_THREADS=4 pr_spmv.aot -g20 -vn300', '207.854911'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '752.853794'), ('OMP_NUM_THREADS=4 tc.aot -g20 -n1', '79.425973'), ('OMP_NUM_THREADS=4 bt.aot', '225.563336'), ('OMP_NUM_THREADS=4 cg.aot', '105.84309'), ('OMP_NUM_THREADS=4 ft.aot', '109.382876'), ('OMP_NUM_THREADS=4 lu.aot', '0.100862'), ('OMP_NUM_THREADS=4 mg.aot', '77.988457'), ('OMP_NUM_THREADS=4 sp.aot', '45.716619'), ('a=b redis.aot', '440.105842'), ('a=b linpack.aot', '20.292488'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1752.427599'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '650.273267'), ('OMP_NUM_THREADS=4 bt.aot', '221.060635'), ('OMP_NUM_THREADS=4 cg.aot', '83.902382'), ('OMP_NUM_THREADS=4 ft.aot', '108.55405'), ('OMP_NUM_THREADS=4 lu.aot', '0.099944'), ('OMP_NUM_THREADS=4 mg.aot', '76.068425'), ('OMP_NUM_THREADS=4 sp.aot', '44.252397'), ('a=b redis.aot', '436.661554'), ('a=b linpack.aot', '20.311249'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1750.945471'), ('OMP_NUM_THREADS=4 cc.aot -g20 -vn300', '257.6362'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '640.248003'), ('OMP_NUM_THREADS=4 cg.aot', '83.665063'), ('OMP_NUM_THREADS=4 ft.aot', '108.397934'), ('OMP_NUM_THREADS=4 lu.aot', '0.099937'), ('OMP_NUM_THREADS=4 mg.aot', '75.730582'), ('OMP_NUM_THREADS=4 sp.aot', '43.246373'), ('a=b redis.aot', '431.965889'), ('a=b linpack.aot', '20.12319'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1744.854772'), ('OMP_NUM_THREADS=4 cc_sv.aot -g20 -vn300', '323.408531'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '639.228057'), ('OMP_NUM_THREADS=4 tc.aot -g20 -n1', '77.180237'), ('OMP_NUM_THREADS=4 bt.aot', '219.571527'), ('OMP_NUM_THREADS=4 cg.aot', '84.089276'), ('OMP_NUM_THREADS=4 ft.aot', '108.373259'), ('OMP_NUM_THREADS=4 lu.aot', '0.101511'), ('OMP_NUM_THREADS=4 mg.aot', '75.808051'), ('OMP_NUM_THREADS=4 sp.aot', '43.507578'), ('a=b redis.aot', '430.646313'), ('a=b linpack.aot', '20.154494'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1747.744954'), ('OMP_NUM_THREADS=4 pr.aot -g20 -vn300', '172.685799'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '639.321099'), ('OMP_NUM_THREADS=4 tc.aot -g20 -n1', '77.176157'), ('OMP_NUM_THREADS=4 bt.aot', '220.643829'), ('OMP_NUM_THREADS=4 cg.aot', '83.617502'), ('OMP_NUM_THREADS=4 ft.aot', '108.375996'), ('OMP_NUM_THREADS=4 lu.aot', '0.099423'), ('OMP_NUM_THREADS=4 mg.aot', '75.713318'), ('OMP_NUM_THREADS=4 sp.aot', '43.262573'), ('a=b redis.aot', '432.311077'), ('a=b linpack.aot', '20.127186'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1747.198434'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '639.339902'), ('OMP_NUM_THREADS=4 tc.aot -g20 -n1', '77.189113'), ('OMP_NUM_THREADS=4 bt.aot', '220.017367'), ('OMP_NUM_THREADS=4 cg.aot', '83.987663'), ('OMP_NUM_THREADS=4 ft.aot', '108.316253'), ('OMP_NUM_THREADS=4 lu.aot', '0.099739'), ('OMP_NUM_THREADS=4 mg.aot', '75.808478'), ('OMP_NUM_THREADS=4 sp.aot', '43.184115'), ('a=b redis.aot', '434.06413'), ('a=b linpack.aot', '20.094815'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1745.192042'), ('OMP_NUM_THREADS=4 bfs.aot -g20 -vn300', '132.498137'), ('OMP_NUM_THREADS=4 pr.aot -g20 -vn300', '172.928574'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '640.216479'), ('OMP_NUM_THREADS=4 bt.aot', '219.973081'), ('OMP_NUM_THREADS=4 cg.aot', '84.080505'), ('OMP_NUM_THREADS=4 ft.aot', '108.467563'), ('OMP_NUM_THREADS=4 lu.aot', '0.099886'), ('OMP_NUM_THREADS=4 mg.aot', '75.930483'), ('OMP_NUM_THREADS=4 sp.aot', '43.790822'), ('a=b redis.aot', '433.181533'), ('a=b linpack.aot', '20.113912'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1746.466368'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '724.950474'), ('OMP_NUM_THREADS=4 cg.aot', '88.982113'), ('OMP_NUM_THREADS=4 ft.aot', '109.39976'), ('OMP_NUM_THREADS=4 lu.aot', '0.103163'), ('OMP_NUM_THREADS=4 mg.aot', '78.80666'), ('OMP_NUM_THREADS=4 sp.aot', '47.4579'), ('a=b redis.aot', '442.873866'), ('a=b linpack.aot', '20.899038'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1770.61544'), ('OMP_NUM_THREADS=4 cc.aot -g20 -vn300', '275.526444'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '728.232795'), ('OMP_NUM_THREADS=4 tc.aot -g20 -n1', '80.402936'), ('OMP_NUM_THREADS=4 bt.aot', '230.24286'), ('OMP_NUM_THREADS=4 cg.aot', '88.678567'), ('OMP_NUM_THREADS=4 ft.aot', '110.228341'), ('OMP_NUM_THREADS=4 lu.aot', '0.101845'), ('OMP_NUM_THREADS=4 mg.aot', '79.984639'), ('OMP_NUM_THREADS=4 sp.aot', '47.672618'), ('a=b redis.aot', '451.670712'), ('a=b linpack.aot', '21.172726'), ('a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', '1792.986851'), ('OMP_NUM_THREADS=4 sssp.aot -g20 -vn300', '777.678223'), ('OMP_NUM_THREADS=4 cg.aot', '90.86123'), ('OMP_NUM_THREADS=4 ft.aot', '112.358934'), ('OMP_NUM_THREADS=4 lu.aot', '0.105421'), ('OMP_NUM_THREADS=4 mg.aot', '81.579015'), ('OMP_NUM_THREADS=4 sp.aot', '50.432014'), ('a=b redis.aot', '452.357001')]
#     write_to_csv("mac.csv")
    results = read_from_csv("mac.csv")
    plot(results, "mac.pdf")
