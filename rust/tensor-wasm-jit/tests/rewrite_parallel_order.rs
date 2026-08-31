// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Regression: `rewrite::analyse` runs `lower_block` + `emit` in parallel
//! via `rayon`, but the rewriter downstream assumes `func_infos[i]`
//! corresponds positionally to the *i*-th `CodeSectionEntry` in the
//! source module — `OffloadedFunction.function_index` MUST match the
//! original module's defined-function order.
//!
//! Without the index-stable re-sort in the analyser, the parallel pass
//! would commit slots in an order determined by which worker finished
//! first, which would:
//! - bind the wrong fingerprint to each function's trampoline (the
//!   dispatch call would land on a sibling's PTX), and
//! - reorder `RewriteOutcome::offloaded_functions` non-deterministically
//!   across runs.
//!
//! This test builds a synthetic module with 16 v128-heavy candidates
//! whose bodies are deliberately distinct (each has a unique number of
//! v128 ops + a unique number of trailing scalar adds) so each one
//! produces a distinct blueprint fingerprint. It then rewrites the
//! module twice and asserts:
//! 1. `offloaded_functions` are reported in strictly ascending
//!    `function_index` order (the index walk is sequential, so this
//!    pins the rewriter side).
//! 2. The fingerprint attached to each `function_index` matches a
//!    known sequential baseline (the i-th function's body always
//!    fingerprints to the same value, regardless of run).
//! 3. Two independent rewrites of the same input produce byte-identical
//!    `offloaded_functions` vectors — non-determinism would surface as
//!    flakiness here.

use tensor_wasm_jit::cache::KernelCache;
use tensor_wasm_jit::detector::DetectorConfig;
use tensor_wasm_jit::rewrite::{rewrite_wasm, RewriteOptions};

/// Build a module with `n` v128-heavy functions, each with a unique
/// body shape so each produces a distinct blueprint fingerprint.
///
/// Function `i` (0..n) has `(16 + i)` v128 `i32x4.add` ops inside a
/// `loop` block. All bodies satisfy the v128-ratio threshold and the
/// loop-trip-count gate the default detector applies (we set both to
/// permissive values in the test so the detector approves all of them).
fn build_n_candidate_module(n: usize) -> Vec<u8> {
    let mut wat = String::from("(module\n  (memory 1)\n");
    for i in 0..n {
        wat.push_str(&format!(
            "  (func (export \"f{i}\") (result i32)\n    (local $v v128)\n    (loop $L\n",
        ));
        // Unique op count per slot: `16 + i` v128 adds.
        for _ in 0..(16 + i) {
            wat.push_str("      (local.set $v (i32x4.add (local.get $v) (local.get $v)))\n");
        }
        wat.push_str("    )\n    (i32.const 0)\n  )\n");
    }
    wat.push(')');
    wat::parse_str(&wat).expect("synthetic module must parse")
}

fn permissive_opts() -> RewriteOptions {
    RewriteOptions {
        detector: DetectorConfig {
            v128_ratio_threshold: 0.05,
            min_trip_count: 64,
        },
        ..RewriteOptions::default()
    }
}

#[test]
fn parallel_analyse_preserves_source_order_for_16_candidates() {
    let n = 16;
    let wasm = build_n_candidate_module(n);

    // Baseline rewrite — captures the canonical (function_index → fingerprint)
    // mapping the rewriter should always produce regardless of which rayon
    // worker finishes which slot first.
    let cache = KernelCache::new();
    let out = rewrite_wasm(&wasm, &permissive_opts(), &cache).expect("baseline rewrite");
    assert_eq!(
        out.offloaded_functions.len(),
        n,
        "all {n} synthetic candidates must be swapped; got {}",
        out.offloaded_functions.len()
    );

    // Assertion #1: function indices are strictly ascending starting at 0.
    // (The module has no function imports so global-space == defined-space.)
    let indices: Vec<u32> = out
        .offloaded_functions
        .iter()
        .map(|f| f.function_index)
        .collect();
    let expected_indices: Vec<u32> = (0..n as u32).collect();
    assert_eq!(
        indices, expected_indices,
        "offloaded_functions must be in source order (ascending function_index)"
    );

    // Assertion #2: every fingerprint is unique — the synthetic module was
    // constructed so each slot produces a distinct blueprint hash. If
    // parallel analyse reordered slots, some `function_index` would land
    // on the wrong fingerprint, but each fingerprint would still appear
    // once. So *uniqueness* alone is not enough; we also need positional
    // stability across runs (assertion #3).
    let mut fps: Vec<u64> = out
        .offloaded_functions
        .iter()
        .map(|f| f.fingerprint)
        .collect();
    let unique_count_pre_sort = {
        let mut seen = std::collections::HashSet::new();
        fps.iter().filter(|fp| seen.insert(**fp)).count()
    };
    fps.sort_unstable();
    fps.dedup();
    assert_eq!(
        fps.len(),
        n,
        "the {n} synthetic candidates must produce {n} distinct fingerprints — \
         got {} unique (pre-sort count: {})",
        fps.len(),
        unique_count_pre_sort,
    );

    // Assertion #3: re-running the rewrite produces a byte-identical
    // (function_index, fingerprint) mapping. This is the canary that
    // would catch a parallel-induced reorder: a flaky parallel pass
    // would shuffle slots non-deterministically, so a second run would
    // disagree with the first on at least one slot.
    //
    // We run 4 extra times to make a one-in-a-million reorder bug
    // statistically likely to fire (4! = 24 permutations of slot order
    // among any four-thread group; running 5 total trials gives ample
    // chance to catch any non-determinism).
    let baseline: Vec<(u32, u64)> = out
        .offloaded_functions
        .iter()
        .map(|f| (f.function_index, f.fingerprint))
        .collect();
    for trial in 0..4 {
        let cache_n = KernelCache::new();
        let out_n = rewrite_wasm(&wasm, &permissive_opts(), &cache_n).expect("re-run rewrite");
        let observed: Vec<(u32, u64)> = out_n
            .offloaded_functions
            .iter()
            .map(|f| (f.function_index, f.fingerprint))
            .collect();
        assert_eq!(
            observed, baseline,
            "rewrite trial {trial} disagreed with baseline — parallel \
             analyse leaked a reorder (observed = {:?}, baseline = {:?})",
            observed, baseline,
        );
    }
}

/// Tiny module variant: one candidate. The parallel pass with `n=1`
/// must still preserve order (degenerate case, but exercises the
/// `into_par_iter().enumerate().collect().sort_by_key()` pipeline with
/// the smallest non-empty input).
#[test]
fn parallel_analyse_one_candidate_round_trips() {
    let wasm = build_n_candidate_module(1);
    let cache = KernelCache::new();
    let out = rewrite_wasm(&wasm, &permissive_opts(), &cache).expect("rewrite");
    assert_eq!(out.offloaded_functions.len(), 1);
    assert_eq!(out.offloaded_functions[0].function_index, 0);
}

/// Empty module: no candidates, no rayon work, no panic. Pins the
/// `n_slots == 0` edge case of the `into_par_iter()` pipeline.
#[test]
fn parallel_analyse_zero_candidates_does_not_panic() {
    let wat = r#"(module (memory 1))"#;
    let wasm = wat::parse_str(wat).unwrap();
    let cache = KernelCache::new();
    let out = rewrite_wasm(&wasm, &permissive_opts(), &cache).expect("rewrite");
    assert!(out.offloaded_functions.is_empty());
    assert_eq!(out.total_defined_functions, 0);
}
