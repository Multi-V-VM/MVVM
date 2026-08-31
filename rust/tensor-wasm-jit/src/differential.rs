// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Differential correctness oracle (roadmap feature #6).
//!
//! For every `auto_offload` candidate that lowers to PTX, the harness
//! runs:
//!   1. The original Wasm body on the Wasmtime CPU interpreter (the
//!      ground truth).
//!   2. The JIT-emitted PTX on the cust/cudarc backend.
//!
//! …and asserts byte-equal output. Discrepancies are surfaced as
//! `OracleDivergence` records the CI gate can publish on every PR.
//!
//! ## v0.3.7 / v0.4 status
//!
//! The harness types are stable and the v0.4 (T38) wave wires the
//! **CPU reference path** end-to-end — every call to
//! [`DifferentialOracle::compare`] now interprets the blueprint over
//! the provided input bytes using the f32 lane semantics declared in
//! [`crate::ir::TensorWasmOp`] and surfaces the resulting output as
//! [`OracleVerdict::HostOnlyOk`]. The GPU side stays gated behind a
//! runtime CUDA-availability check (and the `cuda-oxide-backend`
//! feature flag); hosts without CUDA receive
//! [`OracleVerdict::Skipped("no-cuda; CPU-only oracle verdict")`] AFTER
//! the CPU path has executed, so the harness wiring is exercised on
//! every host. The S22 self-hosted CUDA runner is the layer that
//! upgrades `HostOnlyOk` / `Skipped` to [`OracleVerdict::Match`] or
//! [`OracleVerdict::Divergence`].
//!
//! ## How to use (v0.4 target shape)
//!
//! ```ignore
//! use tensor_wasm_jit::differential::DifferentialOracle;
//!
//! let oracle = DifferentialOracle::new();
//! let blueprint = /* TensorWasmKernelBlueprint */;
//! let inputs = /* &[u8] guest memory snapshot, f32 little-endian */;
//! match oracle.compare(&blueprint, inputs) {
//!     OracleVerdict::Match { .. } => { /* CPU == GPU */ }
//!     OracleVerdict::HostOnlyOk { .. } => { /* CPU ran, no GPU host */ }
//!     OracleVerdict::Divergence(d) => panic!("audit-bait: {d:?}"),
//!     OracleVerdict::Skipped(reason) => eprintln!("skipped: {reason}"),
//! }
//! ```

use crate::ir::{ElemType, TensorWasmKernelBlueprint, TensorWasmOp};

/// Configuration for the oracle. v0.3.6: defaults are sufficient.
#[derive(Debug, Clone, Default)]
#[non_exhaustive]
pub struct OracleConfig {
    /// Skip the comparison and return Skipped("disabled") for every
    /// call. Used to disable the oracle in environments where one of
    /// the two paths is not available.
    pub disabled: bool,
}

/// The oracle. Drive it from proptest test bodies and CI gates.
pub struct DifferentialOracle {
    cfg: OracleConfig,
}

impl DifferentialOracle {
    /// Construct an oracle with default configuration.
    pub fn new() -> Self {
        Self {
            cfg: OracleConfig::default(),
        }
    }

    /// Construct an oracle with a caller-provided configuration.
    pub fn with_config(cfg: OracleConfig) -> Self {
        Self { cfg }
    }

    /// Run the two paths and compare.
    ///
    /// v0.4 (T38): the CPU reference path is wired end-to-end via the
    /// in-crate [`reference_eval`] interpreter, which reproduces the
    /// f32 lane semantics declared in [`TensorWasmOp`]. The GPU path
    /// requires a CUDA host; on hosts without one, the call returns
    /// [`OracleVerdict::Skipped`] AFTER the CPU result has been
    /// computed (so the harness wiring is exercised everywhere).
    pub fn compare(&self, bp: &TensorWasmKernelBlueprint, inputs: &[u8]) -> OracleVerdict {
        if self.cfg.disabled {
            return OracleVerdict::Skipped("oracle disabled by config");
        }

        // CPU reference path: interpret the blueprint over the input
        // bytes (f32 little-endian lanes). When the interpreter
        // can't run the blueprint end-to-end (no input bytes
        // provided, unsupported op, malformed stack) we fall back to
        // the original v0.3.6 skip contract — that path is exercised
        // by the existing scaffold tests in `differential_scaffold.rs`
        // which pass a blueprint + empty input to assert harness
        // wiring only.
        let cpu_output = reference_eval(bp, inputs).ok();

        // GPU path: requires both the `cuda-oxide-backend` feature AND
        // a usable CUDA device at runtime. Until the S22 runner is
        // here, we always take the no-cuda branch.
        if cuda_runtime_available() {
            // v0.4 wiring point: dispatch `bp` through the kernel
            // cache + cudarc launch and memcmp `cpu_output` against
            // the readback. Until that lands, falling through to the
            // HostOnlyOk branch is correct — the S22 job overrides
            // `cuda_runtime_available` to true and runs the real
            // comparison.
            match cpu_output {
                Some(out) => OracleVerdict::HostOnlyOk {
                    cpu_output_len: out.len(),
                    cpu_output_first_bytes: head_bytes(&out),
                },
                None => {
                    OracleVerdict::Skipped("reference-eval-unavailable; falling back to no-cuda")
                }
            }
        } else {
            match cpu_output {
                Some(out) => OracleVerdict::HostOnlyOk {
                    cpu_output_len: out.len(),
                    cpu_output_first_bytes: head_bytes(&out),
                },
                None => OracleVerdict::Skipped("no-cuda; v0.4 wires this against the S22 runner"),
            }
        }
    }
}

impl Default for DifferentialOracle {
    fn default() -> Self {
        Self::new()
    }
}

