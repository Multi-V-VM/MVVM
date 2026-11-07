# Optimized DWARF native continuation, 2026-11-07 UTC

The optimized compiler and runtime passed native save/restore on RISC-V and
x86-64, and 12 cross-ISA restores in each direction. The measured steady-state
overhead over plain AOT is 3.19% / 2.61% for the modified CoreMark workload,
and 8.20% / 45.10% for the high-frequency indirect-call workload, respectively.
The latter remains a substantial x86-64 overhead; the CoreMark result must not
be generalized to all programs.

## Tested source and implementation

- MVVM runtime/tests: `195115979496c5bd5d34d86fc650ea87d2f2fddc`.
- MVVM CI directory-ownership fix: `e617fd784ec9104d7b2b292a9c4e74900818b6b5`.
- WAMR: `7f46ed5d74c80ddfeba47df2c3385f544170a7af`, based on upstream 2.4.5.
- CoreMark algorithms: `1f483d5b8316753a742cbf5590caf5bd0a4e4777`.
- [Source hashes](mvvm-tested-source-sha256.txt) and
  [generated Wasm/AOT/object/ELF hashes](mvvm-tested-aot-sha256.txt) accompany
  the measurements. WAMR history was not rebased.

The compiler now leaves non-suspending functions untouched, promotes ordinary
Wasm locals to SSA, and spills only values whose uses lose dominance when
resume edges are introduced. Those slots retain real DWARF location entries.
LLVM O3 is enabled in resumable functions; normal execution checks a TLS flag
instead of calling a helper at every entry. Empty leave helpers are removed.
Destination restoration reuses one DWARF module cache across the frame chain.
Resumable functions still have fixed DWARF-addressable slots and are noinline.

All numeric native-frame values are extracted through libdw location
evaluation, with libunwind walking the actual native frames. This is not
Asyncify, copying a source machine stack image, or replaying the executed
prefix. Serialization uses GCC 16 C++26 language reflection and `std::format`;
Yalantinglibs/struct_pack and C++ fmt dependencies were removed, including
their legacy MVVM call sites.

## Steady-state performance

Each process is pinned to logical CPU 8. The table gives the median of nine
runs in milliseconds, with the complete samples in `measurements`.
CoreMark uses 1,000 iterations; indirect calls use 1,000,000 iterations.

| Host | Workload | Plain AOT O3 | O4 request + Segue | Optimized DWARF | Overhead vs plain |
|---|---|---:|---:|---:|---:|
| RISC-V / lanxin2 | CoreMark algorithms | 218.734 | 220.306 | 225.718 | +3.19% |
| x86-64 / giga | CoreMark algorithms | 39.629 | 40.065 | 40.662 | +2.61% |
| RISC-V / lanxin2 | Indirect calls | 69.487 | 71.140 | 75.188 | +8.20% |
| x86-64 / giga | Indirect calls | 15.199 | 14.176 | 22.053 | +45.10% |

Formula: `100 * (median(dwarf) / median(plain) - 1)`.
These are modified CoreMark algorithms with checkpoint hooks, not certified
CoreMark scores. All variants retain the same no-op checkpoint imports.
WAMR clamps the O4 request to O3; Segue applies only on x86-64. Software memory,
stack and auxiliary-stack checks are disabled in all compared variants.
The optimized DWARF column uses O3, without Segue. The reported overhead is
relative to plain O3, not relative to the fastest observed variant.

The original conservative implementation measured 472.756 ms / 79.109 ms for
CoreMark on RISC-V / x86-64, versus 225.718 ms / 40.662 ms here. The older runs
were unpinned with five repetitions, so this is historical context rather
than a matched statistical speedup experiment. CoreMark live DWARF slots fell
from 116 to 30; indirect-call slots fell from 38 to 22.

CPU frequency was not locked and the machines were shared; giga also had
Quartus/VMware activity. Affinity reduces migration noise but does not remove
contention or frequency variation. Small differences between plain O3 and
O4-request/Segue are not established as statistically significant. No
hardware-counter profile is claimed for the remaining indirect-call overhead.

