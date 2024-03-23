import csv
import common_util
from common_util import plot, calculate_averages_comparison
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict

cmd = [
    "linpack",
    "llama",
    # "rgbd_tum",
    "bt",
    "cg",
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
    # "ORB_SLAM2",
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
    # ["./ORBvoc.txt", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
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
    "OMP_NUM_THREADS=1",
    # "a=b",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "a=b",
    "a=b",
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
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

                        time_object = datetime.strptime(
                            line.split()[2].replace("elapsed", ""), "%H:%M:%S"
                        ).time()
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
                    hcontainer_results[idx][1],
                    qemu_x86_64_results[idx][1],
                    qemu_aarch64_results[idx][1],
                    native_results[idx][1],
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
                )
            )
        return results


def plot(results, results1):
    font = {"size": 20}
    plt.tight_layout()
    plt.rc("font", **font)
    workloads = defaultdict(list)
    workloads1 = defaultdict(list)
    for (
        workload,
        mvvm_values,
        hcontainer_values,
        qemu_x86_64_values,
        qemu_aarch64_values,
        native_values,
    ) in results:
        if not workload.__contains__("sp") and not workload.__contains__("lu") and not workload.__contains__("tc"):
            workloads[workload.split(" ")[1].replace(".aot", "")].append(
                (
                    hcontainer_values,
                    mvvm_values,
                    qemu_x86_64_values,
                    qemu_aarch64_values,
                    native_values,
                )
            )
    statistics = {}
    for workload, times in workloads.items():
        (
            hcontainer_values,
            mvvm_values,
            qemu_x86_64_values,
            qemu_aarch64_values,
            native_values,
        ) = zip(*times)
        statistics[workload] = {
            "hcontainer_median": np.median(hcontainer_values),
            "mvvm_median": np.median(mvvm_values),
            "qemu_x86_64_median": np.median(qemu_x86_64_values),
            "qemu_aarch64_median": np.median(qemu_aarch64_values),
            "native_median": np.median(native_values),
            "hcontainer_std": np.std(hcontainer_values),
            "mvvm_std": np.std(mvvm_values),
            "qemu_x86_64_std": np.std(qemu_x86_64_values),
            "qemu_aarch64_std": np.std(qemu_aarch64_values),
            "native_std": np.std(native_values),
        }
    for (
        workload,
        mvvm_values,
        hcontainer_values,
        qemu_x86_64_values,
        qemu_aarch64_values,
        native_values,
    ) in results1:
        if not workload.__contains__("sp") and not workload.__contains__("lu") and not workload.__contains__("tc"):
            workloads1[workload.split(" ")[1].replace(".aot", "")].append(
                (
                    hcontainer_values,
                    mvvm_values,
                    qemu_x86_64_values,
                    qemu_aarch64_values,
                    native_values,
                )
            )
    statistics1 = {}
    for workload, times in workloads1.items():
        (
            hcontainer_values,
            mvvm_values,
            qemu_x86_64_values,
            qemu_aarch64_values,
            native_values,
        ) = zip(*times)
        statistics1[workload] = {
            "hcontainer_median": np.median(hcontainer_values),
            "mvvm_median": np.median(mvvm_values),
            "qemu_x86_64_median": np.median(qemu_x86_64_values),
            "qemu_aarch64_median": np.median(qemu_aarch64_values),
            "native_median": np.median(native_values),
            "hcontainer_std": np.std(hcontainer_values),
            "mvvm_std": np.std(mvvm_values),
            "qemu_x86_64_std": np.std(qemu_x86_64_values),
            "qemu_aarch64_std": np.std(qemu_aarch64_values),
            "native_std": np.std(native_values),
        }

    fig, ax = plt.subplots(figsize=(20, 10))
    index = np.arange(len(statistics))
    bar_width = 0.7 / 8

    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["native_median"],
            bar_width,
            yerr=stats["native_std"],
            capsize=5,
            color="cyan",
            label="native" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            statistics1[workload]["native_median"],
            bar_width,
            yerr=statistics1[workload]["native_std"],
            capsize=5,
            color="cyan",
        )
        ax.bar(
            index[i] + bar_width * 2,
            stats["hcontainer_median"],
            bar_width,
            yerr=stats["hcontainer_std"],
            capsize=5,
            color="blue",
            label="hcontainer" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width * 3,
            statistics1[workload]["hcontainer_median"],
            bar_width,
            yerr=statistics1[workload]["hcontainer_std"],
            capsize=5,
            color="blue",
        )

        ax.bar(
            index[i] + bar_width * 4,
            stats["mvvm_median"],
            bar_width,
            yerr=stats["mvvm_std"],
            capsize=5,
            color="red",
            label="mvvm" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width * 5,
            statistics1[workload]["mvvm_median"],
            bar_width,
            yerr=statistics1[workload]["mvvm_std"],
            capsize=5,
            color="red",
        )

        ax.bar(
            index[i] + bar_width * 6,
            stats["qemu_x86_64_median"],
            bar_width,
            yerr=stats["qemu_x86_64_std"],
            capsize=5,
            color="brown",
            label="qemu_x86_64" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width * 7,
            statistics1[workload]["qemu_x86_64_median"],
            bar_width,
            yerr=statistics1[workload]["qemu_x86_64_std"],
            capsize=5,
            color="brown",
        )

        ax.bar(
            index[i] + bar_width * 8,
            stats["qemu_aarch64_median"],
            bar_width,
            yerr=stats["qemu_aarch64_std"],
            capsize=5,
            color="purple",
            label="qemu_aarch64" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width * 9,
            statistics1[workload]["qemu_aarch64_median"],
            bar_width,
            yerr=statistics1[workload]["qemu_aarch64_std"],
            capsize=5,
            color="purple",
        )

        # ax.set_xlabel(workload)
    ticklabel = (x for x in list(statistics.keys()))
    print(statistics.keys())
    ax.set_xticks(index + bar_width * 4)

    ax.set_xticklabels(ticklabel, fontsize=20)
    ax.set_ylabel("Execution time (s)")
    ax.legend()

    # add text at upper left
    ax.legend(loc="upper right")

    plt.savefig("performance_comparison.pdf")


