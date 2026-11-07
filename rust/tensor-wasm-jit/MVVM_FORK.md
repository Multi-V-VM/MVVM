# MVVM downstream fork

This directory is a source-pinned fork of `tensor-wasm-jit` 0.3.8 from
[`craton-co/craton-tensor-wasm`](https://github.com/craton-co/craton-tensor-wasm),
licensed under Apache-2.0. It is independent of and not endorsed by Craton
Software Company.

The downstream C ABI integration: `src/capi.rs` exports a stable,
panic-contained C ABI for MVVM and the library is additionally built as a Rust
`staticlib`. The original Rust API remains available.

`src/checked_simd.rs` adds a bounded downstream execution path for the MVVM
NVIDIA CC backend: f32x4/i32x4 add/sub/mul with two packed vectors and a
separate output. This path emits explicit global loads/stores and a fixed
four-lane launch ABI. The NVIDIA backend rejects the legacy generic emitter's
PTX because that emitter has not been qualified for this execution contract.
See `docs/NVIDIA_CC.md` at the repository root for the accepted Wasm shape,
CPU/GPU differential tests, and scope limits. The checked path retains the
original Wasm bytes; it does not insert transparent WAMR offload calls.
