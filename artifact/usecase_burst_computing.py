import csv
import common_util
from multiprocessing import Pool
from matplotlib import pyplot as plt
import numpy as np
from collections import defaultdict


cmd = [
    "redis", # low priority task
    "rgbd_tum", # high priority task
]
folder = [
    "redis",
    "ORB_SLAM2", # networkbound?
]
arg = [
    [],
    ["./ORBvoc.txt", "./TUM3.yaml", "./", "./associations/fr1_xyz.txt"],
]
envs = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
]

pool = Pool(processes=20)

def get_fasttier_result():
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

def get_slowtier_result():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(pool.apply_async(common_util.run_slowtier, (aot, arg[i], envs[i])))
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant

def get_snapshot_overhead():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(pool.apply_async(common_util.run_checkpoint_restore_slowtier, (aot, arg[i], envs[i])))
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Snapshot time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant

def get_burst_compute():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i] + ".aot"
            results1.append(pool.apply_async(common_util.run_checkpoint_restore_slowtier, (aot, arg[i], envs[i])))
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Snapshot time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant


def plot():
    fasttier = get_fasttier_result()
    slowtier = get_slowtier_result()
    snapshot = get_snapshot_overhead()
    reu = get_burst_compute()
    # plot skew 
    write_to_csv("burst_computing.csv")
    
    results = read_from_csv("burst_computing.csv")
    plot(results)