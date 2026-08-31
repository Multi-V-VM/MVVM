# tensor-wasm-jit

Just-in-time compilation pipeline that opportunistically offloads hot Wasm code to the GPU. Uses a Cranelift CLIF block analyser to detect offload candidates, lowers them through `TensorWasmIR` (a normalised intermediate representation), emits PTX text, and caches compiled modules in a `DashMap`-backed cache with a `parking_lot::Mutex<LruCache>` eviction queue. A `DeoptGuard` provides a safe CPU fallback path whenever GPU offload fails or guards trip at runtime.

## v0.1.0 ABI: dispatch trampoline contract

When `rewrite_wasm` swaps a hot function body for an offload trampoline, the trampoline marshals arguments through a **scratch region** in the guest's first linear memory:

1. `call __tensor_wasm_jit_alloc(scratch_size: i32) -> i32` — host-side arena returns a non-overlapping `i32` pointer.
2. For each parameter, store its raw bytes at `scratch_ptr + arg_off` using the appropriate `<type>.store`. Offsets are byte-packed in declared order with natural alignment (i64/f64 = 8B, i32/f32 = 4B). No inter-arg padding.
3. `call __tensor_wasm_jit_dispatch(fp_lo: i64, fp_hi: i64, scratch_ptr: i32, args_byte_len: i32, results_byte_len: i32) -> i32` — runs the cached PTX kernel (CUDA path) or the host reference (no-CUDA path); writes result bytes to `scratch_ptr + args_byte_len`. Returns `0` on success, nonzero on error.
4. For each result, load its bytes from `scratch_ptr + args_byte_len + result_off` and push to the wasm stack.
5. `call __tensor_wasm_jit_free(scratch_ptr: i32, scratch_size: i32)` — returns the slot to the arena.

The fingerprint is the 64-bit BLAKE3-truncated blueprint hash. We split it into two `i64` halves so a Wasm 1.0 module without the `multi-value` proposal can still carry the full 64-bit value.

### Supported types (v0.1.0)

Only primitive scalars: **i32, i64, f32, f64**. v128 and reftypes in either parameters or results disqualify a candidate; the rewriter falls back to leaving the original body intact and Wasmtime executes it on the CPU. Lifting this restriction is a v0.2 task — it needs richer marshalling and an SoA layout decision on the GPU side.

### Host import signatures

```text
(func $tensor-wasm:jit/host::alloc    (param i32)                            (result i32))
(func $tensor-wasm:jit/host::free     (param i32 i32))
(func $tensor-wasm:jit/host::dispatch (param i64 i64 i32 i32 i32)            (result i32))
```

The host implementation lives in [`tensor_wasm_exec::jit_dispatch`](../tensor-wasm-exec/src/jit_dispatch.rs).

## Feature flags

| Flag | Default | Description |
|---|---|---|
| `auto-offload` | no | Reserved for CUDA-side wiring tested under `--features cuda`. The pipeline itself is always compiled in. |
| `cuda-oxide-backend` | no | Gates the [`pliron_dialect`](src/pliron_dialect.rs) scaffold module — the Cranelift IR → Pliron `dialect-mir` lowering surface tracked in [RFC 0001](../../rfcs/0001-cuda-oxide-integration.md) step 4. v0.3.1 ships scaffold only (`StubLowerer` + mapping table + final trait signature); the real lowering lands in v0.4 once the workspace toolchain bumps to satisfy cuda-oxide's `nightly-2026-04-03` pin. No extra deps today. |
| `pliron-llvm-backend` | no | Strict superset of `cuda-oxide-backend` that pulls in `pliron-llvm` 0.15 (and therefore `llvm-sys = "221"`, which needs a system LLVM 221 install) to wire the stage-2 `twasm.* → llvm.*` rewrite in `pliron_ptx`. Intended for W4.x CI images; the default workstation build will not satisfy the `llvm-sys` link step. |
| `kernel-registry` | no | Compiles the `registry` module so the runtime can resolve PTX kernels published as HMAC-SHA256-signed `KernelManifest` records. Pulls in `hmac`, `sha2`, `serde`, `serde_json`, and `bincode` (the manifest wire/blob format). v0.3.7 ships the in-memory backend + signing helpers; the on-disk store and the server-side `/kernels` route land in v0.4. See [docs/KERNEL-REGISTRY.md](../../docs/KERNEL-REGISTRY.md). |
| `differential-oracle` | no | Compiles the `differential` module — the roadmap feature #6 differential JIT correctness oracle scaffold. v0.3.6 compiles the harness types; v0.4 brings the two-path runner against the self-hosted CUDA runner. The `differential_proptest` integration test opts in via `required-features = ["differential-oracle"]`. See [docs/DIFFERENTIAL-ORACLE.md](../../docs/DIFFERENTIAL-ORACLE.md). No extra runtime deps. |
| `test-utils` | no | Re-exports the internal `lowering_test_support` module (Cranelift fixture builders for the wave-2 lowering passes). Strictly opt-in: external consumers should treat the surface as unstable, since fixture signatures track the in-progress IR and may change in any 0.x.y release. The crate's own unit tests pick the module up via `cfg(test)`; the `lowering_e2e` integration test opts in via `required-features = ["cuda-oxide-backend", "test-utils"]`. No extra deps. |

### Unstable `pliron_*` surface

The `pliron_dialect`, `pliron_lowering`, and `pliron_ptx` modules are gated on `cuda-oxide-backend` and additionally marked `#[doc(hidden)]`. They host the in-progress W3.x/W4.x scaffolding (today: ~700 lines of `NotYetWired` stubs) that the v0.4 author will fill in. Names, signatures, and behaviour inside these modules are NOT covered by semver compatibility until tensor-wasm-jit reaches 1.0 — external consumers should not import from them.

See [docs/BUILD.md](../../docs/BUILD.md) for the project-wide flag taxonomy.

## Dependencies

External crates this crate depends on (pinned at workspace root):

- `blake3` — stable cross-platform fingerprints for the kernel cache key.
- `cranelift-codegen` — CLIF block analysis driving offload detection.
- `dashmap` — lock-free storage for the kernel cache.
- `lru` — eviction policy queue.
- `parking_lot` — non-poisoning mutex backing the eviction queue.
- `thiserror` — derive macro for the JIT pipeline's error enum.
- `tracing` — structured spans/events for compilation and deopt.
- `wasm-encoder` / `wasmparser` — the rewrite pipeline's read/write Wasm machinery.

Internal: `tensor-wasm-core` (only for the optional `TensorWasmMetrics` handle in `DeoptGuard::with_metrics`).