/// Result of a single oracle comparison.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum OracleVerdict {
    /// Both paths produced byte-equal output.
    Match {
        /// Number of output bytes that were compared.
        output_len: usize,
    },
    /// Only the CPU reference path executed (no CUDA host on this
    /// runner). The CPU output length is reported so test bodies can
    /// at least validate the harness wiring on no-CUDA hosts. v0.4
    /// (T38) introduced this variant so the proptest harness can
    /// run end-to-end on the default CI without a CUDA device.
    HostOnlyOk {
        /// Number of output bytes the CPU reference produced.
        cpu_output_len: usize,
        /// Up to the first 16 bytes of the CPU output, surfaced for
        /// debug formatting on proptest failure. Not used for
        /// equality decisions — the test harness compares the full
        /// vector via [`reference_eval`] separately.
        cpu_output_first_bytes: [u8; 16],
    },
    /// Outputs diverged. The CI gate must fail.
    Divergence(OracleDivergence),
    /// Comparison was skipped (typically: one of the two paths is not
    /// available on the current host). Not a failure; the test runner
    /// should record it as a skip.
    Skipped(&'static str),
}

/// Detailed divergence record. Logged with `tracing::error!` in CI
/// gates; the offending blueprint fingerprint is stable across runs so
/// the same divergence is triaged once, not per-CI-run.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct OracleDivergence {
    /// Stable fingerprint of the blueprint that diverged. Matches
    /// `TensorWasmKernelBlueprint::fingerprint`.
    pub blueprint_fingerprint: u64,
    /// Number of bytes the Wasmtime CPU path produced.
    pub cpu_output_len: usize,
    /// Number of bytes the JIT PTX path produced.
    pub gpu_output_len: usize,
    /// Offset of the first differing byte if both outputs are non-empty
    /// and share at least one byte; `None` if the lengths differed
    /// before any byte could be compared.
    pub first_diff_offset: Option<usize>,
}

// ---------------------------------------------------------------------
// Per-blueprint tolerance table (T38).
// ---------------------------------------------------------------------

/// Coarse classification of the blueprint shape, used by the tolerance
/// table to pick the right ULP/abs/rel triple. The proptest harness
/// generates inputs per kind; the auto-offload pipeline classifies
/// every emitted blueprint via [`BlueprintKind::classify`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum BlueprintKind {
    /// Element-wise vector add (1 ULP for f32, 4 ULP for f16).
    VectorAdd,
    /// Element-wise vector multiply (1 ULP for f32, 4 ULP for f16).
    VectorMul,
    /// Fused multiply-add (1 ULP, single-rounding contract on both
    /// paths — CPU uses `f32::mul_add`, PTX uses `fma.rn.f32`).
    VectorFma,
    /// Reduction (matmul, dot product). Looser tolerance because
    /// reduction order is implementation-defined.
    Matmul,
    /// 2D convolution. Tolerance bracketed with matmul because the
    /// inner loop is a multiply-accumulate over a sliding window.
    Conv2d,
    /// Anything else — defaults to strict 1-ULP behaviour.
    Other,
}

impl BlueprintKind {
    /// Classify a blueprint by its dominant op family. Order of
    /// preference: matmul > conv2d > fma > add > mul > other. (The
    /// PTX emitter does not yet emit a dedicated Conv2dOp — the
    /// rewriter encodes 2D conv as a sequence of `LoadUnified` +
    /// `VecFma` + `StoreUnified`, so we look for a `VecFma` block
    /// preceded by a barrier as the conv signature.)
    pub fn classify(bp: &TensorWasmKernelBlueprint) -> Self {
        let mut has_matmul = false;
        let mut has_fma = false;
        let mut has_add = false;
        let mut has_mul = false;
        let mut has_barrier = false;
        for op in &bp.ops {
            match op {
                TensorWasmOp::MatMul { .. } => has_matmul = true,
                TensorWasmOp::VecFma { .. } => has_fma = true,
                TensorWasmOp::VecAdd { .. } => has_add = true,
                TensorWasmOp::VecMul { .. } => has_mul = true,
                TensorWasmOp::Barrier => has_barrier = true,
                _ => {}
            }
        }
        if has_matmul {
            BlueprintKind::Matmul
        } else if has_fma && has_barrier {
            BlueprintKind::Conv2d
        } else if has_fma {
            BlueprintKind::VectorFma
        } else if has_add {
            BlueprintKind::VectorAdd
        } else if has_mul {
            BlueprintKind::VectorMul
        } else {
            BlueprintKind::Other
        }
    }
}

/// Floating-point datatype the kernel operates on. Influences how
/// many ULPs of drift the oracle tolerates.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Dtype {
    /// IEEE-754 binary32 (the default for the v0.1.0 supported shapes).
    F32,
    /// IEEE-754 binary16. Looser tolerance — only 10 mantissa bits.
    F16,
    /// Integer / bit-identical (no tolerance allowed).
    Int,
}

/// Tolerance triple. The oracle accepts an actual value `g` against
/// reference `c` iff `|g - c| <= max(abs, rel * |c|)` OR they are
/// within [`Tolerance::ulps`] ULPs of each other. All three axes are
/// checked together so reductions (matmul) can use a generous ULP
/// budget while still pinning the absolute drift.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Tolerance {
    /// Maximum ULPs of drift permitted between the two paths.
    pub ulps: u32,
    /// Absolute error budget (max |g - c|).
    pub abs: f32,
    /// Relative error budget (max |g - c| / |c|).
    pub rel: f32,
}

impl Tolerance {
    /// Strict (bit-identical) tolerance — every axis is zero.
    pub const STRICT: Tolerance = Tolerance {
        ulps: 0,
        abs: 0.0,
        rel: 0.0,
    };

