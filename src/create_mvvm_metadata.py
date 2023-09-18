import os
import subprocess
import sys

MAGIC_NOP_COLUMN = "514"

def call_llvm_dwarfdump(object_file_name):
    dwarf_file_name = f"{object_file_name}.dwarf"
    os.system(f"llvm-dwarfdump -a {object_file_name} > {dwarf_file_name}")
    return dwarf_file_name

def get_nop_offsets(dwarf_file_name) -> [str]:
    offsets = []
    with open(dwarf_file_name, "r") as f:
        for line in f.readlines():
            eles = line.split()
            if len(eles) >= 5 and eles[2] == MAGIC_NOP_COLUMN and "is_stmt" in line:
                addr = eles[0]
                offsets.append(addr)
    return offsets


if __name__ == "__main__":
    assert len(sys.argv) == 2
    aot_name = sys.argv[1]
    dwarf_file_name = call_llvm_dwarfdump(f"{aot_name}.o")
    nop_offsets = get_nop_offsets(dwarf_file_name)
    print(len(nop_offsets))
    with open(f"{aot_name}.mvvm", "w") as f:
        f.write("x86_64\n")
        f.write("nop\n")
        f.write(f"{len(nop_offsets)}\n")
        for e in nop_offsets:
            f.write(f"{e} ")
        f.write("\n")