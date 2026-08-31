// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Lower Cranelift integer-arithmetic opcodes to [`crate::lowered_ir::LoweredOp`].
//!
//! This is the **arith family** of the wave-1 Pliron pipeline (RFC 0001
//! "Future possibilities"). It maps the seven binary integer ops listed in
//! the [Cranelift → dialect-mir mapping table](crate::pliron_dialect#mapping-table):
//!
//! | Cranelift opcode | `LoweredOp` variant |
//! |------------------|---------------------|
//! | `iadd`           | [`AddI`]            |
//! | `isub`           | [`SubI`]            |
//! | `imul`           | [`MulI`]            |
//! | `sdiv`           | [`DivS`]            |
//! | `udiv`           | [`DivU`]            |
//! | `srem`           | [`RemS`]            |
//! | `urem`           | [`RemU`]            |
//!
//! [`AddI`]: crate::lowered_ir::LoweredOp::AddI
//! [`SubI`]: crate::lowered_ir::LoweredOp::SubI
//! [`MulI`]: crate::lowered_ir::LoweredOp::MulI
//! [`DivS`]: crate::lowered_ir::LoweredOp::DivS
//! [`DivU`]: crate::lowered_ir::LoweredOp::DivU
//! [`RemS`]: crate::lowered_ir::LoweredOp::RemS
//! [`RemU`]: crate::lowered_ir::LoweredOp::RemU
//!
//! # Caller contract
//!
//! [`lower_arith_inst`] is a *family detector*: it returns `Some(LoweredOp)`
//! when the instruction is one of the seven ops it handles, and `None`
//! otherwise (including for unsupported integer widths such as `I128`).
//! The caller is responsible for invoking each `lower_*` family in turn
//! until one returns `Some`. Returning `None` does **not** mean rejection;
//! the caller's detector pass owns reject-on-unknown-op semantics.
//!
//! The caller threads in a mutable `value_map` (Cranelift `Value` →
//! [`LoweredValueId`]) and a mutable `next_id` allocator. Operand `Value`s
//! are looked up in the map; the result `Value` is allocated a fresh
//! [`LoweredValueId`] via `next_id` and inserted into the map. The wider
//! lowering pass is expected to populate the map for entry-block params
//! and other constants before this function runs.
//!
//! [`LoweredValueId`]: crate::lowered_ir::LoweredValueId

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::ir::{self, Function, Inst, Opcode, Value};

use crate::lowered_ir::{LoweredOp, LoweredType, LoweredValueId};