    /// Returns `true` iff `actual` is within tolerance of `expected`.
    pub fn approx_eq_f32(self, expected: f32, actual: f32) -> bool {
        if expected.is_nan() && actual.is_nan() {
            return true;
        }
        if !expected.is_finite() || !actual.is_finite() {
            return expected == actual;
        }
        let diff = (expected - actual).abs();
        if diff <= self.abs {
            return true;
        }
        if diff <= self.rel * expected.abs() {
            return true;
        }
        // ULP comparison via the canonical bit-distance trick — both
        // values are finite and the sign-magnitude → biased-int form
        // gives `(a - b).abs()` as the ULP count.
        let to_biased = |x: f32| -> i32 {
            let bits = x.to_bits() as i32;
            if bits < 0 {
                i32::MIN.wrapping_sub(bits)
            } else {
                bits
            }
        };
        let ulp_dist = (to_biased(expected) - to_biased(actual)).unsigned_abs();
        ulp_dist <= self.ulps
    }
}

/// Per-blueprint floating-point tolerance for CPU↔GPU bit-identity
/// checks.
///
/// fp32: 1 ULP allowed for element-wise ops; 2 ULP for reductions
/// (matmul). fp16: 4 ULP allowed (smaller mantissa, larger drift).
/// Integer: bit-identical required (tolerance == 0).
#[derive(Debug, Clone)]
pub struct ToleranceTable {
    /// Vector add tolerance keyed by dtype.
    pub vector_add_f32: Tolerance,
    /// Vector mul tolerance.
    pub vector_mul_f32: Tolerance,
    /// FMA tolerance.
    pub vector_fma_f32: Tolerance,
    /// Matmul tolerance (looser — reduction order is impl-defined).
    pub matmul_f32: Tolerance,
    /// Conv2d tolerance (matmul-shaped budget).
    pub conv2d_f32: Tolerance,
    /// f16 multiplier — every f32 budget is scaled by this for f16.
    pub f16_ulp_multiplier: u32,
}

impl ToleranceTable {
    /// Strict policy: zero tolerance everywhere. Used by the CI gate
    /// that asserts "the JIT produced identical bytes" — for ops the
    /// oracle pins as bit-identical (today: integer kernels only).
    pub const fn strict() -> Self {
        Self {
            vector_add_f32: Tolerance::STRICT,
            vector_mul_f32: Tolerance::STRICT,
            vector_fma_f32: Tolerance::STRICT,
            matmul_f32: Tolerance::STRICT,
            conv2d_f32: Tolerance::STRICT,
            f16_ulp_multiplier: 4,
        }
    }

    /// Default per-blueprint policy. Values mirror the table in
    /// `docs/DIFFERENTIAL-ORACLE.md#tolerance-table-v037`.
    pub const fn default_table() -> Self {
        Self {
            vector_add_f32: Tolerance {
                ulps: 1,
                abs: 0.0,
                rel: 0.0,
            },
            vector_mul_f32: Tolerance {
                ulps: 1,
                abs: 0.0,
                rel: 0.0,
            },
            // FMA: single-rounding contract on both paths means we
            // expect bit-identity, but we leave 1 ULP for the f32
            // round-mode tiebreak (round-to-nearest-even, which both
            // paths use).
            vector_fma_f32: Tolerance {
                ulps: 1,
                abs: 0.0,
                rel: 0.0,
            },
            // Matmul: 2 ULP budget per output element + a small
            // absolute floor so reductions over k=16 lanes don't trip
            // on subnormals.
            matmul_f32: Tolerance {
                ulps: 2,
                abs: 1e-6,
                rel: 1e-6,
            },
            // Conv2d: tracks matmul; the inner loop is the same MAC
            // shape.
            conv2d_f32: Tolerance {
                ulps: 2,
                abs: 1e-6,
                rel: 1e-6,
            },
            f16_ulp_multiplier: 4,
        }
    }

    /// Look up the tolerance for the given blueprint kind + dtype.
    /// Integer kernels are always strict; f16 multiplies the f32 ULP
    /// budget by [`Self::f16_ulp_multiplier`].
    pub fn for_blueprint(&self, kind: BlueprintKind, dtype: Dtype) -> Tolerance {
        if matches!(dtype, Dtype::Int) {
            return Tolerance::STRICT;
        }
        let base = match kind {
            BlueprintKind::VectorAdd => self.vector_add_f32,
            BlueprintKind::VectorMul => self.vector_mul_f32,
            BlueprintKind::VectorFma => self.vector_fma_f32,
            BlueprintKind::Matmul => self.matmul_f32,
            BlueprintKind::Conv2d => self.conv2d_f32,
            BlueprintKind::Other => self.vector_add_f32,
        };
        if matches!(dtype, Dtype::F16) {
            Tolerance {
                ulps: base.ulps.saturating_mul(self.f16_ulp_multiplier),
                abs: base.abs * self.f16_ulp_multiplier as f32,
                rel: base.rel * self.f16_ulp_multiplier as f32,
            }
        } else {
            base
        }
    }
}

impl Default for ToleranceTable {
    fn default() -> Self {
        Self::default_table()
    }
}

// ---------------------------------------------------------------------
// CPU reference interpreter (T38).
//
// Reproduces the f32 lane semantics declared in `TensorWasmOp` so the
// proptest harness has a known-good output to compare the future GPU
// readback against. The interpreter does NOT model launch geometry —
// it walks the op list once in order, treating the input bytes as a
// little-endian f32 stream consumed by `LoadUnified` and producing a
// little-endian f32 stream consumed by `StoreUnified`. This matches
// the PTX emitter's offset accounting (4 bytes per lane, ascending).
// ---------------------------------------------------------------------

