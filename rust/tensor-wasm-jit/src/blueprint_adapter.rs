// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! `LoweredFunction → TensorWasmKernelBlueprint` adapter.
//!
//! Wave 2.9 of the Phase 1 Pliron pipeline. The wave-1 lowering passes
//! produce a fine-grained [`crate::lowered_ir::LoweredFunction`] from
//! Cranelift IR. The existing PTX emitter (S12), however, consumes the
//! coarse-grained [`crate::ir::TensorWasmKernelBlueprint`]. Until wave 3
//! lands a real `LoweredFunction → pliron::Operation → PTX` path, this
//! adapter bridges the two by pattern-matching a small, well-known set
//! of straight-line op sequences and emitting the corresponding
//! blueprint.
//!
//! # Recognised patterns
//!
//! Single-block (straight-line) functions whose body is exactly
//! a Load + arith + Store triple (terminated by a `Return`):
//!
//! | LoweredOp sequence                          | Blueprint op            |
//! |---------------------------------------------|-------------------------|
//! | `Load` + (`AddF` \| `AddI`) + `Store`       | `VecAdd { elem: ElemType::F32, lanes: 4 }`   |
//! | `Load` + (`MulF` \| `MulI`) + `Store`       | `VecMul { elem: ElemType::F32, lanes: 4 }`   |
//! | `Load` + `Fma` + `Store`                    | `VecFma { elem: ElemType::F32, lanes: 4 }`   |
//!
//! Lane count is fixed at `4` (f32x4 / i32x4) for wave 1. Wave 3 will
//! derive it from the actual vector type carried by the `LoweredOp`s.
//!
//! `MatMul` detection is **deferred to wave 3** — it requires
//! recognising nested loops or wmma-style tile builders, which the
//! pattern-match approach in this module is too simple to handle.
//!
//! Anything else (multi-block, unsupported op mix, extra intermediates)
//! returns a structured [`AdapterError`].

#![cfg(feature = "cuda-oxide-backend")]

use crate::ir::{ElemType, GridHint, TensorWasmKernelBlueprint, TensorWasmOp};
use crate::lowered_ir::{LoweredFunction, LoweredOp, LoweredType};

/// Default lane count for the wave-1 adapter. f32x4 / i32x4. Wave 3
/// will derive this from the actual vector type the LoweredOps carry.
const DEFAULT_LANES: u32 = 4;

/// Map a scalar [`LoweredType`] to the blueprint [`ElemType`].
///
/// jit CRITICAL fix: the adapter previously discarded the arith op's type
/// and always built an f32 blueprint, so an integer add lowered to a float
/// kernel. The element type now flows from the `LoweredOp::Add*`/`Mul*`/`Fma`
/// type field into the blueprint. Non-scalar / non-emittable types are
/// failed closed by the caller.
fn lowered_type_to_elem(ty: &LoweredType) -> Option<ElemType> {
    match ty {
        LoweredType::I8 => Some(ElemType::I8),
        LoweredType::I16 => Some(ElemType::I16),
        LoweredType::I32 => Some(ElemType::I32),
        LoweredType::I64 => Some(ElemType::I64),
        LoweredType::F32 => Some(ElemType::F32),
        LoweredType::F64 => Some(ElemType::F64),
        // Ptr / Bool / V128 have no blueprint element-type representation.
        _ => None,
    }
}

/// Errors produced by the LoweredFunction → blueprint adapter.
#[derive(Debug, thiserror::Error, Clone, PartialEq, Eq)]
pub enum AdapterError {
    /// The LoweredFunction's op sequence didn't match any blueprint pattern
    /// the wave-1 adapter recognizes. Wave 3 will replace the adapter with
    /// real Pliron-based PTX emission, so this is not a permanent gap.
    #[error("blueprint_adapter: function does not match a wave-1 blueprint pattern ({reason})")]
    UnmatchedPattern {
        /// Short description of what was unexpected (e.g. "no Load before
        /// arith op", "MatMul not supported in wave 1").
        reason: String,
    },

