import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict

cmd = [
    "linpack",
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
    "hdastar",
]
arg = [
    [],
    ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
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
    [],
    ["maze-6404.txt", "8"],
]
envs = [
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
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "a=b",
    "a=b",
]

pool = Pool(processes=10)


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
                exec_time = line.split()[-2]
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
        # print(exec, output)
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
                    if line.__contains__("user"):
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
        # print(exec, output)
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
        # print(exec, output)
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
                    if line.__contains__("user"):
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
        # print(exec, output)
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
                    if line.__contains__("user"):
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
            results.append((row[0], float(row[1]), float(row[2]), float(row[3]), float(row[4]), float(row[5])))
        return results

def plot(results):
    font = {'size': 40}
    plt.rc('font', **font)
    workloads = defaultdict(list)
    for workload, hcontainer_values, mvvm_values, qemu_x86_64_values, qemu_aarch64_values, native_values in results:
        workloads[
            workload.replace("OMP_NUM_THREADS=", "")
            .replace("-g20", "")
            .replace("-n300", "")
            .replace(" -f ", "")
            .replace("-vn300", "")
            .replace("maze-6404.txt", "")
            .replace("stories110M.bin", "")
            .replace("-z tokenizer.bin -t 0.0", "").replace("a=b", "").replace("1", "").replace(".aot", "").replace("8", "")
            .strip()
        ].append((hcontainer_values, mvvm_values, qemu_x86_64_values, qemu_aarch64_values, native_values))

    statistics = {}
    for workload, times in workloads.items():
        hcontainer_values, mvvm_values, qemu_x86_64_values, qemu_aarch64_values, native_values = zip(*times)
        native_median = np.median(native_values)
        statistics[workload] = {
            "hcontainer_median": np.median(hcontainer_values) / native_median,
            "mvvm_median": np.median(mvvm_values) / native_median,
            "qemu_x86_64_median": np.median(qemu_x86_64_values) / native_median,
            "qemu_aarch64_median": np.median(qemu_aarch64_values) / native_median,
            "native_median": 1.0,  # normalized to 1
            "hcontainer_std": np.std(hcontainer_values) / native_median,
            "mvvm_std": np.std(mvvm_values) / native_median,
            "qemu_x86_64_std": np.std(qemu_x86_64_values) / native_median,
            "qemu_aarch64_std": np.std(qemu_aarch64_values) / native_median,
            "native_std": np.std(native_values) / native_median,
        }

    fig, ax = plt.subplots(figsize=(20, 10))
    index = np.arange(len(statistics))
    bar_width = 0.18
    bar_height = 30
    
    colors = {
        'hcontainer': 'blue',
        'mvvm': 'red',
        'qemu_x86_64': 'brown',
        'qemu_aarch64': 'purple',
        'native': 'cyan'
    }

    def add_value_label(x, y, value, std):
        if value >= 1000000:
            formatted_value = f'{value/1000000:.1f}M'
        elif value >= 1000:
            formatted_value = f'{value/1000:.1f}k'
        else:
            formatted_value = f'{value:.1f}'
        if formatted_value == "0.0":
            ax.text(x, y + 0.1, f'x', 
                ha='center', va='bottom', color='red', fontsize=30)
        else:
            ax.text(x, y + std + 0.1, f'{formatted_value}x', 
                ha='center', va='bottom', rotation=45, fontsize=20)

    def add_value_label_30(x, y, value, std):
        if value >= 1000000:
            formatted_value = f'{value/1000000:.1f}M'
        elif value >= 1000:
            formatted_value = f'{value/1000:.1f}k'
        else:
            formatted_value = f'{value:.1f}'
        ax.text(x, bar_height+0.1, f'{formatted_value}x', 
                ha='center', va='bottom', rotation=45, fontsize=20)

    for i, (workload, stats) in enumerate(statistics.items()):
        positions = [
            (index[i], stats["native_median"], stats["native_std"], "native"),
            (index[i] + bar_width, stats["mvvm_median"], stats["mvvm_std"], "mvvm"),
            (index[i] + bar_width * 2, stats["hcontainer_median"], stats["hcontainer_std"], "hcontainer"),
            (index[i] + bar_width * 3, stats["qemu_x86_64_median"], stats["qemu_x86_64_std"], "qemu_x86_64"),
            (index[i] + bar_width * 4, stats["qemu_aarch64_median"], stats["qemu_aarch64_std"], "qemu_aarch64"),
        ]

        for pos, median, std, label in positions:
            bar = ax.bar(
                pos,
                min(median, bar_height),
                bar_width,
                yerr=std,
                capsize=5,
                color=colors[label],
                label=label if i == 0 else ""
            )
            if label != "native" and median < 30:
                add_value_label(pos, median, median, std)
            elif median >= 30:
                add_value_label_30(pos, median, median, std)

    ticklabel = list(statistics.keys())
    ax.set_xticks(index + bar_width * 2)
    ax.set_xticklabels(ticklabel,rotation=45, fontsize=30)
    ax.set_ylabel("Normalized execution time")
    ax.legend(loc="upper left", fontsize=30, ncol=1)
    ax.set_ylim(0, 30)
    ax.grid(True, axis='y', linestyle='--', alpha=0.3)

    for container in ax.containers:
        if hasattr(container, 'patches'):
            for bar in container.patches:
                if bar is not None:
                    height = bar.get_height()
                    if height >= 30:
                        ax.text(bar.get_x() + bar.get_width()/2, 29, 
                               '↑', ha='center', va='bottom', 
                               color='black', fontsize=30)

    plt.tight_layout()
    plt.savefig("performance_comparison.pdf", bbox_inches='tight', dpi=300)


