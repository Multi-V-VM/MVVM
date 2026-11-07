# NVIDIA production CC execution

`MVVM_BUILD_NVIDIA_CC=ON` builds `mvvm_nvidia_cc` and the explicit hardware
integration test. GCC 16, C++26 reflection, CUDA Driver API, NVML, OpenSSL and
the repository's locked `tensor-wasm-jit` crate are required. No fmt or
Yalanting dependency is introduced. CPU-only builds keep the NVIDIA backend
unavailable and do not report hardware validation.

```sh
CC=gcc-16 CXX=g++-16 cmake -S . -B build-gpu -G Ninja \
  -DMVVM_BUILD_NVIDIA_CC=ON -DMVVM_ENABLE_DWARF_RUNTIME=OFF \
  -DMVVM_BUILD_REFLECTION_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build-gpu -j4
python3 -m venv .venv-gpu
.venv-gpu/bin/pip install nv-attestation-sdk==2.7.3 wasmtime
.venv-gpu/bin/python test/generate_gpu_cases.py /tmp/mvvm-gpu-cases
build-gpu/gpu/nvidia_gpu_cc_test /tmp/mvvm-gpu-cases \
  "$PWD/.venv-gpu/bin/python" "$PWD/test/nvidia_attest.py"
```

Run inside a supported confidential VM with the physical GPU in production
CC-On. CC-Off and CC-DevTools are rejected. The backend requires an installed
attestation verifier before initialization, gets fresh NVML evidence bound to
its random challenge, and only enables GPU readiness after verification.
`test/nvidia_attest.py` sends the supplied evidence to NVIDIA NRAS through SDK
2.7.3 and checks the signed response nonce. Network access to the vendor
verification service is required. Installing a callback is a trust boundary:
production callers must supply a verifier that validates evidence and policy;
an unconditional `true` callback is not an attestation verifier. The example
verifies the GPU evidence; it does not implement remote CPU TDX quote appraisal
or key provisioning by an external relying party.

## Implemented execution contract

The checked tensor-wasm-jit lowering currently accepts one exported Wasm
function with three i32 parameters (A offset, B offset, destination offset),
one memory, and exactly two `v128.load` operations, one `f32x4` or `i32x4`
add/sub/mul, and one `v128.store`. The CUDA adapter packs A and B into a 32-byte
input allocation and writes a separate 16-byte output allocation. The launch
ABI is `(device_input, device_output, uint32_t lanes=4)`, grid 1, block 4.
Offsets in the load/store instructions must be zero. Overlapping CUDA buffers,
unregistered allocations, other launch shapes, shared/unified host-visible
allocations and unsupported operations are rejected.

The resulting PTX is regenerated from the supplied source Wasm before loading.
The legacy generic tensor emitter is not accepted by the CC backend. This is
explicit function translation and invocation; it does not transparently patch
an arbitrary running WAMR application or migrate an in-flight CUDA instruction.
The hardware test uses Wasmtime to execute the source Wasm independently, then
compares all 16 output bytes for 64 inputs per operation (384 cases). Float
fixtures are finite values; this is not exhaustive IEEE-754 special-value
coverage.

`copyToDevice` / `copyFromDevice` use CUDA transfers through the confidential
VM/GPU path. `encryptMemory` and `decryptMemory` validate the protected allocation
and CC state; they never disable hardware memory protection and are not general
purpose host-buffer encryption routines. Sealed storage is unsupported.

## Checkpoint and restore

Checkpoint synchronizes this backend's CUDA context and serializes its loaded
kernel sources and registered allocations through the C++26 reflection codec.
The payload uses AES-256-GCM with a fresh random nonce and a caller-supplied
32-byte migration key. The snapshot digest is bound into fresh GPU evidence.
Restore authenticates the envelope and verifies that evidence before recreating
kernels and allocations in an empty, separately attested context. Callers must
replace old device pointers using `remapRestoredPointer`. Keys are consumed on
successful checkpoint/restore; secure cross-machine key delivery is outside
this API. Plaintext snapshot buffers are wiped after use. The payload is capped
at 256 MiB and the encoded envelope at 300 MiB.

Wrong keys, corrupted snapshots, altered PTX and invalid ABI/ranges are tested
as failures. The integration test also mutates the source allocation after the
snapshot, restores into another CUDA context, checks its contents, and executes
the restored kernel. This demonstrates context recreation on one physical GPU;
it does not establish migration to a second GPU or cross-vendor compatibility.

## Validated hardware setup (2026-11-07)

6787p: RTX PRO 6000 Blackwell Server Edition, production CC-On, Intel TDX
guest, Ubuntu 24.04 / kernel 6.8.0-106, NVIDIA 590.48.01, CUDA 13.1,
GCC 16. The signed Canonical open module is used with Secure Boot enabled.
The complete test passed 384 source-Wasm/GPU comparisons, six kernels,
17,540-byte encrypted snapshot in the eight-vCPU repeat, and restored-context execution. Results and
fixture/source hashes are in `artifact/result/nvidia_cc_6787p/`.

The guest must preload `ecdsa_generic` and `ecdh` and keep the GPU open through
`nvidia-persistenced`; the tested service uses `--uvm-persistence-mode`. Without
these prerequisites, our first probe failed during SPDM establishment. After
correcting the guest setup and performing an FLR, initialization succeeded.
See NVIDIA's [guest setup guide](https://docs.nvidia.com/confidential-computing/virtual-machines/deployment-guide/guest).
A locally signed DKMS module was rejected by Secure Boot; its files were backed
up in the isolated test guest and replaced in module lookup by the matching
Canonical-signed modules. The original base VM disk was not modified.

The initial eight-vCPU guest on QEMU 9.2 showed an RCU stall. A one-vCPU
baseline passed first. After fixing module lookup, LKCA and persistence setup,
the guest was shut down cleanly and booted with QEMU 10.0.94 and eight vCPUs.
The full 384-case CC test and restored-context execution passed again, without
an RCU stall recorded during the repeat. This establishes the tested working
configuration; it does not isolate the cause of the earlier QEMU 9.2 stall.
`single-vcpu-summary.json` preserves the first run; `summary.json` records the
clean-boot eight-vCPU repeat. Host firmware and host workloads were not restarted. The negative corrupted-snapshot case
intentionally produces a vendor evidence-validation failure, followed by a
successful restore of the unmodified snapshot.

## DWARF integration follow-up

The process-level GPU participant and real-Wasm benchmark are described in
[DWARF_GPU.md](DWARF_GPU.md). The eight-vCPU guest above later developed a
long-running RCU/soft-lockup failure (captured on 2026-11-07). Its short-test
success is not a claim of long-duration SMP stability. Subsequent continuation
experiments use a one-vCPU guest; see the README for their current validation
status. A one-vCPU idle freeze was also observed, so the original short-test
result does not establish long-duration stability for either configuration.

After the host stopped returning SSH banners, an authorized BMC graceful
restart restored access. The previous host kernel journal recorded a CPU 60
soft lockup in `sha512_transform_rorx`, reached through module-signature
verification in `modprobe`; this is an observation, not an isolated root cause.
The combined test then booted with QEMU 10.0.94, one vCPU, 4 GiB RAM and
`idle=poll`, and completed its first separate-process DWARF/GPU restore.
These settings are part of the benchmark configuration. They do not establish
that `idle=poll` fixes either earlier stall, or that long-duration SMP operation
is reliable. Unlike the earlier standalone test, this follow-up did restart
the host with the owner's permission.
