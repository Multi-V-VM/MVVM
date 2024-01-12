import pickle
import common_util
from multiprocessing import Pool

cmd = ["hdastar","hdastar","hdastar","hdastar"]
arg = [
    ["maze-4821.txt", "1"],
    ["maze-4821.txt", "2"],
    ["maze-233.txt", "1"],
    ["maze-233.txt", "2"],
]


pool = Pool(processes=40)

# run the benchmarks
results = []
for i in range(len(cmd)):
    for j in range(len(common_util.aot_variant)):
        for env in ["a=b"]:
            aot = cmd[i] + common_util.aot_variant[j]
            results.append(pool.apply_async(common_util.run, (aot, arg[i], env)))
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
        if line.__contains__("Execution time:"):
            print(line)

# read the results
with open("bench_hdastar_results.pickle", "rb") as f:
    results = pickle.load(f)