    /// The LoweredFunction had multiple blocks; wave-1 adapter only handles
    /// single-block (straight-line) kernels.
    #[error("blueprint_adapter: multi-block LoweredFunction not supported in wave 1 (got {block_count} blocks)")]
    MultiBlockNotSupported {
        /// Number of blocks in the input function.
        block_count: usize,
    },

    /// The LoweredFunction was malformed (e.g. no terminator).
    #[error("blueprint_adapter: malformed LoweredFunction")]
    Malformed,
}

/// Convert a wave-1 [`LoweredFunction`] into a coarse-grained
/// [`TensorWasmKernelBlueprint`] that the existing PTX emitter can render.
///
/// Recognises three blueprint patterns:
/// - `Load` + `AddF`/`AddI` + `Store`  → `VecAdd`
/// - `Load` + `MulF`/`MulI` + `Store`  → `VecMul`
/// - `Load` + `Fma`         + `Store`  → `VecFma`
///
/// All other `LoweredFunction` shapes return
/// [`AdapterError::UnmatchedPattern`].
///
/// # Errors
///
/// - [`AdapterError::Malformed`] if the function has no blocks or its
///   single block has no terminator.
/// - [`AdapterError::MultiBlockNotSupported`] if the function has more
///   than one block.
/// - [`AdapterError::UnmatchedPattern`] if the single block's op
///   sequence doesn't match one of the three recognised patterns.
pub fn lowered_function_to_blueprint(
    func: &LoweredFunction,
) -> Result<TensorWasmKernelBlueprint, AdapterError> {
    // Wave-1 only handles single-block straight-line kernels.
    if func.blocks.is_empty() {
        return Err(AdapterError::Malformed);
    }
    if func.blocks.len() > 1 {
        return Err(AdapterError::MultiBlockNotSupported {
            block_count: func.blocks.len(),
        });
    }

    let block = &func.blocks[0];

    // Block must end in a terminator. We also require that terminator to
    // be `Return` for the patterns we recognise.
    let last = block.ops.last().ok_or(AdapterError::Malformed)?;
    if !last.is_terminator() {
        return Err(AdapterError::Malformed);
    }
    if !matches!(last, LoweredOp::Return { .. }) {
        return Err(AdapterError::UnmatchedPattern {
            reason: format!(
                "terminator is not Return ({} ops in block)",
                block.ops.len()
            ),
        });
    }

    // The recognised patterns are exactly four ops:
    //   [Load, arith, Store, Return]
    // Anything else (zero arith ops, multiple arith ops, extra moves
    // between Load and arith, etc.) is rejected.
    if block.ops.len() != 4 {
        return Err(AdapterError::UnmatchedPattern {
            reason: format!(
                "expected exactly 4 ops (Load, arith, Store, Return), got {}",
                block.ops.len()
            ),
        });
    }

    // op[0] must be a Load.
    let is_load = matches!(&block.ops[0], LoweredOp::Load { .. });
    if !is_load {
        return Err(AdapterError::UnmatchedPattern {
            reason: "no Load before arith op".to_string(),
        });
    }

    // op[2] must be a Store.
    let is_store = matches!(&block.ops[2], LoweredOp::Store { .. });
    if !is_store {
        return Err(AdapterError::UnmatchedPattern {
            reason: "no Store after arith op".to_string(),
        });
    }

    // op[1] must be one of the recognised arith ops. The element type is
    // derived from the op's `ty` field (jit CRITICAL fix) so an integer add
    // produces an integer blueprint, not a silently-miscompiled f32 kernel.
    let unsupported_elem = |ty: &LoweredType| AdapterError::UnmatchedPattern {
        reason: format!("arith op element type `{ty}` has no blueprint representation"),
    };
    let (kernel_op, elem) = match &block.ops[1] {
        LoweredOp::AddF { ty, .. } | LoweredOp::AddI { ty, .. } => {
            let elem = lowered_type_to_elem(ty).ok_or_else(|| unsupported_elem(ty))?;
            (
                TensorWasmOp::VecAdd {
                    elem,
                    lanes: DEFAULT_LANES,
                },
                elem,
            )
        }
        LoweredOp::MulF { ty, .. } | LoweredOp::MulI { ty, .. } => {
            let elem = lowered_type_to_elem(ty).ok_or_else(|| unsupported_elem(ty))?;
            (
                TensorWasmOp::VecMul {
                    elem,
                    lanes: DEFAULT_LANES,
                },
                elem,
            )
        }
        LoweredOp::Fma { ty, .. } => {
            let elem = lowered_type_to_elem(ty).ok_or_else(|| unsupported_elem(ty))?;
            (
                TensorWasmOp::VecFma {
                    elem,
                    lanes: DEFAULT_LANES,
                },
                elem,
            )
        }
        // MatMul detection is deferred to wave 3 — it would need
        // recognising nested loops or wmma tile builders, which the
        // pattern-match approach above is too simple to handle.
        other => {
            return Err(AdapterError::UnmatchedPattern {
                reason: format!(
                    "unsupported middle op: {} (wave 1 recognises Add/Mul/Fma only; \
                     MatMul deferred to wave 3)",
                    debug_op_name(other)
                ),
            });
        }
    };

    // Loads/stores adopt the arith op's element type so the whole kernel is
    // element-type-coherent.
    let blueprint = TensorWasmKernelBlueprint {
        entry: func.name.clone(),
        ops: vec![
            TensorWasmOp::LoadUnified {
                elem,
                lanes: DEFAULT_LANES,
            },
            kernel_op,
            TensorWasmOp::StoreUnified {
                elem,
                lanes: DEFAULT_LANES,
            },
        ],
        grid_hint: GridHint::default(),
        shared_mem_bytes: 0,
    };

    Ok(blueprint)
}

