# DWARF native continuation baseline, 2026-11-07 UTC

Native RISC-V (lanxin2/wr5219) and x86-64 (giga/Threadripper 7960X).
WAMR patch: `231d3ab0e2e7832d22064178a536c703ab117642`, based on upstream 2.4.5.
GCC 16 C++26 reflection and std::format are used; no Asyncify, Yalantinglibs or C++ fmt.

Both hosts passed 12 separate-process save/restore cases and 5 rejection cases.
Both cross-ISA directions passed 12 cases each, checking the final CRC/result
and side-effect count against uninterrupted native AOT. CoreMark captures five
native frames and 116 DWARF slots; indirect-call tests capture three and 38.

Five-run medians, milliseconds; CoreMark has 1,000 iterations, indirect has 1,000,000:

| Host | Workload | Plain AOT O3 | O4 request + Segue | DWARF baseline |
|---|---|---:|---:|---:|
| RISC-V | CoreMark algorithms | 216.218 | 217.038 | 472.756 |
| x86-64 | CoreMark algorithms | 37.487 | 39.345 | 79.109 |
| RISC-V | Indirect calls | 68.586 | 68.101 | 152.754 |
| x86-64 | Indirect calls | 14.486 | 14.588 | 30.101 |

WAMR clamps O4 to O3; Segue applies only to x86-64. Software bounds checks and
auxiliary-stack checks are disabled. The DWARF baseline conservatively pins
spill slots and uses optnone/noinline. These are modified CoreMark workloads
with checkpoint imports, not official CoreMark scores. CPU affinity/frequency
were not fixed; raw samples include the observed spread and support no claim
of statistically significant differences between the two plain configurations.

CoreMark checkpoint component medians (ms):

| Host | Capture + state copy | Reflection encode | Decode + state restore | Native stack restore |
|---|---:|---:|---:|---:|
| RISC-V | 3.951 | 0.511 | 0.197 | 3.172 |
| x86-64 | 1.209 | 0.254 | 0.094 | 1.054 |

CoreMark snapshots are 145,363 bytes; indirect snapshots are 133,582 bytes.
Timing excludes transfer and disk I/O. `run_ms` excludes module loading/setup.
Negative cases cover truncation, wrong Wasm identity, invalid resume PC,
mismatched slot name, and stripped DWARF; each exits with status 1.

Toolchains: lanxin2 GCC 16.0.1 20260315, libdw 0.190, libunwind 1.6.2,
Linux 6.18.3+; giga GCC 16.0.1 20260322, libdw 0.194, libunwind 1.8.3,
Linux 7.0.0-mk1+. Both AOT targets were generated with LLVM 20.1.2.
CoreMark source is `1f483d5b8316753a742cbf5590caf5bd0a4e4777`.

This validates cooperative numeric-Wasm continuations through a debug ELF
companion, not arbitrary-PC native-stack migration or a production WASI/thread
runtime. Full limits and reproduction are in `docs/DWARF_RUNTIME.md`.
Original snapshots remain in `/srv/lanxin/mvvm-dwarf-experiment/results-rv-v3`
and `/root/mvvm-dwarf-20261107/results-x86-v3`. Source save processes exited
before destination restoration; QEMU was not used.

The `measurements` directory preserves raw samples, checksums of each saved
correctness snapshot, summaries and all cross-ISA restore outcomes.
