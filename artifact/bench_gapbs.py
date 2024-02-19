import pickle
import common_util
from multiprocessing import Pool

cmd = [
    "bfs",
    "bfs",
    "bfs",
    "bfs",
    "bfs",
    "bfs",
    "bfs",
    "bfs",
    "bfs",
    "bfs",
    "bc",
    "bc",
    "bfs",
    "cc",
    "cc_sv",
    "pr",
    "pr_spmv",
    "sssp",
    "tc",
]
arg = [
    ["-g15", "-n300"],
    ["-u15", "-n300"],
    ["-f", "./road.sg", "-n300"],
    ["-g15", "-n300"],
    ["-g15", "-n300"],
    ["-g15", "-n300"],
    ["-g15", "-n300"],
    ["-g15", "-n300"],
    ["-g15", "-n300"],
    ["-g15", "-n300"],
    ["-g15", "-vn300"],
    ["-g15", "-vn300"],
    ["-f", "./road.sg", "-n300"],
    ["-g15", "-vn300"],
    ["-g15", "-vn300"],
    ["-g15", "-vn300"],
    ["-g15", "-vn300"],
    ["-g15", "-vn300"],
    ["-g15", "-n300"],
]


pool = Pool(processes=5)

# run the benchmarks
results = []
for i in range(len(cmd)):
    for j in range(len(common_util.aot_variant)):
        for env in common_util.list_of_arg:
            aot = cmd[i] + common_util.aot_variant[j]
            results.append(pool.apply_async(common_util.run, (aot, arg[i], env)))
pool.close()
pool.join()

# print the results
# serialize the results
results = [x.get() for x in results]

with open("bench_gapbs_results.pickle", "wb") as f:
    pickle.dump(results, f)

for exec, output in results:
    print(exec)
    try:
        lines = output.decode("utf-8").split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                print(line)
    except:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                print(line)

# read the results
with open("bench_gapbs_results.pickle", "rb") as f:
    results = pickle.load(f)
