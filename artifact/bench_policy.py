import csv
import common_util
from multiprocessing import Pool

cmd = [
    "llama",
    "bfs",
    "bc",
    "bfs",
    "cc",
    "cc_sv",
    "pr",
    "pr_spmv",
    "sssp",
    "tc",
    "bt",
    "cg",
    "ep",
    "ft",
    "lu",
    "mg",
    "sp",
]
folder = [
    "llama",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "gapbs",
    "nas",
    "nas",
    "nas",
    "nas",
    "nas",
    "nas",
    "nas",
]
arg = [
    ["stories110M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["-f", "./road.sg", "-n300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-f", "./road.sg", "-n300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-vn300"],
    ["-g20", "-n300"],
    [],
    [],
    [],
    [],
    [],
    [],
    [],
]
envs = [
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
    "OMP_NUM_THREADS=1",
]

pool = Pool(processes=1)


def run_mvvm():
    results = []
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
    for exec, output in results1:
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("Execution time:"):
                exec_time = line.split(" ")[-2]
                print(exec, exec_time)
        results.append((exec, exec_time))  # discover 4 aot_variant
    return results


def run_hcontainer():
    results = []
    results1 = []
    for _ in range(common_util.trial):
            for i in range(len(cmd)):
                aot = cmd[i]
                results1.append(
                    pool.apply_async(
                        common_util.run_hcontainer,
                        (aot, "linpack", arg[i], envs[i]),
                    )
                )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
        results.append((exec, exec_time))
    return results


def run_qemu_x86_64():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_qemu_x86_64,
                    (aot, "linpack", arg[i], envs[i]),
                )
            )
    # print the results
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
        results.append((exec, exec_time))
    return results


def run_qemu_aarch64():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_qemu_aarch64,
                    (aot, folder[i], arg[i], envs[i]),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
        results.append((exec, exec_time))

    return results


def run_native():
    results = []
    results1 = []
    for _ in range(common_util.trial):
        for i in range(len(cmd)):
            aot = cmd[i]
            results1.append(
                pool.apply_async(
                    common_util.run_native,
                    (aot, folder[i], arg[i], envs[i]),
                )
            )
    results1 = [x.get() for x in results1]
    for exec, output in results1:
        print(exec, output)
        lines = output.split("\n")
        for line in lines:
            if line.__contains__("user"):
                exec_time = float(line.split(" ")[0].replace("user", ""))
        results.append((exec, exec_time))
    return results


def write_to_csv(filename):
    # 'data' is a list of tuples, e.g., [(checkpoint_result_0, checkpoint_result_1, restore_result_2), ...]

    with open(filename, "a+", newline="") as csvfile:
        writer = csv.writer(csvfile)
        # Optionally write headers
        writer.writerow(
            ["name", "mvvm", "hcontainer", "qemu_x86_64", "qemu_aach64", "native"]
        )

        # Write the data
        for idx, row in enumerate(mvvm_results):
            writer.writerow(
                [
                    row[0],
                    row[1],
                    hcontainer_results[idx][1],
                    qemu_x86_64_results[idx][1],
                    qemu_aarch64_results[idx][1],
                    native_results[idx][1],
                ]
            )


if __name__ == "__main__":
    mvvm_results = run_mvvm()
    native_results = run_native()
    qemu_x86_64_results = run_qemu_x86_64()
    # print the results
    qemu_aarch64_results = run_qemu_aarch64()
    hcontainer_results = run_hcontainer()

    write_to_csv("policy.csv")
