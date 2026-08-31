// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//
//! Proptest harness for the differential JIT correctness oracle
//! (roadmap feature #6 / T38). Generates random inputs for every
//! auto-offload blueprint shape (matmul, vector_add, conv2d) and
//! drives them through:
//!
//!   1. The CPU reference path
//!      ([`tensor_wasm_jit::differential::reference_eval`] for
//!      vector_add, [`matmul_reference`] / [`conv2d_reference`] for
//!      the reduction shapes).
//!   2. The PTX text emission path
//!      ([`tensor_wasm_jit::ptx_emit::emit`]) — emit-only on hosts
//!      without a CUDA device. The S22 self-hosted runner extends
//!      this to a real launch + memcmp.
//!   3. [`DifferentialOracle::compare`] — asserted to return either
//!      [`OracleVerdict::Match`] or [`OracleVerdict::HostOnlyOk`]
//!      (CPU-only path) or [`OracleVerdict::Skipped`] (CUDA missing).
//!
//! ## Scope
//!
//! For v0.4 (T38) we only need the proptest scaffold + the CPU path
//! to run end-to-end. The GPU comparison is `#[ignore]`-gated — those
//! cases are unblocked once the S22 runner is here. The CPU
//! reference is exercised on every CI run via the non-ignored
//! `*_host_only` cases below.

#![cfg(feature = "differential-oracle")]

use proptest::prelude::*;
use tensor_wasm_jit::differential::{
    check_wmma_structure, conv2d_reference, matmul_reference, reference_eval, BlueprintKind,
    DifferentialOracle, Dtype, OracleVerdict, Tolerance, ToleranceTable,
};
use tensor_wasm_jit::ir::{ElemType, GridHint, TensorWasmKernelBlueprint, TensorWasmOp};
use tensor_wasm_jit::ptx_emit::{emit, emit_with, EmitConfig};

// ---------------------------------------------------------------------
// Strategies — small inputs only so proptest shrinks remain readable.
// ---------------------------------------------------------------------

/// vector_add inputs: lane count 1..=64, each lane in
/// [-1024.0, 1024.0).
fn vector_add_inputs() -> impl Strategy<Value = (u32, Vec<f32>, Vec<f32>)> {
    (1u32..=64u32).prop_flat_map(|lanes| {
        (
            Just(lanes),
            prop::collection::vec(-1024.0f32..1024.0f32, lanes as usize),
            prop::collection::vec(-1024.0f32..1024.0f32, lanes as usize),
        )
    })
}

/// matmul inputs: small matrices, M/N/K each in 1..=16, elements in
/// [-32.0, 32.0) so the reduction stays in f32 dynamic range.
fn matmul_inputs() -> impl Strategy<Value = (usize, usize, usize, Vec<f32>, Vec<f32>)> {
    (1usize..=16, 1usize..=16, 1usize..=16).prop_flat_map(|(m, k, n)| {
        (
            Just(m),
            Just(k),
            Just(n),
            prop::collection::vec(-32.0f32..32.0f32, m * k),
            prop::collection::vec(-32.0f32..32.0f32, k * n),
        )
    })
}

/// conv2d inputs: up to 6x6 input + up to 3x3 kernel (kernel never
/// exceeds the input dims), elements in [-16.0, 16.0).
fn conv2d_inputs() -> impl Strategy<Value = (usize, usize, usize, usize, Vec<f32>, Vec<f32>)> {
    (2usize..=6, 2usize..=6, 1usize..=3, 1usize..=3).prop_flat_map(|(h, w, kh, kw)| {
        let kh = kh.min(h);
        let kw = kw.min(w);
        (
            Just(h),
            Just(w),
            Just(kh),
            Just(kw),
            prop::collection::vec(-16.0f32..16.0f32, h * w),
            prop::collection::vec(-16.0f32..16.0f32, kh * kw),
        )
    })
}

// ---------------------------------------------------------------------
// Helpers — blueprint factories.
// ---------------------------------------------------------------------

