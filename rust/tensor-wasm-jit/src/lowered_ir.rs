// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! `LoweredOp` — the pure-Rust interim IR between Cranelift IR and Pliron
//! `dialect-mir`.
//!
//! Wave 1 of the Phase 1 Pliron pipeline (RFC 0001 "Future possibilities")
//! builds this interim IR. It mirrors the [Cranelift → dialect-mir mapping
//! table](crate::pliron_dialect#mapping-table) exactly, but in pure Rust
//! types so the lowering passes can land *before* the `pliron` git-pinned
//! dep is added in wave 3.
//!
//! # Why an interim IR
//!
//! Two reasons:
//!
//! 1. **Pliron's crate is alpha and git-pinned.** Coding directly against
//!    its `Operation` / `Module` types today exposes every lowering pass
//!    to upstream API churn until cuda-oxide ≥ 0.2.0 ships. The interim
//!    IR is a stable contract under our control.
//! 2. **The lowering passes are independently testable.** Each
//!    `lower_*` module (arith, float, memory, cf, vector, conv) returns a
//!    `LoweredOp` and can be unit-tested without dragging in any GPU
//!    toolchain. Wave 3 adds a single `LoweredOp -> pliron::Operation`
//!    converter and the rest of the pipeline downstream stays the same.
//!
//! # SSA shape
//!
//! [`LoweredFunction`] is in pre-SSA form: every op names its operand
//! [`LoweredValueId`]s and its result [`LoweredValueId`] explicitly. Block
//! params carry types. This matches what Cranelift's [`ir::Function`]
//! gives us per-instruction and lets us defer SSA→registers (the
//! downstream `mem2reg` pass in RFC 0001's pipeline) to wave 3.

#![cfg(feature = "cuda-oxide-backend")]

use std::fmt;

/// Stable SSA value identifier inside a [`LoweredFunction`].
///
/// Allocated by the lowering pass when it walks a Cranelift function;
/// not guaranteed to equal Cranelift's own `Value` numbering. The
/// fingerprinting in [`crate::ir::TensorWasmKernelBlueprint::fingerprint`]
/// uses the `LoweredFunction`'s structure (op order, types, edges), not
/// these numeric ids, so the ids being non-stable across runs is safe.
pub type LoweredValueId = u32;

/// Stable basic-block identifier inside a [`LoweredFunction`].
pub type LoweredBlockId = u32;

/// A scalar or vector type carried by a [`LoweredOp`] operand or result.
///
/// Mirrors the subset of Cranelift types that PTX can express. The wave 1
/// lowering rejects any Cranelift type outside this set (see the
/// "Unsupported in v0.4" notes in [`crate::pliron_dialect`]).
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum LoweredType {
    /// 8-bit integer.
    I8,
    /// 16-bit integer.
    I16,
    /// 32-bit integer.
    I32,
    /// 64-bit integer.
    I64,
    /// 32-bit IEEE-754 float.
    F32,
    /// 64-bit IEEE-754 float.
    F64,
    /// Opaque device pointer (unified-memory or device-resident).
    Ptr,
    /// Boolean — lowered to PTX `pred` register.
    Bool,
    /// Fixed-width SIMD vector of `lanes` lanes of `lane_type`.
    ///
    /// `lane_type` is restricted to scalar variants (I8..F64); nested
    /// vectors are not legal. Lane counts mirror Cranelift's v128 = 4xF32
    /// / 8xI16 / 16xI8 etc.
    V128 {
        /// Per-lane scalar type.
        lane_type: Box<LoweredType>,
        /// Lane count.
        lanes: u8,
    },
}

impl LoweredType {
    /// True if the type is a scalar integer (any width).
    pub fn is_int(&self) -> bool {
        matches!(self, Self::I8 | Self::I16 | Self::I32 | Self::I64)
    }

    /// True if the type is a scalar float.
    pub fn is_float(&self) -> bool {
        matches!(self, Self::F32 | Self::F64)
    }

    /// True if the type is a SIMD vector.
    pub fn is_vector(&self) -> bool {
        matches!(self, Self::V128 { .. })
    }

    /// Bit width of a scalar type. Returns `None` for `Ptr`, `Bool`,
    /// and vectors (callers should handle those explicitly).
    pub fn scalar_bits(&self) -> Option<u32> {
        match self {
            Self::I8 => Some(8),
            Self::I16 => Some(16),
            Self::I32 | Self::F32 => Some(32),
            Self::I64 | Self::F64 => Some(64),
            _ => None,
        }
    }
}