/// Lower a single Cranelift arith instruction to a [`LoweredOp`].
///
/// Returns:
///
/// - `Some(LoweredOp::{AddI|SubI|MulI|DivS|DivU|RemS|RemU})` when `inst`'s
///   opcode is one of `iadd`/`isub`/`imul`/`sdiv`/`udiv`/`srem`/`urem`
///   **and** the controlling type is one this family knows how to
///   represent in [`LoweredType`] (I8 / I16 / I32 / I64).
/// - `None` when the opcode is outside the arith family, when the type is
///   unsupported (e.g. `I128`), or when an operand or the result is
///   missing from `value_map` / cannot be allocated.
///
/// The caller is expected to chain `lower_arith_inst`, `lower_float_inst`,
/// `lower_memory_inst`, etc. until one returns `Some`. Returning `None` is
/// **not** an error: the detector pass owns the "unknown op → reject
/// kernel candidate" decision.
///
/// # Side effects
///
/// On success, the single Cranelift result `Value` is inserted into
/// `value_map` with a fresh `LoweredValueId` allocated from `next_id`
/// (which is incremented). On failure (return `None`) `value_map` and
/// `next_id` are left untouched.
///
/// # Panics
///
/// Does not panic. Missing operands in `value_map` are treated as a `None`
/// return rather than a panic — the caller's preflight should have
/// populated entry-block params before invoking the lowering.
pub fn lower_arith_inst(
    inst: Inst,
    func: &Function,
    value_map: &mut HashMap<Value, LoweredValueId>,
    next_id: &mut LoweredValueId,
) -> Option<LoweredOp> {
    let opcode = func.dfg.insts[inst].opcode();

    // Fast-reject anything that isn't one of the seven arith family ops
    // before we touch operand types or the value_map. The early return
    // here is what makes chained `lower_*` calls cheap.
    let kind = match opcode {
        Opcode::Iadd => ArithKind::AddI,
        Opcode::Isub => ArithKind::SubI,
        Opcode::Imul => ArithKind::MulI,
        Opcode::Sdiv => ArithKind::DivS,
        Opcode::Udiv => ArithKind::DivU,
        Opcode::Srem => ArithKind::RemS,
        Opcode::Urem => ArithKind::RemU,
        _ => return None,
    };

    // All seven ops are binary: two operands, one result, all the same
    // type. If Cranelift hands us something with a different arity the
    // mapping table is wrong and we'd rather fail closed than build a
    // malformed LoweredOp.
    let args = func.dfg.inst_args(inst);
    if args.len() != 2 {
        return None;
    }
    let results = func.dfg.inst_results(inst);
    if results.len() != 1 {
        return None;
    }

    let lhs_value = args[0];
    let rhs_value = args[1];
    let result_value = results[0];

    // The op's type is taken from the result. Cranelift guarantees the
    // operands match for these opcodes.
    let cl_ty = func.dfg.value_type(result_value);
    let ty = cranelift_int_type_to_lowered(cl_ty)?;

    // Operand lookup. If either operand is missing from the map the
    // wider lowering pass is not yet ready for this instruction; report
    // `None` so the caller can either bail or populate the map and retry.
    let lhs = *value_map.get(&lhs_value)?;
    let rhs = *value_map.get(&rhs_value)?;

    // Allocate the result id only after every fallible step has
    // succeeded, so a partial failure leaves `next_id` / `value_map`
    // consistent with "this call returned `None`".
    let result = *next_id;
    *next_id = next_id.checked_add(1)?;
    value_map.insert(result_value, result);

    Some(match kind {
        ArithKind::AddI => LoweredOp::AddI {
            ty,
            lhs,
            rhs,
            result,
        },
        ArithKind::SubI => LoweredOp::SubI {
            ty,
            lhs,
            rhs,
            result,
        },
        ArithKind::MulI => LoweredOp::MulI {
            ty,
            lhs,
            rhs,
            result,
        },
        ArithKind::DivS => LoweredOp::DivS {
            ty,
            lhs,
            rhs,
            result,
        },
        ArithKind::DivU => LoweredOp::DivU {
            ty,
            lhs,
            rhs,
            result,
        },
        ArithKind::RemS => LoweredOp::RemS {
            ty,
            lhs,
            rhs,
            result,
        },
        ArithKind::RemU => LoweredOp::RemU {
            ty,
            lhs,
            rhs,
            result,
        },
    })
}

/// Internal tag for the seven opcodes this family handles. Used only as a
/// dispatch helper inside [`lower_arith_inst`] so the opcode → variant
/// match and the operand-extraction match aren't duplicated.
enum ArithKind {
    AddI,
    SubI,
    MulI,
    DivS,
    DivU,
    RemS,
    RemU,
}