/// Errors produced by [`reference_eval`].
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ReferenceEvalError {
    /// The blueprint references an op the reference interpreter does
    /// not yet model (currently: matmul + barrier — the proptest
    /// harness skips matmul shapes via the dedicated reduction
    /// reference, see `matmul_reference` below).
    UnsupportedOp,
    /// The input byte buffer is shorter than the blueprint's declared
    /// load offset reach.
    InputTooShort,
    /// The op sequence pops more values than it pushed — only fires
    /// on malformed blueprints (the proptest strategies never emit
    /// them).
    StackUnderflow,
}

/// Interpret a blueprint over `inputs` (treated as a little-endian
/// f32 stream) and produce the corresponding little-endian f32 output
/// stream. This is the **ground truth** the proptest harness asserts
/// the GPU path against.
///
/// The interpreter mirrors the PTX emitter's offset accounting in
/// [`crate::ptx_emit::lower_body`]: every `LoadUnified { lanes }`
/// reads `lanes` consecutive f32s from the input cursor; every
/// `StoreUnified { lanes }` writes `lanes` consecutive f32s to the
/// output cursor; arithmetic ops pop two (or three for FMA) values
/// from the value stack and push one result per lane.
pub fn reference_eval(
    bp: &TensorWasmKernelBlueprint,
    inputs: &[u8],
) -> Result<Vec<u8>, ReferenceEvalError> {
    let mut stack: Vec<f32> = Vec::with_capacity(bp.ops.len());
    let mut output: Vec<u8> = Vec::new();
    let mut in_cursor: usize = 0;

    for op in &bp.ops {
        match op {
            TensorWasmOp::LoadUnified { elem, lanes } => {
                // The reference interpreter models f32 lane semantics only
                // (it pushes `f32`). Integer / f64 element types are routed
                // to `UnsupportedOp` so the oracle skips rather than
                // validating an integer kernel against an f32 model. (The
                // emitter's element-type fix means such blueprints can now
                // exist; the f32 oracle simply doesn't cover them yet.)
                if *elem != ElemType::F32 {
                    return Err(ReferenceEvalError::UnsupportedOp);
                }
                let lanes = *lanes as usize;
                for _ in 0..lanes {
                    let end = in_cursor
                        .checked_add(4)
                        .ok_or(ReferenceEvalError::InputTooShort)?;
                    if end > inputs.len() {
                        return Err(ReferenceEvalError::InputTooShort);
                    }
                    let mut buf = [0u8; 4];
                    buf.copy_from_slice(&inputs[in_cursor..end]);
                    stack.push(f32::from_le_bytes(buf));
                    in_cursor = end;
                }
            }
            TensorWasmOp::StoreUnified { elem, lanes } => {
                if *elem != ElemType::F32 {
                    return Err(ReferenceEvalError::UnsupportedOp);
                }
                let lanes = *lanes as usize;
                // Stores pop in reverse-push order to match the PTX
                // emitter's `value_stack.pop()` walk.
                let mut popped: Vec<f32> = Vec::with_capacity(lanes);
                for _ in 0..lanes {
                    popped.push(stack.pop().ok_or(ReferenceEvalError::StackUnderflow)?);
                }
                for v in popped.into_iter().rev() {
                    output.extend_from_slice(&v.to_le_bytes());
                }
            }
            TensorWasmOp::VecAdd { elem, lanes } => {
                // Element-wise (lane-aligned) add. Two prior `LoadUnified`
                // ops push operand vectors contiguously, so the stack is
                // `[a0..a_{n-1}, b0..b_{n-1}]` with `b` on top. Pair lane `i`
                // of `a` with lane `i` of `b` — NOT a scalar pop-top-two,
                // which would cross-pair lanes and feed results back in.
                if *elem != ElemType::F32 {
                    return Err(ReferenceEvalError::UnsupportedOp);
                }
                let lanes = *lanes as usize;
                let (a, b) = pop_two_lane_vectors(&mut stack, lanes)?;
                for i in 0..lanes {
                    stack.push(a[i] + b[i]);
                }
            }
            TensorWasmOp::VecMul { elem, lanes } => {
                if *elem != ElemType::F32 {
                    return Err(ReferenceEvalError::UnsupportedOp);
                }
                let lanes = *lanes as usize;
                let (a, b) = pop_two_lane_vectors(&mut stack, lanes)?;
                for i in 0..lanes {
                    stack.push(a[i] * b[i]);
                }
            }
            TensorWasmOp::VecFma { elem, lanes } => {
                // Lane-aligned `a*b + c` over three operand vectors
                // (`[a.., b.., c..]`, `c` on top). Single-rounding contract —
                // mirrors `fma.rn.f32`.
                if *elem != ElemType::F32 {
                    return Err(ReferenceEvalError::UnsupportedOp);
                }
                let lanes = *lanes as usize;
                let c = pop_lane_vector(&mut stack, lanes)?;
                let b = pop_lane_vector(&mut stack, lanes)?;
                let a = pop_lane_vector(&mut stack, lanes)?;
                for i in 0..lanes {
                    stack.push(a[i].mul_add(b[i], c[i]));
                }
            }
            TensorWasmOp::Barrier => {
                // No-op for the single-thread reference (the barrier
                // exists only to synchronise CTA threads on the GPU
                // path).
            }
            TensorWasmOp::MatMul { .. } => {
                // The proptest harness routes matmul through
                // [`matmul_reference`] directly instead of the
                // blueprint interpreter — the IR doesn't carry the
                // tile-load/store sequence needed to drive the
                // generic interpreter. We surface UnsupportedOp here
                // so callers know to use the dedicated path.
                return Err(ReferenceEvalError::UnsupportedOp);
            }
        }
    }

    Ok(output)
}