## Checkpoint and correctness

Checkpoint component medians from the nine timed save/restore repetitions:

| Host | Workload | Capture + state copy | Reflection encode | Decode + state restore | Native stack restore |
|---|---|---:|---:|---:|---:|
| RISC-V | CoreMark | 3.785 ms | 0.541 ms | 0.138 ms | 1.535 ms |
| x86-64 | CoreMark | 0.973 ms | 0.188 ms | 0.079 ms | 0.383 ms |
| RISC-V | Indirect calls | 3.755 ms | 0.478 ms | 0.143 ms | 1.347 ms |
| x86-64 | Indirect calls | 0.914 ms | 0.156 ms | 0.041 ms | 0.344 ms |

CoreMark snapshots contain five native frames, 30 DWARF slots, and 141,583
bytes. Indirect snapshots contain three frames, 22 slots, and 132,891 bytes.
Timing excludes network transfer and file I/O. `run_ms` excludes module setup.
The stack-restore time excludes execution of the remaining application suffix.

- Each native host: 12 save/restore cases and seven rejection cases passed,
  both in the initial optimized validation and the final pinned experiment.
- RISC-V to x86-64: 12/12 passed.
- x86-64 to RISC-V: 12/12 passed.
- Source processes exited before cross-ISA destination restoration. Both
  targets execute natively, without QEMU.
- CoreMark's upstream 2K seed CRCs are checked on every completed run. Final
  CRC/result and the side-effect counter must equal uninterrupted AOT.
- Rejection checks cover truncation, wrong Wasm identity, wrong root argument,
  invalid resume PC, mismatched slot, compiler schema mismatch, and stripped
  DWARF. They fail explicitly instead of fabricating absent values.

The cross-ISA inputs came from `results-rv-opt2` and `results-x86-opt2`; those
source summaries and snapshot hashes are retained alongside the cross results.
The `*-pinned` directories contain the final performance samples. Original
snapshot files remain in `/srv/lanxin/mvvm-dwarf-experiment/results-rv-opt2`
and `/root/mvvm-dwarf-20261107/results-x86-opt2`.

## Toolchains, reproduction and limits

lanxin2/wr5219: native riscv64, GCC 16.0.1 20260315, Linux 6.18.3+,
libdw 0.190, libunwind 1.6.2. giga: Threadripper 7960X, GCC 16.0.1 20260322,
Linux 7.0.0-mk1+, libdw 0.194, libunwind 1.8.3. Both AOT targets were
generated by the same patched WAMR compiler with LLVM 20.1.2.

Follow `docs/DWARF_RUNTIME.md` to build the compiler, runtime and benchmark
artifacts. The builder defaults to `DWARF_OPTIMIZE=1`; set it to zero for the
conservative baseline. The final measurement invocation is:

```sh
python3 test/run_dwarf_benchmarks.py /path/to/mvvm_dwarf_runner \
  /path/to/aot-artifacts /path/to/results --cpu 8 --repeats 9
```

Use `--cross-from /path/to/other-host-results` for cross-ISA validation.
The supported experiment is cooperative safe-point recovery of numeric,
little-endian Wasm through an ELF companion on native Linux RISC-V/x86-64.
It is not arbitrary-PC capture or a complete WASI/thread resource migrator.
Memory/table growth, GC/externref, SIMD, tail calls, module starts and
multi-value ABI corner cases remain outside the validated boundary.

## CI

At source `e617fd7`, all six Debug/Release jobs completed successfully:
[Linux native DWARF](https://github.com/Multi-V-VM/MVVM/actions/runs/42343683895),
[macOS codec](https://github.com/Multi-V-VM/MVVM/actions/runs/42343683915),
[Windows codec](https://github.com/Multi-V-VM/MVVM/actions/runs/42343683891).
Linux builds the patched AOT compiler and runs actual native continuation
and rejection tests. macOS and Windows build and test the portable C++26
reflection codec; the Linux native DWARF runtime is not claimed to be ported.
The legacy private download server and stale package snapshots are no longer
used. The experiment workflows do not trigger the old automatic publisher.
