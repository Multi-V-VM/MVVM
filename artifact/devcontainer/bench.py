import os
import multiprocessing
import time
import pickle

BUILDDIR = "/workspaces/MVVM/build"
RUNNER_WORKDIR = f"{BUILDDIR}/bench"
MAX_COMPILE_THREADS = 16

def make_many(tests):
    names = [f"{test}_bench_compile" for test in tests]
    names = " ".join(names)
    os.system(f"cd {BUILDDIR}/bench && make -j{MAX_COMPILE_THREADS} {names}")

def get_variants(test):
    files = os.listdir(f"{BUILDDIR}/bench")
    aot_files = [f for f in files if f.endswith(".aot")]
    variants = [f for f in aot_files if f.startswith(test)]
    variants = [f.split(".")[0] for f in variants]
    return variants

def run_command_and_get_stdout_and_stderr(command):
    import subprocess
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    out, err = process.communicate()
    code = process.returncode
    return code, out.decode("utf-8"), err.decode("utf-8")

def runner(numa_node, core, command, mutex, available_cores):
    os.chdir(RUNNER_WORKDIR)
    with mutex:
        print(f"Core {core} started, command: {command}")
    command = f"numactl --cpunodebind={numa_node} --membind={numa_node} --physcpubind={core} {command}"
    code, out, err = run_command_and_get_stdout_and_stderr(command)
    with mutex:
        available_cores.append(core)
        lines = out.splitlines()
        print(f"Core {core} finished, command: {command}")
        if code != 0:
            print(f"Error: {code}")
            print(err)
            print("\n".join(lines[-10:]))
    return out, err

def bench(benchmarks, repeat=1):
    manager = multiprocessing.Manager()
    available_cores = manager.list([i for i in range(14, 14 + 8)])
    mutex = manager.Lock()

    # cpu cores
    # cores = [i for i in range(0, 10)] + [i for i in range(14, 14 + 10)]
    def get_numa_node(core):
        if core < 14:
            return 0
        elif 14 <= core and core < 28:
            return 1

    async_results = {}
    results = {}
    pool = multiprocessing.Pool()

    for test, args in benchmarks:
        arg = ""
        for e in args:
            arg = f"{arg} -a {e}"

        variants = get_variants(test)
        print("Variants:")
        print(variants)

        for v in variants:
            async_results[v] = []
            results[v] = []

        for _ in range(repeat):
            for v in variants:
                command = f"../MVVM_checkpoint -t {v}.aot {arg}"

                mutex.acquire()
                counter = 0
                while len(available_cores) == 0:
                    if counter % 60 == 0:
                        print("No available cores, waiting...")
                    counter = counter + 1
                    mutex.release()
                    time.sleep(1)
                    mutex.acquire()
                core = available_cores.pop()
                mutex.release()

                print(_, core, v)
                async_results[v].append(pool.apply_async(runner, (get_numa_node(core), core, command, mutex, available_cores)))

    pool.close()
    pool.join()

    for test, args in benchmarks:
        variants = get_variants(test)
        for v in variants:
            for r in async_results[v]:
                out, err = r.get()
                results[v].append((out, err))
    
    return results

benchmarks = [
    # ("llama", ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"]),
    # ("hdastar", ["maze-6404.txt", "8"]),

    # ("bc",      ["-g18", "-vn300"]),
    # ("bfs",     ["-g18", "-vn300"]),
    # ("cc",      ["-g18", "-vn300"]),
    # ("cc_sv",   ["-g18", "-vn300"]),
    # ("pr",      ["-g18", "-vn300"]),
    # ("pr_spmv", ["-g18", "-vn300"]),
    # ("sssp",    ["-g18", "-vn300"]),
    # ("tc",      ["-g20", "-n1"]),
    ("bt", []),
    # ("cg", []),
    # ("ft", []),
    # ("lu", []),
    # ("mg", []),
    # ("sp", []),
    # ("redis", []),

    # ("ep", []), # segfault
]

if __name__ == "__main__":
    tests = [b[0] for b in benchmarks]
    make_many(tests)

    results = bench(benchmarks, repeat=1)

    current_time = time.strftime("%Y%m%d-%H%M%S")
    pickle.dump(results, open(f"results-{current_time}.pkl", "wb"))