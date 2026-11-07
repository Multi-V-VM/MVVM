# bench x86 slowdown experiment — 2026-11-07

6787p, Xeon 6710E, logical CPU 8. One warmup plus seven measured repetitions
per variant, shuffled order. Timing covers the workload invocation, including
its ordinary I/O/verification, but excludes process startup and module loading.
All eight NAS Class W programs passed their built-in numerical verification;
LINPACK 200x200 passed a solution-error bound of 1e-9 for 32 pairs of rolled
and unrolled solves. The full experiment has 288 successful process invocations.

1× is native GCC 16 -O3; lower is faster. The figure excludes MG at the user's
request. The full table and raw measurements below retain it.

| Benchmark | GCC O3 ms | WAMR O3 / GCC | Segue / GCC | DWARF / GCC | DWARF vs WAMR O3 |
|---|---:|---:|---:|---:|---:|
| EP | 2777.673 | 0.888× | 0.883× | 0.885× | -0.29% |
| CG | 258.395 | 1.119× | 1.135× | 1.130× | +0.99% |
| FT | 154.270 | 1.340× | 1.248× | 1.255× | -6.38% |
| MG | 107.094 | 2.194× | 2.010× | 2.282× | +4.01% |
| BT | 2144.161 | 1.010× | 0.963× | 0.993× | -1.68% |
| LU | 3248.088 | 1.192× | 1.175× | 1.196× | +0.34% |
| SP | 3147.229 | 1.376× | 1.264× | 1.373× | -0.22% |
| IS | 153.021 | 0.830× | 0.836× | 0.828× | -0.27% |
| LINPACK | 56.261 | 2.133× | 1.790× | — | — |

## Interpretation and limits

- The requested `--opt-level=4 --enable-segue` is effectively O3 + Segue:
  this WAMR compiler clamps levels above 3. All WAMR modes have software memory,
  stack/aux-stack checks and SIMD disabled, matching the earlier runtime setup.
- Native GCC 16 and the Clang 21/WASI input both use -O3 and -ffp-contract=off;
  native target is generic x86-64. GCC uses host libc/libm and Wasm uses an
  isolated Ubuntu wasi-libc package. Ratios include compiler/libc/ABI effects,
  not only VM dispatch overhead. The host was shared and frequency was not fixed.
- NAS execution includes the existing call structure, without extra per-loop
  checkpoint imports. The optimized DWARF pass emits 16–40 resumable functions
  per NAS module around imports/indirect calls. These are steady-state compiler
  instrumentation measurements, not validated checkpoint/restore or arbitrary
  mid-kernel migration. Small negative overheads are measured differences, not
  an established causal speedup from DWARF.
- The fixed-work LINPACK kernel has no native imports or indirect calls and
  the optimized pass emits zero resumable functions. Its DWARF-mode timing is
  retained in raw JSON but omitted from the DWARF bars/table to avoid claiming
  zero-cost migration. AOT slowdown is still measured and shown.
- The initial 256x256 LINPACK input hit a zero pivot in native GCC; the failed
  warmup is preserved, excluded from performance statistics, and replaced with
  the valid 200x200 input. No numerical kernels were changed.
- Existing NAS parameter headers had nonstandard sizes/iteration counts despite
  Class S comments. The repository setparams utility regenerated standard Class W
  parameters in an isolated copy. `prepared-source.patch.gz` records the exact
  wrapper, declaration and fixed-work adaptations. Original bench sources were not changed.

## Evidence

- `summary.json`: combined medians, min/max and all seven samples.
- `nas-results/raw.jsonl`, `linpack-results/raw.jsonl`: commands, return values,
  invocation/wall time and full stdout/stderr log references. Extract
  `run-logs.tar.gz` here to restore the referenced per-run logs.
- `manifest.json`: original/prepared source and binary hashes, pinned submodules,
  compiler identity and count of DWARF-resumable functions.
- `prepare.py`, `build.py`, `run.py`, `native.c`, `wrapper.c`: isolated experiment
  scripts used on 6787p, with the original source copied under
  `/root/mvvm-bench-suite/source/{nas,linpack}`.
