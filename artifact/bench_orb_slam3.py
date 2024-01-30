import pickle
import common_util
from multiprocessing import Pool

cmd = [
   "rgbd_tum",
]
arg = [
    [ "./ORBvoc.txt","./TUM1.yaml","./","./associations/fr2_desk.txt"],
]


pool = Pool(processes=40)

# run the benchmarks
results = []
for i in range(len(cmd)):
    for j in range(len(common_util.aot_variant)):
        for env in ["LINPACK_ARRAY_SIZE=600"]:
            aot = cmd[i] + common_util.aot_variant[j]
            results.append(pool.apply_async(common_util.run, (aot, arg[i], env)))
pool.close()
pool.join()

# print the results
results = [x.get() for x in results]
# serialize the results
with open("bench_linpack_results.pickle", "wb") as f:
    pickle.dump(results, f)
for exec, output in results:
    print(exec)
    lines = output.split("\n")
    for line in lines:
        if line.__contains__("Execution time:"):
            print(line)

# read the results
with open("bench_linpack_results.pickle", "rb") as f:
    results = pickle.load(f)
