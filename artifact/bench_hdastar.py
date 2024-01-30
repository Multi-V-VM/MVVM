import pickle
import common_util
from multiprocessing import Pool

cmd = ["hw5","hw5", "hw5", "hw5"]
arg = [
    ["maze-6404.txt", "1"],
    ["maze-6404.txt", "2"],
    ["maze-6404.txt", "4"],
    ["maze-6404.txt", "8"],
]


pool = Pool(processes=5)

# run the benchmarks
results = []
for i in range(len(cmd)):
    aot = cmd[i]
    results.append(pool.apply_async(common_util.run_hcontainer, (aot,"hdastar", arg[i], "OMP_NUM_THREADS=1")))
pool.close()
pool.join()

# print the results
results = [x.get() for x in results]
# serialize the results
with open("bench_hdastar_results.pickle", "wb") as f:
    pickle.dump(results, f)
for exec, output in results:
    print(exec)
    lines = output.split("\n")
    for line in lines:
        if line.__contains__("real"):
            print(line)

# read the results
with open("bench_hdastar_results.pickle", "rb") as f:
    results = pickle.load(f)