impl fmt::Display for LoweredType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::I8 => f.write_str("i8"),
            Self::I16 => f.write_str("i16"),
            Self::I32 => f.write_str("i32"),
            Self::I64 => f.write_str("i64"),
            Self::F32 => f.write_str("f32"),
            Self::F64 => f.write_str("f64"),
            Self::Ptr => f.write_str("ptr"),
            Self::Bool => f.write_str("bool"),
            Self::V128 { lane_type, lanes } => write!(f, "v{lanes}x{lane_type}"),
        }
    }
}

/// A single lowered operation.
///
/// Each variant maps to one row of the [Cranelift → dialect-mir mapping
/// table](crate::pliron_dialect#mapping-table). The variants are grouped
/// by lowering family so the per-family `lower_*` modules add cleanly.
#[derive(Debug, Clone, PartialEq)]
pub enum LoweredOp {
    // ---- Arith family (lower_arith) -------------------------------------
    /// Integer add. Wrap on overflow (Cranelift semantics).
    AddI {
        /// Operand/result type.
        ty: LoweredType,
        /// Left operand.
        lhs: LoweredValueId,
        /// Right operand.
        rhs: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Integer subtract.
    SubI {
        /// Operand/result type.
        ty: LoweredType,
        /// Left operand.
        lhs: LoweredValueId,
        /// Right operand.
        rhs: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Integer multiply.
    MulI {
        /// Operand/result type.
        ty: LoweredType,
        /// Left operand.
        lhs: LoweredValueId,
        /// Right operand.
        rhs: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Signed integer divide.
    DivS {
        /// Operand/result type.
        ty: LoweredType,
        /// Dividend.
        lhs: LoweredValueId,
        /// Divisor.
        rhs: LoweredValueId,
        /// Quotient.
        result: LoweredValueId,
    },
    /// Unsigned integer divide.
    DivU {
        /// Operand/result type.
        ty: LoweredType,
        /// Dividend.
        lhs: LoweredValueId,
        /// Divisor.
        rhs: LoweredValueId,
        /// Quotient.
        result: LoweredValueId,
    },
    /// Signed integer remainder.
    RemS {
        /// Operand/result type.
        ty: LoweredType,
        /// Dividend.
        lhs: LoweredValueId,
        /// Divisor.
        rhs: LoweredValueId,
        /// Remainder.
        result: LoweredValueId,
    },
    /// Unsigned integer remainder.
    RemU {
        /// Operand/result type.
        ty: LoweredType,
        /// Dividend.
        lhs: LoweredValueId,
        /// Divisor.
        rhs: LoweredValueId,
        /// Remainder.
        result: LoweredValueId,
    },

    // ---- Float family (lower_float) -------------------------------------
    /// IEEE-754 float add.
    AddF {
        /// Operand/result type (F32 or F64).
        ty: LoweredType,
        /// Left operand.
        lhs: LoweredValueId,
        /// Right operand.
        rhs: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// IEEE-754 float subtract.
    SubF {
        /// Operand/result type (F32 or F64).
        ty: LoweredType,
        /// Left operand.
        lhs: LoweredValueId,
        /// Right operand.
        rhs: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// IEEE-754 float multiply.
    MulF {
        /// Operand/result type (F32 or F64).
        ty: LoweredType,
        /// Left operand.
        lhs: LoweredValueId,
        /// Right operand.
        rhs: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// IEEE-754 float divide. PTX `div.rn.f32` rounding by default.
    DivF {
        /// Operand/result type (F32 or F64).
        ty: LoweredType,
        /// Dividend.
        lhs: LoweredValueId,
        /// Divisor.
        rhs: LoweredValueId,
        /// Quotient.
        result: LoweredValueId,
    },
    /// Fused multiply-add: `result = a*b + c` (single rounding).
    Fma {
        /// Operand/result type (F32 or F64).
        ty: LoweredType,
        /// Multiplicand A.
        a: LoweredValueId,
        /// Multiplicand B.
        b: LoweredValueId,
        /// Addend.
        c: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Float negation.
    FNeg {
        /// Operand/result type (F32 or F64).
        ty: LoweredType,
        /// Source.
        src: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Float absolute value.
    FAbs {
        /// Operand/result type (F32 or F64).
        ty: LoweredType,
        /// Source.
        src: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },

    // ---- Memory family (lower_memory) -----------------------------------
    /// Load from device pointer `base + offset`.
    Load {
        /// Loaded type.
        ty: LoweredType,
        /// Base device pointer.
        base: LoweredValueId,
        /// Byte offset (signed).
        offset: i32,
        /// Result.
        result: LoweredValueId,
    },
    /// Store `value` to device pointer `base + offset`.
    Store {
        /// Stored type.
        ty: LoweredType,
        /// Value to store.
        value: LoweredValueId,
        /// Base device pointer.
        base: LoweredValueId,
        /// Byte offset (signed).
        offset: i32,
    },
    /// SSA-local alloca for Cranelift stack slots. Lowered to registers
    /// by the downstream `mem2reg` pass.
    StackAlloc {
        /// Element type.
        ty: LoweredType,
        /// Size in bytes (for arrays); 0 means single-value.
        bytes: u32,
        /// SSA pointer to the alloca.
        result: LoweredValueId,
    },

    // ---- Control flow family (lower_cf) ---------------------------------
    /// Unconditional branch with block-arg passing.
    Br {
        /// Target block.
        target: LoweredBlockId,
        /// Values passed as block parameters to `target`.
        args: Vec<LoweredValueId>,
    },
    /// Conditional branch.
    CondBr {
        /// Boolean condition.
        cond: LoweredValueId,
        /// Target when `cond` is true.
        then_target: LoweredBlockId,
        /// Args passed to the then-target.
        then_args: Vec<LoweredValueId>,
        /// Target when `cond` is false.
        else_target: LoweredBlockId,
        /// Args passed to the else-target.
        else_args: Vec<LoweredValueId>,
    },
    /// Multi-way switch (Cranelift `br_table`).
    Switch {
        /// Value being switched on.
        value: LoweredValueId,
        /// Default target when no case matches.
        default_target: LoweredBlockId,
        /// `(case_value, target)` pairs.
        cases: Vec<(u32, LoweredBlockId)>,
    },
    /// Return from the function.
    Return {
        /// Values returned (matches signature length).
        values: Vec<LoweredValueId>,
    },

    // ---- Vector family (lower_vector) -----------------------------------
    /// Per-lane minimum.
    VMin {
        /// Per-lane type.
        lane_ty: LoweredType,
        /// Signedness of the comparison for INTEGER lanes.
        ///
        /// jit MED fix (finding 5): Cranelift's `smin` (signed) and `umin`
        /// (unsigned) previously both collapsed onto `VMin` with no
        /// signedness, so an unsigned min would lower to a signed PTX `min`
        /// (`min.s32` vs `min.u32`) — a miscompile for operands straddling
        /// the sign boundary. `signed` now carries the distinction.
        ///
        /// For FLOAT lanes (`lane_ty.is_float()`) this field is ignored
        /// (float min/max has no signedness); the lowering sets it to
        /// `true` for floats by convention.
        signed: bool,
        /// Left operand vector.
        lhs: LoweredValueId,
        /// Right operand vector.
        rhs: LoweredValueId,
        /// Result vector.
        result: LoweredValueId,
    },
    /// Per-lane maximum.
    VMax {
        /// Per-lane type.
        lane_ty: LoweredType,
        /// Signedness of the comparison for INTEGER lanes (see
        /// [`LoweredOp::VMin`] — jit MED fix finding 5). Ignored for float
        /// lanes.
        signed: bool,
        /// Left operand vector.
        lhs: LoweredValueId,
        /// Right operand vector.
        rhs: LoweredValueId,
        /// Result vector.
        result: LoweredValueId,
    },
    /// Broadcast scalar across all lanes.
    VSplat {
        /// Per-lane type.
        lane_ty: LoweredType,
        /// Source scalar.
        src: LoweredValueId,
        /// Result vector.
        result: LoweredValueId,
    },
    /// Per-lane select.
    VSelect {
        /// Per-lane type.
        lane_ty: LoweredType,
        /// Per-lane mask vector.
        cond: LoweredValueId,
        /// Value when mask lane is true.
        then_v: LoweredValueId,
        /// Value when mask lane is false.
        else_v: LoweredValueId,
        /// Result vector.
        result: LoweredValueId,
    },
    /// Warp-wide AND reduction (Cranelift `vall_true`).
    VAllTrue {
        /// Source mask vector.
        src: LoweredValueId,
        /// Boolean result.
        result: LoweredValueId,
    },
    /// Warp-wide OR reduction (Cranelift `vany_true`).
    VAnyTrue {
        /// Source mask vector.
        src: LoweredValueId,
        /// Boolean result.
        result: LoweredValueId,
    },

    // ---- Conversion family (lower_conv) ---------------------------------
    /// Three-operand select (Cranelift `select`). Maps to PTX `selp`.
    Select {
        /// Result type.
        ty: LoweredType,
        /// Boolean condition.
        cond: LoweredValueId,
        /// Value when true.
        then_v: LoweredValueId,
        /// Value when false.
        else_v: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Reinterpret bits. Free in PTX.
    Bitcast {
        /// Source type.
        from_ty: LoweredType,
        /// Destination type (same bit width).
        to_ty: LoweredType,
        /// Source.
        src: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Width-reducing integer truncation (Cranelift `ireduce`).
    TruncI {
        /// Source integer type (wider).
        from_ty: LoweredType,
        /// Destination integer type (narrower).
        to_ty: LoweredType,
        /// Source.
        src: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Zero-extending integer widening (Cranelift `uextend`).
    ExtendU {
        /// Source integer type (narrower).
        from_ty: LoweredType,
        /// Destination integer type (wider).
        to_ty: LoweredType,
        /// Source.
        src: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
    /// Sign-extending integer widening (Cranelift `sextend`).
    ExtendS {
        /// Source integer type (narrower).
        from_ty: LoweredType,
        /// Destination integer type (wider).
        to_ty: LoweredType,
        /// Source.
        src: LoweredValueId,
        /// Result.
        result: LoweredValueId,
    },
}

impl LoweredOp {
    /// Result value id if this op produces one. `None` for `Store`, `Br`,
    /// `CondBr`, `Switch`, `Return`.
    pub fn result(&self) -> Option<LoweredValueId> {
        match self {
            Self::AddI { result, .. }
            | Self::SubI { result, .. }
            | Self::MulI { result, .. }
            | Self::DivS { result, .. }
            | Self::DivU { result, .. }
            | Self::RemS { result, .. }
            | Self::RemU { result, .. }
            | Self::AddF { result, .. }
            | Self::SubF { result, .. }
            | Self::MulF { result, .. }
            | Self::DivF { result, .. }
            | Self::Fma { result, .. }
            | Self::FNeg { result, .. }
            | Self::FAbs { result, .. }
            | Self::Load { result, .. }
            | Self::StackAlloc { result, .. }
            | Self::VMin { result, .. }
            | Self::VMax { result, .. }
            | Self::VSplat { result, .. }
            | Self::VSelect { result, .. }
            | Self::VAllTrue { result, .. }
            | Self::VAnyTrue { result, .. }
            | Self::Select { result, .. }
            | Self::Bitcast { result, .. }
            | Self::TruncI { result, .. }
            | Self::ExtendU { result, .. }
            | Self::ExtendS { result, .. } => Some(*result),
            Self::Store { .. }
            | Self::Br { .. }
            | Self::CondBr { .. }
            | Self::Switch { .. }
            | Self::Return { .. } => None,
        }
    }

    /// True if this op transfers control (terminator).
    pub fn is_terminator(&self) -> bool {
        matches!(
            self,
            Self::Br { .. } | Self::CondBr { .. } | Self::Switch { .. } | Self::Return { .. }
        )
    }
}

/// Signature of a [`LoweredFunction`]: param and return types.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct LoweredSignature {
    /// Parameter types in order.
    pub params: Vec<LoweredType>,
    /// Return types (multi-return supported).
    pub returns: Vec<LoweredType>,
}

impl LoweredSignature {
    /// Construct an empty signature.
    pub fn new() -> Self {
        Self {
            params: Vec::new(),
            returns: Vec::new(),
        }
    }
}

impl Default for LoweredSignature {
    fn default() -> Self {
        Self::new()
    }
}

/// One basic block in a [`LoweredFunction`].
#[derive(Debug, Clone, PartialEq)]
pub struct LoweredBlock {
    /// Block identifier.
    pub id: LoweredBlockId,
    /// Block parameters: `(value_id, type)` pairs. Entry-block params come
    /// from the function signature.
    pub params: Vec<(LoweredValueId, LoweredType)>,
    /// Operations in order. The last op must be a terminator (see
    /// [`LoweredOp::is_terminator`]).
    pub ops: Vec<LoweredOp>,
}

impl LoweredBlock {
    /// Construct an empty block with the given id.
    pub fn new(id: LoweredBlockId) -> Self {
        Self {
            id,
            params: Vec::new(),
            ops: Vec::new(),
        }
    }

    /// True if the block ends in a terminator op.
    pub fn is_well_formed(&self) -> bool {
        self.ops
            .last()
            .map(LoweredOp::is_terminator)
            .unwrap_or(false)
    }
}

/// A complete lowered function, ready for the wave 3
/// `LoweredFunction -> pliron::Operation` converter.
#[derive(Debug, Clone, PartialEq)]
pub struct LoweredFunction {
    /// Function name. Used as the PTX entry symbol.
    pub name: String,
    /// Signature (param + return types).
    pub signature: LoweredSignature,
    /// Blocks in order. The entry block is identified by `entry`.
    pub blocks: Vec<LoweredBlock>,
    /// Entry block id.
    pub entry: LoweredBlockId,
}

impl LoweredFunction {
    /// Construct an empty function with the given name and signature.
    pub fn new(name: impl Into<String>, signature: LoweredSignature) -> Self {
        Self {
            name: name.into(),
            signature,
            blocks: Vec::new(),
            entry: 0,
        }
    }

    /// Lookup a block by id.
    pub fn block(&self, id: LoweredBlockId) -> Option<&LoweredBlock> {
        self.blocks.iter().find(|b| b.id == id)
    }

    /// True if every block ends in a terminator.
    pub fn is_well_formed(&self) -> bool {
        !self.blocks.is_empty() && self.blocks.iter().all(LoweredBlock::is_well_formed)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lowered_type_scalar_bits() {
        assert_eq!(LoweredType::I32.scalar_bits(), Some(32));
        assert_eq!(LoweredType::F64.scalar_bits(), Some(64));
        assert_eq!(LoweredType::Ptr.scalar_bits(), None);
        assert_eq!(LoweredType::Bool.scalar_bits(), None);
        assert_eq!(
            LoweredType::V128 {
                lane_type: Box::new(LoweredType::F32),
                lanes: 4,
            }
            .scalar_bits(),
            None
        );
    }

    #[test]
    fn lowered_type_classification() {
        assert!(LoweredType::I32.is_int());
        assert!(!LoweredType::I32.is_float());
        assert!(LoweredType::F32.is_float());
        assert!(!LoweredType::F32.is_int());
        assert!(LoweredType::V128 {
            lane_type: Box::new(LoweredType::F32),
            lanes: 4
        }
        .is_vector());
    }

    #[test]
    fn lowered_type_display() {
        assert_eq!(LoweredType::I32.to_string(), "i32");
        assert_eq!(LoweredType::F32.to_string(), "f32");
        assert_eq!(LoweredType::Ptr.to_string(), "ptr");
        let v = LoweredType::V128 {
            lane_type: Box::new(LoweredType::F32),
            lanes: 4,
        };
        assert_eq!(v.to_string(), "v4xf32");
    }

    #[test]
    fn op_result_arith() {
        let op = LoweredOp::AddI {
            ty: LoweredType::I32,
            lhs: 1,
            rhs: 2,
            result: 3,
        };
        assert_eq!(op.result(), Some(3));
        assert!(!op.is_terminator());
    }

    #[test]
    fn op_result_store() {
        let op = LoweredOp::Store {
            ty: LoweredType::I32,
            value: 1,
            base: 2,
            offset: 0,
        };
        assert_eq!(op.result(), None);
        assert!(!op.is_terminator());
    }

    #[test]
    fn op_is_terminator() {
        assert!(LoweredOp::Br {
            target: 0,
            args: vec![]
        }
        .is_terminator());
        assert!(LoweredOp::Return { values: vec![] }.is_terminator());
        assert!(LoweredOp::CondBr {
            cond: 0,
            then_target: 1,
            then_args: vec![],
            else_target: 2,
            else_args: vec![]
        }
        .is_terminator());
        assert!(LoweredOp::Switch {
            value: 0,
            default_target: 1,
            cases: vec![]
        }
        .is_terminator());
    }

    #[test]
    fn block_well_formed_requires_terminator() {
        let mut block = LoweredBlock::new(0);
        assert!(!block.is_well_formed());
        block.ops.push(LoweredOp::AddI {
            ty: LoweredType::I32,
            lhs: 1,
            rhs: 2,
            result: 3,
        });
        assert!(!block.is_well_formed());
        block.ops.push(LoweredOp::Return { values: vec![3] });
        assert!(block.is_well_formed());
    }

    #[test]
    fn function_lookup_and_well_formed() {
        let mut func = LoweredFunction::new(
            "kernel",
            LoweredSignature {
                params: vec![LoweredType::Ptr],
                returns: vec![],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        b0.ops.push(LoweredOp::Return { values: vec![] });
        func.blocks.push(b0);
        assert!(func.block(0).is_some());
        assert!(func.block(99).is_none());
        assert!(func.is_well_formed());
    }

    #[test]
    fn empty_function_not_well_formed() {
        let func = LoweredFunction::new("empty", LoweredSignature::default());
        assert!(!func.is_well_formed());
    }
}
