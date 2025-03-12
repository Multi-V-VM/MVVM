import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from common_util import parse_time, parse_time_no_msec, get_avg_99percent
from collections import defaultdict
import subprocess

# ip = ["192.168.0.24", "192.168.0.23"]
ip = ["128.114.53.32", "128.114.59.212"]
port = 12347
port2 = 12346
new_port = 1235
new_port1 = 1238
new_port2 = 1236
new_port12 = 14440
cmd = [
    "redis",  # low priority task
    "rgbd_tum",  # high priority task
]
folder = [
    "redis",
    "ORB_SLAM2",  # frame per cents
]
arg = [
    [],
    [
        "./ORBvoc.txt",
        "./TUM3.yaml",
        "./rgbd_dataset_freiburg3_long_office_household_validation",
        "./associations/fr3_office_val.txt",
    ],
]
envs = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
]

pool = Pool(processes=20)


def get_cloud_result():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(pool.apply_async(common_util.run, (aot, arg[i], envs[i])))
    # print the results
    results1 = [x.get() for x in results1]
    exec = ""
    exec_time = ""
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def get_edge_result():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(common_util.run_burst(aot, arg[i], envs[i]))
    # print the results
    exec = ""
    exec_time = ""
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def get_snapshot_overhead():
    results = []
    results1 = []
    for i in range(len(cmd)):
        aot = cmd[i] + ".aot"
        results1 = common_util.run_checkpoint_restore_burst_overhead(
            aot,
            arg[i],
            envs[i],
            f"-o {ip[1]} -s {port}",
            f"-i {ip[1]} -e {port}",
        )
        for exec, output in results1:
            lines = output.split("\n")
            for line in lines:
                if line.__contains__("Snapshot time:"):
                    exec_time = line.split(" ")[-2]
                    print(exec, exec_time)
                    results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def get_burst_compute():
    results = []
    results1 = []
    aot = cmd[0] + ".aot"
    aot1 = cmd[1] + ".aot"
    results1 = common_util.run_checkpoint_restore_burst(
        aot,
        arg[0],
        aot1,
        arg[1],
        envs[0],
        f"-o {ip[1]} -s {port}",
        f"-i {ip[1]} -e {port} -o {ip[0]} -s {new_port}",
        f"-i {ip[0]} -e {new_port} -o {ip[1]} -s {new_port1}",
        f"-i {ip[1]} -e {new_port1} -o {ip[0]} -s {port}",
        f"-i {ip[0]} -e {port}",
        f"-o {ip[0]} -s {port2}",
        f"-i {ip[0]} -e {port2} -o {ip[1]} -s {new_port2}",
        f"-i {ip[1]} -e {new_port2} -o {ip[0]} -s {new_port12}",
        f"-i {ip[0]} -e {new_port12}",
    )
    return results1


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]
    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            [
                "name",
                "fasttier",
                "slowtier",
                "snapshot Time",
            ]
        )

        # Write the data
        for idx, row in enumerate(fasttier):
            writer.writerow([row[0], row[1], slowtier[idx][1], snapshot[idx][1]])


def read_from_csv(filename):
    results = []
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        for idx, row in enumerate(reader):
            if idx == 0:
                continue
            results.append(row)
    return results
def average(array):
    return sum(array) / len(array)