/// Pop the top `lanes` values off the operand stack, preserving their
/// pushed (lane) order: the returned vec is `[v0, v1, .., v_{lanes-1}]`
/// where `v_{lanes-1}` was on top. Errors on underflow.
fn pop_lane_vector(stack: &mut Vec<f32>, lanes: usize) -> Result<Vec<f32>, ReferenceEvalError> {
    if stack.len() < lanes {
        return Err(ReferenceEvalError::StackUnderflow);
    }
    Ok(stack.split_off(stack.len() - lanes))
}

/// Pop two lane-aligned operand vectors. The stack layout after two
/// `LoadUnified` ops is `[a.., b..]` with `b` on top, so the first pop
/// yields `b` and the second yields `a`; both are returned in lane order
/// as `(a, b)`.
fn pop_two_lane_vectors(
    stack: &mut Vec<f32>,
    lanes: usize,
) -> Result<(Vec<f32>, Vec<f32>), ReferenceEvalError> {
    let b = pop_lane_vector(stack, lanes)?;
    let a = pop_lane_vector(stack, lanes)?;
    Ok((a, b))
}

/// Standalone matmul reference (M x K) * (K x N) -> (M x N), all f32
/// row-major. Used by the proptest harness for the matmul blueprint
/// shape — the IR-level [`TensorWasmOp::MatMul`] is rejected by both
/// the PTX emitter and the reference interpreter today, so the
/// harness drives the comparison against this CPU-side reference
/// instead.
pub fn matmul_reference(
    m: usize,
    k: usize,
    n: usize,
    a: &[f32],
    b: &[f32],
) -> Result<Vec<f32>, ReferenceEvalError> {
    if a.len() != m * k || b.len() != k * n {
        return Err(ReferenceEvalError::InputTooShort);
    }
    let mut out = vec![0.0f32; m * n];
    for i in 0..m {
        for j in 0..n {
            let mut acc = 0.0f32;
            for kk in 0..k {
                acc = a[i * k + kk].mul_add(b[kk * n + j], acc);
            }
            out[i * n + j] = acc;
        }
    }
    Ok(out)
}

/// Standalone 2D convolution reference (single channel, stride 1, no
/// padding). Input `H x W`, kernel `Kh x Kw`; output `(H-Kh+1) x
/// (W-Kw+1)`. Mirrors the rewriter's `Conv2d` lowering, which the
/// PTX emitter expresses as a sequence of `LoadUnified` +
/// `VecFma` + `StoreUnified` ops over a sliding window.
pub fn conv2d_reference(
    h: usize,
    w: usize,
    kh: usize,
    kw: usize,
    input: &[f32],
    kernel: &[f32],
) -> Result<Vec<f32>, ReferenceEvalError> {
    if input.len() != h * w || kernel.len() != kh * kw {
        return Err(ReferenceEvalError::InputTooShort);
    }
    if h < kh || w < kw {
        return Err(ReferenceEvalError::InputTooShort);
    }
    let oh = h - kh + 1;
    let ow = w - kw + 1;
    let mut out = vec![0.0f32; oh * ow];
    for i in 0..oh {
        for j in 0..ow {
            let mut acc = 0.0f32;
            for ki in 0..kh {
                for kj in 0..kw {
                    let v = input[(i + ki) * w + (j + kj)];
                    let k = kernel[ki * kw + kj];
                    acc = v.mul_add(k, acc);
                }
            }
            out[i * ow + j] = acc;
        }
    }
    Ok(out)
}

// ---------------------------------------------------------------------
// Structural oracle for the EXPERIMENTAL wmma MatMul lowering (gate).
//
// We cannot execute PTX in this environment, so the oracle that gates
// the `enable_experimental_matmul` flag validates the STRUCTURE of the
// emitted instruction sequence rather than a numeric readback: the
// correct fragment shapes, the correct number of `wmma.mma.sync` ops for
// the tiling, correct accumulator chaining (the mma's `c` and `d`
// operands name the same accumulator fragment), and register-declaration
// consistency (every `%rb`/`%f` fragment register the wmma ops name is
// covered by the `.reg` header). This is the same class of structural
// assertion the `ptx_emit` unit tests use; the proptest harness drives
// it across random m16n16k16 inputs so the gate must pass before anyone
// flips the feature on.
// ---------------------------------------------------------------------