/// Vector-add blueprint: load A, load B, add lane-wise, store result.
/// The differential oracle models f32 lane semantics, so this fixture
/// stays f32.
fn vector_add_blueprint(lanes: u32) -> TensorWasmKernelBlueprint {
    let elem = ElemType::F32;
    TensorWasmKernelBlueprint::new("vector_add")
        .push(TensorWasmOp::LoadUnified { elem, lanes })
        .push(TensorWasmOp::LoadUnified { elem, lanes })
        .push(TensorWasmOp::VecAdd { elem, lanes })
        .push(TensorWasmOp::StoreUnified { elem, lanes })
        .with_grid(GridHint {
            total_threads: lanes.max(1),
            preferred_block_size: lanes.clamp(1, 128),
        })
}

/// Matmul blueprint (IR-level). The PTX emitter refuses
/// `TensorWasmOp::MatMul` with `NotYetImplemented` under the default
/// config; only the EXPERIMENTAL `enable_experimental_matmul` flag lowers
/// the modelled `m16n16k16` shape to a wmma sequence (validated by the
/// structural oracle, not on hardware). The proptest harness asserts the
/// safe default AND drives the CPU reference via [`matmul_reference`].
fn matmul_blueprint(m: u32, n: u32, k: u32) -> TensorWasmKernelBlueprint {
    TensorWasmKernelBlueprint::new("matmul").push(TensorWasmOp::MatMul { m, n, k })
}

/// Conv2d blueprint expressed as a `Barrier` + `VecFma` sequence —
/// the rewriter's lowering of a sliding-window MAC. `BlueprintKind::
/// classify` returns `Conv2d` for this shape.
fn conv2d_blueprint(window_lanes: u32) -> TensorWasmKernelBlueprint {
    let elem = ElemType::F32;
    TensorWasmKernelBlueprint::new("conv2d")
        .push(TensorWasmOp::Barrier)
        .push(TensorWasmOp::LoadUnified {
            elem,
            lanes: window_lanes,
        })
        .push(TensorWasmOp::LoadUnified {
            elem,
            lanes: window_lanes,
        })
        .push(TensorWasmOp::LoadUnified {
            elem,
            lanes: window_lanes,
        })
        .push(TensorWasmOp::VecFma {
            elem,
            lanes: window_lanes,
        })
        .push(TensorWasmOp::StoreUnified {
            elem,
            lanes: window_lanes,
        })
}

