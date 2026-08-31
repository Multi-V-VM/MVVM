// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Conversion-family lowering (wave-1 L6).
//!
//! Lowers the *conversion* row of the [Cranelift → dialect-mir mapping
//! table](crate::pliron_dialect#mapping-table) into
//! [`crate::lowered_ir::LoweredOp`] variants. The family covers Cranelift
//! opcodes that change a value's type (extend / truncate / bitcast) plus the
//! three-operand [`Opcode::Select`] which the dialect-mir table also groups
//! under `arith.*`.
//!
//! | Cranelift opcode | `LoweredOp` variant       | PTX target |
//! |------------------|---------------------------|------------|
//! | `select`         | [`LoweredOp::Select`]     | `selp` |
//! | `bitcast`        | [`LoweredOp::Bitcast`]    | (free — reinterpret) |
//! | `ireduce`        | [`LoweredOp::TruncI`]     | `cvt.<narrow>.<wide>` |
//! | `uextend`        | [`LoweredOp::ExtendU`]    | `cvt.u<wide>.u<narrow>` |
//! | `sextend`        | [`LoweredOp::ExtendS`]    | `cvt.s<wide>.s<narrow>` |
//!
//! The dialect-mir mapping table calls the truncation `arith.trunci` and
//! lists "breduce" as the source opcode; in `cranelift-codegen` 0.111 the
//! corresponding opcode is [`Opcode::Ireduce`], and that is what this module
//! recognises.
//!
//! The entry point is [`lower_conv_inst`]; it returns `None` for
//! instructions outside this family so the dispatching pass can try the
//! next family in turn.

#![cfg(feature = "cuda-oxide-backend")]
#![allow(dead_code)]

use std::collections::HashMap;

use cranelift_codegen::ir::{self, Function, Inst, Opcode, Value};

use crate::lowered_ir::{LoweredOp, LoweredType, LoweredValueId};

/// Map a Cranelift scalar/`Bool`/`Ptr` type to its [`LoweredType`] mirror.
///
/// Returns `None` for vector types and any scalar width outside the set
/// PTX can represent (I128, F16, F128, reference types). The conversion
/// family only handles scalars + booleans, so vectors are rejected at the
/// op-level lowering rather than here.
fn cranelift_type_to_lowered(ty: ir::Type) -> Option<LoweredType> {
    use cranelift_codegen::ir::types;
    match ty {
        types::I8 => Some(LoweredType::I8),
        types::I16 => Some(LoweredType::I16),
        types::I32 => Some(LoweredType::I32),
        types::I64 => Some(LoweredType::I64),
        types::F32 => Some(LoweredType::F32),
        types::F64 => Some(LoweredType::F64),
        _ => None,
    }
}

/// Resolve the [`LoweredValueId`] for a Cranelift [`Value`].
///
/// Returns `None` when the value has not been seen by the surrounding
/// lowering walk yet. The wave-1 driver guarantees a topological walk so
/// every operand is registered before its consumer; per-family helpers
/// surface a `None` instead of panicking so the driver can decide whether
/// to abort or defer.
fn lookup_value(value_map: &HashMap<Value, LoweredValueId>, v: Value) -> Option<LoweredValueId> {
    value_map.get(&v).copied()
}

/// Allocate a fresh [`LoweredValueId`] and advance the cursor.
///
/// jit LOW fix (finding 7): standardize on `checked_add(1)?` (the
/// `lower_arith` idiom). `saturating_add` previously capped at `u32::MAX`,
/// which silently aliased two distinct SSA values onto the same id once the
/// counter saturated — a miscompile. Overflow now returns `None`, which the
/// caller `?`-propagates into a structured lowering miss.
fn alloc_result(next_value_id: &mut LoweredValueId) -> Option<LoweredValueId> {
    let id = *next_value_id;
    *next_value_id = next_value_id.checked_add(1)?;
    Some(id)
}

