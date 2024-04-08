import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict

cmd = [
    "linpack",
    "llama",
    "rgbd_tum",
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
    "redis",
    "hdastar"
]
folder = [
    "linpack",
    "llama",
    "ORB_SLAM2"
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
    "hdastar"
]
arg = [
    [],
    ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["./ORBvoc.txt,", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
    ["-g20","-n1"],
    ["-g20","-n1"],
    ["-g20","-n1"],
    ["-g20","-n1"],
    ["-g20","-n1"],
    ["-g20","-n1"],
    ["-g20","-n1"],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
    ["maze-6404.txt", "8"]
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
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "a=b",
    "a=b"
]
pool = Pool(processes=20)

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

                        time_object = datetime.strptime(line.split()[2].replace("elapsed", ""), "%H:%M:%S").time()
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

                        time_object = datetime.strptime(line.split()[2].replace("elapsed", ""), "%H:%M:%S").time()
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

                        time_object = datetime.strptime(line.split()[2].replace("elapsed", ""), "%H:%M:%S").time()
                        print(time_object)
                    except:
                        exec_time = float(line.split()[0].replace("user", ""))
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

                        time_object = datetime.strptime(line.split()[2].replace("elapsed", ""), "%H:%M:%S").time()
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
        writer.writerow(
            ["name", "mvvm", "hcontainer", "qemu_x86_64", "qemu_aach64", "native"]
        )

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    # hcontainer_results[idx][1],
                    # qemu_x86_64_results[idx][1],
                    # qemu_aarch64_results[idx][1],
                    native_results[idx][1],
                ]
            )
def read_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append((row[0], float(row[1]),float(row[2])))
        return results

def plot(results):
    font = {'size': 18}
 
    plt.rc('font', **font)
    workloads = defaultdict(list)
    for workload, mvvm_values,native_values in results:
            workloads[
                workload.replace("OMP_NUM_THREADS=", "")
                .replace("-g20", "")
                .replace("-n300", "")
                .replace(" -f ", "")
                .replace("-vn300", "")
                .replace("maze-6404.txt", "")
                .replace("stories110M.bin", "")
                .replace("-z tokenizer.bin -t 0.0", "").replace("a=b", "")
                .strip()
            ].append(( mvvm_values,native_values))

    statistics = {}
    for workload, times in workloads.items():
        mvvm_values,native_values= zip(*times)
        statistics[workload] = {
            # "hcontainer_median": np.median(hcontainer_values),
            "mvvm_median": np.median(mvvm_values),
            # "qemu_x86_64_median" :np.median(qemu_x86_64_values),
            # "qemu_aarch64_median" :np.median(qemu_aarch64_values),
            "native_median" :np.median(native_values),
            # "hcontainer_std": np.std(hcontainer_values),
            "mvvm_std": np.std(mvvm_values),
            # "qemu_x86_64_std" :np.std(qemu_x86_64_values),
            # "qemu_aarch64_std" :np.std(qemu_aarch64_values),
            "native_std" :np.std(native_values),
        }
        print(workload, np.mean(mvvm_values )/np.mean(native_values))

    fig, ax = plt.subplots(figsize=(20, 10))
    index = np.arange(len(statistics))
    bar_width = 0.7/5

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["hcontainer_median"],
            bar_width,
            yerr=stats["hcontainer_std"],
            capsize=5,
            color="blue",
            label="hcontainer" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["mvvm_median"],
            bar_width,
            yerr=stats["mvvm_std"],
            capsize=5,
            color="red",
            label="mvvm" if i == 0 else "",
        )
        ax.bar(
            index[i]+ bar_width *2,
            stats["qemu_x86_64_median"],
            bar_width,
            yerr=stats["qemu_x86_64_std"],
            capsize=5,
            color="brown",
            label="qemu_x86_64" if i == 0 else "",
        )
        ax.bar(
            index[i]+ bar_width*3,
            stats["qemu_aarch64_median"],
            bar_width,
            yerr=stats["qemu_aarch64_std"],
            capsize=5,
            color="purple",
            label="qemu_aarch64" if i == 0 else "",
        )
        ax.bar(
            index[i]+ bar_width*4,
            stats["native_median"],
            bar_width,
            yerr=stats["native_std"],
            capsize=5,
            color="cyan",
            label="native" if i == 0 else "",
        )
        # ax.set_xlabel(workload)
    ticklabel = (x for x in list(statistics.keys()))
    print(statistics.keys())
    ax.set_xticks(index)

    ax.set_xticklabels(ticklabel,fontsize =10)
    ax.set_ylabel("Execution time (s)")
    ax.legend()

    # add text at upper left
    ax.legend(loc="upper right")

    plt.savefig("performance_comparison.pdf")

