# Native DWARF continuation experiment

The default build uses GCC 16, `-std=c++26 -freflection`, `std::format`,
elfutils/libdw and libunwind. Yalantinglibs and the C++ fmt dependency have been
removed from MVVM's build and serialization call sites, including the disabled
legacy paths. The codec uses language reflection (`<meta>`), not a third-party
reflection library. Old struct-pack checkpoints must be regenerated. Legacy
WAMR-internal code remains disabled and is not certified to build against WAMR
2.4.5 by these experiments.

## Execution and restoration

`mvvm_dwarf_runner` executes native WAMR AOT functions. The patched compiler
emits a resume dispatch at function entry and continuation markers at Wasm call
boundaries, including `call_indirect`. Numeric values that must survive entry
at those boundaries are spilled, described by `DW_AT_location`, and retained
in native DWARF. libunwind traverses the real native call chain; libdw resolves
and reads the slots. RISC-V and x86-64 native-call assembly now have CFI.

An imported `env.mvvm_checkpoint` is the cooperative safe point. Capture saves
numeric slots, function/site identities, linear memory, globals and function
table indices. Suspension then propagates through generated return paths.
Restore starts a fresh process and instance, restores the portable state,
re-enters the original export, and rebuilds the native call chain through the
compiler's dispatches. Each target frame's slot addresses are resolved from its
own DWARF. No source native stack image or machine addresses are restored.
No Asyncify transformation is used.

The experiment uses a debug ELF companion because ordinary WAMR AOT containers
do not provide an ELF mapping discoverable by libdwfl. The runner binds the
companion's `aot_func#N` symbols to the corresponding loaded AOT module before
instantiation. This adapter uses WAMR's internal module structures; snapshot
frame extraction uses DWARF. The companion, container, and Wasm must come from
the same build script. This is an experimental loader, not general production
loading of arbitrary `.aot` / `.so` combinations.

`mvvm_dwarf_checkpoint_test` is the older symbolization/codec unit test. Its
synthetic portable frame is not evidence of execution restoration. Actual
restoration evidence comes from `run_dwarf_benchmarks.py` and separate
save/restore processes.

## Reproduce

Prerequisites: GCC/g++ 16 with reflection, CMake, Ninja, libdw-dev,
libunwind-dev, LLVM/Clang 20, binutils, and Python 3. The WAMR submodule is pinned
to the tested patch on upstream WAMR 2.4.5; no history rebase is needed.

```sh
CC=gcc-16 CXX=g++-16 cmake -S . -B build-dwarf -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-dwarf --parallel 12
ctest --test-dir build-dwarf --output-on-failure

cmake -S lib/wasm-micro-runtime/wamr-compiler -B build-wamrc -G Ninja \
  -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_CXX_COMPILER=g++-16 \
  -DWAMR_BUILD_WITH_CUSTOM_LLVM=1 \
  -DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm
cmake --build build-wamrc --parallel 12

# EEMBC/coremark at 1f483d5b8316753a742cbf5590caf5bd0a4e4777.
WAMRC="$PWD/build-wamrc/wamrc" bash test/build_dwarf_benchmarks.sh \
  "$PWD/dwarf-artifacts" /path/to/coremark
python3 test/run_dwarf_benchmarks.py \
  "$PWD/build-dwarf/dwarf/mvvm_dwarf_runner" \
  "$PWD/dwarf-artifacts" "$PWD/dwarf-results"
```

The builder emits both RISC-V and x86-64 objects and AOT containers. On the
other native host, copy its objects, containers and exact Wasm files; link
each object with `gcc-16 -shared name.arch.o -o name.arch.so`. Build the same
MVVM checkout there with GCC 16, then run the same test script. Copy the other
host's result directory and run with `--cross-from /path/to/source-results` to
validate its 12 saved continuations. The cross test does not rerun the source
prefix and does not use QEMU.

## Measurement definitions

- `plain`: WAMR AOT O3 with software memory/stack checks and auxiliary-stack
  checks disabled. The Wasm retains the same no-op checkpoint imports.
- `o4-segue`: requests O4 and Segue with the same checks disabled. WAMR 2.4.5
  clamps O4 to O3. Segue applies on x86-64, not RISC-V; this is not an O4 score.
- `dwarf`: native debug ELF with continuation lowering. Functions currently
  use `optnone`/`noinline`, LLVM code generation O3 and pinned volatile slots.
  Overhead includes this conservative lowering and hook costs, not only DWARF.
- `run_ms`: export execution, excluding load/setup, file I/O and shutdown.
- `capture_ms`: native stack extraction and copying guest state at the hook.
- `encode_ms`: reflection encoding, excluding file writes.
- `decode_ms`: decode, layout validation, guest-state copying and arming.
- `stack_restore_ms`: DWARF lookup/slot restore in destination prologues.
  Remaining application execution is separate; network transfer is not timed.

CoreMark uses the upstream list, matrix, state and CRC algorithms with a custom
deterministic runner. A hook follows state transitions and increments an
observable counter before each checkpoint. The upstream 2K seed CRCs are checked
on every completed run; final CRC/counter must equal uninterrupted AOT.
This modified workload is not an official/certified CoreMark score. The other
workload exercises indirect calls, nested native frames and values live across
calls.

## Validated boundary

Native Linux riscv64 and x86-64, little-endian numeric Wasm values, one
fixed-size memory, numeric globals and function-index tables are validated.
Threads, arbitrary-PC signal capture, WASI descriptors, GC/externref objects,
SIMD, memory/table growth, multi-value ABI corner cases, tail calls, module
start functions, other OSes, and 32-bit/big-endian targets are not validated.
LLVM verification rejects unsupported live values; DWARF decode, slot schema
and resume-PC checks fail rather than substituting missing values.

The pass is opt-in (`--mllvm=-mvvm-dwarf-resume`). Ordinary compiler output is
unaffected apart from native-call CFI. Raw measurements and tested identifiers
are in `artifact/result/dwarf_native_20261107`.
