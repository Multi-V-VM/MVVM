# DWARF + GPU process continuation

Build with GCC 16 / C++26 reflection and both `MVVM_ENABLE_DWARF_RUNTIME=ON`
and `MVVM_BUILD_NVIDIA_CC=ON`. This adds a GPU participant to
`mvvm_dwarf_runner`; CPU-only builds and their snapshot format are unchanged.
The participant uses the existing production NVIDIA backend and the checked
`tensor-wasm-jit` lowering described in [NVIDIA_CC.md](NVIDIA_CC.md).

## Application interface

The Wasm application imports these functions from `env`:

| Import | Wasm signature | Meaning |
|---|---|---|
| `mvvm_gpu_alloc` | `(i32 bytes) -> i32` | Allocate zeroed device memory; return a stable handle |
| `mvvm_gpu_upload` | `(i32 handle, i32 offset, i32 bytes)` | Copy Wasm linear memory to the allocation |
| `mvvm_gpu_download` | `(i32 handle, i32 offset, i32 bytes)` | Copy the allocation to Wasm linear memory |
| `mvvm_gpu_compile` | `(i32 wasm_offset, i32 wasm_bytes)` | Verify and translate the checked `op` kernel |
| `mvvm_gpu_execute` | `(i32 input_handle, i32 output_handle)` | Synchronous checked four-lane kernel launch |
| `mvvm_checkpoint` | `()` | Existing compiler/DWARF safe point |

The current participant supports one checked kernel, device 0, one stopped
execution thread, at most 64 MiB of registered GPU allocations, and a combined
encoded snapshot below 128 MiB. It does not capture arbitrary CUDA libraries,
external allocations, live CUDA instruction state, streams, or other host
resources. Wasm pointers and lengths are validated at the import boundary.

At a checkpoint, the WAMR safe-point trampoline suspends execution. Native
locals and operand slots are read through their actual DWARF locations. CPU
linear memory, globals and tables retain the ordinary continuation format.
After unwinding, while the application remains stopped, GPU kernels and
allocations are checkpointed. An outer AES-256-GCM envelope authenticates the
CPU continuation, GPU ciphertext and handle mapping together. The GPU backend
also binds its own snapshot to fresh NVIDIA evidence. Neither the CPU half nor
the GPU half can be replaced independently without authentication failure.

Restore runs in a **new process**. It authenticates the combined envelope,
validates the CPU module/schema/layout, recreates the GPU context, reloads the
verified kernel, restores device allocations and remaps the stable handles.
Only then does it arm the native DWARF rewind. The rebuilt call chain resumes
at the saved safe point without executing prior GPU operations again. Native
CUDA addresses never enter the Wasm stack or its linear memory.

The 32-byte key is provided through a private regular file, outside the
snapshot. The example does not implement an external key broker, CPU TDX quote
appraisal, anti-rollback storage, or cross-machine key delivery. Protect that
file and provision it separately on the destination. GPU evidence verification
requires network access to NVIDIA NRAS. The production path rejects CC-Off and
CC-DevTools. The separate `plain` comparator requires **hardware CC-Off** and is
only for measuring the matched non-CC execution path; it does not claim
confidential execution.

## Reproduction

Inside the confidential guest, with the physical GPU in production CC-On:

```sh
CC=gcc-16 CXX=g++-16 cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DMVVM_BUILD_NVIDIA_CC=ON \
  -DMVVM_ENABLE_DWARF_RUNTIME=ON
cmake --build build -j2
# A compatible prebuilt Rust static library can alternatively be provided with
# -DMVVM_GPU_TENSOR_LIBRARY=/absolute/path/libtensor_wasm_jit.a.
python3 -m venv .venv-gpu
.venv-gpu/bin/pip install nv-attestation-sdk==2.7.3 wasmtime==50.0.1
.venv-gpu/bin/python test/generate_gpu_cases.py /tmp/gpu-cases
WAMRC=/path/to/patched/wamrc GPU_KERNEL=/tmp/gpu-cases/i32x4_add.wasm \
  bash test/build_dwarf_gpu.sh /tmp/dwarf-gpu-workload
umask 077
head -c 32 /dev/urandom > /tmp/mvvm-migration.key
export MVVM_GPU_MODE=cc
export MVVM_GPU_KEY_FILE=/tmp/mvvm-migration.key
export MVVM_GPU_ATTEST_PYTHON="$PWD/.venv-gpu/bin/python"
export MVVM_GPU_ATTEST_HELPER="$PWD/test/nvidia_attest.py"
python3 test/run_dwarf_gpu_benchmarks.py \
  --runner build/dwarf/mvvm_dwarf_runner --workload /tmp/dwarf-gpu-workload \
  --output /tmp/dwarf-gpu-cc --repeats 5 --iterations 1000 10000
```

For the non-confidential comparator, keep the same disk image, vCPU/memory
configuration and artifacts, but boot a regular KVM guest after externally
switching the isolated GPU to CC-Off. Set `MVVM_GPU_MODE=plain` and run into a
different output directory. NVIDIA 590.48.01 rejects a CC-Off GPU in a TDX
guest ([driver check](https://github.com/NVIDIA/open-gpu-kernel-modules/blob/590.48.01/src/nvidia/src/kernel/gpu/conf_compute/conf_compute.c)).
Consequently this comparison measures the combined TDX + GPU CC configuration,
including the strict runtime path, not GPU encryption in isolation. The script never changes GPU mode or reboots a machine.

The test executes a real WAMR AOT application. Its nested/indirect call chain
launches a translated `i32x4.add` Wasm kernel in each iteration. Checkpoints
occur after the kernel completes but **before its output is copied into Wasm**.
Restore therefore needs the saved GPU output to produce the correct result.
A separate 1 MiB allocation is filled with a pattern and checked after restore.
The independent oracle is `17 + 6*n*(n-1) + 54*n` modulo 2^32. First, middle
and final checkpoints verify CPU side-effect counts, distinct process IDs,
and the exact number of GPU launches before and after restore. Wrong keys,
modified ciphertext and a wrong root invocation argument must fail.

`raw.json` contains every command, exit status, process wall time and internal
phase measurements. `summary.json` contains samples and median/min/max.
`run_ms` covers the Wasm invocation including its GPU initialization imports,
allocations, transfers and kernels; process-level CUDA/attestation startup is
reported separately. Migration wall time is the save-process wall time plus
the restore-process wall time, excluding the Python gap between them. Recovery
wall time starts at destination process creation. File I/O is included but
there is no durable-storage fsync guarantee. These are local sequential
process handoffs on one GPU, not network transfer or two-GPU migration.

Combined CC overhead is computed as TDX/CC-On divided by KVM/CC-Off median
`run_ms`, minus one. Both use the same source, binary and workload parameters.
DWARF execution overhead compares uninterrupted DWARF-enabled execution with
ordinary AOT execution in the same hardware mode. Migration overhead compares
the two process wall times against one uninterrupted DWARF process. NRAS time
is a component of those totals, not an additional amount to add again.
