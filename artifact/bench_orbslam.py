import csv
import common_util
from multiprocessing import Pool
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

pool = Pool(processes=16)
cmd1 = [
    "rgbd_tum",
    "rgbd_tum",
    "rgbd_tum",
    "rgbd_tum",
]
cmd = [
    "rgbd_tum_o0",
    "rgbd_tum_o1",
    "rgbd_tum_o2",
    "rgbd_tum",
]
arg = [
    [
        "./ORBvoc.txt",
        "./TUM3.yaml",
        "./rgbd_dataset_freiburg3_long_office_household_validation/",
        "./associations/fr3_office_val.txt",
    ],
    [
        "./ORBvoc.txt",
        "./TUM3.yaml",
        "./rgbd_dataset_freiburg3_long_office_household_validation/",
        "./associations/fr3_office_val.txt",
    ],
    [
        "./ORBvoc.txt",
        "./TUM3.yaml",
        "./rgbd_dataset_freiburg3_long_office_household_validation/",
        "./associations/fr3_office_val.txt",
    ],
    [
        "./ORBvoc.txt",
        "./TUM3.yaml",
        "./rgbd_dataset_freiburg3_long_office_household_validation/",
        "./associations/fr3_office_val.txt",
    ],
]
folder = [
    "/mnt/MVVM/bench/ORB_SLAM2/build/",
    "/mnt/MVVM/bench/ORB_SLAM2/build_o1/",
    "/mnt/MVVM/bench/ORB_SLAM2/build_o2/",
    "/mnt/MVVM/bench/ORB_SLAM2/build_o3/",
]
envs = ["a=b", "a=b", "a=b", "a=b"]

def write_to_csv_raw(data, filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "w", newline="") as csvfile:
        csvfile.write("name, output\n")
        # Optionally write headers

        # Write the data
        csvfile.write(str(data))

def run_mvvm():
    results_0 = []
    results_1 = []
    results_2 = []
    results_3 = []
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
    write_to_csv_raw(results1,"policy_orbslam1_raw.csv")
    exec_time = ""
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("mean tracking time:"):
                exec_time = line.split(" ")[-1]
                # print(exec, exec_time)
                if exec == "rgbd_tum_o0":
                    results_0.append(exec_time)
                elif exec == "rgbd_tum_o1":
                    results_1.append(exec_time)
                elif exec == "rgbd_tum_o2":
                    results_2.append(exec_time)
                else:
                    results_3.append(exec_time)
    result = list(zip(cmd,results_0, results_1, results_2, results_3))
    print(result)
    return result


def run_native():
    results_0 = []
    results_1 = []
    results_2 = []
    results_3 = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd1)):
            for j in range(len(common_util.aot_variant)):
                aot = cmd1[i]
                results1.append(
                    pool.apply_async(common_util.run_native, (aot,folder[i], arg[i], envs[i]))
                )
    # print the results
    results1 = [x.get() for x in results1]
    write_to_csv_raw(results1,"policy_orbslam_raw.csv")
    exec_time = ""
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("mean tracking time:"):
                exec_time = line.split(" ")[-1]
                # print(exec, exec_time)
                if exec == "rgbd_tum_o0":
                    results_0.append(exec_time)
                elif exec == "rgbd_tum_o1":
                    results_1.append(exec_time)
                elif exec == "rgbd_tum_o2":
                    results_2.append(exec_time)
                else:
                    results_3.append(exec_time)
    result = list(zip(cmd,results_0, results_1, results_2, results_3))
    print(result)
    return result


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]
    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            [
                "rgbd_tum_o0",
                "rgbd_tum_o1",
                "rgbd_tum_o2",
                "rgbd_tum",
            ]
        )

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow([row[0], row[1], row[2], row[3]])


def read_from_csv(filename):
    with open(filename, "r") as csvfile:
        reader = csv.reader(csvfile)
        next(reader)
        results = []
        for row in reader:
            results.append(
                (
                    float(row[0]),
                    float(row[1]),
                    float(row[2]),
                    float(row[3]),
                )
            )
        return results

if __name__ == "__main__":
    # mvvm_results = run_native()
    # print(mvvm_results)
    # write_to_csv("policy_orbslam1.csv")
    mvvm_results = run_mvvm()
    print(mvvm_results)
    write_to_csv("policy_orbslam.csv")
    # mvvm_results = read_from_csv("policy_orbslam.csv")
