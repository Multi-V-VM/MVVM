import sys
import os
import time
import subprocess
import pickle

cmd = ["bfs", "bfs", "bfs", "bfs", "bfs", "bfs", "bfs", "bfs", "bfs",
    "bc", "bfs", "cc", "cc_sv", "pr", "pr_spmv", "sssp", "tc"]
arg = [["-g20", "-n30"],
    ["-u20", "-n30"],
    # ["-f", "../../bench/gapbs/test/graphs/4.gr", "-n1000"],
    # ["-f", "../../bench/gapbs/test/graphs/4.el", "-n1000"],
    # ["-f", "../../bench/gapbs/test/graphs/4.wel", "-n1000"],
    # ["-f", "../../bench/gapbs/test/graphs/4.graph", "-n1000"],
    # ["-f", "../../bench/gapbs/test/graphs/4w.graph", "-n1000"],
    # ["-f", "../../bench/gapbs/test/graphs/4.mtx", "-n1000"],
    # ["-f", "../../bench/gapbs/test/graphs/4w.mtx", "-n1000"],
    ["-g20", "-n30"],
    ["-g20", "-n30"],
    ["-g20", "-n30"],
    ["-g20", "-n30"],
    ["-g20", "-n30"],
    ["-g20", "-n30"],
    ["-g20", "-n30"],

    ["-g20", "-vn30"],
    ["-g20", "-vn30"],
    ["-g20", "-vn30"],
    ["-g20", "-vn30"],
    ["-g20", "-vn30"],
    ["-g20", "-vn30"],
    ["-g20", "-vn30"],
    ["-g20", "-n2"]]

# aot_variant = [".aot", "-pure.aot", "-stack.aot", "-ckpt.aot", "-ckpt-br.aot"]
aot_variant = ["-ckpt-every-dirty.aot"]

def run(aot_file: str, arg: list[str]) -> tuple[str, str]:
    cmd = f"../MVVM_checkpoint -t {aot_file} {' '.join(['-a ' + str(x) for x in arg])}"
    print(cmd)
    cmd = cmd.split()
    result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)    
    output = result.stdout.decode("utf-8")
    exec = " ".join([aot_file] + arg)
    # print(exec)
    # print(output)
    return (exec, output)

# set up a process pool
from multiprocessing import Pool
pool = Pool(processes=40)

# run the benchmarks
results = []
for i in range(len(cmd)):
    for j in range(len(aot_variant)):
        aot = cmd[i] + aot_variant[j]
        # run(aot, arg[i])
        results.append(pool.apply_async(run, (aot, arg[i])))
pool.close()
pool.join()

# print the results
results = [x.get() for x in results]
# serialize the results
with open("bench_results.pickle", "wb") as f:
    pickle.dump(results, f)
for (exec, output) in results:
    print(exec)
    lines = output.split("\n")
    for line in lines:
        if line.startswith("Execution time:"):
            print(line)

# read the results
# with open("bench_results.pickle", "rb") as f:
#     results = pickle.load(f)