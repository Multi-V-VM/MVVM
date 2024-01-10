import pickle
import common_util.aot_variant as aot_variant
cmd = ["bfs", "bfs", "bfs", "bfs", "bfs", "bfs", "bfs", "bfs", "bfs",
    "bc", "bfs", "cc", "cc_sv", "pr", "pr_spmv", "sssp", "tc"]
arg = [["-g15", "-n30"],
    ["-u15", "-n30"],
    ["-f", "./4.gr", "-n1000"],
    ["-f", "./4.el", "-n1000"],
    ["-f", "./4.wel", "-n1000"],
    ["-f", "./4.graph", "-n1000"],
    ["-f", "./4w.graph", "-n1000"],
    ["-f", "./4.mtx", "-n1000"],
    ["-f", "./4w.mtx", "-n1000"],
    ["-g15", "-n30"],
    ["-g15", "-n30"],
    ["-g15", "-n30"],
    ["-g15", "-n30"],
    ["-g15", "-n30"],
    ["-g15", "-n30"],
    ["-g15", "-n30"],
    ["-g15", "-vn30"],
    ["-g15", "-vn30"],
    ["-g15", "-vn30"],
    ["-g15", "-vn30"],
    ["-g15", "-vn30"],
    ["-g15", "-vn30"],
    ["-g15", "-vn30"],
    ["-g15", "-n2"]]

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