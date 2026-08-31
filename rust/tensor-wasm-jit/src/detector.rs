// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! GPU-offload-candidate detector.
//!
//! Walks a [`BlockIR`] (a simplified, in-house representation modelled after
//! Cranelift CLIF basic blocks) and decides whether the block should be
//! lowered to PTX. The decision rule, per the plan, is:
//!
//! > Flag a block if **>80 %** of its instructions are v128 SIMD ops AND the
//! > loop trip count is statically known.
//!
//! The simplified IR keeps this crate independent of the Cranelift runtime
//! API surface — the real-CLIF integration is documented in
//! `docs/WASMTIME-FORK.md` and lands in a follow-up session once the
//! Wasmtime team upstreams the necessary hooks.

use std::fmt;

use crate::ir::ElemType;

/// Instruction kinds the detector recognises.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Op {
    /// 128-bit SIMD add. Carries the per-lane element type and the true
    /// lane count decoded from the WASM opcode.
    ///
    /// jit CRITICAL fix: previously `F32x4Add | I32x4Add | I16x8Add | …`
    /// all collapsed onto a bare `V128Add` that lost both the element type
    /// and the real lane count, and the downstream emitter unconditionally
    /// emitted `add.f32` — silently miscompiling integer / f64 SIMD. The
    /// element type and lane count now travel from the opcode through to the
    /// PTX emitter.
    V128Add {
        /// Per-lane element type.
        lane_ty: ElemType,
        /// Number of lanes (4 for `*x4`, 2 for `*x2`, 8 for `*x8`, …).
        lanes: u32,
    },
    /// 128-bit SIMD multiply (carries element type + lane count, as `V128Add`).
    V128Mul {
        /// Per-lane element type.
        lane_ty: ElemType,
        /// Number of lanes.
        lanes: u32,
    },
    /// 128-bit SIMD fused-multiply-add (carries element type + lane count).
    V128Fma {
        /// Per-lane element type.
        lane_ty: ElemType,
        /// Number of lanes.
        lanes: u32,
    },
    /// Scalar arithmetic add.
    ScalarAdd,
    /// Scalar arithmetic multiply.
    ScalarMul,
    /// Load from linear memory.
    Load,
    /// Store to linear memory.
    Store,
    /// Conditional branch.
    Branch,
    /// Function call (including host imports).
    Call,
    /// Any operator the detector does not have a more specific bucket for
    /// (e.g. `local.get`, `i32.const`, `drop`). Counted in the denominator of
    /// the v128 ratio but never in the numerator. Replaces the prior
    /// "everything-else falls through to `ScalarAdd`" behaviour that biased
    /// non-arithmetic functions toward looking arithmetic.
    Other,
}

impl Op {
    /// True if this op operates on `v128` SIMD values.
    pub fn is_v128(self) -> bool {
        matches!(
            self,
            Op::V128Add { .. } | Op::V128Mul { .. } | Op::V128Fma { .. }
        )
    }
}

impl fmt::Display for Op {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Op::V128Add { lane_ty, lanes } => return write!(f, "v128.add.{lane_ty}x{lanes}"),
            Op::V128Mul { lane_ty, lanes } => return write!(f, "v128.mul.{lane_ty}x{lanes}"),
            Op::V128Fma { lane_ty, lanes } => return write!(f, "v128.fma.{lane_ty}x{lanes}"),
            _ => {}
        }
        let s = match self {
            Op::ScalarAdd => "scalar.add",
            Op::ScalarMul => "scalar.mul",
            Op::Load => "load",
            Op::Store => "store",
            Op::Branch => "branch",
            Op::Call => "call",
            Op::Other => "other",
            Op::V128Add { .. } | Op::V128Mul { .. } | Op::V128Fma { .. } => unreachable!(),
        };
        f.write_str(s)
    }
}

/// A simplified Cranelift-style basic block.
#[derive(Debug, Clone)]
pub struct BlockIR {
    /// Linear sequence of ops in this block.
    pub ops: Vec<Op>,
    /// Static loop trip count (if the loop in which this block sits has
    /// a statically-known iteration count). `None` if unknown/dynamic.
    pub loop_trip_count: Option<u64>,
    /// Human-readable name (used in tests and trace output).
    pub name: String,
}

impl BlockIR {
    /// Construct a new block.
    pub fn new(name: impl Into<String>, ops: Vec<Op>, loop_trip_count: Option<u64>) -> Self {
        Self {
            name: name.into(),
            ops,
            loop_trip_count,
        }
    }

