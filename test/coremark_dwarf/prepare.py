#!/usr/bin/env python3
"""Create a separate instrumented CoreMark source tree, leaving upstream intact."""
from pathlib import Path
import shutil
import sys

source, output = map(Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
for name in ("coremark.h", "core_list_join.c", "core_matrix.c", "core_state.c", "core_util.c", "LICENSE.md"):
    shutil.copy2(source / name, output / name)
for name in ("driver.c", "core_portme.h"):
    shutil.copy2(Path(__file__).parent / name, output / name)
path = output / "core_state.c"
code = path.read_text()
needle = "        final_counts[fstate]++;"
assert code.count(needle) == 2
code = code.replace('#include "coremark.h"', '#include "coremark.h"\nextern void mvvm_coremark_checkpoint(void);', 1)
code = code.replace(needle, needle + "\n        mvvm_coremark_checkpoint();")
path.write_text(code)
