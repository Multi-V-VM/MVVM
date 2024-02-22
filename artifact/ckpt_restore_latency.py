# for thread cound to 1, 2, 4, 8, 16
import csv
import common_util
from collections import defaultdict
import numpy as np
from matplotlib import pyplot as plt
from multiprocessing import Pool

folder = [
    "linpack",
    "llama","llama",
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
cmd = [
    "linpack",
    "llama",
    "orb_slam2",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "bt",
    "cg",
    "ep",
    "ft",
    "lu",
    "mg",
    "sp",
    "redis",
    "hdastar",
    "hdastar",
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
    write_to_csv(results1, "ckpt_restore_latency_raw.csv")
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            for line in lines:
                if line.__contains__("Snapshot time:"):
                    time = line.split(" ")[-2]
                    snapshot_time = float(time)
                if line.__contains__("Recover time:"):
                    time = line.split(" ")[-1]
                    recover_time = float(time)

            results += [(exec, snapshot_time, recover_time)]
    # print the results
    # results += results1


def run_criu():
    global results
    results1 = []
    for i in range(len(cmd)):
        aot = cmd[i]
        results1.append(
            pool.apply_async(
                common_util.run_criu_checkpoint_restore,
                (aot, folder[i], arg[i], envs[i]),
            )
        )
    # print the results
    results1 = [x.get() for x in results1]
    write_to_csv_raw(results1, "ckpt_restore_latency_criu_raw.csv")
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            for line in lines:
                if line.__contains__("Dumping finished successfully"):
                    time = line.split(" ")[0].replace("(", "").replace(")", "")
                    snapshot_time = float(time)
                if line.__contains__("Restore finished successfully."):
                    time = line.split(" ")[0].replace("(", "").replace(")", "")
                    recover_time = float(time)
            # print(output[o])
            results += [(exec, snapshot_time, recover_time)]


def run_qemu():
    global results
    results1 = []

    for i in range(len(cmd)):
        aot = cmd[i]
        results1.append(
            pool.apply_async(
                common_util.run_qemu_checkpoint_restore,
                (aot, folder[i], arg[i], envs[i]),
            )
        )
    # print the results
    results1 = [x.get() for x in results1]
    write_to_csv_raw(results1, "ckpt_restore_latency_qemu_raw.csv")
    for exec, output in results1:
        for o in range(len(output)):
            lines = output[o].split("\n")
            for line in lines:
                if line.__contains__("total time: "):
                    time = line.split(" ")[-2]
                    snapshot_time = float(time)/1000
            print(exec, snapshot_time)
            results += [(exec, snapshot_time, 0.0)]


def write_to_csv_raw(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        csvfile.write("name, output\n")
        # Optionally write headers

        # Write the data
        csvfile.write(str(data))


def write_to_csv(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(["name", "snapshot time(s)", "recovery time(s)"])

        # Write the data
        for row in data:
            writer.writerow(row)

# print the results
def plot_qemu(result, file_name="ckpt_restore_latency_qemu.pdf"):
    workloads = defaultdict(list)
    for workload, Total, Down in result:
        if Total !=0 or Down !=0: 
            workloads[workload.replace("OMP_NUM_THREADS=","").replace("-g20","").replace("-n300","").replace(" -f ","").replace("-vn300","").replace("maze-6404.txt","").replace("stories15M.bin","").replace("-z tokenizer.bin -t 0.0","").strip()].append((Total, Down))

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        Totals, recoveries = zip(*times)
        statistics[workload] = {
            "Total_median": np.median(Totals),
            "Down_median": np.median(recoveries),
            "Total_std": np.std(Totals),
            "Down_std": np.std(recoveries),
        }

    # Plotting
    fig, ax = plt.subplots(figsize=(15, 7))
    # Define the bar width and positions
    bar_width = 0.35
    index = np.arange(len(statistics))

    # Plot the bars for each workload
    # for i, (workload, stats) in enumerate(statistics.items()):
    #     ax.bar(index[i], stats['Total_median'], bar_width, yerr=stats['Total_std'], capsize=5, label=f'Total')
    #     ax.bar(index[i] + bar_width, stats['Down_median'], bar_width, yerr=stats['Down_std'], capsize=5, label=f'Down')
    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["Total_median"],
            bar_width,
            yerr=stats["Total_std"],
            capsize=5,
            color="blue",
            label="Total" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["Down_median"],
            bar_width,
            yerr=stats["Down_std"],
            capsize=5,
            color="red",
            label="Down" if i == 0 else "",
        )

    # Labeling and formatting
    ax.set_xlabel("Workload")
    ax.set_ylabel("Time")
    ax.set_title("Median and Variation of Total and Down Times by Workload")
    ax.set_xticks(index + bar_width / 2)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, rotation=45)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig(file_name)
    
# print the results
def plot(result, file_name="ckpt_restore_latency.pdf"):
    workloads = defaultdict(list)
    for workload, snapshot, recovery in result:
        workloads[workload.replace("OMP_NUM_THREADS=","").replace("-g15","").replace("-n300","").replace(" -f ","").replace("-vn300","").replace("maze-6404.txt","").replace("stories15M.bin","").replace("-z tokenizer.bin -t 0.0","").strip()].append((snapshot, recovery))

    # Calculate the medians and standard deviations for each workload
    statistics = {}
    for workload, times in workloads.items():
        snapshots, recoveries = zip(*times)
        statistics[workload] = {
            "snapshot_median": np.median(snapshots),
            "recovery_median": np.median(recoveries),
            "snapshot_std": np.std(snapshots),
            "recovery_std": np.std(recoveries),
        }

    # Plotting
    fig, ax = plt.subplots(figsize=(15, 7))
    # Define the bar width and positions
    bar_width = 0.35
    index = np.arange(len(statistics))

    # Plot the bars for each workload
    # for i, (workload, stats) in enumerate(statistics.items()):
    #     ax.bar(index[i], stats['snapshot_median'], bar_width, yerr=stats['snapshot_std'], capsize=5, label=f'Snapshot')
    #     ax.bar(index[i] + bar_width, stats['recovery_median'], bar_width, yerr=stats['recovery_std'], capsize=5, label=f'Recovery')
    for i, (workload, stats) in enumerate(statistics.items()):
        ax.bar(
            index[i],
            stats["snapshot_median"],
            bar_width,
            yerr=stats["snapshot_std"],
            capsize=5,
            color="blue",
            label="Snapshot" if i == 0 else "",
        )
        ax.bar(
            index[i] + bar_width,
            stats["recovery_median"],
            bar_width,
            yerr=stats["recovery_std"],
            capsize=5,
            color="red",
            label="Recovery" if i == 0 else "",
        )

    # Labeling and formatting
    ax.set_xlabel("Workload")
    ax.set_ylabel("Time")
    ax.set_title("Median and Variation of Snapshot and Recovery Times by Workload")
    ax.set_xticks(index + bar_width / 2)
    ticklabel = (x.replace("a=b", "") for x in list(statistics.keys()))
    ax.set_xticklabels(ticklabel, rotation=45)
    ax.legend()

    # Show the plot
    plt.tight_layout()
    plt.show()
    plt.savefig(file_name)
    # %%


if __name__ == "__main__":
    run_mvvm()
    print(results)
    print(len(arg),len(cmd),len(envs))
    run_criu()
    write_to_csv(results, "ckpt_restore_latency_criu.csv")
    plot_qemu(results, "ckpt_restore_latency_criu.pdf")
    run_qemu()
    write_to_csv(results, "ckpt_restore_latency_qemu.csv")
    plot_qemu(results,"ckpt_restore_latency_qemu.pdf")
