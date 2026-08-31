// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//
//! Scaffold-level tests for the differential JIT correctness oracle
//! (roadmap feature #6). These tests pin the harness API shape that
//! the v0.4 author wires the real two-path runner behind. Until the
//! self-hosted CUDA runner lands, every call returns
//! `OracleVerdict::Skipped(...)` — the tests assert exactly that
//! contract so future regressions on the default-disabled / no-CUDA
//! path are caught.

#![cfg(feature = "differential-oracle")]

use tensor_wasm_jit::differential::{
    DifferentialOracle, OracleConfig, OracleDivergence, OracleVerdict,
};
use tensor_wasm_jit::ir::{ElemType, TensorWasmKernelBlueprint, TensorWasmOp};

fn fixture_blueprint() -> TensorWasmKernelBlueprint {
    TensorWasmKernelBlueprint::new("oracle_fixture").push(TensorWasmOp::VecAdd {
        elem: ElemType::F32,
        lanes: 4,
    })
}

#[test]
fn oracle_new_default_skips_with_no_cuda() {
    let oracle = DifferentialOracle::new();
    let bp = fixture_blueprint();
    match oracle.compare(&bp, &[]) {
        OracleVerdict::Skipped(reason) => {
            // The exact phrasing is part of the v0.3.6 contract: the
            // CI gate filters on "no-cuda" to distinguish "harness not
            // wired" from "oracle disabled by operator".
            assert!(
                reason.contains("no-cuda"),
                "expected no-cuda skip, got: {reason}"
            );
        }
        other => panic!("expected Skipped, got {other:?}"),
    }
}

#[test]
fn oracle_disabled_returns_skipped_disabled() {
    let mut cfg = OracleConfig::default();
    cfg.disabled = true;
    let oracle = DifferentialOracle::with_config(cfg);
    let bp = fixture_blueprint();
    assert_eq!(
        oracle.compare(&bp, &[]),
        OracleVerdict::Skipped("oracle disabled by config"),
    );
}

#[test]
fn divergence_record_is_stable_under_round_trip() {
    let original = OracleDivergence {
        blueprint_fingerprint: 0xDEAD_BEEF_CAFE_F00D,
        cpu_output_len: 128,
        gpu_output_len: 128,
        first_diff_offset: Some(42),
    };
    let cloned = original.clone();
    assert_eq!(original, cloned);

    // Wrapped in a verdict, equality must still hold so the CI gate
    // can deduplicate divergences by fingerprint across runs.
    let verdict_a = OracleVerdict::Divergence(original.clone());
    let verdict_b = OracleVerdict::Divergence(cloned);
    assert_eq!(verdict_a, verdict_b);
}
