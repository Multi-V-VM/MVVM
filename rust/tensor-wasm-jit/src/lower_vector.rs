// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Vector / SIMD lowering family (wave 1, L5).
//!
//! Lowers the SIMD subset of Cranelift IR that the Pliron
//! [`vector` dialect](crate::pliron_dialect#mapping-table) covers to the
//! pure-Rust [`crate::lowered_ir::LoweredOp`] interim IR. The implementation
//! is intentionally narrow: only the opcodes present in the
//! `cranelift-codegen` version pinned by the workspace (`0.111`) and listed
//! in the wave-1 spec are handled. Every other opcode returns `None` so the
//! caller can fall back to its scalar/control-flow family.
//!
//! # Opcodes handled
//!
//! The mapping below mirrors the [pliron mapping
//! table](crate::pliron_dialect#mapping-table) row-for-row, restricted to
//! the entries Cranelift 0.111 actually exposes:
//!
//! | Cranelift opcode | Operand shape       | Lowered variant                         |
//! |------------------|---------------------|-----------------------------------------|
//! | `fmin`           | `Binary` (vector)   | [`LoweredOp::VMin`]                     |
//! | `fmax`           | `Binary` (vector)   | [`LoweredOp::VMax`]                     |
//! | `smin`           | `Binary` (vector)   | [`LoweredOp::VMin`]                     |
//! | `smax`           | `Binary` (vector)   | [`LoweredOp::VMax`]                     |
//! | `umin`           | `Binary` (vector)   | [`LoweredOp::VMin`]                     |
//! | `umax`           | `Binary` (vector)   | [`LoweredOp::VMax`]                     |
//! | `splat`          | `Unary`             | [`LoweredOp::VSplat`]                   |
//! | `bitselect`      | `Ternary` (vector)  | [`LoweredOp::VSelect`]                  |
//! | `vall_true`      | `Unary`             | [`LoweredOp::VAllTrue`]                 |
//! | `vany_true`      | `Unary`             | [`LoweredOp::VAnyTrue`]                 |
//!
//! # Opcodes deliberately *not* handled here
//!
//! - `imin_s` / `imin_u` / `imax_s` / `imax_u` — Cranelift 0.111 names these
//!   `smin`/`umin`/`smax`/`umax`. The hyphenated aliases mentioned in the
//!   wave-1 brief don't exist in the pinned version.
//! - `vselect` — Cranelift 0.111 has no `vselect` opcode. The Wasm
//!   `v128.bitselect` lowers to Cranelift `bitselect` (which can operate
//!   on either scalars or vectors); we treat the vector form as the wave-1
//!   `VSelect`. The scalar form is `Opcode::Bitselect` with non-vector
//!   types and is routed to the conversion family by returning `None`.
//! - Scalar `fmin`/`fmax`/`smin`/`umin`/`smax`/`umax` — these are not in
//!   the vector family. We explicitly return `None` for the scalar case so
//!   the caller can route to [`crate::lower_arith`] /
//!   [`crate::lower_float`].

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::ir::{self, InstructionData, Opcode};

use crate::lowered_ir::{LoweredOp, LoweredType, LoweredValueId};

/// Convert a Cranelift scalar `Type` (`I8`/`I16`/`I32`/`I64`/`F32`/`F64`)
/// to the matching [`LoweredType`].
///
/// Returns `None` for any non-scalar input (vectors, `Bool`, references,
/// `I128`, `F16`, `F128`). The caller is expected to have already extracted
/// the lane type via [`ir::Type::lane_type`] before calling this helper.
fn lane_to_lowered(ty: ir::Type) -> Option<LoweredType> {
    Some(match ty {
        ir::types::I8 => LoweredType::I8,
        ir::types::I16 => LoweredType::I16,
        ir::types::I32 => LoweredType::I32,
        ir::types::I64 => LoweredType::I64,
        ir::types::F32 => LoweredType::F32,
        ir::types::F64 => LoweredType::F64,
        _ => return None,
    })
}

