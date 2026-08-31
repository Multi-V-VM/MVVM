# MVVM downstream fork

This directory is a source-pinned fork of `tensor-wasm-jit` 0.3.8 from
[`craton-co/craton-tensor-wasm`](https://github.com/craton-co/craton-tensor-wasm),
licensed under Apache-2.0. It is independent of and not endorsed by Craton
Software Company.

The downstream change is deliberately narrow: `src/capi.rs` exports a stable,
panic-contained C ABI for MVVM and the library is additionally built as a Rust
`staticlib`. The original Rust API and implementation remain intact.