def calculate_averages_comparison(result):
    averages = defaultdict(list)
    for workload, mvvm_values, hcontainer_values, qemu_x86_64_values, qemu_aarch64_values, native_values in result:
        averages[workload].append((mvvm_values, hcontainer_values, qemu_x86_64_values, qemu_aarch64_values, native_values))
    for workload, values in averages.items():
        mvvm_values, hcontainer_values, qemu_x86_64_values, qemu_aarch64_values, native_values = zip(*values)
        averages[workload] = {
            "mvvm": np.mean(mvvm_values),
            "hcontainer": np.mean(hcontainer_values),
            "qemu_x86_64": np.mean(qemu_x86_64_values),
            "qemu_aarch64": np.mean(qemu_aarch64_values),
            "native": np.mean(native_values),
        }
        print(workload, np.mean(mvvm_values )/np.mean(native_values))
    return averages

if __name__ == "__main__":
    # mvvm_results = [("../build/bench/hdastar.aot -a maze-6404.txt -a 8 -e",8.858212),("a=b linpack.aot", 30.06617),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.584174),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 39.399803),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 35.02072),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 34.987732),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.784216),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 35.168331),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 34.65938),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.692912),("OMP_NUM_THREADS=1 bt.aot", 84.808419),("OMP_NUM_THREADS=1 cg.aot", 32.366688),("OMP_NUM_THREADS=1 ft.aot", 40.790877),("OMP_NUM_THREADS=1 lu.aot", 0.046912),("OMP_NUM_THREADS=1 mg.aot", 26.732236),("OMP_NUM_THREADS=1 sp.aot", 17.464637),("a=b redis.aot", 241.176576),("a=b hdastar.aot maze-6404.txt 8", 10.279565),("a=b linpack.aot", 30.664042),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.640625),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 39.096505),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 36.460657),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 35.169937),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 35.485751),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 34.885553),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 35.549893),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.329971),("OMP_NUM_THREADS=1 bt.aot", 86.088249),("OMP_NUM_THREADS=1 cg.aot", 32.833793),("OMP_NUM_THREADS=1 ft.aot", 40.034767),("OMP_NUM_THREADS=1 lu.aot", 0.057583),("OMP_NUM_THREADS=1 mg.aot", 26.62458),("OMP_NUM_THREADS=1 sp.aot", 14.515114),("a=b redis.aot", 242.994483),("a=b hdastar.aot maze-6404.txt 8", 8.858212),("a=b linpack.aot", 29.008376),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 26.573535),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 38.873074),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 35.951567),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 36.361035),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 36.665305),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 35.160924),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 35.670047),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.158749),("OMP_NUM_THREADS=1 bt.aot", 84.352398),("OMP_NUM_THREADS=1 cg.aot", 32.104601),("OMP_NUM_THREADS=1 ft.aot", 41.463589),("OMP_NUM_THREADS=1 lu.aot", 0.064426),("OMP_NUM_THREADS=1 mg.aot", 27.602197),("OMP_NUM_THREADS=1 sp.aot", 15.584595),("a=b redis.aot", 243.237992),("a=b hdastar.aot maze-6404.txt 8", 7.957386),("a=b linpack.aot", 29.836055),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 29.451285),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 40.336093),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 34.768868),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 36.469427),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 35.957681),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 34.0922),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 35.300618),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 6.360993),("OMP_NUM_THREADS=1 bt.aot", 84.563536),("OMP_NUM_THREADS=1 cg.aot", 33.101476),("OMP_NUM_THREADS=1 ft.aot", 42.240096),("OMP_NUM_THREADS=1 lu.aot", 0.057878),("OMP_NUM_THREADS=1 mg.aot", 27.767437),("OMP_NUM_THREADS=1 sp.aot", 15.144339),("a=b redis.aot", 244.64784),("a=b hdastar.aot maze-6404.txt 8", 9.845638),("a=b linpack.aot", 32.447804),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 26.982592),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 38.718143),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 36.129443),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 35.03907),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.671518),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 35.304735),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 34.817553),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.387042),("OMP_NUM_THREADS=1 bt.aot", 86.865323),("OMP_NUM_THREADS=1 cg.aot", 32.358395),("OMP_NUM_THREADS=1 ft.aot", 40.465195),("OMP_NUM_THREADS=1 lu.aot", 0.083769),("OMP_NUM_THREADS=1 mg.aot", 28.080219),("OMP_NUM_THREADS=1 sp.aot", 15.638535),("a=b redis.aot", 240.778638),("a=b hdastar.aot maze-6404.txt 8", 7.96329),("a=b linpack.aot", 30.748278),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.416494),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 40.131322),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 34.184033),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 35.302234),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.587159),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 37.009367),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 35.375392),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 9.916613),("OMP_NUM_THREADS=1 bt.aot", 84.415609),("OMP_NUM_THREADS=1 cg.aot", 34.504724),("OMP_NUM_THREADS=1 ft.aot", 42.205323),("OMP_NUM_THREADS=1 lu.aot", 0.051305),("OMP_NUM_THREADS=1 mg.aot", 27.260932),("OMP_NUM_THREADS=1 sp.aot", 14.469471),("a=b redis.aot", 239.236408),("a=b hdastar.aot maze-6404.txt 8", 8.662038),("a=b linpack.aot", 31.141618),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.034958),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 38.442589),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 34.344597),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 33.162001),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.860809),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 36.050757),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 33.933716),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.298542),("OMP_NUM_THREADS=1 bt.aot", 86.822154),("OMP_NUM_THREADS=1 cg.aot", 32.695834),("OMP_NUM_THREADS=1 ft.aot", 40.713004),("OMP_NUM_THREADS=1 lu.aot", 0.059475),("OMP_NUM_THREADS=1 mg.aot", 27.964345),("OMP_NUM_THREADS=1 sp.aot", 15.135506),("a=b hdastar.aot maze-6404.txt 8", 8.940171),("a=b linpack.aot", 30.009879),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 26.319805),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 38.590552),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 34.638147),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 40.229486),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.375108),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 34.881497),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 34.905947),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.942529),("OMP_NUM_THREADS=1 bt.aot", 81.615473),("OMP_NUM_THREADS=1 cg.aot", 31.900385),("OMP_NUM_THREADS=1 ft.aot", 40.988048),("OMP_NUM_THREADS=1 lu.aot", 0.049904),("OMP_NUM_THREADS=1 mg.aot", 27.595124),("OMP_NUM_THREADS=1 sp.aot", 15.348782),("a=b hdastar.aot maze-6404.txt 8", 10.116388),("a=b linpack.aot", 28.881638),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 26.803026),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 38.192459),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 33.277288),("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 32.89712),("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 33.065028),("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 31.713299),("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 32.225434),("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 6.03101),("OMP_NUM_THREADS=1 bt.aot", 76.180545),("OMP_NUM_THREADS=1 cg.aot", 29.029776),("OMP_NUM_THREADS=1 ft.aot", 37.663777),("OMP_NUM_THREADS=1 lu.aot", 0.051351),("OMP_NUM_THREADS=1 mg.aot", 25.954153),("OMP_NUM_THREADS=1 sp.aot", 14.001794),("a=b hdastar.aot maze-6404.txt 8", 8.769647),("a=b linpack.aot", 27.815176),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.420691),("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 37.163123),("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 32.739905),("OMP_NUM_THREADS=1 lu.aot", 0.051968),("OMP_NUM_THREADS=1 mg.aot", 23.360445),("OMP_NUM_THREADS=1 sp.aot", 13.984599),("a=b redis.aot", 201.14073),("a=b hdastar.aot maze-6404.txt 8", 10.093217)]
    # native_results = run_native()
    # qemu_x86_64_results = run_qemu_x86_64()
    # # # print the results
    # qemu_aarch64_results = run_qemu_aarch64()
    # hcontainer_results = run_hcontainer()

    # write_to_csv("comparison.csv")
    
    results = read_from_csv("comparison.csv")
    plot(results)
    print(calculate_averages_comparison(results))