/// Lower a single Cranelift instruction to a conversion-family
/// [`LoweredOp`], or return `None` if the opcode is not in this family.
///
/// # Recognised opcodes
///
/// - [`Opcode::Select`] → [`LoweredOp::Select`] (three-operand, PTX `selp`).
/// - [`Opcode::Bitcast`] → [`LoweredOp::Bitcast`] (reinterpret bits).
/// - [`Opcode::Ireduce`] → [`LoweredOp::TruncI`] (width-reducing integer
///   truncation; the dialect-mir mapping calls this `arith.trunci`).
/// - [`Opcode::Uextend`] → [`LoweredOp::ExtendU`] (zero-extending widening).
/// - [`Opcode::Sextend`] → [`LoweredOp::ExtendS`] (sign-extending widening).
///
/// # Returns
///
/// - `Some(op)` if `inst`'s opcode is one of the above **and** all the
///   operand / result Cranelift types map cleanly into [`LoweredType`]
///   scalars (vectors and unsupported widths are filtered out by
///   returning `None`).
/// - `None` if the opcode is outside this family — the driver should try
///   the next per-family lowering.
/// - `None` if an opcode *is* in this family but its operand resolution
///   fails (unmapped `Value` in `value_map`, vector operands, etc.); the
///   driver treats this as "wave-1 cannot lower this", same as a hard
///   `UnsupportedOp` at the dialect-mir layer.
///
/// # Parameters
///
/// * `inst` — the Cranelift instruction to lower.
/// * `func` — the surrounding function (for `dfg.value_type` /
///   `dfg.inst_args` / `dfg.inst_results` queries).
/// * `value_map` — map from already-walked Cranelift [`Value`]s to their
///   [`LoweredValueId`] in the interim IR.
/// * `next_value_id` — cursor for allocating fresh result ids; advanced
///   in-place when a new result is produced.
pub fn lower_conv_inst(
    inst: Inst,
    func: &Function,
    value_map: &HashMap<Value, LoweredValueId>,
    next_value_id: &mut LoweredValueId,
) -> Option<LoweredOp> {
    let opcode = func.dfg.insts[inst].opcode();
    let args = func.dfg.inst_args(inst);
    let results = func.dfg.inst_results(inst);

    match opcode {
        Opcode::Select => {
            // Cranelift `select` operand order is `[c, x, y]`:
            // c = condition, x = then-value, y = else-value.
            if args.len() != 3 || results.len() != 1 {
                return None;
            }
            let cond_v = args[0];
            let then_v = args[1];
            let else_v = args[2];
            let result_v = results[0];

            // Result type = then/else type (Cranelift verifier guarantees
            // x and y match).
            let ty = cranelift_type_to_lowered(func.dfg.value_type(result_v))?;

            let cond = lookup_value(value_map, cond_v)?;
            let then_id = lookup_value(value_map, then_v)?;
            let else_id = lookup_value(value_map, else_v)?;
            let result = alloc_result(next_value_id)?;

            Some(LoweredOp::Select {
                ty,
                cond,
                then_v: then_id,
                else_v: else_id,
                result,
            })
        }
        Opcode::Bitcast => {
            if args.len() != 1 || results.len() != 1 {
                return None;
            }
            let src_v = args[0];
            let result_v = results[0];

            let from_ty = cranelift_type_to_lowered(func.dfg.value_type(src_v))?;
            let to_ty = cranelift_type_to_lowered(func.dfg.value_type(result_v))?;

            let src = lookup_value(value_map, src_v)?;
            let result = alloc_result(next_value_id)?;

            Some(LoweredOp::Bitcast {
                from_ty,
                to_ty,
                src,
                result,
            })
        }
        Opcode::Ireduce => {
            // Width-reducing integer truncation: wide → narrow.
            if args.len() != 1 || results.len() != 1 {
                return None;
            }
            let src_v = args[0];
            let result_v = results[0];

            let from_ty = cranelift_type_to_lowered(func.dfg.value_type(src_v))?;
            let to_ty = cranelift_type_to_lowered(func.dfg.value_type(result_v))?;
            // Defensive: both must be ints (Cranelift verifier guarantees
            // this, but cheap to assert).
            if !from_ty.is_int() || !to_ty.is_int() {
                return None;
            }

            let src = lookup_value(value_map, src_v)?;
            let result = alloc_result(next_value_id)?;

            Some(LoweredOp::TruncI {
                from_ty,
                to_ty,
                src,
                result,
            })
        }
        Opcode::Uextend => {
            // Zero-extending integer widening: narrow → wide.
            if args.len() != 1 || results.len() != 1 {
                return None;
            }
            let src_v = args[0];
            let result_v = results[0];

            let from_ty = cranelift_type_to_lowered(func.dfg.value_type(src_v))?;
            let to_ty = cranelift_type_to_lowered(func.dfg.value_type(result_v))?;
            if !from_ty.is_int() || !to_ty.is_int() {
                return None;
            }

            let src = lookup_value(value_map, src_v)?;
            let result = alloc_result(next_value_id)?;

            Some(LoweredOp::ExtendU {
                from_ty,
                to_ty,
                src,
                result,
            })
        }
        Opcode::Sextend => {
            // Sign-extending integer widening: narrow → wide.
            if args.len() != 1 || results.len() != 1 {
                return None;
            }
            let src_v = args[0];
            let result_v = results[0];

            let from_ty = cranelift_type_to_lowered(func.dfg.value_type(src_v))?;
            let to_ty = cranelift_type_to_lowered(func.dfg.value_type(result_v))?;
            if !from_ty.is_int() || !to_ty.is_int() {
                return None;
            }

            let src = lookup_value(value_map, src_v)?;
            let result = alloc_result(next_value_id)?;

            Some(LoweredOp::ExtendS {
                from_ty,
                to_ty,
                src,
                result,
            })
        }
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use cranelift_codegen::cursor::{Cursor, FuncCursor};
    use cranelift_codegen::ir::types::{F32, I16, I32, I64, I8};
    use cranelift_codegen::ir::{Function, InstBuilder, MemFlags};

    /// Build a tiny function with one entry block, append the instruction
    /// constructed by `build`, and run the lowering on its sole non-iconst
    /// instruction. Returns `(LoweredOp, value_map_size_used)` for the
    /// caller to inspect.
    ///
    /// `param_types` describes block-parameter types that `build` may
    /// consume as Cranelift `Value`s; each param is pre-registered in the
    /// `value_map` with the corresponding `LoweredValueId` starting at 0.
    fn run_lower<F>(param_types: &[ir::Type], build: F) -> Option<LoweredOp>
    where
        F: FnOnce(&mut FuncCursor<'_>, &[ir::Value]) -> Inst,
    {
        let mut func = Function::new();
        let block = func.dfg.make_block();
        let mut params = Vec::with_capacity(param_types.len());
        for ty in param_types {
            params.push(func.dfg.append_block_param(block, *ty));
        }

        let mut value_map: HashMap<Value, LoweredValueId> = HashMap::new();
        for (i, v) in params.iter().enumerate() {
            value_map.insert(*v, i as LoweredValueId);
        }
        let mut next_value_id: LoweredValueId = params.len() as LoweredValueId;

        let inst = {
            let mut cur = FuncCursor::new(&mut func);
            cur.insert_block(block);
            build(&mut cur, &params)
        };

        lower_conv_inst(inst, &func, &value_map, &mut next_value_id)
    }

    #[test]
    fn select_lowers_to_loweredop_select() {
        // select i32: result = cond ? a : b
        let op = run_lower(&[I8, I32, I32], |cur, params| {
            let cond = params[0];
            let then_v = params[1];
            let else_v = params[2];
            cur.ins().select(cond, then_v, else_v);
            // Grab the last inserted instruction via the layout.
            cur.current_block()
                .and_then(|b| cur.func.layout.last_inst(b))
                .expect("instruction was appended")
        })
        .expect("select must lower");
        match op {
            LoweredOp::Select {
                ty,
                cond,
                then_v,
                else_v,
                result,
            } => {
                assert_eq!(ty, LoweredType::I32);
                assert_eq!(cond, 0);
                assert_eq!(then_v, 1);
                assert_eq!(else_v, 2);
                assert_eq!(result, 3);
            }
            other => panic!("expected LoweredOp::Select, got {other:?}"),
        }
    }

    #[test]
    fn bitcast_lowers_to_loweredop_bitcast() {
        // bitcast i32 -> f32
        let op = run_lower(&[I32], |cur, params| {
            cur.ins().bitcast(F32, MemFlags::new(), params[0]);
            cur.current_block()
                .and_then(|b| cur.func.layout.last_inst(b))
                .expect("instruction was appended")
        })
        .expect("bitcast must lower");
        match op {
            LoweredOp::Bitcast {
                from_ty,
                to_ty,
                src,
                result,
            } => {
                assert_eq!(from_ty, LoweredType::I32);
                assert_eq!(to_ty, LoweredType::F32);
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected LoweredOp::Bitcast, got {other:?}"),
        }
    }

    #[test]
    fn ireduce_lowers_to_loweredop_trunci() {
        // ireduce i64 -> i16
        let op = run_lower(&[I64], |cur, params| {
            cur.ins().ireduce(I16, params[0]);
            cur.current_block()
                .and_then(|b| cur.func.layout.last_inst(b))
                .expect("instruction was appended")
        })
        .expect("ireduce must lower");
        match op {
            LoweredOp::TruncI {
                from_ty,
                to_ty,
                src,
                result,
            } => {
                assert_eq!(from_ty, LoweredType::I64);
                assert_eq!(to_ty, LoweredType::I16);
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected LoweredOp::TruncI, got {other:?}"),
        }
    }

    #[test]
    fn uextend_lowers_to_loweredop_extendu() {
        // uextend i8 -> i32
        let op = run_lower(&[I8], |cur, params| {
            cur.ins().uextend(I32, params[0]);
            cur.current_block()
                .and_then(|b| cur.func.layout.last_inst(b))
                .expect("instruction was appended")
        })
        .expect("uextend must lower");
        match op {
            LoweredOp::ExtendU {
                from_ty,
                to_ty,
                src,
                result,
            } => {
                assert_eq!(from_ty, LoweredType::I8);
                assert_eq!(to_ty, LoweredType::I32);
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected LoweredOp::ExtendU, got {other:?}"),
        }
    }

    #[test]
    fn sextend_lowers_to_loweredop_extends() {
        // sextend i16 -> i64
        let op = run_lower(&[I16], |cur, params| {
            cur.ins().sextend(I64, params[0]);
            cur.current_block()
                .and_then(|b| cur.func.layout.last_inst(b))
                .expect("instruction was appended")
        })
        .expect("sextend must lower");
        match op {
            LoweredOp::ExtendS {
                from_ty,
                to_ty,
                src,
                result,
            } => {
                assert_eq!(from_ty, LoweredType::I16);
                assert_eq!(to_ty, LoweredType::I64);
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected LoweredOp::ExtendS, got {other:?}"),
        }
    }

    #[test]
    fn non_conv_opcode_returns_none() {
        // iadd is in the arith family, not conv — must be passed through.
        let op = run_lower(&[I32, I32], |cur, params| {
            cur.ins().iadd(params[0], params[1]);
            cur.current_block()
                .and_then(|b| cur.func.layout.last_inst(b))
                .expect("instruction was appended")
        });
        assert!(op.is_none(), "lower_conv_inst should ignore non-conv ops");
    }

    #[test]
    fn unmapped_operand_returns_none() {
        // Build a uextend whose operand is NOT in value_map; the lowering
        // must surface a `None` rather than panicking.
        let mut func = Function::new();
        let block = func.dfg.make_block();
        let v_in = func.dfg.append_block_param(block, I8);

        let inst = {
            let mut cur = FuncCursor::new(&mut func);
            cur.insert_block(block);
            cur.ins().uextend(I32, v_in);
            cur.current_block()
                .and_then(|b| cur.func.layout.last_inst(b))
                .expect("instruction was appended")
        };

        let value_map: HashMap<Value, LoweredValueId> = HashMap::new(); // intentionally empty
        let mut next_value_id: LoweredValueId = 0;
        let op = lower_conv_inst(inst, &func, &value_map, &mut next_value_id);
        assert!(op.is_none(), "unmapped operand must surface as None");
        // Result id cursor must not advance on failure.
        assert_eq!(next_value_id, 0);
    }
}
