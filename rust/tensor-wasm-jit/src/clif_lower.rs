// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Lower a [`crate::detector::BlockIR`] into a
//! [`crate::ir::TensorWasmKernelBlueprint`].

use thiserror::Error;

use crate::detector::{BlockIR, Op};
use crate::ir::{ElemType, GridHint, TensorWasmKernelBlueprint, TensorWasmOp};

/// Errors produced by the lowering pass.
#[derive(Debug, Error, PartialEq)]
pub enum LowerError {
    /// The block contained operations the lowering pass doesn't yet handle.
    #[error("unsupported op in block `{block}`: {op:?}")]
    UnsupportedOp {
        /// Block name from `BlockIR::name`.
        block: String,
        /// The op the lowering didn't know how to handle.
        op: Op,
    },
    /// The lowering refused because of a memory-safety concern (e.g. an
    /// out-of-bounds load pattern).
    #[error("memory-reference out of bounds in block `{block}`")]
    OutOfBoundsMemory {
        /// Block name.
        block: String,
    },
    /// jit CRITICAL fix: the block mixed SIMD element types (e.g. an
    /// `f32x4.add` next to an `i32x4.add`). The flat blueprint op stream
    /// has a single load/store element width inferred from the block's SIMD
    /// ops, so a heterogeneous block cannot be lowered without miscompiling
    /// the loads/stores. We fail closed; the caller keeps the function on
    /// the CPU path.
    #[error("mixed SIMD element types in block `{block}` ({first} vs {second})")]
    MixedElementTypes {
        /// Block name.
        block: String,
        /// First element type seen.
        first: ElemType,
        /// Conflicting element type seen later.
        second: ElemType,
    },
}

/// Default lane width used when a block has no SIMD op to infer width from
/// (e.g. a pure load/store block); kept for the f32x4 production shape.
const DEFAULT_LANES: u32 = 4;

/// A memory reference seen in the source block: an absolute byte offset
/// into the instance's linear memory and an access width in bytes. The
/// lowering pass uses these to refuse offload candidates whose accesses
/// extend past the instance's allocated `UnifiedBuffer` extent.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MemRef {
    /// Byte offset into the instance linear memory.
    pub offset: u64,
    /// Number of bytes accessed.
    pub len: u64,
}

/// Infer the single SIMD element type + lane count used by a block.
///
/// jit CRITICAL fix: the blueprint op stream is flat and the load/store
/// element width must match the block's SIMD compute width. We scan the
/// SIMD ops and require they all agree on element type and lane count; a
/// mismatch is failed closed via [`LowerError::MixedElementTypes`]. Blocks
/// with no SIMD op (pure load/store) fall back to the f32x4 default — the
/// only shape the production auto-offload path emits for such a block.
fn infer_block_shape(block: &BlockIR) -> Result<(ElemType, u32), LowerError> {
    let mut shape: Option<(ElemType, u32)> = None;
    for op in &block.ops {
        let cur = match op {
            Op::V128Add { lane_ty, lanes }
            | Op::V128Mul { lane_ty, lanes }
            | Op::V128Fma { lane_ty, lanes } => (*lane_ty, *lanes),
            _ => continue,
        };
        match shape {
            None => shape = Some(cur),
            Some((seen_ty, _)) if seen_ty != cur.0 => {
                return Err(LowerError::MixedElementTypes {
                    block: block.name.clone(),
                    first: seen_ty,
                    second: cur.0,
                });
            }
            Some(_) => {}
        }
    }
    Ok(shape.unwrap_or((ElemType::F32, DEFAULT_LANES)))
}

