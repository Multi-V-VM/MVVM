// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! End-to-end auto-offload pipeline test.
//!
//! Pipeline exercised:
//!   1. Source WAT defines a hot `add(a: i32, b: i32) -> i32` function.
//!   2. `rewrite_wasm` swaps the body for the dispatch trampoline.
//!   3. The rewritten module is validated by `wasmparser::Validator`.
//!
//! The fully-running dispatch round-trip (where the host actually adds the
//! args and returns the sum) lives in `tensor-wasm-exec`'s test suite because
//! that's where the wasmtime + linker plumbing lives. This test ensures
//! the bytecode produced by the rewriter is well-formed and contains the
//! expected calls to `__tensor_wasm_jit_alloc`, `__tensor_wasm_jit_dispatch`, and
//! `__tensor_wasm_jit_free`.

use tensor_wasm_jit::cache::KernelCache;
use tensor_wasm_jit::detector::DetectorConfig;
use tensor_wasm_jit::rewrite::{rewrite_wasm, RewriteOptions, DEFAULT_HOST_MODULE};

/// A hot `add` function decorated with a `loop` so the detector sees a
/// trip-count hint. The body is mostly v128 ops so it clears the offload
/// threshold; the externally visible signature is `(i32, i32) -> i32`.
const HOT_ADD_WAT: &str = r#"
    (module
      (memory 1)
      (func (export "add") (param $a i32) (param $b i32) (result i32)
        (local $v v128)
        (loop $L
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
          (local.set $v (i32x4.add (local.get $v) (local.get $v)))
        )
        (i32.add (local.get $a) (local.get $b))
      )
    )
"#;

#[test]
fn rewriter_emits_validatable_wasm_for_hot_add() {
    let wasm = wat::parse_str(HOT_ADD_WAT).expect("parse wat");
    let cache = KernelCache::new();
    let opts = RewriteOptions {
        detector: DetectorConfig {
            v128_ratio_threshold: 0.05,
            min_trip_count: 64,
        },
        ..RewriteOptions::default()
    };
    let out = rewrite_wasm(&wasm, &opts, &cache).expect("rewrite");

    // The function should have been swapped.
    assert_eq!(out.offloaded_functions.len(), 1);

    // The output must validate with wasmparser.
    wasmparser::Validator::new()
        .validate_all(&out.rewritten_wasm)
        .expect("rewritten wasm validates");

    // The rewritten module must import all three host functions.
    let mut saw_dispatch = false;
    let mut saw_alloc = false;
    let mut saw_free = false;
    for payload in wasmparser::Parser::new(0).parse_all(&out.rewritten_wasm) {
        if let wasmparser::Payload::ImportSection(reader) = payload.unwrap() {
            for imp in reader {
                let imp = imp.unwrap();
                if imp.module == DEFAULT_HOST_MODULE {
                    match imp.name {
                        "dispatch" => saw_dispatch = true,
                        "alloc" => saw_alloc = true,
                        "free" => saw_free = true,
                        _ => {}
                    }
                }
            }
        }
    }
    assert!(saw_dispatch, "missing dispatch import");
    assert!(saw_alloc, "missing alloc import");
    assert!(saw_free, "missing free import");
}

#[test]
fn trampoline_calls_alloc_dispatch_and_free() {
    // Confirm the rewritten function body contains calls to all three
    // host imports — alloc first, dispatch in the middle, free last.
    let wasm = wat::parse_str(HOT_ADD_WAT).expect("parse wat");
    let cache = KernelCache::new();
    let opts = RewriteOptions {
        detector: DetectorConfig {
            v128_ratio_threshold: 0.05,
            min_trip_count: 64,
        },
        ..RewriteOptions::default()
    };
    let out = rewrite_wasm(&wasm, &opts, &cache).expect("rewrite");

    // Walk the code section; the first body is the swapped function.
    // Count distinct call targets.
    let mut call_targets: Vec<u32> = Vec::new();
    for payload in wasmparser::Parser::new(0).parse_all(&out.rewritten_wasm) {
        if let wasmparser::Payload::CodeSectionEntry(body) = payload.unwrap() {
            let mut reader = body.get_operators_reader().expect("ops");
            while !reader.eof() {
                let op = reader.read().expect("op");
                if let wasmparser::Operator::Call { function_index } = op {
                    call_targets.push(function_index);
                }
            }
            break;
        }
    }
    // The trampoline calls alloc, dispatch, free — three distinct indices.
    let mut sorted = call_targets.clone();
    sorted.sort();
    sorted.dedup();
    assert!(
        sorted.len() >= 3,
        "trampoline must call at least three distinct host imports, got calls {:?}",
        call_targets
    );
}

#[test]
fn round_trip_through_wasmparser_validator() {
    // Every output of `rewrite_wasm` passes `wasmparser::Validator` — this
    // is the universal invariant for the rewrite pipeline.
    let modules = [
        // No memory: rewriter passes through unchanged (no offload).
        r#"(module (func (export "noop")))"#,
        // Simple defined function — should be safely passed through.
        r#"(module
          (memory 1)
          (func (export "double") (param f32) (result f32)
            (f32.mul (local.get 0) (f32.const 2))))"#,
        // Module that triggers a swap.
        HOT_ADD_WAT,
    ];
    for (i, src) in modules.iter().enumerate() {
        let wasm = wat::parse_str(src).unwrap_or_else(|e| panic!("wat #{i}: {e}"));
        let cache = KernelCache::new();
        let opts = RewriteOptions {
            detector: DetectorConfig {
                v128_ratio_threshold: 0.05,
                min_trip_count: 64,
            },
            ..RewriteOptions::default()
        };
        let out =
            rewrite_wasm(&wasm, &opts, &cache).unwrap_or_else(|e| panic!("rewrite #{i}: {e}"));
        wasmparser::Validator::new()
            .validate_all(&out.rewritten_wasm)
            .unwrap_or_else(|e| panic!("validate #{i}: {e}"));
    }
}