    /// Fraction of the block that is v128 ops (between 0.0 and 1.0).
    pub fn v128_ratio(&self) -> f32 {
        v128_ratio_of(&self.ops)
    }
}

/// Annotation attached to a [`BlockIR`] after the detector runs.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DetectorVerdict {
    /// Block should be GPU-offloaded.
    Offload,
    /// Block should stay on the CPU path.
    KeepOnCpu,
}

/// Configurable parameters for the detector.
#[derive(Debug, Clone, Copy)]
pub struct DetectorConfig {
    /// Minimum fraction of v128 ops to consider offloading (default 0.8).
    pub v128_ratio_threshold: f32,
    /// Minimum loop trip count to bother with offload setup overhead (default 64).
    pub min_trip_count: u64,
}

impl Default for DetectorConfig {
    fn default() -> Self {
        Self {
            v128_ratio_threshold: 0.8,
            min_trip_count: 64,
        }
    }
}

/// Inspect a [`BlockIR`] and return a [`DetectorVerdict`].
pub fn classify(block: &BlockIR, cfg: &DetectorConfig) -> DetectorVerdict {
    classify_ops(&block.ops, block.loop_trip_count, cfg)
}

/// Classify directly from an op slice + trip count, without requiring an
/// owned [`BlockIR`].
///
/// jit PERF fix (finding 11): the rewriter previously CLONED the whole
/// `detector_ops` vector just to wrap it in a throwaway [`BlockIR`] for
/// [`classify`], then moved the original vector into its per-function slot.
/// Classifying over a borrowed slice removes that per-function clone.
pub fn classify_ops(
    ops: &[Op],
    loop_trip_count: Option<u64>,
    cfg: &DetectorConfig,
) -> DetectorVerdict {
    let ratio = v128_ratio_of(ops);
    let trip_ok = loop_trip_count
        .map(|n| n >= cfg.min_trip_count)
        .unwrap_or(false);
    if ratio >= cfg.v128_ratio_threshold && trip_ok {
        DetectorVerdict::Offload
    } else {
        DetectorVerdict::KeepOnCpu
    }
}

/// Fraction of `ops` that are v128 ops (0.0 for an empty slice). Shared by
/// [`BlockIR::v128_ratio`] and [`classify_ops`].
fn v128_ratio_of(ops: &[Op]) -> f32 {
    if ops.is_empty() {
        return 0.0;
    }
    let v128 = ops.iter().filter(|o| o.is_v128()).count();
    v128 as f32 / ops.len() as f32
}

/// Convenience: classify with default config.
pub fn classify_default(block: &BlockIR) -> DetectorVerdict {
    classify(block, &DetectorConfig::default())
}

// ---------------------------------------------------------------------------
// Wave-2 Pliron pipeline opt-in (W2.6)
// ---------------------------------------------------------------------------
//
// The block-based detector above produces *blueprint candidates*
// (matmul / vector_add / conv2d 3x3 — picked by `classify` returning
// `Offload`). Wave 2 adds a second, additive candidate-producing path that
// runs the function through the W2.4 module-level lowering driver.
//
// The integration contract — documented for downstream callers
// (`tensor-wasm-exec` / `tensor-wasm-tenant`) — is:
//
// * The existing block-based detector keeps producing blueprint candidates;
//   its behaviour is **unchanged**.
// * When `cuda-oxide-backend` is enabled **and** the runtime opt-in is on,
//   [`try_pliron_candidate`] additionally produces a [`PlironCandidate`] from
//   a Cranelift function the blueprint detector did not recognise.
// * The orchestration ("blueprint first, then Pliron fallback") lives in the
//   caller. `detector.rs` only exposes both candidate-producing APIs.
//
// Everything below is gated on `cuda-oxide-backend`. Callers must cfg-gate
// at the call site; we deliberately do not provide a feature-off shim
// (wave 3 will wire the call sites and the shim would just add dead surface
// area in the meantime).

/// Runtime opt-in for the wave-2 Pliron pipeline. When true and the
/// `cuda-oxide-backend` feature is enabled, [`Detector`](self) additionally
/// produces Pliron-pipeline candidates from blocks the blueprint detector
/// did NOT recognize. When false (the default), only blueprint candidates
/// are produced.
///
/// Set via the `TENSOR_WASM_PLIRON_PIPELINE` environment variable (presence
/// of any non-empty value enables it). Wave 3+ will replace this env-var
/// with a structured runtime config.
#[cfg(feature = "cuda-oxide-backend")]
pub fn pliron_pipeline_enabled() -> bool {
    std::env::var("TENSOR_WASM_PLIRON_PIPELINE")
        .map(|v| !v.is_empty())
        .unwrap_or(false)
}