/// Convert a Cranelift integer [`ir::Type`] to a [`LoweredType`].
///
/// Returns `None` for non-integer types and for integer types outside the
/// `I8..=I64` range that [`LoweredType`] models (notably `I128`, which
/// PTX cannot natively express — see the "Unsupported in v0.4" notes in
/// [`crate::pliron_dialect`]).
fn cranelift_int_type_to_lowered(ty: ir::Type) -> Option<LoweredType> {
    if ty == ir::types::I8 {
        Some(LoweredType::I8)
    } else if ty == ir::types::I16 {
        Some(LoweredType::I16)
    } else if ty == ir::types::I32 {
        Some(LoweredType::I32)
    } else if ty == ir::types::I64 {
        Some(LoweredType::I64)
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::cursor::{Cursor, FuncCursor};
    use cranelift_codegen::ir::types::{I128, I16, I32, I64, I8};
    use cranelift_codegen::ir::{Function, InstBuilder};

    /// Build a one-block, one-instruction Cranelift function with a binary
    /// integer op of the given opcode and type.
    ///
    /// Returns `(func, inst, lhs_val, rhs_val)` so callers can pre-populate
    /// the value-map with the block-param operands.
    fn fixture_with_binary_op(opcode: Opcode, ty: ir::Type) -> (Function, Inst, Value, Value) {
        let mut func = Function::new();
        let block = func.dfg.make_block();
        let lhs = func.dfg.append_block_param(block, ty);
        let rhs = func.dfg.append_block_param(block, ty);
        let mut pos = FuncCursor::new(&mut func);
        pos.insert_block(block);
        let inst = match opcode {
            Opcode::Iadd => pos.ins().iadd(lhs, rhs),
            Opcode::Isub => pos.ins().isub(lhs, rhs),
            Opcode::Imul => pos.ins().imul(lhs, rhs),
            Opcode::Sdiv => pos.ins().sdiv(lhs, rhs),
            Opcode::Udiv => pos.ins().udiv(lhs, rhs),
            Opcode::Srem => pos.ins().srem(lhs, rhs),
            Opcode::Urem => pos.ins().urem(lhs, rhs),
            other => panic!("fixture_with_binary_op: opcode {other:?} not in arith family"),
        };
        // `.ins().iadd(..)` returns the result Value; we want the Inst.
        let inst = pos.func.dfg.value_def(inst).unwrap_inst();
        (func, inst, lhs, rhs)
    }

    /// Seed the value-map with the two block params so `lower_arith_inst`
    /// can resolve operands. Returns `(value_map, next_id)`.
    fn seed_map(lhs: Value, rhs: Value) -> (HashMap<Value, LoweredValueId>, LoweredValueId) {
        let mut map = HashMap::new();
        map.insert(lhs, 0);
        map.insert(rhs, 1);
        (map, 2)
    }

    #[test]
    fn lowers_iadd_i32() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Iadd, I32);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let op = lower_arith_inst(inst, &func, &mut map, &mut next_id).expect("iadd lowered");
        match op {
            LoweredOp::AddI {
                ty,
                lhs,
                rhs,
                result,
            } => {
                assert_eq!(ty, LoweredType::I32);
                assert_eq!(lhs, 0);
                assert_eq!(rhs, 1);
                assert_eq!(result, 2);
            }
            other => panic!("expected AddI, got {other:?}"),
        }
        assert_eq!(next_id, 3, "result-id allocator advanced once");
        // Result Value must be in the map.
        let result_value = func.dfg.inst_results(inst)[0];
        assert_eq!(map.get(&result_value), Some(&2));
    }

    #[test]
    fn lowers_isub_i64() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Isub, I64);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let op = lower_arith_inst(inst, &func, &mut map, &mut next_id).expect("isub lowered");
        assert!(matches!(
            op,
            LoweredOp::SubI {
                ty: LoweredType::I64,
                lhs: 0,
                rhs: 1,
                result: 2
            }
        ));
    }

    #[test]
    fn lowers_imul_i16() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Imul, I16);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let op = lower_arith_inst(inst, &func, &mut map, &mut next_id).expect("imul lowered");
        assert!(matches!(
            op,
            LoweredOp::MulI {
                ty: LoweredType::I16,
                lhs: 0,
                rhs: 1,
                result: 2
            }
        ));
    }

    #[test]
    fn lowers_sdiv_i32() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Sdiv, I32);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let op = lower_arith_inst(inst, &func, &mut map, &mut next_id).expect("sdiv lowered");
        assert!(matches!(
            op,
            LoweredOp::DivS {
                ty: LoweredType::I32,
                lhs: 0,
                rhs: 1,
                result: 2
            }
        ));
    }

    #[test]
    fn lowers_udiv_i64() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Udiv, I64);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let op = lower_arith_inst(inst, &func, &mut map, &mut next_id).expect("udiv lowered");
        assert!(matches!(
            op,
            LoweredOp::DivU {
                ty: LoweredType::I64,
                lhs: 0,
                rhs: 1,
                result: 2
            }
        ));
    }

    #[test]
    fn lowers_srem_i8() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Srem, I8);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let op = lower_arith_inst(inst, &func, &mut map, &mut next_id).expect("srem lowered");
        assert!(matches!(
            op,
            LoweredOp::RemS {
                ty: LoweredType::I8,
                lhs: 0,
                rhs: 1,
                result: 2
            }
        ));
    }

    #[test]
    fn lowers_urem_i32() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Urem, I32);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let op = lower_arith_inst(inst, &func, &mut map, &mut next_id).expect("urem lowered");
        assert!(matches!(
            op,
            LoweredOp::RemU {
                ty: LoweredType::I32,
                lhs: 0,
                rhs: 1,
                result: 2
            }
        ));
    }

    /// Non-arith opcodes must return `None` cleanly without mutating
    /// `value_map` or `next_id`. Use `Iconst` (BinaryImm64-shape) and
    /// `Return` (no-result terminator) as out-of-family stand-ins.
    #[test]
    fn returns_none_for_non_arith_opcode() {
        let mut func = Function::new();
        let block = func.dfg.make_block();
        let mut pos = FuncCursor::new(&mut func);
        pos.insert_block(block);
        // `iconst` is a unary-imm op, not in the arith family.
        let v = pos.ins().iconst(I32, 7);
        let iconst_inst = pos.func.dfg.value_def(v).unwrap_inst();

        let mut map = HashMap::new();
        let mut next_id: LoweredValueId = 0;
        assert!(lower_arith_inst(iconst_inst, &func, &mut map, &mut next_id).is_none());
        assert_eq!(next_id, 0, "next_id untouched on miss");
        assert!(map.is_empty(), "value_map untouched on miss");
    }

    /// I128 is in Cranelift's type system but outside [`LoweredType`]'s
    /// integer range (PTX has no native i128). Must return `None` and
    /// leave the allocator untouched.
    #[test]
    fn returns_none_for_i128() {
        let (func, inst, lhs, rhs) = fixture_with_binary_op(Opcode::Iadd, I128);
        let (mut map, mut next_id) = seed_map(lhs, rhs);
        let before_next = next_id;
        let before_len = map.len();
        assert!(lower_arith_inst(inst, &func, &mut map, &mut next_id).is_none());
        assert_eq!(
            next_id, before_next,
            "next_id untouched on unsupported type"
        );
        assert_eq!(
            map.len(),
            before_len,
            "value_map untouched on unsupported type"
        );
    }

    /// If an operand is missing from `value_map`, the lowering must
    /// return `None` and leave the allocator untouched (so the caller
    /// can decide to populate the map and retry, or bail entirely).
    #[test]
    fn returns_none_when_operand_missing_from_map() {
        let (func, inst, _lhs, rhs) = fixture_with_binary_op(Opcode::Iadd, I32);
        let mut map = HashMap::new();
        // Only insert the rhs; lhs is missing.
        map.insert(rhs, 9);
        let mut next_id: LoweredValueId = 10;
        assert!(lower_arith_inst(inst, &func, &mut map, &mut next_id).is_none());
        assert_eq!(next_id, 10);
        assert_eq!(map.len(), 1);
    }

    /// `lower_arith_inst` must allocate fresh ids in sequence when called
    /// repeatedly. Locks in the contract that the caller's `next_id`
    /// counter is the single source of truth for SSA renumbering.
    #[test]
    fn allocates_sequential_ids_across_calls() {
        let (func_a, inst_a, lhs_a, rhs_a) = fixture_with_binary_op(Opcode::Iadd, I32);
        let (func_b, inst_b, lhs_b, rhs_b) = fixture_with_binary_op(Opcode::Imul, I32);

        let mut map_a = HashMap::new();
        map_a.insert(lhs_a, 0);
        map_a.insert(rhs_a, 1);
        let mut next_id: LoweredValueId = 2;

        let op_a = lower_arith_inst(inst_a, &func_a, &mut map_a, &mut next_id).unwrap();
        assert_eq!(op_a.result(), Some(2));
        assert_eq!(next_id, 3);

        // Re-use the same allocator across a different function — the
        // ids must remain unique even when the value-map resets.
        let mut map_b = HashMap::new();
        map_b.insert(lhs_b, 100);
        map_b.insert(rhs_b, 101);

        let op_b = lower_arith_inst(inst_b, &func_b, &mut map_b, &mut next_id).unwrap();
        assert_eq!(op_b.result(), Some(3));
        assert_eq!(next_id, 4);
    }
}
