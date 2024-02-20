# windows mac and linux
import os
import common_util

folder = [
    "linpack",
    "rgbd_tum",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bfs", "bfs", "bfs",
    "bc", "bc", "bc",
    "bc", "bc", "bc",
    "bfs", "bfs", "bfs",
    "cc", "cc", "cc",
    "cc_sv", "cc_sv", "cc_sv",
    "pr", "pr", "pr",
    "pr_spmv", "pr_spmv", "pr_spmv",
    "sssp", "sssp", "sssp",
    "tc", "tc", "tc",
    "llama","llama",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "nas","nas","nas",
    "redis",
    "hdastar",
    "hdastar",
    "hdastar",
]
cmd = [
    "linpack",
    "llama","llama",
    "orb_slam2",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "gapbs","gapbs","gapbs",
    "bt","bt","bt",
    "cg","cg","cg",
    "ep","ep","ep",
    "ft","ft","ft",
    "lu","lu","lu",
    "mg","mg","mg",
    "sp","sp","sp",
    "redis",
    "hdastar",
    "hdastar",
    "hdastar",
]
arg = [
    [],
    ["stories15M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["stories15M.bin", "-z", "tokenizer.bin", "-t", "0.0"],
    ["./ORBvoc.txt,","./TUM3.yaml","./","./associations/fr1_xyz.txt"]
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-u15", "-n300"],["-u15", "-n300"],["-u15", "-n300"],
    ["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    ["-g15", "-vn300"],["-g15", "-vn300"],["-g15", "-vn300"],
    ["-g15", "-vn300"],["-g15", "-vn300"],["-g15", "-vn300"],
    ["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],["-f", "./road.sg", "-n300"],
    ["-g15", "-vn300"],["-g15", "-vn300"],["-g15", "-vn300"],
    ["-g15", "-vn300"],["-g15", "-vn300"],["-g15", "-vn300"],
    ["-g15", "-vn300"],["-g15", "-vn300"],["-g15", "-vn300"],
    ["-g15", "-vn300"],["-g15", "-vn300"],["-g15", "-vn300"],
    ["-g15", "-vn300"],["-g15", "-vn300"],["-g15", "-vn300"],
    ["-g15", "-n300"],["-g15", "-n300"],["-g15", "-n300"],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],[],[],
    [],
    ["maze-6404.txt", "2"],
    ["maze-6404.txt", "4"],
    ["maze-6404.txt", "8"],
]
envs = [
    "a=b",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "OMP_NUM_THREADS=1","OMP_NUM_THREADS=2","OMP_NUM_THREADS=4",
    "a=b",
    "a=b","a=b","a=b",
]


def get_size_linear_memory():
    pass
def get_size_runtime_memory():
    pass
def get_size_heap_memory():
    pass


if __name__ == "__main__":
    get_size_heap_memory()
    get_size_runtime_memory()
    get_size_linear_memory()