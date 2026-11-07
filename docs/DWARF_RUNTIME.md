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

DWARF here describes compiler-generated continuation slots for Wasm execution,
not the original C/C++ source variables. Recovery does not require source-level
DWARF and does not decompile native code back into Wasm. It reconstructs the
Wasm continuation on another native AOT call stack.

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
libunwind-dev, LLVM/Clang 20, binutils, Python 3, and `python3-pyelftools`. The WAMR submodule is pinned
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
- `dwarf`: native debug ELF with optimized continuation lowering (the build
  script default). A call-graph pass leaves non-suspending functions untouched.
  Promoting Wasm locals to SSA and checking dominance after adding resume
  edges identifies only values that actually require DWARF-backed spills.
  LLVM O3 optimizes the remaining code; an inline TLS flag avoids helper calls
  during normal execution. Resumable functions remain noinline. Set
  `DWARF_OPTIMIZE=0` to reproduce the conservative optnone baseline.
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

The pass is opt-in (`--mllvm=-mvvm-dwarf-resume`); add
`--mllvm=-mvvm-dwarf-optimize` and use optimized LLVM IR for the fast version.
Snapshots also validate a compiler-generated continuation schema and root
invocation argument; incompatible lowering variants are rejected. A single
DWARF module cache is reused while rebuilding all frames of one continuation.
Ordinary compiler output is
unaffected apart from native-call CFI. Raw measurements and tested identifiers
are in `artifact/result/dwarf_native_20261107`.

## Checkpoint completeness

The compiler emits `mvvm_dwarf_requirements`, an allocated read-only ELF object
independent of `.debug_info`. Its `MVVMREQ3` encoding records functions,
continuation call targets, slot names and byte widths. The entire inventory is
bound to `mvvm_dwarf_schema_id`. It contains no stack offsets, machine addresses
or value payloads, and cannot substitute for missing DWARF locations.

The inventory conservatively requires every scalar continuation slot in a
function at every one of its safe points. Compiler dominance analysis identifies
values that require spills after resume edges are added; unsupported live types
or slot widths fail compilation. This is a conservative contract for this
compiler pipeline, not a general liveness proof for arbitrary native binaries.
It may reject a missing slot even when that slot is dead at the current site.

On capture and destination-frame reconstruction, libdw's variables must match
that independent inventory exactly. Missing/duplicate/unexpected variables,
width mismatches, unavailable or unsupported locations and invalid resume PCs
fail. All slots in a destination frame are validated before any are written.
An incomplete capture propagates an error before snapshot serialization or
publication. Compiler-created spills remain necessary, but there is no runtime
spill-address fallback when DWARF is unavailable. Parsed inventories are cached
only for a capture/restore operation; ordinary execution never parses them.
Pre-inventory debug companions fail capture, and old snapshots fail schema
validation. Rebuild companions and regenerate snapshots, including GPU workloads.

Concrete negative test: `test/delete_dwarf_entry.py` removes the 15-byte
`DW_TAG_variable` for `mvvm_slot_0:l1.1.reg2mem` in the indirect workload's
`aot_func#0`. This holds the loop accumulator initialized to 17 and used after
the indirect call. The mutator repairs DIE references and public indexes,
verifies exactly one DIE disappeared, and checks every allocated ELF section
is unchanged. Both saving and restoring then report:

```text
FAIL: missing required DWARF slot mvvm_slot_0:l1.1.reg2mem in function 0
```

No rejected checkpoint file exists. A separate snapshot mutation deletes one
serialized slot. Together with existing malformed-PC, renamed-slot, schema,
identity, argument, truncation and stripped-DWARF cases, the suite has 10
negative cases per native ISA. Actual correctness covers first/middle/final
positions and compares final result and side-effect count with uninterrupted
execution: 12 x86-native, 12 RISC-V-native and 12 in each cross-ISA direction.

Incremental timing experiment on 6787p, Xeon 6710E, GCC 16 Release / LLVM 20:
use identical Wasm inputs, one CPU (8), one warmup and 11 measured rounds, with
before/after ordering shuffled by fixed seed 20261107. Indirect uses 10,000,000
iterations and CoreMark-derived uses 10,000. Every timing round also saves and
restores in separate processes and checks result/side effects. The before
compiler is `7f46ed5d74c80ddfeba47df2c3385f544170a7af`; the before runtime is
MVVM `c315376ae044c89fe506647470d185689c26c0a6` with the same CPU-only build options.

```sh
python3 test/measure_dwarf_integrity.py   /path/to/before/mvvm_dwarf_runner /path/to/after/mvvm_dwarf_runner   /path/to/before/artifacts /path/to/after/artifacts   /path/to/fresh/results --cpu 8 --repeats 11
```

Execution medians changed -0.95% (indirect) and -0.03% (CoreMark-derived), within
shared-host timing variation; no speedup is established. Capture changed by
+0.006 ms and +0.069 ms, native restore by +0.005 ms and +0.035 ms. These are
incremental costs of completeness enforcement, not total continuation overhead.
[Raw results](../artifact/result/dwarf_integrity_20261107/) retain all samples,
negative diagnostics, deletion evidence and artifact hashes. The original
NAS/LINPACK execution-only chart and earlier GPU experiments have separate scopes.

## CI coverage

The Linux workflow builds GCC 16/C++26 Debug and Release configurations, builds
the patched LLVM 20 AOT compiler, and executes native checkpoint restoration
and rejection tests. It uses the Ubuntu 26.04 package repositories and no
private WASI/LLVM download server. macOS and Windows run the portable
reflection codec tests using native GCC 16 toolchains. They do not claim that
the Linux/libdw native continuation runtime is ported to those systems.
Workflow names distinguish this coverage from the legacy build pipelines and
do not trigger the old automatic release publisher for experiment branches.