/// Pack a vec of f32s as little-endian bytes — the wire format the
/// reference interpreter consumes.
fn pack_le(values: &[f32]) -> Vec<u8> {
    let mut out = Vec::with_capacity(values.len() * 4);
    for v in values {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out
}

/// Assert the verdict is one of the host-only-acceptable shapes. The
/// proptest body never expects [`OracleVerdict::Divergence`] on the
/// no-CUDA path (the GPU side never executed).
fn assert_host_only_ok(verdict: OracleVerdict) {
    match verdict {
        OracleVerdict::Match { .. } => {}
        OracleVerdict::HostOnlyOk { .. } => {}
        OracleVerdict::Skipped(reason) => {
            assert!(
                reason.contains("no-cuda") || reason.contains("reference-eval"),
                "unexpected skip reason: {reason}"
            );
        }
        OracleVerdict::Divergence(d) => {
            panic!("oracle reported divergence on host-only run: {d:?}")
        }
    }
}

// ---------------------------------------------------------------------
// Proptest bodies — one block per blueprint shape.
// ---------------------------------------------------------------------

proptest! {
    /// vector_add: drive `DifferentialOracle::compare` against the
    /// CPU reference and the (emit-only) PTX path. Runs end-to-end on
    /// every CI host (no CUDA required).
    #[test]
    fn vector_add_host_only(case in vector_add_inputs()) {
        let (lanes, a, b) = case;
        let bp = vector_add_blueprint(lanes);
        let kind = BlueprintKind::classify(&bp);
        prop_assert_eq!(kind, BlueprintKind::VectorAdd);

        // Per-blueprint tolerance lookup. f32 vector_add: 1 ULP.
        let tol: Tolerance =
            ToleranceTable::default().for_blueprint(kind, Dtype::F32);
        prop_assert_eq!(tol.ulps, 1);

        // CPU reference via the blueprint interpreter.
        let mut input = pack_le(&a);
        input.extend_from_slice(&pack_le(&b));
        let cpu_bytes = reference_eval(&bp, &input)
            .map_err(|e| TestCaseError::fail(format!("reference_eval: {e:?}")))?;
        prop_assert_eq!(cpu_bytes.len(), (lanes as usize) * 4);
        for i in 0..(lanes as usize) {
            let mut buf = [0u8; 4];
            buf.copy_from_slice(&cpu_bytes[i * 4..(i + 1) * 4]);
            let actual = f32::from_le_bytes(buf);
            let expected = a[i] + b[i];
            prop_assert!(
                tol.approx_eq_f32(expected, actual),
                "lane {i}: expected {expected}, actual {actual}"
            );
        }

        // PTX emission must succeed (vector_add is a supported shape).
        let _ptx = emit(&bp)
            .map_err(|e| TestCaseError::fail(format!("emit: {e:?}")))?;

        // Oracle verdict: host-only path on no-CUDA CI.
        let verdict = DifferentialOracle::new().compare(&bp, &input);
        assert_host_only_ok(verdict);
    }

    /// conv2d (FMA sliding window): drive against the standalone
    /// `conv2d_reference`. The blueprint itself is classified as
    /// `Conv2d`; the CPU output is checked element-wise against the
    /// reference.
    #[test]
    fn conv2d_host_only(case in conv2d_inputs()) {
        let (h, w, kh, kw, input, kernel) = case;
        let out = conv2d_reference(h, w, kh, kw, &input, &kernel)
            .map_err(|e| TestCaseError::fail(format!("conv2d_reference: {e:?}")))?;
        let oh = h - kh + 1;
        let ow = w - kw + 1;
        prop_assert_eq!(out.len(), oh * ow);

        // Classification + tolerance lookup. conv2d_blueprint is
        // synthetic — the real lowering shape from the rewriter — so
        // we just spot-check the classifier doesn't drift.
        let bp = conv2d_blueprint(1);
        prop_assert_eq!(BlueprintKind::classify(&bp), BlueprintKind::Conv2d);
        let tol: Tolerance =
            ToleranceTable::default().for_blueprint(BlueprintKind::Conv2d, Dtype::F32);
        prop_assert_eq!(tol.ulps, 2);
        // PTX emission for the synthetic conv2d blueprint must
        // succeed — it's a sequence of supported ops.
        let _ptx = emit(&bp)
            .map_err(|e| TestCaseError::fail(format!("emit: {e:?}")))?;

        // Spot-check tolerance against a hand-recomputed value so the
        // proptest body doesn't tautologically compare the reference
        // to itself.
        for i in 0..oh {
            for j in 0..ow {
                let mut expected = 0.0f32;
                for ki in 0..kh {
                    for kj in 0..kw {
                        let v = input[(i + ki) * w + (j + kj)];
                        let k = kernel[ki * kw + kj];
                        expected = v.mul_add(k, expected);
                    }
                }
                prop_assert!(
                    tol.approx_eq_f32(expected, out[i * ow + j]),
                    "conv2d cell ({i},{j}): expected {expected}, actual {}",
                    out[i * ow + j]
                );
            }
        }
    }

    /// matmul: drive against the standalone `matmul_reference` for the
    /// CPU ground truth, and gate the EXPERIMENTAL wmma lowering via the
    /// structural oracle.
    ///
    /// The default-config emit must ALWAYS refuse matmul
    /// (`NotYetImplemented`) — this is the safe-default pin. Only the
    /// modelled `m16n16k16` shape, emitted with the experimental flag on,
    /// is structurally validated by [`check_wmma_structure`]. Any other
    /// shape is still refused even with the flag on.
    #[test]
    fn matmul_host_only(case in matmul_inputs()) {
        let (m, k, n, a, b) = case;
        let out = matmul_reference(m, k, n, &a, &b)
            .map_err(|e| TestCaseError::fail(format!("matmul_reference: {e:?}")))?;
        prop_assert_eq!(out.len(), m * n);

        // Classification + tolerance.
        let bp = matmul_blueprint(m as u32, n as u32, k as u32);
        prop_assert_eq!(BlueprintKind::classify(&bp), BlueprintKind::Matmul);
        let tol: Tolerance =
            ToleranceTable::default().for_blueprint(BlueprintKind::Matmul, Dtype::F32);
        prop_assert_eq!(tol.ulps, 2);

        // SAFE-DEFAULT PIN: default-config emit must refuse matmul.
        prop_assert!(
            emit(&bp).is_err(),
            "default-config PTX emit unexpectedly succeeded for matmul"
        );

        // EXPERIMENTAL opt-in path. Only the modelled 16x16x16 shape is
        // lowered; the structural oracle is the GATE that must pass
        // before the feature can be flipped on in a shipping path.
        let exp_cfg = EmitConfig {
            enable_experimental_matmul: true,
            ..EmitConfig::default()
        };
        if (m, n, k) == (16, 16, 16) {
            let ptx = emit_with(&bp, &exp_cfg)
                .map_err(|e| TestCaseError::fail(format!("opt-in emit: {e:?}")))?;
            let errs = check_wmma_structure(&ptx.text, 16, 16, 16);
            prop_assert!(
                errs.is_empty(),
                "wmma structural oracle flagged the emitted PTX: {errs:?}"
            );
        } else {
            // Unmodelled shape: refused even with the flag on.
            prop_assert!(
                emit_with(&bp, &exp_cfg).is_err(),
                "opt-in emit must still refuse non-16x16x16 matmul ({m}x{n}x{k})"
            );
        }

        // Naive recomputation for tolerance spot-check.
        for i in 0..m {
            for j in 0..n {
                let mut expected = 0.0f32;
                for kk in 0..k {
                    expected = a[i * k + kk].mul_add(b[kk * n + j], expected);
                }
                prop_assert!(
                    tol.approx_eq_f32(expected, out[i * n + j]),
                    "matmul cell ({i},{j}): expected {expected}, actual {}",
                    out[i * n + j]
                );
            }
        }
    }
}

/// Dedicated structural-oracle gate for the EXPERIMENTAL wmma lowering.
/// Deterministically exercises the modelled `m16n16k16` shape so the gate
/// runs even if proptest never samples exactly (16,16,16). This is the
/// case the team must keep green before enabling
/// `enable_experimental_matmul` anywhere it can reach a GPU launch.
#[test]
fn wmma_m16n16k16_structural_gate() {
    let bp = matmul_blueprint(16, 16, 16);
    let exp_cfg = EmitConfig {
        enable_experimental_matmul: true,
        ..EmitConfig::default()
    };
    let ptx = emit_with(&bp, &exp_cfg).expect("opt-in emit for m16n16k16");
    let errs = check_wmma_structure(&ptx.text, 16, 16, 16);
    assert!(errs.is_empty(), "wmma structural gate failed: {errs:?}");

    // Safe-default pin lives alongside the gate.
    assert!(emit(&bp).is_err(), "default-config emit must refuse matmul");
}

// ---------------------------------------------------------------------
// CUDA-path stubs — `#[ignore]` until the S22 self-hosted runner is
// here. These bodies are the wiring contract the runner will fill in:
// build the blueprint, dispatch through the kernel cache, memcmp the
// readback against the CPU reference, assert
// `OracleVerdict::Match { output_len }`.
// ---------------------------------------------------------------------

#[test]
#[ignore = "requires CUDA; runs on the S22 self-hosted runner only"]
fn vector_add_cuda_match() {
    // S22 wiring point — see `docs/DIFFERENTIAL-ORACLE.md#v04-
    // implementation-plan` step 2.
    let bp = vector_add_blueprint(4);
    let input = pack_le(&[1.0f32, 2.0, 3.0, 4.0, 10.0, 20.0, 30.0, 40.0]);
    let verdict = DifferentialOracle::new().compare(&bp, &input);
    match verdict {
        OracleVerdict::Match { output_len } => assert_eq!(output_len, 16),
        other => panic!("expected Match, got {other:?}"),
    }
}

#[test]
#[ignore = "requires CUDA; runs on the S22 self-hosted runner only"]
fn matmul_cuda_match() {
    // S22 wiring point — the matmul GPU path also needs the
    // wmma lowering land first (currently `NotYetImplemented` in
    // `ptx_emit::lower_body`).
    let bp = matmul_blueprint(16, 16, 16);
    let verdict = DifferentialOracle::new().compare(&bp, &[]);
    // After both wirings land, this asserts `OracleVerdict::Match`.
    let _ = verdict;
}

#[test]
#[ignore = "requires CUDA; runs on the S22 self-hosted runner only"]
fn conv2d_cuda_match() {
    // S22 wiring point.
    let bp = conv2d_blueprint(1);
    let verdict = DifferentialOracle::new().compare(&bp, &[]);
    let _ = verdict;
}