/// Build the per-lane [`LoweredType`] for a Cranelift vector `Type`.
///
/// Returns `None` if `ty` is not a (fixed-width) SIMD vector or its lane
/// type isn't in the wave-1 supported set.
fn vector_lane_type(ty: ir::Type) -> Option<LoweredType> {
    if !ty.is_vector() {
        return None;
    }
    lane_to_lowered(ty.lane_type())
}

/// Allocate a fresh [`LoweredValueId`] from the bump counter.
///
/// The counter is the caller's monotonic id source — the same one used by
/// the sibling `lower_*` families so SSA ids stay unique inside the
/// resulting [`crate::lowered_ir::LoweredFunction`].
///
/// jit LOW fix (finding 7): returns `None` on counter overflow rather than
/// panicking (`.expect(...)`). All `lower_*` families now standardize on
/// `checked_add(1)?` so a single function exceeding `u32::MAX` SSA values
/// surfaces as a structured lowering miss (the caller propagates `None`,
/// which the driver maps to a `LoweringError`) instead of crashing the
/// process.
fn fresh_id(next: &mut LoweredValueId) -> Option<LoweredValueId> {
    let id = *next;
    *next = next.checked_add(1)?;
    Some(id)
}

/// Resolve a Cranelift [`ir::Value`] to its [`LoweredValueId`].
///
/// Returns `None` if the operand has not yet been lowered — that's
/// definitely a bug in the caller (operands of an instruction must already
/// be in scope), so callers typically `?`-propagate this back as "skip
/// this instruction".
fn map_value(
    value_map: &HashMap<ir::Value, LoweredValueId>,
    v: ir::Value,
) -> Option<LoweredValueId> {
    value_map.get(&v).copied()
}