/// Short variant name for an unsupported `LoweredOp`. Used only for
/// constructing descriptive error messages — not part of any API
/// contract.
fn debug_op_name(op: &LoweredOp) -> &'static str {
    match op {
        LoweredOp::AddI { .. } => "AddI",
        LoweredOp::SubI { .. } => "SubI",
        LoweredOp::MulI { .. } => "MulI",
        LoweredOp::DivS { .. } => "DivS",
        LoweredOp::DivU { .. } => "DivU",
        LoweredOp::RemS { .. } => "RemS",
        LoweredOp::RemU { .. } => "RemU",
        LoweredOp::AddF { .. } => "AddF",
        LoweredOp::SubF { .. } => "SubF",
        LoweredOp::MulF { .. } => "MulF",
        LoweredOp::DivF { .. } => "DivF",
        LoweredOp::Fma { .. } => "Fma",
        LoweredOp::FNeg { .. } => "FNeg",
        LoweredOp::FAbs { .. } => "FAbs",
        LoweredOp::Load { .. } => "Load",
        LoweredOp::Store { .. } => "Store",
        LoweredOp::StackAlloc { .. } => "StackAlloc",
        LoweredOp::Br { .. } => "Br",
        LoweredOp::CondBr { .. } => "CondBr",
        LoweredOp::Switch { .. } => "Switch",
        LoweredOp::Return { .. } => "Return",
        LoweredOp::VMin { .. } => "VMin",
        LoweredOp::VMax { .. } => "VMax",
        LoweredOp::VSplat { .. } => "VSplat",
        LoweredOp::VSelect { .. } => "VSelect",
        LoweredOp::VAllTrue { .. } => "VAllTrue",
        LoweredOp::VAnyTrue { .. } => "VAnyTrue",
        LoweredOp::Select { .. } => "Select",
        LoweredOp::Bitcast { .. } => "Bitcast",
        LoweredOp::TruncI { .. } => "TruncI",
        LoweredOp::ExtendU { .. } => "ExtendU",
        LoweredOp::ExtendS { .. } => "ExtendS",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lowered_ir::{
        LoweredBlock, LoweredFunction, LoweredOp, LoweredSignature, LoweredType,
    };

    /// Helper: build a single-block function `[load, mid, store, return]`.
    fn straight_line_fn(name: &str, mid: LoweredOp) -> LoweredFunction {
        let mut func = LoweredFunction::new(
            name,
            LoweredSignature {
                params: vec![LoweredType::Ptr, LoweredType::Ptr],
                returns: vec![],
            },
        );
        let mut block = LoweredBlock::new(0);
        block.params = vec![(1, LoweredType::Ptr), (2, LoweredType::Ptr)];
        block.ops.push(LoweredOp::Load {
            ty: LoweredType::F32,
            base: 1,
            offset: 0,
            result: 10,
        });
        block.ops.push(mid);
        block.ops.push(LoweredOp::Store {
            ty: LoweredType::F32,
            value: 11,
            base: 2,
            offset: 0,
        });
        block.ops.push(LoweredOp::Return { values: vec![] });
        func.blocks.push(block);
        func
    }

    #[test]
    fn load_addf_store_becomes_vec_add() {
        let func = straight_line_fn(
            "vector_add",
            LoweredOp::AddF {
                ty: LoweredType::F32,
                lhs: 10,
                rhs: 10,
                result: 11,
            },
        );
        let bp = lowered_function_to_blueprint(&func).expect("should produce blueprint");
        assert_eq!(bp.entry, "vector_add");
        assert_eq!(
            bp.ops,
            vec![
                TensorWasmOp::LoadUnified {
                    elem: ElemType::F32,
                    lanes: 4
                },
                TensorWasmOp::VecAdd {
                    elem: ElemType::F32,
                    lanes: 4
                },
                TensorWasmOp::StoreUnified {
                    elem: ElemType::F32,
                    lanes: 4
                },
            ]
        );
        assert_eq!(bp.shared_mem_bytes, 0);
        assert_eq!(bp.grid_hint, GridHint::default());
    }

    #[test]
    fn load_addi_store_also_becomes_vec_add() {
        let func = straight_line_fn(
            "int_add",
            LoweredOp::AddI {
                ty: LoweredType::I32,
                lhs: 10,
                rhs: 10,
                result: 11,
            },
        );
        let bp = lowered_function_to_blueprint(&func).expect("should produce blueprint");
        assert!(matches!(
            bp.ops[1],
            TensorWasmOp::VecAdd {
                elem: ElemType::F32,
                lanes: 4
            }
        ));
    }

    #[test]
    fn load_mulf_store_becomes_vec_mul() {
        let func = straight_line_fn(
            "vector_mul",
            LoweredOp::MulF {
                ty: LoweredType::F32,
                lhs: 10,
                rhs: 10,
                result: 11,
            },
        );
        let bp = lowered_function_to_blueprint(&func).expect("should produce blueprint");
        assert_eq!(bp.entry, "vector_mul");
        assert!(matches!(
            bp.ops[1],
            TensorWasmOp::VecMul {
                elem: ElemType::F32,
                lanes: 4
            }
        ));
    }

    #[test]
    fn load_muli_store_also_becomes_vec_mul() {
        let func = straight_line_fn(
            "int_mul",
            LoweredOp::MulI {
                ty: LoweredType::I32,
                lhs: 10,
                rhs: 10,
                result: 11,
            },
        );
        let bp = lowered_function_to_blueprint(&func).expect("should produce blueprint");
        assert!(matches!(
            bp.ops[1],
            TensorWasmOp::VecMul {
                elem: ElemType::F32,
                lanes: 4
            }
        ));
    }

    #[test]
    fn load_fma_store_becomes_vec_fma() {
        let func = straight_line_fn(
            "vector_fma",
            LoweredOp::Fma {
                ty: LoweredType::F32,
                a: 10,
                b: 10,
                c: 10,
                result: 11,
            },
        );
        let bp = lowered_function_to_blueprint(&func).expect("should produce blueprint");
        assert_eq!(bp.entry, "vector_fma");
        assert!(matches!(
            bp.ops[1],
            TensorWasmOp::VecFma {
                elem: ElemType::F32,
                lanes: 4
            }
        ));
    }

    #[test]
    fn multi_block_rejected() {
        let mut func = LoweredFunction::new(
            "multi",
            LoweredSignature {
                params: vec![],
                returns: vec![],
            },
        );
        // Two blocks, both terminated.
        let mut b0 = LoweredBlock::new(0);
        b0.ops.push(LoweredOp::Br {
            target: 1,
            args: vec![],
        });
        let mut b1 = LoweredBlock::new(1);
        b1.ops.push(LoweredOp::Return { values: vec![] });
        func.blocks.push(b0);
        func.blocks.push(b1);

        let err = lowered_function_to_blueprint(&func).unwrap_err();
        assert_eq!(err, AdapterError::MultiBlockNotSupported { block_count: 2 });
    }

    #[test]
    fn unsupported_pattern_rejected_with_descriptive_reason() {
        // Load + Bitcast + Store + Return — Bitcast is not a recognised
        // arith op, so the adapter should reject with a UnmatchedPattern
        // error mentioning the offending op.
        let func = straight_line_fn(
            "weird",
            LoweredOp::Bitcast {
                from_ty: LoweredType::F32,
                to_ty: LoweredType::I32,
                src: 10,
                result: 11,
            },
        );
        let err = lowered_function_to_blueprint(&func).unwrap_err();
        match err {
            AdapterError::UnmatchedPattern { reason } => {
                assert!(
                    reason.contains("Bitcast"),
                    "reason should name the offending op, got: {reason}"
                );
            }
            other => panic!("expected UnmatchedPattern, got {other:?}"),
        }
    }

    #[test]
    fn empty_function_is_malformed() {
        let func = LoweredFunction::new("empty", LoweredSignature::default());
        let err = lowered_function_to_blueprint(&func).unwrap_err();
        assert_eq!(err, AdapterError::Malformed);
    }

    #[test]
    fn block_without_terminator_is_malformed() {
        // Single block with no ops at all → no last op → Malformed.
        let mut func = LoweredFunction::new("bad", LoweredSignature::default());
        func.blocks.push(LoweredBlock::new(0));
        let err = lowered_function_to_blueprint(&func).unwrap_err();
        assert_eq!(err, AdapterError::Malformed);
    }

    #[test]
    fn extra_intermediate_op_rejected() {
        // Load + AddF + Bitcast + Store + Return — five ops, with an
        // extra Bitcast between the arith and the Store. Must NOT match
        // (the adapter requires exactly Load+arith+Store+Return).
        let mut func = LoweredFunction::new(
            "extra",
            LoweredSignature {
                params: vec![LoweredType::Ptr],
                returns: vec![],
            },
        );
        let mut block = LoweredBlock::new(0);
        block.ops.push(LoweredOp::Load {
            ty: LoweredType::F32,
            base: 1,
            offset: 0,
            result: 10,
        });
        block.ops.push(LoweredOp::AddF {
            ty: LoweredType::F32,
            lhs: 10,
            rhs: 10,
            result: 11,
        });
        block.ops.push(LoweredOp::Bitcast {
            from_ty: LoweredType::F32,
            to_ty: LoweredType::I32,
            src: 11,
            result: 12,
        });
        block.ops.push(LoweredOp::Store {
            ty: LoweredType::I32,
            value: 12,
            base: 1,
            offset: 0,
        });
        block.ops.push(LoweredOp::Return { values: vec![] });
        func.blocks.push(block);

        let err = lowered_function_to_blueprint(&func).unwrap_err();
        assert!(matches!(err, AdapterError::UnmatchedPattern { .. }));
    }

    #[test]
    fn adapter_error_displays_useful_messages() {
        // Ensure the thiserror Display strings actually carry the structured
        // data — surface tests so a future refactor of the error formats
        // doesn't silently drop information callers rely on.
        let e = AdapterError::MultiBlockNotSupported { block_count: 3 };
        let s = e.to_string();
        assert!(s.contains("multi-block"));
        assert!(s.contains('3'));

        let e = AdapterError::UnmatchedPattern {
            reason: "no Load before arith op".to_string(),
        };
        let s = e.to_string();
        assert!(s.contains("no Load before arith op"));
    }
}