if __name__ == "__main__":
    # mvvm_results = run_mvvm()
#     mvvm_results = [("a=b linpack.aot", 24.051724),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.498126),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 34.554974),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 35.418895),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 33.874318),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.727675),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 34.455454),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 35.989471),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.745931),
# ("OMP_NUM_THREADS=1 tc.aot", 0.00075),
# ("OMP_NUM_THREADS=1 bt.aot", 74.333353),
# ("OMP_NUM_THREADS=1 cg.aot", 27.475734),
# ("OMP_NUM_THREADS=1 ft.aot", 22.433527),
# ("OMP_NUM_THREADS=1 lu.aot", 0.041839),
# ("OMP_NUM_THREADS=1 mg.aot", 21.66601),
# ("OMP_NUM_THREADS=1 sp.aot", 15.039917),
# ("a=b redis.aot", 222.849357),
# ("a=b hdastar.aot maze-6404.txt 8", 0.070218),
# ("a=b linpack.aot", 24.193076),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 25.841198),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 33.86114),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 36.475341),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 33.478864),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 33.23006),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 33.654952),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 33.854174),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.626626),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000513),
# ("OMP_NUM_THREADS=1 bt.aot", 76.380093),
# ("OMP_NUM_THREADS=1 cg.aot", 27.031861),
# ("OMP_NUM_THREADS=1 ft.aot", 21.630726),
# ("OMP_NUM_THREADS=1 lu.aot", 0.057158),
# ("OMP_NUM_THREADS=1 mg.aot", 20.645581),
# ("OMP_NUM_THREADS=1 sp.aot", 17.325489),
# ("a=b redis.aot", 222.942524),
# ("a=b hdastar.aot maze-6404.txt 8", 0.058349),
# ("a=b linpack.aot", 23.704412),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 25.68846),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 32.780387),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 32.892309),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 32.56815),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 33.698366),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 36.040531),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 34.412912),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.573794),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000626),
# ("OMP_NUM_THREADS=1 bt.aot", 73.828508),
# ("OMP_NUM_THREADS=1 cg.aot", 26.703423),
# ("OMP_NUM_THREADS=1 ft.aot", 23.164908),
# ("OMP_NUM_THREADS=1 lu.aot", 0.062608),
# ("OMP_NUM_THREADS=1 mg.aot", 20.992349),
# ("OMP_NUM_THREADS=1 sp.aot", 17.213291),
# ("a=b redis.aot", 219.857292),
# ("a=b hdastar.aot maze-6404.txt 8", 0.055414),
# ("a=b linpack.aot", 23.704412),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 25.68846),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 32.780387),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 32.892309),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 32.56815),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 33.698366),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 36.040531),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 34.412912),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.573794),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000626),
# ("OMP_NUM_THREADS=1 bt.aot", 73.828508),
# ("OMP_NUM_THREADS=1 cg.aot", 26.703423),
# ("OMP_NUM_THREADS=1 ft.aot", 23.164908),
# ("OMP_NUM_THREADS=1 lu.aot", 0.062608),
# ("OMP_NUM_THREADS=1 mg.aot", 20.992349),
# ("OMP_NUM_THREADS=1 sp.aot", 17.213291),
# ("a=b redis.aot", 219.857292),
# ("a=b hdastar.aot maze-6404.txt 8", 0.055414),
# ("a=b linpack.aot", 24.064801),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 25.387697),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 33.872854),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 32.967307),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 31.90839),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.708826),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 33.311904),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 35.06914),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 6.164746),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000954),
# ("OMP_NUM_THREADS=1 bt.aot", 74.377462),
# ("OMP_NUM_THREADS=1 cg.aot", 30.048014),
# ("OMP_NUM_THREADS=1 ft.aot", 22.399091),
# ("OMP_NUM_THREADS=1 lu.aot", 0.056737),
# ("OMP_NUM_THREADS=1 mg.aot", 22.3365),
# ("OMP_NUM_THREADS=1 sp.aot", 16.720873),
# ("a=b redis.aot", 217.359213),
# ("a=b hdastar.aot maze-6404.txt 8", 0.05848),
# ("a=b linpack.aot", 27.333117),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 25.470631),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 32.572486),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 33.682446),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 31.532664),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 36.579764),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 32.612183),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 33.402302),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 6.306509),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000922),
# ("OMP_NUM_THREADS=1 bt.aot", 77.100751),
# ("OMP_NUM_THREADS=1 cg.aot", 28.81345),
# ("OMP_NUM_THREADS=1 ft.aot", 22.227123),
# ("OMP_NUM_THREADS=1 lu.aot", 0.056462),
# ("OMP_NUM_THREADS=1 mg.aot", 21.28103),
# ("OMP_NUM_THREADS=1 sp.aot", 16.831051),
# ("a=b redis.aot", 218.655539),
# ("a=b hdastar.aot maze-6404.txt 8", 0.057008),
# ("a=b linpack.aot", 24.040806),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 26.275667),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 32.650548),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 34.498504),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 33.3976),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 32.925),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 33.887107),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 33.094112),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 10.478637),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000588),
# ("OMP_NUM_THREADS=1 bt.aot", 74.188404),
# ("OMP_NUM_THREADS=1 cg.aot", 26.417047),
# ("OMP_NUM_THREADS=1 ft.aot", 23.13545),
# ("OMP_NUM_THREADS=1 lu.aot", 0.057645),
# ("OMP_NUM_THREADS=1 mg.aot", 20.867812),
# ("OMP_NUM_THREADS=1 sp.aot", 15.441146),
# ("a=b redis.aot", 219.433262),
# ("a=b hdastar.aot maze-6404.txt 8", 0.08584),
# ("a=b linpack.aot", 23.75123),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 25.421627),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 32.069046),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 32.797024),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 31.807405),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 32.873512),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 33.391417),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 34.64371),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.197638),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000503),
# ("OMP_NUM_THREADS=1 bt.aot", 74.243422),
# ("OMP_NUM_THREADS=1 cg.aot", 27.738641),
# ("OMP_NUM_THREADS=1 ft.aot", 23.062701),
# ("OMP_NUM_THREADS=1 lu.aot", 0.058234),
# ("OMP_NUM_THREADS=1 mg.aot", 21.837959),
# ("OMP_NUM_THREADS=1 sp.aot", 16.355948),
# ("a=b redis.aot", 215.286749),
# ("a=b hdastar.aot maze-6404.txt 8", 0.511079),
# ("a=b linpack.aot", 24.693788),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 24.973263),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 33.972158),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 33.319535),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 31.967302),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 34.917329),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 34.403851),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 32.555248),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.728645),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000986),
# ("OMP_NUM_THREADS=1 bt.aot", 73.747182),
# ("OMP_NUM_THREADS=1 cg.aot", 26.627856),
# ("OMP_NUM_THREADS=1 ft.aot", 22.501073),
# ("OMP_NUM_THREADS=1 lu.aot", 0.03408),
# ("OMP_NUM_THREADS=1 mg.aot", 20.495007),
# ("OMP_NUM_THREADS=1 sp.aot", 15.445544),
# ("a=b redis.aot", 215.351272),
# ("a=b hdastar.aot maze-6404.txt 8", 0.461437),
# ("a=b linpack.aot", 24.361359),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 26.233969),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 32.464246),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 32.340579),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 31.554139),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 32.364611),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 33.028051),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 32.718957),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 6.257637),
# ("OMP_NUM_THREADS=1 tc.aot", 0.000669),
# ("OMP_NUM_THREADS=1 bt.aot", 72.610964),
# ("OMP_NUM_THREADS=1 cg.aot", 26.544318),
# ("OMP_NUM_THREADS=1 ft.aot", 22.094545),
# ("OMP_NUM_THREADS=1 lu.aot", 0.058129),
# ("OMP_NUM_THREADS=1 mg.aot", 20.578836),
# ("OMP_NUM_THREADS=1 sp.aot", 15.370683),
# ("a=b redis.aot", 215.007835),
# ("a=b hdastar.aot maze-6404.txt 8", 0.270007)]
#     native_results = [("a=b linpack.aot", 23.72),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 29.038),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 6.079),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 7.009),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 6.055),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.05),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 7.029),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 7.052),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.046),
# ("OMP_NUM_THREADS=1 tc.aot", 20.38),
# ("OMP_NUM_THREADS=1 bt.aot", 38.04),
# ("OMP_NUM_THREADS=1 cg.aot", 16.085),
# ("OMP_NUM_THREADS=1 ft.aot", 24.052),
# ("OMP_NUM_THREADS=1 lu.aot", 0.004),
# ("OMP_NUM_THREADS=1 mg.aot", 8.082),
# ("OMP_NUM_THREADS=1 sp.aot", 1.026),
# ("a=b redis.aot", 1.007),
# ("a=b hdastar.aot maze-6404.txt 8", 5.036),
# ("a=b linpack.aot", 23.83),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.049),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 6.089),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 7.029),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 7.024),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 5.089),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 6.073),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 6.096),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.095),
# ("OMP_NUM_THREADS=1 tc.aot", 20.16),
# ("OMP_NUM_THREADS=1 bt.aot", 39.035),
# ("OMP_NUM_THREADS=1 cg.aot", 17.044),
# ("OMP_NUM_THREADS=1 ft.aot", 22.041),
# ("OMP_NUM_THREADS=1 lu.aot", 0.003),
# ("OMP_NUM_THREADS=1 mg.aot", 8.023),
# ("OMP_NUM_THREADS=1 sp.aot", 0.085),
# ("a=b redis.aot", 1.002),
# ("a=b hdastar.aot maze-6404.txt 8", 5.072),
# ("a=b linpack.aot", 22.63),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 23.075),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 6.038),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 6.064),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 6.008),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.043),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 6.094),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 7.047),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.012),
# ("OMP_NUM_THREADS=1 tc.aot", 20.87),
# ("OMP_NUM_THREADS=1 bt.aot", 40.042),
# ("OMP_NUM_THREADS=1 cg.aot", 17.006),
# ("OMP_NUM_THREADS=1 ft.aot", 22.081),
# ("OMP_NUM_THREADS=1 lu.aot", 0.003),
# ("OMP_NUM_THREADS=1 mg.aot", 8.02),
# ("OMP_NUM_THREADS=1 sp.aot", 1.04),
# ("a=b redis.aot", 1.005),
# ("a=b hdastar.aot maze-6404.txt 8", 7.005),
# ("a=b linpack.aot", 22.93),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 27.093),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 7.044),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 6.063),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 6.074),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.04),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 7.006),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 7.082),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 6.076),
# ("OMP_NUM_THREADS=1 tc.aot", 21.13),
# ("OMP_NUM_THREADS=1 bt.aot", 38.041),
# ("OMP_NUM_THREADS=1 cg.aot", 17.02),
# ("OMP_NUM_THREADS=1 ft.aot", 22.0),
# ("OMP_NUM_THREADS=1 lu.aot", 0.002),
# ("OMP_NUM_THREADS=1 mg.aot", 8.048),
# ("OMP_NUM_THREADS=1 sp.aot", 1.033),
# ("a=b redis.aot", 1.007),
# ("a=b hdastar.aot maze-6404.txt 8", 5.057),
# ("a=b linpack.aot", 22.50),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 28.095),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 6.079),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 6.069),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 6.056),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.052),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 7.07),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 6.081),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.002),
# ("OMP_NUM_THREADS=1 tc.aot", 20.15),
# ("OMP_NUM_THREADS=1 bt.aot", 38.061),
# ("OMP_NUM_THREADS=1 cg.aot", 16.088),
# ("OMP_NUM_THREADS=1 ft.aot", 22.019),
# ("OMP_NUM_THREADS=1 lu.aot", 0.002),
# ("OMP_NUM_THREADS=1 mg.aot", 8.039),
# ("OMP_NUM_THREADS=1 sp.aot", 0.092),
# ("a=b redis.aot", 1.009),
# ("a=b hdastar.aot maze-6404.txt 8", 6.081),
# ("a=b linpack.aot", 24.39),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 30.04),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 7.074),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 7.071),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 6.049),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.042),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 7.033),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 7.028),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.02),
# ("OMP_NUM_THREADS=1 tc.aot", 20.21),
# ("OMP_NUM_THREADS=1 bt.aot", 39.033),
# ("OMP_NUM_THREADS=1 cg.aot", 16.077),
# ("OMP_NUM_THREADS=1 ft.aot", 21.026),
# ("OMP_NUM_THREADS=1 lu.aot", 0.003),
# ("OMP_NUM_THREADS=1 mg.aot", 8.037),
# ("OMP_NUM_THREADS=1 sp.aot", 1.037),
# ("a=b redis.aot", 1.003),
# ("a=b hdastar.aot maze-6404.txt 8", 5.013),
# ("a=b linpack.aot", 23.92),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 28.007),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 7.031),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 7.001),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 6.056),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.027),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 7.022),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 6.085),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.077),
# ("OMP_NUM_THREADS=1 tc.aot", 20.23),
# ("OMP_NUM_THREADS=1 bt.aot", 38.035),
# ("OMP_NUM_THREADS=1 cg.aot", 17.001),
# ("OMP_NUM_THREADS=1 ft.aot", 22.064),
# ("OMP_NUM_THREADS=1 lu.aot", 0.003),
# ("OMP_NUM_THREADS=1 mg.aot", 8.042),
# ("OMP_NUM_THREADS=1 sp.aot", 1.032),
# ("a=b redis.aot", 0.081),
# ("a=b hdastar.aot maze-6404.txt 8", 4.054),
# ("a=b linpack.aot", 23.17),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 24.029),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 6.081),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 6.076),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 5.093),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.035),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 6.077),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 6.087),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.019),
# ("OMP_NUM_THREADS=1 tc.aot", 20.18),
# ("OMP_NUM_THREADS=1 bt.aot", 39.055),
# ("OMP_NUM_THREADS=1 cg.aot", 19.005),
# ("OMP_NUM_THREADS=1 ft.aot", 22.01),
# ("OMP_NUM_THREADS=1 lu.aot", 0.003),
# ("OMP_NUM_THREADS=1 mg.aot", 8.063),
# ("OMP_NUM_THREADS=1 sp.aot", 1.002),
# ("a=b redis.aot", 1.005),
# ("a=b hdastar.aot maze-6404.txt 8", 6.077),
# ("a=b linpack.aot", 22.54),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 26.056),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 6.09),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 7.026),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 6.042),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.047),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 6.089),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 6.097),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 8.042),
# ("OMP_NUM_THREADS=1 tc.aot", 20.19),
# ("OMP_NUM_THREADS=1 bt.aot", 40.034),
# ("OMP_NUM_THREADS=1 cg.aot", 16.097),
# ("OMP_NUM_THREADS=1 ft.aot", 22.04),
# ("OMP_NUM_THREADS=1 lu.aot", 0.002),
# ("OMP_NUM_THREADS=1 mg.aot", 8.032),
# ("OMP_NUM_THREADS=1 sp.aot", 1.045),
# ("a=b redis.aot", 0.065),
# ("a=b hdastar.aot maze-6404.txt 8", 4.062),
# ("a=b linpack.aot", 24.61),
# ("OMP_NUM_THREADS=1 llama.aot stories110M.bin -z tokenizer.bin -t 0.0", 23.045),
# ("OMP_NUM_THREADS=1 bc.aot -g20 -n1", 7.065),
# ("OMP_NUM_THREADS=1 bfs.aot -g20 -n1", 6.016),
# ("OMP_NUM_THREADS=1 cc.aot -g20 -n1", 5.096),
# ("OMP_NUM_THREADS=1 cc_sv.aot -g20 -n1", 6.064),
# ("OMP_NUM_THREADS=1 pr.aot -g20 -n1", 7.059),
# ("OMP_NUM_THREADS=1 pr_spmv.aot -g20 -n1", 7.003),
# ("OMP_NUM_THREADS=1 sssp.aot -g20 -n1", 7.033),
# ("OMP_NUM_THREADS=1 tc.aot", 20.23),
# ("OMP_NUM_THREADS=1 bt.aot", 40.008),
# ("OMP_NUM_THREADS=1 cg.aot", 17.063),
# ("OMP_NUM_THREADS=1 ft.aot", 21.094),
# ("OMP_NUM_THREADS=1 lu.aot", 0.003),
# ("OMP_NUM_THREADS=1 mg.aot", 8.05),
# ("OMP_NUM_THREADS=1 sp.aot", 1.038),
# ("a=b redis.aot", 1.007),
# ("a=b hdastar.aot maze-6404.txt 8", 6.01)]
#     # qemu_x86_64_results = run_qemu_x86_64()
#     # # print the results
#     qemu_aarch64_results = run_qemu_aarch64()
#     hcontainer_results = run_hcontainer()

#     write_to_csv("comparison.csv")
    
    results = read_from_csv("comparison.csv")
    plot(results)
    print(common_util.calculate_averages_comparison(results))