/// Lower a single vector-family Cranelift instruction to a
/// [`LoweredOp`].
///
/// Returns `None` if `inst` is not one of the opcodes handled by this
/// family (see the [module-level table](self#opcodes-handled)) **or** if
/// it is one of those opcodes but applied to a scalar type (e.g. scalar
/// `fmin`). In both cases the caller is expected to route the
/// instruction to the appropriate sibling family.
///
/// The function:
///
/// 1. Inspects `func.dfg.insts[inst]` to read the opcode and operands.
/// 2. Maps each Cranelift `Value` operand through `value_map` to its
///    pre-assigned [`LoweredValueId`].
/// 3. Allocates a fresh result id from `next_value_id` and constructs the
///    matching [`LoweredOp`] variant.
///
/// `value_map` is **not** mutated here — the caller is responsible for
/// inserting the new instruction's `result` after the returned op is
/// pushed into a block (the caller has the Cranelift `Value` for the
/// result; we only see the id we allocated).
pub fn lower_vector_inst(
    inst: ir::Inst,
    func: &ir::Function,
    value_map: &HashMap<ir::Value, LoweredValueId>,
    next_value_id: &mut LoweredValueId,
) -> Option<LoweredOp> {
    let data = &func.dfg.insts[inst];
    match data {
        // ---- Binary min/max ------------------------------------------------
        InstructionData::Binary { opcode, args } => {
            // The result type (== operand type for these ops) tells us
            // whether to treat it as a vector op. Scalar fmin/fmax/etc.
            // are handled by lower_arith / lower_float.
            let result_ty = func.dfg.value_type(func.dfg.first_result(inst));
            let lane_ty = vector_lane_type(result_ty)?;
            let lhs = map_value(value_map, args[0])?;
            let rhs = map_value(value_map, args[1])?;
            let result = fresh_id(next_value_id)?;
            // jit MED fix (finding 5): carry signedness so `umin`/`umax`
            // are not silently lowered as signed `min`/`max`. Float
            // min/max have no signedness; `signed = true` by convention
            // (the downstream emitter ignores it for float lanes).
            match opcode {
                Opcode::Fmin => Some(LoweredOp::VMin {
                    lane_ty,
                    signed: true,
                    lhs,
                    rhs,
                    result,
                }),
                Opcode::Smin => Some(LoweredOp::VMin {
                    lane_ty,
                    signed: true,
                    lhs,
                    rhs,
                    result,
                }),
                Opcode::Umin => Some(LoweredOp::VMin {
                    lane_ty,
                    signed: false,
                    lhs,
                    rhs,
                    result,
                }),
                Opcode::Fmax => Some(LoweredOp::VMax {
                    lane_ty,
                    signed: true,
                    lhs,
                    rhs,
                    result,
                }),
                Opcode::Smax => Some(LoweredOp::VMax {
                    lane_ty,
                    signed: true,
                    lhs,
                    rhs,
                    result,
                }),
                Opcode::Umax => Some(LoweredOp::VMax {
                    lane_ty,
                    signed: false,
                    lhs,
                    rhs,
                    result,
                }),
                _ => None,
            }
        }

        // ---- Unary: splat / vall_true / vany_true -------------------------
        InstructionData::Unary { opcode, arg } => match opcode {
            Opcode::Splat => {
                // splat: input is the lane scalar; result is the full
                // vector. We carry the per-lane type so downstream PTX
                // emission can pick the right `mov`/`shuffle` width.
                let result_value = func.dfg.first_result(inst);
                let result_ty = func.dfg.value_type(result_value);
                let lane_ty = vector_lane_type(result_ty)?;
                let src = map_value(value_map, *arg)?;
                let result = fresh_id(next_value_id)?;
                Some(LoweredOp::VSplat {
                    lane_ty,
                    src,
                    result,
                })
            }
            Opcode::VallTrue => {
                // vall_true: input is a vector mask; result is a scalar
                // `i8` boolean in Cranelift (0 or 1). We model it as
                // `LoweredType::Bool` in the interim IR (the i8 → Bool
                // narrowing is implicit at the IR boundary; the PTX
                // emitter materialises it via `vote.all` + predicate).
                let input_ty = func.dfg.value_type(*arg);
                // Require the source to actually be a vector — guards
                // against an accidental scalar caller.
                if !input_ty.is_vector() {
                    return None;
                }
                let src = map_value(value_map, *arg)?;
                let result = fresh_id(next_value_id)?;
                Some(LoweredOp::VAllTrue { src, result })
            }
            Opcode::VanyTrue => {
                let input_ty = func.dfg.value_type(*arg);
                if !input_ty.is_vector() {
                    return None;
                }
                let src = map_value(value_map, *arg)?;
                let result = fresh_id(next_value_id)?;
                Some(LoweredOp::VAnyTrue { src, result })
            }
            _ => None,
        },

        // ---- Ternary: bitselect (vector form == VSelect) -------------------
        InstructionData::Ternary { opcode, args } => {
            if *opcode != Opcode::Bitselect {
                return None;
            }
            let result_ty = func.dfg.value_type(func.dfg.first_result(inst));
            // Cranelift's `bitselect` accepts scalar or vector operands.
            // Only the vector form is in the L5 family; the scalar form
            // is left for [`crate::lower_conv`] (it becomes a regular
            // three-operand `select` after lowering).
            let lane_ty = vector_lane_type(result_ty)?;
            // Cranelift `bitselect` operand order is `(c, x, y)` where
            // `c` is the per-bit/per-lane mask, `x` is selected when the
            // mask bit is 1 and `y` when it is 0.
            let cond = map_value(value_map, args[0])?;
            let then_v = map_value(value_map, args[1])?;
            let else_v = map_value(value_map, args[2])?;
            let result = fresh_id(next_value_id)?;
            Some(LoweredOp::VSelect {
                lane_ty,
                cond,
                then_v,
                else_v,
                result,
            })
        }

        // Any other InstructionData variant is outside this family.
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::ir::types;
    use cranelift_codegen::ir::{
        Function, InstructionData, Opcode, Signature, UserFuncName, Value,
    };
    use cranelift_codegen::isa::CallConv;

    /// Build an empty Cranelift function with a single block and the
    /// requested per-parameter types appended to that block. Returns the
    /// function and the list of `Value` handles for the params, in order.
    ///
    /// We do **not** add the block to the layout — `lower_vector_inst`
    /// reads from `dfg` only, so the layout is irrelevant for these unit
    /// tests.
    fn skeleton(param_tys: &[ir::Type]) -> (Function, Vec<Value>) {
        let mut func =
            Function::with_name_signature(UserFuncName::user(0, 0), Signature::new(CallConv::Fast));
        let block = func.dfg.make_block();
        let mut values = Vec::with_capacity(param_tys.len());
        for &ty in param_tys {
            values.push(func.dfg.append_block_param(block, ty));
        }
        (func, values)
    }

    /// Append a `Binary` instruction with the given opcode and operands,
    /// return the (Inst, result Value). Uses `ctrl_ty` as the controlling
    /// type variable for result-type inference.
    fn push_binary(
        func: &mut Function,
        opcode: Opcode,
        lhs: Value,
        rhs: Value,
        ctrl_ty: ir::Type,
    ) -> (ir::Inst, Value) {
        let inst = func.dfg.make_inst(InstructionData::Binary {
            opcode,
            args: [lhs, rhs],
        });
        func.dfg.make_inst_results(inst, ctrl_ty);
        let result = func.dfg.first_result(inst);
        (inst, result)
    }

    /// Append a `Unary` instruction with the given opcode and operand.
    fn push_unary(
        func: &mut Function,
        opcode: Opcode,
        arg: Value,
        ctrl_ty: ir::Type,
    ) -> (ir::Inst, Value) {
        let inst = func.dfg.make_inst(InstructionData::Unary { opcode, arg });
        func.dfg.make_inst_results(inst, ctrl_ty);
        let result = func.dfg.first_result(inst);
        (inst, result)
    }

    /// Append a `Ternary` instruction (bitselect).
    fn push_ternary(
        func: &mut Function,
        opcode: Opcode,
        args: [Value; 3],
        ctrl_ty: ir::Type,
    ) -> (ir::Inst, Value) {
        let inst = func
            .dfg
            .make_inst(InstructionData::Ternary { opcode, args });
        func.dfg.make_inst_results(inst, ctrl_ty);
        let result = func.dfg.first_result(inst);
        (inst, result)
    }

    /// Map the first N block params to consecutive LoweredValueIds [0..N).
    /// Returns the populated map and the next-free id.
    fn seed_map(params: &[Value]) -> (HashMap<ir::Value, LoweredValueId>, LoweredValueId) {
        let mut map = HashMap::new();
        for (i, &v) in params.iter().enumerate() {
            map.insert(v, i as LoweredValueId);
        }
        (map, params.len() as LoweredValueId)
    }

    // ---- Binary min/max ----------------------------------------------------

    #[test]
    fn fmin_vector_lowers_to_vmin() {
        let (mut func, params) = skeleton(&[types::F32X4, types::F32X4]);
        let (inst, _res) = push_binary(&mut func, Opcode::Fmin, params[0], params[1], types::F32X4);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        match op {
            LoweredOp::VMin {
                lane_ty,
                signed,
                lhs,
                rhs,
                result,
            } => {
                assert_eq!(lane_ty, LoweredType::F32);
                // Float min has no signedness; convention is `true`.
                assert!(signed);
                assert_eq!(lhs, 0);
                assert_eq!(rhs, 1);
                assert_eq!(result, 2);
            }
            other => panic!("expected VMin, got {other:?}"),
        }
        assert_eq!(next, 3);
    }

    #[test]
    fn fmax_vector_lowers_to_vmax() {
        let (mut func, params) = skeleton(&[types::F64X2, types::F64X2]);
        let (inst, _) = push_binary(&mut func, Opcode::Fmax, params[0], params[1], types::F64X2);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(
            op,
            LoweredOp::VMax {
                lane_ty: LoweredType::F64,
                ..
            }
        ));
    }

    /// jit MED fix (finding 5): `smin` lowers to a SIGNED VMin.
    #[test]
    fn smin_vector_lowers_to_vmin_with_signed_lane() {
        let (mut func, params) = skeleton(&[types::I32X4, types::I32X4]);
        let (inst, _) = push_binary(&mut func, Opcode::Smin, params[0], params[1], types::I32X4);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(
            op,
            LoweredOp::VMin {
                lane_ty: LoweredType::I32,
                signed: true,
                ..
            }
        ));
    }

    /// jit MED fix (finding 5): `umin` lowers to an UNSIGNED VMin — the
    /// distinction `smin`/`umin` was previously LOST (both became `VMin`
    /// with no signedness, so `umin` miscompiled to a signed `min`).
    #[test]
    fn umin_vector_lowers_to_unsigned_vmin() {
        let (mut func, params) = skeleton(&[types::I32X4, types::I32X4]);
        let (inst, _) = push_binary(&mut func, Opcode::Umin, params[0], params[1], types::I32X4);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(
            op,
            LoweredOp::VMin {
                lane_ty: LoweredType::I32,
                signed: false,
                ..
            }
        ));
    }

    /// jit MED fix (finding 5): `smin` and `umin` on the SAME lane type
    /// now produce distinguishable ops (the regression this finding fixes).
    #[test]
    fn smin_and_umin_are_distinguishable() {
        let (mut sf, sp) = skeleton(&[types::I32X4, types::I32X4]);
        let (si, _) = push_binary(&mut sf, Opcode::Smin, sp[0], sp[1], types::I32X4);
        let (smap, mut sn) = seed_map(&sp);
        let s = lower_vector_inst(si, &sf, &smap, &mut sn).expect("smin");

        let (mut uf, up) = skeleton(&[types::I32X4, types::I32X4]);
        let (ui, _) = push_binary(&mut uf, Opcode::Umin, up[0], up[1], types::I32X4);
        let (umap, mut un) = seed_map(&up);
        let u = lower_vector_inst(ui, &uf, &umap, &mut un).expect("umin");

        let s_signed = matches!(s, LoweredOp::VMin { signed: true, .. });
        let u_unsigned = matches!(u, LoweredOp::VMin { signed: false, .. });
        assert!(s_signed && u_unsigned, "smin must be signed, umin unsigned");
    }

    #[test]
    fn umax_vector_lowers_to_vmax_with_unsigned_lane() {
        let (mut func, params) = skeleton(&[types::I16X8, types::I16X8]);
        let (inst, _) = push_binary(&mut func, Opcode::Umax, params[0], params[1], types::I16X8);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(
            op,
            LoweredOp::VMax {
                lane_ty: LoweredType::I16,
                signed: false,
                ..
            }
        ));
    }

    #[test]
    fn smax_i8x16_lowers_to_vmax() {
        let (mut func, params) = skeleton(&[types::I8X16, types::I8X16]);
        let (inst, _) = push_binary(&mut func, Opcode::Smax, params[0], params[1], types::I8X16);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(
            op,
            LoweredOp::VMax {
                lane_ty: LoweredType::I8,
                signed: true,
                ..
            }
        ));
    }

    #[test]
    fn umin_i64x2_lowers_to_vmin() {
        let (mut func, params) = skeleton(&[types::I64X2, types::I64X2]);
        let (inst, _) = push_binary(&mut func, Opcode::Umin, params[0], params[1], types::I64X2);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(
            op,
            LoweredOp::VMin {
                lane_ty: LoweredType::I64,
                signed: false,
                ..
            }
        ));
    }

    /// Scalar `fmin` is *not* a vector op — we expect `None` so the caller
    /// routes it to the float family.
    #[test]
    fn scalar_fmin_returns_none() {
        let (mut func, params) = skeleton(&[types::F32, types::F32]);
        let (inst, _) = push_binary(&mut func, Opcode::Fmin, params[0], params[1], types::F32);
        let (map, mut next) = seed_map(&params);
        assert!(lower_vector_inst(inst, &func, &map, &mut next).is_none());
    }

    /// Same for scalar `smin`.
    #[test]
    fn scalar_smin_returns_none() {
        let (mut func, params) = skeleton(&[types::I32, types::I32]);
        let (inst, _) = push_binary(&mut func, Opcode::Smin, params[0], params[1], types::I32);
        let (map, mut next) = seed_map(&params);
        assert!(lower_vector_inst(inst, &func, &map, &mut next).is_none());
    }

    // ---- Splat -------------------------------------------------------------

    #[test]
    fn splat_f32_lowers_to_vsplat() {
        // splat takes a scalar -> vector. Use F32 input, F32X4 result.
        let (mut func, params) = skeleton(&[types::F32]);
        let (inst, _) = push_unary(&mut func, Opcode::Splat, params[0], types::F32X4);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        match op {
            LoweredOp::VSplat {
                lane_ty,
                src,
                result,
            } => {
                assert_eq!(lane_ty, LoweredType::F32);
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected VSplat, got {other:?}"),
        }
    }

    #[test]
    fn splat_i32_lowers_to_vsplat() {
        let (mut func, params) = skeleton(&[types::I32]);
        let (inst, _) = push_unary(&mut func, Opcode::Splat, params[0], types::I32X4);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(
            op,
            LoweredOp::VSplat {
                lane_ty: LoweredType::I32,
                ..
            }
        ));
    }

    // ---- vall_true / vany_true -------------------------------------------

    #[test]
    fn vall_true_lowers() {
        let (mut func, params) = skeleton(&[types::I32X4]);
        let (inst, _) = push_unary(&mut func, Opcode::VallTrue, params[0], types::I32X4);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        match op {
            LoweredOp::VAllTrue { src, result } => {
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected VAllTrue, got {other:?}"),
        }
    }

    #[test]
    fn vany_true_lowers() {
        let (mut func, params) = skeleton(&[types::I8X16]);
        let (inst, _) = push_unary(&mut func, Opcode::VanyTrue, params[0], types::I8X16);
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        assert!(matches!(op, LoweredOp::VAnyTrue { src: 0, result: 1 }));
    }

    // ---- bitselect (vector form == VSelect) ------------------------------

    #[test]
    fn bitselect_vector_lowers_to_vselect() {
        let (mut func, params) = skeleton(&[types::I32X4, types::I32X4, types::I32X4]);
        let (inst, _) = push_ternary(
            &mut func,
            Opcode::Bitselect,
            [params[0], params[1], params[2]],
            types::I32X4,
        );
        let (map, mut next) = seed_map(&params);
        let op = lower_vector_inst(inst, &func, &map, &mut next).expect("must lower");
        match op {
            LoweredOp::VSelect {
                lane_ty,
                cond,
                then_v,
                else_v,
                result,
            } => {
                assert_eq!(lane_ty, LoweredType::I32);
                assert_eq!(cond, 0);
                assert_eq!(then_v, 1);
                assert_eq!(else_v, 2);
                assert_eq!(result, 3);
            }
            other => panic!("expected VSelect, got {other:?}"),
        }
    }

    /// Scalar bitselect is left for the conversion family.
    #[test]
    fn scalar_bitselect_returns_none() {
        let (mut func, params) = skeleton(&[types::I32, types::I32, types::I32]);
        let (inst, _) = push_ternary(
            &mut func,
            Opcode::Bitselect,
            [params[0], params[1], params[2]],
            types::I32,
        );
        let (map, mut next) = seed_map(&params);
        assert!(lower_vector_inst(inst, &func, &map, &mut next).is_none());
    }

    // ---- Negative paths ---------------------------------------------------

    /// An op outside the vector family (here: scalar `iadd`) yields `None`
    /// so the caller can route to the arith family.
    #[test]
    fn unrelated_op_returns_none() {
        let (mut func, params) = skeleton(&[types::I32, types::I32]);
        let (inst, _) = push_binary(&mut func, Opcode::Iadd, params[0], params[1], types::I32);
        let (map, mut next) = seed_map(&params);
        assert!(lower_vector_inst(inst, &func, &map, &mut next).is_none());
    }

    /// The `next_value_id` cursor advances exactly once per lowered op
    /// (the result id), independent of how many operands the op has.
    #[test]
    fn fresh_id_allocation_is_monotone() {
        let (mut func, params) = skeleton(&[types::F32X4, types::F32X4]);
        let (inst, _) = push_binary(&mut func, Opcode::Fmin, params[0], params[1], types::F32X4);
        let (map, mut next) = seed_map(&params);
        let before = next;
        let _ = lower_vector_inst(inst, &func, &map, &mut next).unwrap();
        assert_eq!(next, before + 1);
    }
}