def plot_time(reu, aot_energy, aot_ps, aot1_energy, aot1_ps):
    # get from reu
    # start time -> end time -> start time
    font = {"size": 25}

    plt.rc("font", **font)

    reu = reu.replace("\\n", "\n").replace("\\r", "\n").split("\n")
    state = 0
    time = []
    exec_time = [[], [], [], [], []]
    exec_time1 = [[]]
    for line in reu:
        try:
            if line.__contains__("Trial") and state < 5:
                to_append = float(line.split(" ")[-1])
                # print("exec_time ",exec_time[-1])
                if to_append >= 0 and to_append < 7:
                    exec_time[state].append(to_append)
            elif line.__contains__("Trial"):
                to_append = float(line.split(" ")[-1])
                # print("exec_time ",exec_time[-1])
                if to_append >= 0 and to_append < 7:
                    exec_time1[state - 5].append(to_append)
            if line.__contains__("Snapshot"):
                # print("line ", line)
                time.append(
                    parse_time(line.split(" ")[1].replace("]", ""))
                    - float(line.split(" ")[-2])
                )
                print("time ", time)
                state += 1
            if line.__contains__("Execution"):
                time.append(parse_time(line.split(" ")[1].replace("]", "")))
                print("time ", time)
                state += 1
        except Exception as e:
            print(line)
            # pass
            print(e)
    # print(exec_time)
    # print(exec_time1[0])
    # print(time)
    # record time
    fig, ax = plt.subplots(figsize=(20, 10))
    base = time[0] - sum(exec_time[0])
    sum_aot = exec_time[0] + exec_time[1] + exec_time[2] + exec_time[3] + exec_time[4]
    sum_aot1 = exec_time1[0]  # + exec_time1[1] + exec_time1[2] + exec_time1[3]
    time_spots = [time[0] - sum(exec_time[0]) - base]

    for idx, i in enumerate(exec_time):
        to_pop = len(i) - 1
        for x in i:
            # Add the current increment to the last time spot
            new_time_spot = time_spots[-1] + 0.2
            # Append the new time spot to the sequence
            time_spots.append(new_time_spot)
        time_spots.pop(to_pop)
        if idx != len(exec_time) - 1:
            time_spots.append(time_spots[-1])
    print(len(time))
    # print(len(exec_time1))
    # print(len(exec_time))
    time_spots1 = [0]
    for idx, i in enumerate(exec_time1):
        to_pop = len(i) - 1
        for x in i:
            # Add the current increment to the last time spot
            new_time_spot = time_spots1[-1] + 0.2
            # Append the new time spot to the sequence
            time_spots1.append(new_time_spot)
        time_spots1.pop(to_pop)
        if idx != len(exec_time1) - 1:
            time_spots1.append(time[idx + 6] - sum(exec_time1[idx + 1]) - base)
    print(time_spots1[-1]/ time_spots[-1])
    sum_aot = [1 / x for x in sum_aot]
    avg_extended, percentile99_extended = get_avg_99percent(sum_aot, 3)
    sum_aot1 = [1 / x for x in sum_aot1]
    avg_exec_time1, percentile99_extended1 = get_avg_99percent(sum_aot1, 3)
    # print("avg_exec_time1 ", exec_time1, "time_spots1 ", time[5])
    ax.plot(time_spots, avg_extended, "blue", label="MVVM")
    # ax.plot(time_spots, percentile99_extended, color="purple", linestyle="-")
    ax.plot(time_spots1, avg_exec_time1, "r", label="Native")
    # ax.plot(time_spots1, percentile_99_exec_time1, color="pink", linestyle="-")
    # Add a legend to the plot
    ax.legend()
    print((1/average(sum_aot1))/average(exec_time[0]+exec_time[2]+exec_time[4]))
    print(average(exec_time[1]+exec_time[3])/(1/average(sum_aot1)))
    print(average(exec_time[1]+exec_time[3]))
    print(average(exec_time[0]+exec_time[2]+exec_time[4]))
    print(1/average(sum_aot1))
    # ax.plot(time_spots,sum_aot, "blue")
    # ax.plot(time_spots1,sum_aot1, "r")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Trials / Second")
    plt.savefig("burst.pdf")


if __name__ == "__main__":
    # fasttier = get_cloud_result()
    # print("fasttier = ", fasttier)
    # slowtier = get_edge_result()
    # print("slowtier = ", slowtier)
    # snapshot = get_snapshot_overhead()
    # print("snapshot = ", snapshot)
    # # plot skew
    # write_to_csv("burst_computing.csv")

    # results = read_from_csv("burst_computing.csv")
    # plot(results)
    reu = get_burst_compute()
    # reu=""
    with open("burst.txt", "w") as f:
        f.write(str(reu))
    reu = ""
    with open("burst.txt", "r") as f:
        reu = f.read()
    # with open("MVVM_checkpoint.ps.1.out") as f:
    #     checkpoint1 = f.read()
    # with open("MVVM_checkpoint.ps.out") as f:
    #     checkpoint = f.read()
    # with open("MVVM_restore.ps.6.out") as f:
    #     restore6 = f.read()
    # with open("MVVM_restore.ps.5.out") as f:
    #     restore5 = f.read()
    # with open("MVVM_restore.ps.4.out") as f:
    #     restore4 = f.read()
    # with open("MVVM_restore.ps.3.out") as f:
    #     restore3 = f.read()
    # with open("MVVM_restore.ps.2.out") as f:
    #     restore2 = f.read()
    # with open("MVVM_restore.ps.1.out") as f:
    #     restore1 = f.read()
    # with open("MVVM_restore.ps.out") as f:
    #     restore = f.read()

    # with open("MVVM_checkpoint.energy.1.out") as f:
    #     checkpoint_energy1 = f.read()
    # with open("MVVM_checkpoint.energy.out") as f:
    #     checkpoint_energy = f.read()
    # with open("MVVM_restore.energy.6.out") as f:
    #     restore_energy6 = f.read()
    # with open("MVVM_restore.energy.5.out") as f:
    #     restore_energy5 = f.read()
    # with open("MVVM_restore.energy.4.out") as f:
    #     restore_energy4 = f.read()
    # with open("MVVM_restore.energy.3.out") as f:
    #     restore_energy3 = f.read()
    # with open("MVVM_restore.energy.2.out") as f:
    #     restore_energy2 = f.read()
    # with open("MVVM_restore.energy.1.out") as f:
    #     restore_energy1 = f.read()
    # with open("MVVM_restore.energy.out") as f:
    #     restore_energy = f.read()

    plot_time(
        reu,
        None,
        None,
        None,
        None,
        # [
        #     checkpoint_energy,
        #     restore_energy,
        #     restore_energy1,
        #     restore_energy3,
        #     restore_energy2,
        # ],
        # [checkpoint1, restore3, restore4, restore5, restore6],
        # [
        #     checkpoint_energy1,
        #     restore_energy3,
        #     restore_energy4,
        #     restore_energy5,
        #     restore_energy6,
        # ],
        # [checkpoint1, restore3, restore4, restore5, restore6],
    )