/// Lower a [`BlockIR`] into a [`TensorWasmKernelBlueprint`].
///
/// The mapping is intentionally narrow. Each SIMD op carries its element
/// type and lane count through to the blueprint; load/store ops adopt the
/// block's inferred SIMD element type (see [`infer_block_shape`]):
///
/// | Source                                       | Target                                  |
/// |----------------------------------------------|-----------------------------------------|
/// | `Op::V128Add { lane_ty, lanes }`             | `TensorWasmOp::VecAdd { elem, lanes }`  |
/// | `Op::V128Mul { lane_ty, lanes }`             | `TensorWasmOp::VecMul { elem, lanes }`  |
/// | `Op::V128Fma { lane_ty, lanes }`             | `TensorWasmOp::VecFma { elem, lanes }`  |
/// | `Op::Load`                                   | `TensorWasmOp::LoadUnified { elem, .. }`|
/// | `Op::Store`                                  | `TensorWasmOp::StoreUnified { elem, ..}`|
/// | Anything else                                | `UnsupportedOp` error                   |
pub fn lower_block(block: &BlockIR) -> Result<TensorWasmKernelBlueprint, LowerError> {
    let (mem_elem, mem_lanes) = infer_block_shape(block)?;
    let mut bp = TensorWasmKernelBlueprint::new(&block.name);
    for op in &block.ops {
        let lo = match op {
            Op::V128Add { lane_ty, lanes } => TensorWasmOp::VecAdd {
                elem: *lane_ty,
                lanes: *lanes,
            },
            Op::V128Mul { lane_ty, lanes } => TensorWasmOp::VecMul {
                elem: *lane_ty,
                lanes: *lanes,
            },
            Op::V128Fma { lane_ty, lanes } => TensorWasmOp::VecFma {
                elem: *lane_ty,
                lanes: *lanes,
            },
            Op::Load => TensorWasmOp::LoadUnified {
                elem: mem_elem,
                lanes: mem_lanes,
            },
            Op::Store => TensorWasmOp::StoreUnified {
                elem: mem_elem,
                lanes: mem_lanes,
            },
            unsupported => {
                return Err(LowerError::UnsupportedOp {
                    block: block.name.clone(),
                    op: *unsupported,
                });
            }
        };
        bp.ops.push(lo);
    }

    // Trip count clamped into u32; saturate rather than truncate so a
    // (genuinely huge) static loop doesn't masquerade as a tiny grid.
    let total_threads = u32::try_from(block.loop_trip_count.unwrap_or(256)).unwrap_or(u32::MAX);
    bp.grid_hint = GridHint {
        total_threads,
        preferred_block_size: 128,
    };
    Ok(bp)
}