if __name__ == "__main__":
    # mvvm_results = [("a=b linpack.aot",34.021069),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",29.995634),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",29.995634),("OMP_NUM_THREADS=1 bt.aot",97.646537),("OMP_NUM_THREADS=1 cg.aot",37.130862),("OMP_NUM_THREADS=1 ft.aot",46.20586),("OMP_NUM_THREADS=1 lu.aot",0.050734),("OMP_NUM_THREADS=1 mg.aot",29.199269),("OMP_NUM_THREADS=1 sp.aot",16.257109),("a=b redis.aot",296.145142),("a=b hdastar.aot maze-6404.txt 8",11.045916),("a=b linpack.aot",34.206223),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",27.570724),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",27.570724),("OMP_NUM_THREADS=1 bt.aot",95.72704),("OMP_NUM_THREADS=1 cg.aot",35.174173),("OMP_NUM_THREADS=1 ft.aot",46.675348),("OMP_NUM_THREADS=1 lu.aot",0.058697),("OMP_NUM_THREADS=1 mg.aot",31.437348),("OMP_NUM_THREADS=1 sp.aot",17.091653),("a=b redis.aot",288.328081),("a=b hdastar.aot maze-6404.txt 8",11.405105),("a=b linpack.aot",35.299232),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",28.656332),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",28.656332),("OMP_NUM_THREADS=1 bt.aot",95.897016),("OMP_NUM_THREADS=1 cg.aot",35.501491),("OMP_NUM_THREADS=1 ft.aot",46.906197),("OMP_NUM_THREADS=1 lu.aot",0.03796),("OMP_NUM_THREADS=1 mg.aot",29.226483),("OMP_NUM_THREADS=1 sp.aot",17.092985),("a=b redis.aot",288.250117),("a=b hdastar.aot maze-6404.txt 8",10.65366),("a=b linpack.aot",33.738135),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",27.527596),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",27.527596),("OMP_NUM_THREADS=1 bt.aot",94.089478),("OMP_NUM_THREADS=1 cg.aot",35.294456),("OMP_NUM_THREADS=1 ft.aot",47.10809),("OMP_NUM_THREADS=1 lu.aot",0.053327),("OMP_NUM_THREADS=1 mg.aot",29.876738),("OMP_NUM_THREADS=1 sp.aot",17.344869),("a=b redis.aot",286.45943),("a=b hdastar.aot maze-6404.txt 8",11.559773),("a=b linpack.aot",33.81198),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",28.370864),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",28.370864),("OMP_NUM_THREADS=1 bt.aot",95.656937),("OMP_NUM_THREADS=1 cg.aot",36.493237),("OMP_NUM_THREADS=1 ft.aot",49.011925),("OMP_NUM_THREADS=1 lu.aot",0.050512),("OMP_NUM_THREADS=1 mg.aot",30.709787),("OMP_NUM_THREADS=1 sp.aot",17.512012),("a=b redis.aot",283.937262),("a=b hdastar.aot maze-6404.txt 8",11.183503),("a=b linpack.aot",32.98269),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",29.27254),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",29.27254),("OMP_NUM_THREADS=1 bt.aot",95.943804),("OMP_NUM_THREADS=1 cg.aot",36.596166),("OMP_NUM_THREADS=1 ft.aot",47.009408),("OMP_NUM_THREADS=1 lu.aot",0.044345),("OMP_NUM_THREADS=1 mg.aot",29.069261),("OMP_NUM_THREADS=1 sp.aot",17.241978),("a=b redis.aot",287.613588),("a=b hdastar.aot maze-6404.txt 8",11.186353),("a=b linpack.aot",33.10233),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",28.730093),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",28.730093),("OMP_NUM_THREADS=1 bt.aot",97.747853),("OMP_NUM_THREADS=1 cg.aot",36.673356),("OMP_NUM_THREADS=1 ft.aot",46.847511),("OMP_NUM_THREADS=1 lu.aot",0.052715),("OMP_NUM_THREADS=1 mg.aot",30.12969),("OMP_NUM_THREADS=1 sp.aot",16.956676),("a=b redis.aot",280.486803),("a=b hdastar.aot maze-6404.txt 8",11.134241),("a=b linpack.aot",35.097291),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",28.005771),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",28.005771),("OMP_NUM_THREADS=1 bt.aot",96.974046),("OMP_NUM_THREADS=1 cg.aot",36.951569),("OMP_NUM_THREADS=1 ft.aot",48.541943),("OMP_NUM_THREADS=1 lu.aot",0.063633),("OMP_NUM_THREADS=1 mg.aot",31.42804),("OMP_NUM_THREADS=1 sp.aot",16.8186),("a=b redis.aot",275.056764),("a=b hdastar.aot maze-6404.txt 8",11.378461),("a=b linpack.aot",33.157102),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",27.924637),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",27.924637),("OMP_NUM_THREADS=1 bt.aot",95.841526),("OMP_NUM_THREADS=1 cg.aot",35.334907),("OMP_NUM_THREADS=1 ft.aot",47.986966),("OMP_NUM_THREADS=1 lu.aot",0.061085),("OMP_NUM_THREADS=1 mg.aot",29.009124),("OMP_NUM_THREADS=1 sp.aot",17.008031),("a=b redis.aot",263.225509),("a=b hdastar.aot maze-6404.txt 8",9.820092),("a=b linpack.aot",32.903653),("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0",28.312841),("a=b rgbd_tum.aot ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt",28.312841),("OMP_NUM_THREADS=1 bt.aot",89.99542),("OMP_NUM_THREADS=1 cg.aot",34.771269),("OMP_NUM_THREADS=1 ft.aot",44.030195),("OMP_NUM_THREADS=1 lu.aot",0.046369),("OMP_NUM_THREADS=1 mg.aot",29.505974),("OMP_NUM_THREADS=1 sp.aot",17.565224),("a=b redis.aot",250.541232),("a=b hdastar.aot maze-6404.txt 8",12.546132)]
    # # print("mvvm_results=", mvvm_results)
    # # native_results = run_native()
    # # print("native_results=", native_results)
    # native_results= [('a=b linpack', 34.025), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 26.027), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.004), ('OMP_NUM_THREADS=1 bt', 57.026), ('OMP_NUM_THREADS=1 cg', 23.001), ('OMP_NUM_THREADS=1 ft', 33.019), ('OMP_NUM_THREADS=1 lu', 0.007), ('OMP_NUM_THREADS=1 mg', 11.0), ('OMP_NUM_THREADS=1 sp', 2.0), ('a=b redis', 23.039), ('a=b hdastar maze-6404.txt 8', 4.051), ('a=b linpack', 32.026), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 25.078), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.005), ('OMP_NUM_THREADS=1 bt', 54.019), ('OMP_NUM_THREADS=1 cg', 21.093), ('OMP_NUM_THREADS=1 ft', 30.087), ('OMP_NUM_THREADS=1 lu', 0.007), ('OMP_NUM_THREADS=1 mg', 12.052), ('OMP_NUM_THREADS=1 sp', 1.08), ('a=b redis', 21.027), ('a=b hdastar maze-6404.txt 8', 5.011), ('a=b linpack', 33.02), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 25.067), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 54.022), ('OMP_NUM_THREADS=1 cg', 22.06), ('OMP_NUM_THREADS=1 ft', 32.004), ('OMP_NUM_THREADS=1 lu', 0.003), ('OMP_NUM_THREADS=1 mg', 10.067), ('OMP_NUM_THREADS=1 sp', 1.024), ('a=b redis', 20.034), ('a=b hdastar maze-6404.txt 8', 4.078), ('a=b linpack', 32.025), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 29.091), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.002), ('OMP_NUM_THREADS=1 bt', 53.063), ('OMP_NUM_THREADS=1 cg', 21.099), ('OMP_NUM_THREADS=1 ft', 30.027), ('OMP_NUM_THREADS=1 lu', 0.004), ('OMP_NUM_THREADS=1 mg', 10.026), ('OMP_NUM_THREADS=1 sp', 1.023), ('a=b redis', 25.023), ('a=b hdastar maze-6404.txt 8', 25.023), ('a=b linpack', 32.021), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 30.077), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.002), ('OMP_NUM_THREADS=1 bt', 53.075), ('OMP_NUM_THREADS=1 cg', 22.039), ('OMP_NUM_THREADS=1 ft', 31.062), ('OMP_NUM_THREADS=1 lu', 0.004), ('OMP_NUM_THREADS=1 mg', 10.059), ('OMP_NUM_THREADS=1 sp', 1.03), ('a=b redis', 18.038), ('a=b hdastar maze-6404.txt 8', 6.082), ('a=b linpack', 31.094), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 29.044), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 53.048), ('OMP_NUM_THREADS=1 cg', 21.052), ('OMP_NUM_THREADS=1 ft', 31.047), ('OMP_NUM_THREADS=1 lu', 0.003), ('OMP_NUM_THREADS=1 mg', 13.042), ('OMP_NUM_THREADS=1 sp', 1.024), ('a=b redis', 19.006), ('a=b hdastar maze-6404.txt 8', 7.05), ('a=b linpack', 32.075), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 25.041), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.004), ('OMP_NUM_THREADS=1 bt', 52.045), ('OMP_NUM_THREADS=1 cg', 22.087), ('OMP_NUM_THREADS=1 ft', 32.04), ('OMP_NUM_THREADS=1 lu', 0.008), ('OMP_NUM_THREADS=1 mg', 10.067), ('OMP_NUM_THREADS=1 sp', 1.063), ('a=b redis', 20.054), ('a=b hdastar maze-6404.txt 8', 5.033), ('a=b linpack', 33.081), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 25.064), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.004), ('OMP_NUM_THREADS=1 bt', 51.014), ('OMP_NUM_THREADS=1 cg', 21.089), ('OMP_NUM_THREADS=1 ft', 32.032), ('OMP_NUM_THREADS=1 lu', 0.003), ('OMP_NUM_THREADS=1 mg', 10.04), ('OMP_NUM_THREADS=1 sp', 1.024), ('a=b redis', 19.049), ('a=b hdastar maze-6404.txt 8', 5.041), ('a=b linpack', 32.081), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 29.003), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 49.034), ('OMP_NUM_THREADS=1 cg', 22.002), ('OMP_NUM_THREADS=1 ft', 31.013), ('OMP_NUM_THREADS=1 lu', 0.003), ('OMP_NUM_THREADS=1 mg', 10.027), ('OMP_NUM_THREADS=1 sp', 1.023), ('a=b redis', 20.073), ('a=b hdastar maze-6404.txt 8', 4.035), ('a=b linpack', 30.021), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 28.049), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 46.038), ('OMP_NUM_THREADS=1 cg', 21.04), ('OMP_NUM_THREADS=1 ft', 28.06), ('OMP_NUM_THREADS=1 lu', 0.003), ('OMP_NUM_THREADS=1 mg', 10.045), ('OMP_NUM_THREADS=1 sp', 1.02), ('a=b redis', 18.048), ('a=b hdastar maze-6404.txt 8', 4.081)]
    # # qemu_x86_64_results = run_qemu_x86_64()
    # qemu_x86_64_results = [('a=b linpack', 35.096), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 5618), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.006), ('OMP_NUM_THREADS=1 bt', 7830), ('OMP_NUM_THREADS=1 cg', 660.065), ('OMP_NUM_THREADS=1 ft', 7611), ('OMP_NUM_THREADS=1 lu', 3.046), ('OMP_NUM_THREADS=1 mg', 3361.038), ('OMP_NUM_THREADS=1 sp', 155.066), ('a=b redis', 119.01), ('a=b hdastar maze-6404.txt 8', 11.001), ('a=b linpack', 36.014), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 5615), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.005), ('OMP_NUM_THREADS=1 bt', 0.005), ('OMP_NUM_THREADS=1 cg', 660.004), ('OMP_NUM_THREADS=1 ft', 660.004), ('OMP_NUM_THREADS=1 lu', 3.031), ('OMP_NUM_THREADS=1 mg', 3413.076), ('OMP_NUM_THREADS=1 sp', 155.022), ('a=b redis', 211.012), ('a=b hdastar maze-6404.txt 8', 9.052), ('a=b linpack', 39.049), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 39.049), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.003), ('OMP_NUM_THREADS=1 bt', 0.003), ('OMP_NUM_THREADS=1 cg', 654.092), ('OMP_NUM_THREADS=1 ft', 654.092), ('OMP_NUM_THREADS=1 lu', 2.075), ('OMP_NUM_THREADS=1 mg', 3404.098), ('OMP_NUM_THREADS=1 sp', 182.026), ('a=b redis', 281.051), ('a=b hdastar maze-6404.txt 8', 281.051), ('a=b linpack', 63.074), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 6267), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.003), ('OMP_NUM_THREADS=1 bt', 0.003), ('OMP_NUM_THREADS=1 cg', 634.09), ('OMP_NUM_THREADS=1 ft', 7436), ('OMP_NUM_THREADS=1 lu', 2.082), ('OMP_NUM_THREADS=1 mg', 3368.078), ('OMP_NUM_THREADS=1 sp', 154.015), ('a=b redis', 336.001), ('a=b hdastar maze-6404.txt 8', 336.001), ('a=b linpack', 144.05), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 7414), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.003), ('OMP_NUM_THREADS=1 bt', 7462), ('OMP_NUM_THREADS=1 cg', 613.075), ('OMP_NUM_THREADS=1 ft', 6135), ('OMP_NUM_THREADS=1 lu', 2.081), ('OMP_NUM_THREADS=1 mg', 3269.094), ('OMP_NUM_THREADS=1 sp', 153.095), ('a=b redis', 261.009), ('a=b hdastar maze-6404.txt 8', 261.009), ('a=b linpack', 36.009), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 5431), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.005), ('OMP_NUM_THREADS=1 bt', 7536), ('OMP_NUM_THREADS=1 cg', 620.086), ('OMP_NUM_THREADS=1 ft', 6205), ('OMP_NUM_THREADS=1 lu', 2.068), ('OMP_NUM_THREADS=1 mg', 3256.077), ('OMP_NUM_THREADS=1 sp', 156.033), ('a=b redis', 236.085), ('a=b hdastar maze-6404.txt 8', 12.052), ('a=b linpack', 35.063), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 6234), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.004), ('OMP_NUM_THREADS=1 bt', 7416), ('OMP_NUM_THREADS=1 cg', 617.067), ('OMP_NUM_THREADS=1 ft', 6235), ('OMP_NUM_THREADS=1 lu', 2.089), ('OMP_NUM_THREADS=1 mg', 3264.088), ('OMP_NUM_THREADS=1 sp', 153.005), ('a=b redis', 231.013), ('a=b hdastar maze-6404.txt 8', 231.013), ('a=b linpack', 35.084), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 5530), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.002), ('OMP_NUM_THREADS=1 bt', 7580), ('OMP_NUM_THREADS=1 cg', 651.054), ('OMP_NUM_THREADS=1 ft', 7391), ('OMP_NUM_THREADS=1 lu', 2.035), ('OMP_NUM_THREADS=1 mg', 3263.014), ('OMP_NUM_THREADS=1 sp', 154.016), ('a=b redis', 256.025), ('a=b hdastar maze-6404.txt 8',43730), ('a=b linpack', 37.063), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 5513), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.002), ('OMP_NUM_THREADS=1 bt', >>>), ('OMP_NUM_THREADS=1 cg', 623.095), ('OMP_NUM_THREADS=1 ft', 7223), ('OMP_NUM_THREADS=1 lu', 2.035), ('OMP_NUM_THREADS=1 mg', 3232.095), ('OMP_NUM_THREADS=1 sp', 153.046), ('a=b redis', 228.085), ('a=b hdastar maze-6404.txt 8', 9.048), ('a=b linpack', 35.056), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 5228), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.004), ('OMP_NUM_THREADS=1 bt', 6998), ('OMP_NUM_THREADS=1 cg', 615.044), ('OMP_NUM_THREADS=1 ft', 6814), ('OMP_NUM_THREADS=1 lu', 2.037), ('OMP_NUM_THREADS=1 mg', 3164.03), ('OMP_NUM_THREADS=1 sp', 154.067), ('a=b redis', 221.009), ('a=b hdastar maze-6404.txt 8', 10.077)]
    # # # print the results
    # # print("qemu_x86_64_results=", qemu_x86_64_results)
    # qemu_aarch64_results = run_qemu_aarch64()
    # print("qemu_aarch64_results=", qemu_aarch64_results)
    # # hcontainer_results = run_hcontainer()
    # # print("hcontainer_results=", hcontainer_results)
    # hcontainer_results= [('a=b linpack', 26.038), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 28.023), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 171.037), ('OMP_NUM_THREADS=1 cg', 25.09), ('OMP_NUM_THREADS=1 ft', 69.088), ('OMP_NUM_THREADS=1 lu', 0.02), ('OMP_NUM_THREADS=1 mg', 18.048), ('OMP_NUM_THREADS=1 sp', 1.024), ('a=b redis', 169.013), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 26.05), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 28.085), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 172.038), ('OMP_NUM_THREADS=1 cg', 25.032), ('OMP_NUM_THREADS=1 ft', 73.039), ('OMP_NUM_THREADS=1 lu', 0.02), ('OMP_NUM_THREADS=1 mg', 17.097), ('OMP_NUM_THREADS=1 sp', 1.027), ('a=b redis', 195.034), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 26.023), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 27.098), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 173.055), ('OMP_NUM_THREADS=1 cg', 25.001), ('OMP_NUM_THREADS=1 ft', 71.098), ('OMP_NUM_THREADS=1 lu', 0.004), ('OMP_NUM_THREADS=1 mg', 17.057), ('OMP_NUM_THREADS=1 sp', 1.041), ('a=b redis', 174.041), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 25.092), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 27.087), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 168.01), ('OMP_NUM_THREADS=1 cg', 24.095), ('OMP_NUM_THREADS=1 ft', 72.004), ('OMP_NUM_THREADS=1 lu', 0.005), ('OMP_NUM_THREADS=1 mg', 17.088), ('OMP_NUM_THREADS=1 sp', 1.067), ('a=b redis', 179.019), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 26.064), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 27.061), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 171.028), ('OMP_NUM_THREADS=1 cg', 24.075), ('OMP_NUM_THREADS=1 ft', 72.025), ('OMP_NUM_THREADS=1 lu', 0.005), ('OMP_NUM_THREADS=1 mg', 17.05), ('OMP_NUM_THREADS=1 sp', 1.06), ('a=b redis', 205.011), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 28.047), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 30.028), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 165.071), ('OMP_NUM_THREADS=1 cg', 25.0), ('OMP_NUM_THREADS=1 ft', 71.06), ('OMP_NUM_THREADS=1 lu', 0.005), ('OMP_NUM_THREADS=1 mg', 17.011), ('OMP_NUM_THREADS=1 sp', 1.071), ('a=b redis', 167.011), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 26.087), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 26.099), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 163.098), ('OMP_NUM_THREADS=1 cg', 25.089), ('OMP_NUM_THREADS=1 ft', 66.056), ('OMP_NUM_THREADS=1 lu', 0.006), ('OMP_NUM_THREADS=1 mg', 18.018), ('OMP_NUM_THREADS=1 sp', 1.052), ('a=b redis', 161.032), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 27.05), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 26.082), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 155.076), ('OMP_NUM_THREADS=1 cg', 24.088), ('OMP_NUM_THREADS=1 ft', 68.08), ('OMP_NUM_THREADS=1 lu', 0.004), ('OMP_NUM_THREADS=1 mg', 17.069), ('OMP_NUM_THREADS=1 sp', 1.065), ('a=b redis', 189.096), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 27.053), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 28.003), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 147.058), ('OMP_NUM_THREADS=1 cg', 26.021), ('OMP_NUM_THREADS=1 ft', 64.068), ('OMP_NUM_THREADS=1 lu', 0.005), ('OMP_NUM_THREADS=1 mg', 18.001), ('OMP_NUM_THREADS=1 sp', 1.013), ('a=b redis', 164.056), ('a=b hdastar maze-6404.txt 8', 0.0), ('a=b linpack', 26.035), ('OMP_NUM_THREADS=1 llama stories110M.bin -z tokenizer.bin -t 0.0', 27.024), ('a=b rgbd_tum ./ORBvoc.txt ./TUM3.yaml ./ ./associations/fr1_xyz.txt', 0.0), ('OMP_NUM_THREADS=1 bt', 139.076), ('OMP_NUM_THREADS=1 cg', 24.047), ('OMP_NUM_THREADS=1 ft', 60.056), ('OMP_NUM_THREADS=1 lu', 0.005), ('OMP_NUM_THREADS=1 mg', 17.008), ('OMP_NUM_THREADS=1 sp', 1.023), ('a=b redis', 138.061), ('a=b hdastar maze-6404.txt 8', 0.0)]
    # write_to_csv("comparison.csv")

    results = read_from_csv("comparison.csv")
    print(calculate_averages_comparison(results))
    results1 = read_from_csv("comparison_mac.csv")
    print(calculate_averages_comparison(results1))
    plot(results, results1)