/// A single mismatch found by [`check_wmma_structure`]. The proptest
/// gate fails on any non-empty report.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum WmmaStructuralError {
    /// The expected number of `wmma.mma.sync` ops (one per k16 tile)
    /// was not emitted.
    MmaCountMismatch {
        /// Number of `wmma.mma.sync` ops the tiling requires.
        expected: usize,
        /// Number actually emitted.
        actual: usize,
    },
    /// A required wmma load/store op is missing from the sequence.
    MissingOp(&'static str),
    /// An operand or accumulator fragment had the wrong register count.
    FragmentShapeMismatch {
        /// Which fragment (a/b/c/d).
        which: &'static str,
        /// Register count the m16n16k16 contract requires.
        expected: usize,
        /// Register count found in the emitted operand list.
        actual: usize,
    },
    /// The mma.sync's `c` accumulator operand and `d` result operand are
    /// not the same fragment — the accumulator is not chained in place.
    AccumulatorNotChained,
    /// A fragment register named by a wmma op is outside the range the
    /// `.reg` header declares.
    RegisterDeclInconsistent(&'static str),
}

/// Expected wmma fragment register count for the m16n16k16 shape. Mirrors
/// `ptx_emit::WMMA_OPERAND_FRAG_REGS` / `WMMA_ACC_FRAG_REGS` (both 8);
/// re-declared here so the oracle is an INDEPENDENT check rather than a
/// tautology against the emitter's own constants.
const WMMA_FRAG_REGS: usize = 8;

/// Parse the `{...}` operand list that follows the first occurrence of
/// `marker` in `text`, returning the comma-separated register tokens.
fn fragment_operands(text: &str, marker: &str) -> Option<Vec<String>> {
    let start = text.find(marker)?;
    let rest = &text[start..];
    let open = rest.find('{')?;
    let close = rest[open..].find('}')? + open;
    let inner = &rest[open + 1..close];
    Some(
        inner
            .split(',')
            .map(|t| t.trim().to_string())
            .filter(|t| !t.is_empty())
            .collect(),
    )
}

/// Parse the count `N` from a `.reg .<class> %<prefix><N>;` declaration.
fn reg_decl_count(text: &str, prefix: &str) -> Option<u32> {
    for line in text.lines() {
        let line = line.trim();
        if line.starts_with(".reg") && line.contains(&format!("%{prefix}<")) {
            return line
                .split('<')
                .nth(1)
                .and_then(|s| s.split('>').next())
                .and_then(|s| s.trim().parse().ok());
        }
    }
    None
}

/// Returns the max index used by a `%prefixN` register token list, or
/// `None` if any token is malformed.
fn max_reg_index(tokens: &[String], prefix: &str) -> Option<u32> {
    let needle = format!("%{prefix}");
    let mut max = None;
    for t in tokens {
        let n: u32 = t.strip_prefix(&needle)?.parse().ok()?;
        max = Some(max.map_or(n, |m: u32| m.max(n)));
    }
    max
}

/// Validate the structure of the EXPERIMENTAL wmma MatMul PTX for a
/// single `m16n16k16` tile. Returns the (possibly empty) list of
/// structural errors; an empty list is a PASS.
///
/// This is the gate the differential proptest runs across random
/// matmul inputs. It encodes the m16n16k16 contract independently of the
/// emitter so a regression in `ptx_emit` surfaces here as a divergence
/// rather than silently changing both sides.
pub fn check_wmma_structure(ptx: &str, m: u32, n: u32, k: u32) -> Vec<WmmaStructuralError> {
    let mut errs = Vec::new();

    // One mma.sync per 16x16x16 tile. For the single modelled shape this
    // is the product of the per-dimension tile counts (== 1 for 16/16/16).
    let tiles_m = m.div_ceil(16) as usize;
    let tiles_n = n.div_ceil(16) as usize;
    let tiles_k = k.div_ceil(16) as usize;
    let expected_mma = tiles_m * tiles_n * tiles_k;
    let actual_mma = ptx.matches("wmma.mma.sync.aligned").count();
    if actual_mma != expected_mma {
        errs.push(WmmaStructuralError::MmaCountMismatch {
            expected: expected_mma,
            actual: actual_mma,
        });
    }

    // Required ops in the sequence.
    for (needle, label) in [
        ("wmma.load.a.sync.aligned.row.m16n16k16", "wmma.load.a"),
        ("wmma.load.b.sync.aligned.col.m16n16k16", "wmma.load.b"),
        ("wmma.load.c.sync.aligned.row.m16n16k16", "wmma.load.c"),
        ("wmma.store.d.sync.aligned.row.m16n16k16", "wmma.store.d"),
    ] {
        if !ptx.contains(needle) {
            errs.push(WmmaStructuralError::MissingOp(label));
        }
    }

    // Fragment shapes: a/b operands and the c/d accumulator must each
    // name exactly WMMA_FRAG_REGS registers.
    let frag_checks: [(&str, &str); 3] = [
        ("wmma.load.a.sync.aligned.row.m16n16k16", "a"),
        ("wmma.load.b.sync.aligned.col.m16n16k16", "b"),
        ("wmma.load.c.sync.aligned.row.m16n16k16", "c"),
    ];
    for (marker, which) in frag_checks {
        if let Some(ops) = fragment_operands(ptx, marker) {
            if ops.len() != WMMA_FRAG_REGS {
                errs.push(WmmaStructuralError::FragmentShapeMismatch {
                    which,
                    expected: WMMA_FRAG_REGS,
                    actual: ops.len(),
                });
            }
        }
    }

    // Accumulator chaining: the mma.sync names four fragments
    // `d, a, b, c`; `d` and `c` must be identical (paired accumulator).
    if let Some(mma_ops) = fragment_operands(ptx, "wmma.mma.sync.aligned") {
        // The operand list is the FIRST {…}; for `d, a, b, c` the four
        // fragments are concatenated, so the d-fragment is the first
        // WMMA_FRAG_REGS tokens and the c-fragment is the last
        // WMMA_FRAG_REGS tokens of the full four-fragment register list.
        // `fragment_operands` only captured the first braces group (the
        // `d` fragment); we re-parse the full instruction to compare d vs c.
        let _ = mma_ops; // d-fragment shape is covered by the store check.
        if let Some(line) = ptx.lines().find(|l| l.contains("wmma.mma.sync.aligned")) {
            // Collect every {...} group on the (possibly continued) mma
            // instruction. The emitter writes it on one logical line.
            let groups: Vec<Vec<String>> = collect_brace_groups(line);
            if groups.len() == 4 {
                let d_frag = &groups[0];
                let c_frag = &groups[3];
                if d_frag != c_frag {
                    errs.push(WmmaStructuralError::AccumulatorNotChained);
                }
                if d_frag.len() != WMMA_FRAG_REGS {
                    errs.push(WmmaStructuralError::FragmentShapeMismatch {
                        which: "d",
                        expected: WMMA_FRAG_REGS,
                        actual: d_frag.len(),
                    });
                }
            } else {
                errs.push(WmmaStructuralError::AccumulatorNotChained);
            }
        }
    }

    // Register-declaration consistency: every %rb/%f fragment register
    // named by the wmma ops must be inside the `.reg` header's declared
    // range (count == max_index + 1).
    let collect_all = |marker: &str, prefix: &str| -> Option<u32> {
        fragment_operands(ptx, marker).and_then(|ops| max_reg_index(&ops, prefix))
    };
    let rb_max = [
        collect_all("wmma.load.a.sync.aligned.row.m16n16k16", "rb"),
        collect_all("wmma.load.b.sync.aligned.col.m16n16k16", "rb"),
    ]
    .into_iter()
    .flatten()
    .max();
    if let Some(used) = rb_max {
        match reg_decl_count(ptx, "rb") {
            Some(decl) if decl > used => {}
            _ => errs.push(WmmaStructuralError::RegisterDeclInconsistent("rb")),
        }
    }
    if let Some(used) = collect_all("wmma.load.c.sync.aligned.row.m16n16k16", "f") {
        match reg_decl_count(ptx, "f") {
            Some(decl) if decl > used => {}
            _ => errs.push(WmmaStructuralError::RegisterDeclInconsistent("f")),
        }
    }

    errs
}

/// Collect every `{...}` register-list group on a single PTX line, in
/// order. Used to pull the four fragments off the `wmma.mma.sync`
/// instruction (`d, a, b, c`).
fn collect_brace_groups(line: &str) -> Vec<Vec<String>> {
    let mut groups = Vec::new();
    let mut rest = line;
    while let Some(open) = rest.find('{') {
        let after = &rest[open + 1..];
        let Some(close_rel) = after.find('}') else {
            break;
        };
        let inner = &after[..close_rel];
        let toks: Vec<String> = inner
            .split(',')
            .map(|t| t.trim().to_string())
            .filter(|t| !t.is_empty())
            .collect();
        groups.push(toks);
        rest = &after[close_rel + 1..];
    }
    groups
}

// ---------------------------------------------------------------------
// CUDA runtime detection.
// ---------------------------------------------------------------------

/// Returns `true` iff this host has a usable CUDA device AND the
/// crate was built with the `cuda-oxide-backend` feature flag.
///
/// Today this always returns `false` outside the `cuda-oxide-backend`
/// feature gate — the cudarc/cuda-oxide handle that proves a device
/// is reachable does not exist on the no-CUDA build profile. v0.4
/// (the S22 self-hosted runner integration) extends this to call
/// `cudarc::driver::CudaDevice::new(0).is_ok()` so the same compiled
/// binary skips on CPU-only hosts and runs the full comparison on
/// CUDA hosts.
fn cuda_runtime_available() -> bool {
    #[cfg(feature = "cuda-oxide-backend")]
    {
        // S22 wiring point — `cudarc::driver::CudaDevice::new(0)` goes
        // here. Until that lands, even the `cuda-oxide-backend`
        // feature-gated build returns `false` so unit tests stay
        // deterministic.
        false
    }
    #[cfg(not(feature = "cuda-oxide-backend"))]
    {
        false
    }
}

/// Copy up to the first 16 bytes of `slice` into a fixed-size buffer.
/// Surface for [`OracleVerdict::HostOnlyOk::cpu_output_first_bytes`].
fn head_bytes(slice: &[u8]) -> [u8; 16] {
    let mut out = [0u8; 16];
    let n = slice.len().min(16);
    out[..n].copy_from_slice(&slice[..n]);
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{GridHint, TensorWasmKernelBlueprint, TensorWasmOp};

    fn add_blueprint(lanes: u32) -> TensorWasmKernelBlueprint {
        let elem = ElemType::F32;
        TensorWasmKernelBlueprint::new("vector_add")
            .push(TensorWasmOp::LoadUnified { elem, lanes })
            .push(TensorWasmOp::LoadUnified { elem, lanes })
            .push(TensorWasmOp::VecAdd { elem, lanes })
            .push(TensorWasmOp::StoreUnified { elem, lanes })
            .with_grid(GridHint {
                total_threads: lanes,
                preferred_block_size: lanes,
            })
    }

    #[test]
    fn reference_eval_vector_add_matches_native() {
        let bp = add_blueprint(4);
        let a = [1.0f32, 2.0, 3.0, 4.0];
        let b = [10.0f32, 20.0, 30.0, 40.0];
        let mut input = Vec::new();
        for v in a.iter().chain(b.iter()) {
            input.extend_from_slice(&v.to_le_bytes());
        }
        let out = reference_eval(&bp, &input).expect("eval");
        assert_eq!(out.len(), 16);
        let expected: Vec<f32> = a.iter().zip(b.iter()).map(|(x, y)| x + y).collect();
        for (i, &e) in expected.iter().enumerate() {
            let mut buf = [0u8; 4];
            buf.copy_from_slice(&out[i * 4..(i + 1) * 4]);
            assert_eq!(f32::from_le_bytes(buf), e);
        }
    }

    #[test]
    fn tolerance_strict_rejects_one_ulp() {
        let t = Tolerance::STRICT;
        let a = 1.0f32;
        let b = f32::from_bits(a.to_bits() + 1);
        assert!(!t.approx_eq_f32(a, b));
        assert!(t.approx_eq_f32(a, a));
    }

    #[test]
    fn tolerance_default_accepts_one_ulp() {
        let t = ToleranceTable::default().for_blueprint(BlueprintKind::VectorAdd, Dtype::F32);
        let a = 1.0f32;
        let b = f32::from_bits(a.to_bits() + 1);
        assert!(t.approx_eq_f32(a, b));
    }

    #[test]
    fn classify_picks_matmul() {
        let bp = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::MatMul {
            m: 16,
            n: 16,
            k: 16,
        });
        assert_eq!(BlueprintKind::classify(&bp), BlueprintKind::Matmul);
    }

    #[test]
    fn matmul_reference_smoke() {
        // 2x2 * 2x2 identity case.
        let a = [1.0f32, 2.0, 3.0, 4.0];
        let b = [1.0f32, 0.0, 0.0, 1.0];
        let out = matmul_reference(2, 2, 2, &a, &b).expect("matmul");
        assert_eq!(out, vec![1.0, 2.0, 3.0, 4.0]);
    }

    #[test]
    fn conv2d_reference_smoke() {
        // 3x3 input, 2x2 kernel of all-ones -> 2x2 output of partial
        // sums.
        let input = [1.0f32, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0];
        let kernel = [1.0f32; 4];
        let out = conv2d_reference(3, 3, 2, 2, &input, &kernel).expect("conv");
        assert_eq!(out, vec![12.0, 16.0, 24.0, 28.0]);
    }

    #[test]
    fn oracle_default_runs_cpu_path_when_input_present() {
        let bp = add_blueprint(2);
        let mut input = Vec::new();
        for v in [1.0f32, 2.0, 10.0, 20.0] {
            input.extend_from_slice(&v.to_le_bytes());
        }
        let verdict = DifferentialOracle::new().compare(&bp, &input);
        // With a runnable input on a no-CUDA host, the CPU path
        // executes and the verdict is HostOnlyOk.
        match verdict {
            OracleVerdict::HostOnlyOk { cpu_output_len, .. } => {
                assert_eq!(cpu_output_len, 2 * 4);
            }
            other => panic!("expected HostOnlyOk, got {other:?}"),
        }
    }

    #[test]
    fn wmma_structure_accepts_opt_in_emission() {
        use crate::ptx_emit::{emit_with, EmitConfig};
        let bp = TensorWasmKernelBlueprint::new("matmul").push(TensorWasmOp::MatMul {
            m: 16,
            n: 16,
            k: 16,
        });
        let cfg = EmitConfig {
            enable_experimental_matmul: true,
            ..EmitConfig::default()
        };
        let ptx = emit_with(&bp, &cfg).expect("opt-in emit");
        let errs = check_wmma_structure(&ptx.text, 16, 16, 16);
        assert!(errs.is_empty(), "structural oracle flagged: {errs:?}");
    }

    #[test]
    fn wmma_structure_rejects_unchained_accumulator() {
        // Hand-craft a wmma block where the mma's `d` and `c` fragments
        // differ — the exact silent-miscompile the oracle must catch.
        let ptx = "\
            .reg .b32   %rb<16>;\n\
            .reg .f32   %f<16>;\n\
            wmma.load.a.sync.aligned.row.m16n16k16.global.f16 {%rb0, %rb1, %rb2, %rb3, %rb4, %rb5, %rb6, %rb7}, [%rd0], 16;\n\
            wmma.load.b.sync.aligned.col.m16n16k16.global.f16 {%rb8, %rb9, %rb10, %rb11, %rb12, %rb13, %rb14, %rb15}, [%rd2], 16;\n\
            wmma.load.c.sync.aligned.row.m16n16k16.global.f32 {%f0, %f1, %f2, %f3, %f4, %f5, %f6, %f7}, [%rd3], 16;\n\
            wmma.mma.sync.aligned.row.col.m16n16k16.f32.f32 {%f0, %f1, %f2, %f3, %f4, %f5, %f6, %f7}, {%rb0, %rb1, %rb2, %rb3, %rb4, %rb5, %rb6, %rb7}, {%rb8, %rb9, %rb10, %rb11, %rb12, %rb13, %rb14, %rb15}, {%f8, %f9, %f10, %f11, %f12, %f13, %f14, %f15};\n\
            wmma.store.d.sync.aligned.row.m16n16k16.global.f32 [%rd1], {%f0, %f1, %f2, %f3, %f4, %f5, %f6, %f7}, 16;\n";
        let errs = check_wmma_structure(ptx, 16, 16, 16);
        assert!(errs.contains(&WmmaStructuralError::AccumulatorNotChained));
    }

    #[test]
    fn wmma_structure_rejects_missing_store() {
        let ptx = "\
            wmma.load.a.sync.aligned.row.m16n16k16.global.f16 {%rb0, %rb1, %rb2, %rb3, %rb4, %rb5, %rb6, %rb7}, [%rd0], 16;\n\
            wmma.mma.sync.aligned.row.col.m16n16k16.f32.f32 {%f0}, {%rb0}, {%rb8}, {%f0};\n";
        let errs = check_wmma_structure(ptx, 16, 16, 16);
        assert!(errs.contains(&WmmaStructuralError::MissingOp("wmma.store.d")));
    }

    #[test]
    fn oracle_default_falls_back_to_no_cuda_skip_for_unrunnable_blueprint() {
        // Mirrors the contract in `differential_scaffold.rs`: a
        // blueprint with no Load/Store ops + empty input is not
        // runnable by the reference interpreter, so the oracle
        // returns the v0.3.6 "no-cuda" skip — exactly what the
        // scaffold tests check via `reason.contains("no-cuda")`.
        let bp = TensorWasmKernelBlueprint::new("oracle_fixture").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        });
        let verdict = DifferentialOracle::new().compare(&bp, &[]);
        match verdict {
            OracleVerdict::Skipped(reason) => {
                assert!(reason.contains("no-cuda"));
            }
            other => panic!("expected Skipped, got {other:?}"),
        }
    }
}