/// Lower a [`BlockIR`] with explicit memory-reference validation.
///
/// `mem_refs` is the list of memory accesses extracted from the source
/// block by the caller's side-band analysis (e.g. `wasmparser`). Each
/// `(offset, len)` pair must satisfy `offset + len <= memory_bytes`;
/// any violation aborts with [`LowerError::OutOfBoundsMemory`] so the
/// caller falls back to CPU execution.
pub fn lower_block_checked(
    block: &BlockIR,
    mem_refs: &[MemRef],
    memory_bytes: u64,
) -> Result<TensorWasmKernelBlueprint, LowerError> {
    for r in mem_refs {
        let end = r.offset.checked_add(r.len);
        let out_of_bounds = match end {
            Some(end) => end > memory_bytes,
            None => true, // overflow → treat as OOB
        };
        if out_of_bounds {
            return Err(LowerError::OutOfBoundsMemory {
                block: block.name.clone(),
            });
        }
    }
    lower_block(block)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn block(name: &str, ops: Vec<Op>, loop_n: Option<u64>) -> BlockIR {
        BlockIR::new(name, ops, loop_n)
    }

    fn add(lane_ty: ElemType, lanes: u32) -> Op {
        Op::V128Add { lane_ty, lanes }
    }
    fn mul(lane_ty: ElemType, lanes: u32) -> Op {
        Op::V128Mul { lane_ty, lanes }
    }
    fn fma(lane_ty: ElemType, lanes: u32) -> Op {
        Op::V128Fma { lane_ty, lanes }
    }

    #[test]
    fn lower_simple_vector_add() {
        let b = block(
            "vec_add",
            vec![Op::Load, Op::Load, add(ElemType::F32, 4), Op::Store],
            Some(128),
        );
        let bp = lower_block(&b).unwrap();
        assert_eq!(bp.entry, "vec_add");
        assert_eq!(bp.ops.len(), 4);
        assert!(matches!(
            bp.ops[2],
            TensorWasmOp::VecAdd {
                elem: ElemType::F32,
                lanes: 4
            }
        ));
        // Loads/stores adopt the block's SIMD element type.
        assert!(matches!(
            bp.ops[0],
            TensorWasmOp::LoadUnified {
                elem: ElemType::F32,
                lanes: 4
            }
        ));
        assert_eq!(bp.grid_hint.total_threads, 128);
    }

    /// jit CRITICAL: an `i32x4.add` block must lower to an i32 blueprint —
    /// element type and lane count survive the lowering, and the loads/
    /// stores adopt the integer width.
    #[test]
    fn lower_i32x4_add_carries_integer_element_type() {
        let b = block(
            "i32_add",
            vec![Op::Load, Op::Load, add(ElemType::I32, 4), Op::Store],
            Some(128),
        );
        let bp = lower_block(&b).unwrap();
        assert!(matches!(
            bp.ops[2],
            TensorWasmOp::VecAdd {
                elem: ElemType::I32,
                lanes: 4
            }
        ));
        assert!(matches!(
            bp.ops[0],
            TensorWasmOp::LoadUnified {
                elem: ElemType::I32,
                ..
            }
        ));
        // It must NOT have silently become an f32 kernel.
        assert!(!bp.ops.iter().any(|o| matches!(
            o,
            TensorWasmOp::VecAdd {
                elem: ElemType::F32,
                ..
            }
        )));
    }

    /// jit CRITICAL: a block mixing element types is failed closed rather
    /// than lowered with a single (wrong) load/store width.
    #[test]
    fn lower_mixed_element_types_rejected() {
        let b = block(
            "mixed",
            vec![add(ElemType::F32, 4), add(ElemType::I32, 4)],
            Some(128),
        );
        let err = lower_block(&b).unwrap_err();
        assert!(matches!(err, LowerError::MixedElementTypes { .. }));
    }

    /// Non-default lane counts (f64x2) survive lowering.
    #[test]
    fn lower_f64x2_lane_count_survives() {
        let b = block("f64_add", vec![add(ElemType::F64, 2)], Some(128));
        let bp = lower_block(&b).unwrap();
        assert!(matches!(
            bp.ops[0],
            TensorWasmOp::VecAdd {
                elem: ElemType::F64,
                lanes: 2
            }
        ));
    }

    #[test]
    fn unsupported_op_rejected() {
        let b = block("with_call", vec![add(ElemType::F32, 4), Op::Call], Some(64));
        let err = lower_block(&b).unwrap_err();
        assert!(matches!(err, LowerError::UnsupportedOp { .. }));
    }

    #[test]
    fn missing_trip_count_uses_default() {
        let b = block("dyn", vec![add(ElemType::F32, 4)], None);
        let bp = lower_block(&b).unwrap();
        assert_eq!(bp.grid_hint.total_threads, 256);
    }

    #[test]
    fn all_v128_lower() {
        let b = block(
            "fma_loop",
            vec![
                fma(ElemType::F32, 4),
                fma(ElemType::F32, 4),
                mul(ElemType::F32, 4),
            ],
            Some(64),
        );
        let bp = lower_block(&b).unwrap();
        for op in &bp.ops {
            assert!(matches!(
                op,
                TensorWasmOp::VecFma { .. }
                    | TensorWasmOp::VecMul { .. }
                    | TensorWasmOp::VecAdd { .. }
            ));
        }
    }

    #[test]
    fn lower_block_checked_passes_with_in_bounds_refs() {
        let b = block(
            "vec_add",
            vec![Op::Load, add(ElemType::F32, 4), Op::Store],
            Some(128),
        );
        let refs = &[
            MemRef { offset: 0, len: 16 },
            MemRef {
                offset: 1024,
                len: 16,
            },
        ];
        let bp = lower_block_checked(&b, refs, 64 * 1024).expect("in-bounds ok");
        assert_eq!(bp.ops.len(), 3);
    }

    #[test]
    fn lower_block_checked_rejects_out_of_bounds_refs() {
        let b = block("oob", vec![Op::Load], Some(64));
        // memory_bytes = 1024 but the ref reads past it.
        let refs = &[MemRef {
            offset: 1020,
            len: 16,
        }];
        let err = lower_block_checked(&b, refs, 1024).unwrap_err();
        assert!(matches!(err, LowerError::OutOfBoundsMemory { .. }));
    }

    #[test]
    fn lower_block_checked_rejects_overflow_refs() {
        let b = block("overflow", vec![Op::Load], Some(64));
        let refs = &[MemRef {
            offset: u64::MAX - 4,
            len: 16,
        }];
        let err = lower_block_checked(&b, refs, 1024).unwrap_err();
        assert!(matches!(err, LowerError::OutOfBoundsMemory { .. }));
    }
}