/// A Pliron-pipeline candidate produced by the wave-2 path.
///
/// Distinct from the blueprint candidate produced by [`classify`] (which
/// encodes one of the three hand-written blueprints: `matmul`,
/// `vector_add`, `conv2d` 3x3). A `PlironCandidate` carries a
/// [`crate::lowered_ir::LoweredFunction`] that the wave-3 PTX emitter
/// will consume directly.
#[cfg(feature = "cuda-oxide-backend")]
#[derive(Debug, Clone)]
pub struct PlironCandidate {
    /// The lowered function ready for the wave-3 `pliron::Operation`
    /// converter.
    pub lowered: crate::lowered_ir::LoweredFunction,
}

/// Attempt to route a Cranelift [`cranelift_codegen::ir::Function`] through
/// the wave-2 Pliron pipeline.
///
/// Returns `None` if the function is rejected by the reject-list or fails
/// any lowering pass; the caller should fall back to the blueprint path.
/// Returns `Some` with the lowered function when the W2.4 driver succeeds.
///
/// Behaviour matrix:
///
/// * `cuda-oxide-backend` feature disabled → the entire function is
///   compiled out; callers must cfg-gate the call site.
/// * Feature enabled, [`pliron_pipeline_enabled`] returns `false` → always
///   returns `None` without running the driver.
/// * Feature enabled, opt-in on, reject-list hit → returns `None`.
/// * Feature enabled, opt-in on, lowering error → returns `None`
///   (`Result::ok` discards the diagnostic; callers wanting the structured
///   error should invoke [`crate::lowering_driver::lower_function`]
///   directly).
/// * Feature enabled, opt-in on, lowering succeeds → returns `Some`.
#[cfg(feature = "cuda-oxide-backend")]
pub fn try_pliron_candidate(func: &cranelift_codegen::ir::Function) -> Option<PlironCandidate> {
    if !pliron_pipeline_enabled() {
        return None;
    }
    // Reject-list preflight (cheap upfront check; wave-1 lowerings also
    // surface their own per-op errors, but the reject-list catches the
    // common "this whole function is doomed" cases without running the
    // full driver).
    if crate::reject_list::check_function(func).is_some() {
        return None;
    }
    crate::lowering_driver::lower_function(func)
        .map(|lowered| PlironCandidate { lowered })
        .ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn block(name: &str, ops: Vec<Op>, loop_n: Option<u64>) -> BlockIR {
        BlockIR::new(name, ops, loop_n)
    }

    /// Test-helper f32x4 SIMD ops (the production default shape).
    fn add() -> Op {
        Op::V128Add {
            lane_ty: ElemType::F32,
            lanes: 4,
        }
    }
    fn mul() -> Op {
        Op::V128Mul {
            lane_ty: ElemType::F32,
            lanes: 4,
        }
    }
    fn fma() -> Op {
        Op::V128Fma {
            lane_ty: ElemType::F32,
            lanes: 4,
        }
    }

    #[test]
    fn mixed_v128_ratio_below_threshold_is_kept_on_cpu() {
        let b = block(
            "vector_add_loop",
            vec![Op::Load, Op::Load, add(), add(), add(), add(), Op::Store],
            Some(128),
        );
        // 4/7 = 57% — under threshold. Need >80%.
        assert_eq!(classify_default(&b), DetectorVerdict::KeepOnCpu);
    }

    #[test]
    fn high_v128_ratio_offloaded() {
        let b = block(
            "matmul_inner",
            vec![
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                mul(),
                Op::Store,
            ],
            Some(512),
        );
        // 9/10 = 90% — over threshold AND trip count 512 > 64.
        assert_eq!(classify_default(&b), DetectorVerdict::Offload);
    }

    #[test]
    fn pure_v128_matmul_tile_is_offloaded() {
        let b = block(
            "matmul_tile",
            vec![
                Op::Load,
                Op::Load,
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                fma(),
                Op::Store,
            ],
            Some(256),
        );
        // 12/15 = 80% — meets threshold AND trip count 256 > 64.
        assert_eq!(classify_default(&b), DetectorVerdict::Offload);
    }

    #[test]
    fn pure_v128_vector_mul_loop_is_offloaded() {
        let b = block(
            "vector_mul",
            vec![
                Op::Load,
                mul(),
                mul(),
                mul(),
                mul(),
                add(),
                add(),
                add(),
                add(),
                Op::Store,
            ],
            Some(1024),
        );
        // 8/10 = 80% — meets threshold AND trip count 1024 > 64.
        assert_eq!(classify_default(&b), DetectorVerdict::Offload);
    }

    #[test]
    fn dynamic_loop_not_offloaded_even_if_v128_heavy() {
        let b = block(
            "dynamic_loop",
            vec![add(); 16],
            None, // unknown trip count
        );
        assert_eq!(classify_default(&b), DetectorVerdict::KeepOnCpu);
    }

    #[test]
    fn tiny_loop_not_offloaded() {
        let b = block(
            "tiny",
            vec![add(); 16],
            Some(8), // trip < threshold (64)
        );
        assert_eq!(classify_default(&b), DetectorVerdict::KeepOnCpu);
    }

    #[test]
    fn scalar_heavy_not_offloaded() {
        let b = block(
            "scalar",
            vec![
                Op::ScalarAdd,
                Op::ScalarAdd,
                Op::ScalarMul,
                Op::Branch,
                Op::Call,
                Op::Load,
            ],
            Some(1024),
        );
        assert_eq!(classify_default(&b), DetectorVerdict::KeepOnCpu);
    }

    #[test]
    fn op_is_v128_taxonomy() {
        assert!(add().is_v128());
        assert!(mul().is_v128());
        assert!(fma().is_v128());
        // Element type does not change the taxonomy classification.
        assert!(Op::V128Add {
            lane_ty: ElemType::I32,
            lanes: 4
        }
        .is_v128());
        assert!(!Op::ScalarAdd.is_v128());
        assert!(!Op::Load.is_v128());
    }

    #[test]
    fn op_display_carries_element_type() {
        assert_eq!(add().to_string(), "v128.add.f32x4");
        assert_eq!(
            Op::V128Mul {
                lane_ty: ElemType::I32,
                lanes: 4
            }
            .to_string(),
            "v128.mul.i32x4"
        );
        assert_eq!(Op::Load.to_string(), "load");
    }

    #[test]
    fn config_threshold_tunable() {
        let b = block("borderline", vec![add(), add(), Op::Load], Some(128));
        // 2/3 = 67% — below default 80% threshold.
        assert_eq!(classify_default(&b), DetectorVerdict::KeepOnCpu);
        // Lower the threshold to 60% — now it's offloaded.
        let cfg = DetectorConfig {
            v128_ratio_threshold: 0.6,
            ..DetectorConfig::default()
        };
        assert_eq!(classify(&b, &cfg), DetectorVerdict::Offload);
    }
}

// ---------------------------------------------------------------------------
// Wave-2 Pliron pipeline tests (W2.6)
// ---------------------------------------------------------------------------
//
// These tests cover the opt-in `try_pliron_candidate` path and the
// `pliron_pipeline_enabled` env-var probe. They are gated on
// `cuda-oxide-backend` because the production code they exercise is also
// feature-gated.
//
// Env-var note: `std::env::set_var` / `remove_var` mutate process-global
// state and are unsafe to race with other tests, so all tests touching
// `TENSOR_WASM_PLIRON_PIPELINE` serialize through a shared mutex. Cargo
// runs unit tests inside a single binary on multiple threads by default;
// the mutex is the standard fix.
#[cfg(all(test, feature = "cuda-oxide-backend"))]
mod pliron_pipeline_tests {
    use super::*;
    use crate::lowering_test_support::function_with_binary_op;
    use cranelift_codegen::ir::immediates::Offset32;
    use cranelift_codegen::ir::instructions::InstructionData;
    use cranelift_codegen::ir::{
        types, AbiParam, Function, MemFlags, Opcode, Signature, UserFuncName, Value,
    };
    use cranelift_codegen::isa::CallConv;
    use std::sync::Mutex;

    /// Global lock for env-var manipulation. `set_var` / `remove_var` are
    /// not thread-safe; without this lock cargo's parallel test runner
    /// produces flaky results.
    ///
    /// `OnceLock` would be nicer than a `static Mutex` constructed at
    /// link-time, but `Mutex::new` is `const` since 1.63 so this is fine.
    static ENV_LOCK: Mutex<()> = Mutex::new(());

    /// `pliron_pipeline_enabled` returns `false` when the env var is unset.
    /// Default behaviour — the wave-2 path is opt-in, not opt-out.
    #[test]
    fn pliron_pipeline_disabled_by_default() {
        let _guard = ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        std::env::remove_var("TENSOR_WASM_PLIRON_PIPELINE");
        assert!(!pliron_pipeline_enabled());
    }

    /// `pliron_pipeline_enabled` returns `true` when the env var is set to
    /// any non-empty value. The check is "presence of a value", not
    /// "value == '1'" — wave 3+ will replace the env-var with a structured
    /// runtime config, so we keep the wave-2 contract as loose as possible.
    #[test]
    fn pliron_pipeline_enabled_when_env_var_set() {
        let _guard = ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        std::env::set_var("TENSOR_WASM_PLIRON_PIPELINE", "1");
        let enabled = pliron_pipeline_enabled();
        // Reset before asserting so a failing assert doesn't leak the
        // env var into sibling tests in the same binary.
        std::env::remove_var("TENSOR_WASM_PLIRON_PIPELINE");
        assert!(enabled);
    }

    /// `try_pliron_candidate` on a simple `iadd; return` function returns
    /// `Some` when the pipeline is enabled. Mirrors the wave-2 driver's
    /// happy-path test from `lowering_driver::tests::lowers_single_block_iadd_return`.
    #[test]
    fn try_pliron_candidate_some_on_iadd_return() {
        let _guard = ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        std::env::set_var("TENSOR_WASM_PLIRON_PIPELINE", "1");
        let (func, _inst) = function_with_binary_op(Opcode::Iadd, types::I32);
        let candidate = try_pliron_candidate(&func);
        std::env::remove_var("TENSOR_WASM_PLIRON_PIPELINE");

        let candidate = candidate.expect("iadd+return must lower");
        // Sanity-check the lowered function is non-trivial: at least one
        // block and at least the `AddI + Return` op pair.
        assert!(!candidate.lowered.blocks.is_empty());
        assert!(candidate.lowered.blocks[0].ops.len() >= 2);
    }

    /// `try_pliron_candidate` returns `None` on a function the reject-list
    /// flags (here: `atomic_load`) even when the pipeline is enabled. The
    /// reject-list preflight is what saves us from running the full driver
    /// on a function we know is doomed.
    #[test]
    fn try_pliron_candidate_none_on_reject_list_hit() {
        let _guard = ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        std::env::set_var("TENSOR_WASM_PLIRON_PIPELINE", "1");

        // Build a function whose first instruction is `atomic_load` —
        // pinned in `reject_list::classify_opcode` as
        // `RejectReason::Atomic("atomic_load")`. The detector only looks
        // at the opcode, so the dummy `Value(0)` arg is fine.
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I32));
        let mut func =
            Function::with_name_signature(UserFuncName::testcase("atomic_fn".as_bytes()), sig);
        let block = func.dfg.make_block();
        let _param = func.dfg.append_block_param(block, types::I32);
        func.layout.append_block(block);

        let inst = func.dfg.make_inst(InstructionData::LoadNoOffset {
            opcode: Opcode::AtomicLoad,
            flags: MemFlags::new(),
            arg: Value::from_u32(0),
        });
        func.layout.append_inst(inst, block);

        let candidate = try_pliron_candidate(&func);
        std::env::remove_var("TENSOR_WASM_PLIRON_PIPELINE");

        assert!(
            candidate.is_none(),
            "atomic_load must be reject-listed; got Some",
        );
    }

    /// `try_pliron_candidate` returns `None` when the pipeline is disabled,
    /// regardless of whether the function would otherwise lower. Pins the
    /// "off by default" contract that matters most for callers who do not
    /// yet cfg-gate.
    #[test]
    fn try_pliron_candidate_none_when_disabled() {
        let _guard = ENV_LOCK.lock().unwrap_or_else(|e| e.into_inner());
        std::env::remove_var("TENSOR_WASM_PLIRON_PIPELINE");
        let (func, _inst) = function_with_binary_op(Opcode::Iadd, types::I32);
        assert!(try_pliron_candidate(&func).is_none());
    }

    /// Sanity: keep the `Offset32` import live. Cranelift's import surface
    /// is wide; this assertion documents that `try_pliron_candidate` is
    /// opcode-driven (it doesn't inspect immediates), matching the
    /// reject-list's own posture.
    #[test]
    fn offset_field_is_irrelevant_to_pliron_dispatch() {
        let _ = Offset32::new(0);
    }
}
