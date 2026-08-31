// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Float-family Cranelift→[`LoweredOp`] lowering (wave 1, task L2).
//!
//! This module covers the float rows of the [Cranelift → `dialect-mir`
//! mapping table](crate::pliron_dialect#mapping-table):
//!
//! | Cranelift op | Produces                                       |
//! |--------------|------------------------------------------------|
//! | `fadd`       | [`LoweredOp::AddF`]                            |
//! | `fsub`       | [`LoweredOp::SubF`]                            |
//! | `fmul`       | [`LoweredOp::MulF`]                            |
//! | `fdiv`       | [`LoweredOp::DivF`]                            |
//! | `fma`        | [`LoweredOp::Fma`] (`a*b + c`, single rounding)|
//! | `fneg`       | [`LoweredOp::FNeg`]                            |
//! | `fabs`       | [`LoweredOp::FAbs`]                            |
//!
//! Only `f32` / `f64` scalars are accepted — `f16`, `v128`, or any other
//! Cranelift type returns [`None`] so the caller can fall back to a
//! different lowering family (`lower_vector` handles vector float ops in a
//! sibling module). Per-row Pliron equivalents and PTX semantics are
//! captured in the mapping table; this module purposely does not repeat
//! those notes here so the single source of truth stays in
//! [`crate::pliron_dialect`].
//!
//! # Result-id allocation
//!
//! Each successful lowering allocates a fresh [`LoweredValueId`] for the
//! op's SSA result by reading `*next_value_id` and post-incrementing it.
//! Operand ids come from the caller-owned `value_map` which maps each
//! Cranelift [`cranelift_codegen::ir::Value`] to the already-assigned
//! [`LoweredValueId`]. Missing operands are a caller bug and panic — the
//! lowering walker must populate the map for every block parameter and
//! every prior instruction's result before recursing into this family.
//!
//! # Scope
//!
//! - Pure: no side effects beyond `*next_value_id` and reading
//!   `func.dfg`. Safe to call in any order; the caller controls
//!   block-walk ordering.
//! - Stateless: no module-level statics; multiple lowerings can run
//!   concurrently (one `&mut next_value_id` per pass, as is the
//!   wave-1 convention shared with `lower_arith` etc.).

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::ir::{self, Function, Inst, Opcode, Value};

use crate::lowered_ir::{LoweredOp, LoweredType, LoweredValueId};

/// Try to lower one Cranelift instruction as a float-family op.
///
/// Returns `Some(LoweredOp)` if `inst`'s opcode is one of the seven this
/// module handles (`fadd`/`fsub`/`fmul`/`fdiv`/`fma`/`fneg`/`fabs`) **and**
/// its result type is `f32` or `f64`. Returns `None` otherwise — the
/// caller is expected to consult sibling `lower_*` modules in that case
/// (vector float ops live in `lower_vector`; non-float ops live in
/// `lower_arith`, `lower_memory`, etc.).
///
/// # Parameters
///
/// - `inst`: the Cranelift instruction being lowered.
/// - `func`: the enclosing function. Used read-only for opcode lookup,
///   operand fetch (`dfg.inst_args`), and result-type inspection
///   (`dfg.value_type` on the instruction's first result).
/// - `value_map`: caller-owned map from already-lowered Cranelift
///   `Value`s to their assigned [`LoweredValueId`]s. Every operand of
///   `inst` must be present.
/// - `next_value_id`: caller-owned monotonic counter for fresh result
///   ids. On a successful lowering, the current value is consumed as
///   the new op's `result` field and then incremented by one.
///
/// # Panics
///
/// Panics if an operand `Value` is missing from `value_map`. This
/// signals a walker bug (a use-before-def in the caller); the wave-1
/// design treats it as unrecoverable rather than threading a `Result`
/// through every per-family lowering.
pub fn lower_float_inst(
    inst: Inst,
    func: &Function,
    value_map: &HashMap<Value, LoweredValueId>,
    next_value_id: &mut LoweredValueId,
) -> Option<LoweredOp> {
    let opcode = func.dfg.insts[inst].opcode();

    // Quick reject: anything not in our seven-op set returns None
    // without touching `next_value_id`. Keeps the function side-effect
    // free on the rejection path so the caller can fall through to the
    // next lowering family cleanly.
    match opcode {
        Opcode::Fadd
        | Opcode::Fsub
        | Opcode::Fmul
        | Opcode::Fdiv
        | Opcode::Fma
        | Opcode::Fneg
        | Opcode::Fabs => {}
        _ => return None,
    }

    // Determine the result type. All seven ops produce exactly one
    // result whose type matches their operands. Reject anything outside
    // the f32/f64 scalar set — vector float ops (e.g. fadd on f32x4)
    // are handled by `lower_vector`, and f16 is not in the wave-1
    // mapping table.
    let result_value = func.dfg.first_result(inst);
    let ty = match func.dfg.value_type(result_value) {
        t if t == ir::types::F32 => LoweredType::F32,
        t if t == ir::types::F64 => LoweredType::F64,
        _ => return None,
    };

    let args = func.dfg.inst_args(inst);
    let result = *next_value_id;
    // jit LOW fix (finding 7): standardize on `checked_add(1)?` (the
    // `lower_arith` idiom) instead of `.expect(...)`. A function exceeding
    // `u32::MAX` SSA values now surfaces as a structured lowering miss
    // (`None`, mapped to a `LoweringError` by the driver) rather than
    // panicking the process.
    *next_value_id = next_value_id.checked_add(1)?;

    let lowered = match opcode {
        Opcode::Fadd => LoweredOp::AddF {
            ty,
            lhs: lookup(value_map, args[0])?,
            rhs: lookup(value_map, args[1])?,
            result,
        },
        Opcode::Fsub => LoweredOp::SubF {
            ty,
            lhs: lookup(value_map, args[0])?,
            rhs: lookup(value_map, args[1])?,
            result,
        },
        Opcode::Fmul => LoweredOp::MulF {
            ty,
            lhs: lookup(value_map, args[0])?,
            rhs: lookup(value_map, args[1])?,
            result,
        },
        Opcode::Fdiv => LoweredOp::DivF {
            ty,
            lhs: lookup(value_map, args[0])?,
            rhs: lookup(value_map, args[1])?,
            result,
        },
        Opcode::Fma => LoweredOp::Fma {
            ty,
            a: lookup(value_map, args[0])?,
            b: lookup(value_map, args[1])?,
            c: lookup(value_map, args[2])?,
            result,
        },
        Opcode::Fneg => LoweredOp::FNeg {
            ty,
            src: lookup(value_map, args[0])?,
            result,
        },
        Opcode::Fabs => LoweredOp::FAbs {
            ty,
            src: lookup(value_map, args[0])?,
            result,
        },
        // Unreachable: the earlier `match` already filtered the opcode
        // set down to the seven listed above.
        _ => unreachable!("opcode already validated by the dispatch match"),
    };

    Some(lowered)
}

/// Resolve a Cranelift `Value` to its already-assigned
/// [`LoweredValueId`]. Returns `None` if the operand was not
/// pre-mapped (jit S-4: a malformed Cranelift function with a
/// backward branch whose block-param references a not-yet-seen
/// value used to panic the worker; now it gracefully degrades to
/// `lower_float_inst` returning `None`, which the walker treats
/// as "skip this instruction" — same as for unsupported ops).
fn lookup(map: &HashMap<Value, LoweredValueId>, v: Value) -> Option<LoweredValueId> {
    let id = map.get(&v).copied();
    if id.is_none() {
        tracing::debug!(
            target: "tensor_wasm_jit::lower_float",
            value = ?v,
            "operand not pre-mapped; skipping float instruction"
        );
    }
    id
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::ir::{
        types, AbiParam, Block, Function, InstructionData, Signature, UserFuncName, Value,
    };
    use cranelift_codegen::isa::CallConv;

    /// Fixture: a one-instruction function and the operand `Value`s
    /// used to build it. The test asserts against `inst`; operand ids
    /// are pre-populated into a `value_map` via [`map_operands`] so
    /// the lowering can resolve them.
    struct InstFixture {
        func: Function,
        inst: Inst,
        operands: Vec<Value>,
    }

    /// Build a one-instruction function whose only block has parameters
    /// of `param_ty`, then append a single instruction described by
    /// `make_data` (operands pulled from the block params, one per
    /// `arg_count`). The result type is set by `make_inst_results`
    /// using `param_ty` as the controlling typevar.
    fn build_func_with_inst(
        param_ty: cranelift_codegen::ir::Type,
        arg_count: usize,
        make_data: impl FnOnce(&[Value]) -> InstructionData,
    ) -> InstFixture {
        let mut sig = Signature::new(CallConv::Fast);
        for _ in 0..arg_count {
            sig.params.push(AbiParam::new(param_ty));
        }
        sig.returns.push(AbiParam::new(param_ty));

        let mut func = Function::with_name_signature(UserFuncName::testcase("t"), sig);
        let block: Block = func.dfg.make_block();
        let mut operands: Vec<Value> = Vec::with_capacity(arg_count);
        for _ in 0..arg_count {
            operands.push(func.dfg.append_block_param(block, param_ty));
        }
        let data = make_data(&operands);
        let inst = func.dfg.make_inst(data);
        func.dfg.make_inst_results(inst, param_ty);
        InstFixture {
            func,
            inst,
            operands,
        }
    }

    /// Map the fixture's operand `Value`s to fresh
    /// [`LoweredValueId`]s starting at 0. Returns the map and the
    /// first id not yet used (i.e. the id the lowering should assign
    /// to the instruction's result).
    fn map_operands(operands: &[Value]) -> (HashMap<Value, LoweredValueId>, LoweredValueId) {
        let mut map = HashMap::new();
        let mut next: LoweredValueId = 0;
        for v in operands {
            map.insert(*v, next);
            next += 1;
        }
        (map, next)
    }

    fn binary(opcode: Opcode, args: [Value; 2]) -> InstructionData {
        InstructionData::Binary { opcode, args }
    }

    fn ternary(opcode: Opcode, args: [Value; 3]) -> InstructionData {
        InstructionData::Ternary { opcode, args }
    }

    fn unary(opcode: Opcode, arg: Value) -> InstructionData {
        InstructionData::Unary { opcode, arg }
    }

    #[test]
    fn lower_fadd_f32() {
        let fx = build_func_with_inst(types::F32, 2, |params| {
            binary(Opcode::Fadd, [params[0], params[1]])
        });
        let (map, mut next) = map_operands(&fx.operands);
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next).expect("fadd lowers");
        match op {
            LoweredOp::AddF {
                ty,
                lhs,
                rhs,
                result,
            } => {
                assert_eq!(ty, LoweredType::F32);
                assert_eq!(lhs, 0);
                assert_eq!(rhs, 1);
                assert_eq!(result, 2);
            }
            other => panic!("expected AddF, got {other:?}"),
        }
        assert_eq!(next, 3);
    }

    #[test]
    fn lower_fsub_f64() {
        let fx = build_func_with_inst(types::F64, 2, |params| {
            binary(Opcode::Fsub, [params[0], params[1]])
        });
        let (map, mut next) = map_operands(&fx.operands);
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next).expect("fsub lowers");
        match op {
            LoweredOp::SubF { ty, .. } => assert_eq!(ty, LoweredType::F64),
            other => panic!("expected SubF, got {other:?}"),
        }
    }

    #[test]
    fn lower_fmul_f32() {
        let fx = build_func_with_inst(types::F32, 2, |params| {
            binary(Opcode::Fmul, [params[0], params[1]])
        });
        let (map, mut next) = map_operands(&fx.operands);
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next).expect("fmul lowers");
        match op {
            LoweredOp::MulF { ty, .. } => assert_eq!(ty, LoweredType::F32),
            other => panic!("expected MulF, got {other:?}"),
        }
    }

    #[test]
    fn lower_fdiv_f64() {
        let fx = build_func_with_inst(types::F64, 2, |params| {
            binary(Opcode::Fdiv, [params[0], params[1]])
        });
        let (map, mut next) = map_operands(&fx.operands);
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next).expect("fdiv lowers");
        match op {
            LoweredOp::DivF { ty, .. } => assert_eq!(ty, LoweredType::F64),
            other => panic!("expected DivF, got {other:?}"),
        }
    }

    #[test]
    fn lower_fma_f32() {
        let fx = build_func_with_inst(types::F32, 3, |params| {
            ternary(Opcode::Fma, [params[0], params[1], params[2]])
        });
        let (map, mut next) = map_operands(&fx.operands);
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next).expect("fma lowers");
        match op {
            LoweredOp::Fma {
                ty,
                a,
                b,
                c,
                result,
            } => {
                assert_eq!(ty, LoweredType::F32);
                assert_eq!(a, 0);
                assert_eq!(b, 1);
                assert_eq!(c, 2);
                assert_eq!(result, 3);
            }
            other => panic!("expected Fma, got {other:?}"),
        }
    }

    #[test]
    fn lower_fneg_f64() {
        let fx = build_func_with_inst(types::F64, 1, |params| unary(Opcode::Fneg, params[0]));
        let (map, mut next) = map_operands(&fx.operands);
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next).expect("fneg lowers");
        match op {
            LoweredOp::FNeg { ty, src, result } => {
                assert_eq!(ty, LoweredType::F64);
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected FNeg, got {other:?}"),
        }
    }

    #[test]
    fn lower_fabs_f32() {
        let fx = build_func_with_inst(types::F32, 1, |params| unary(Opcode::Fabs, params[0]));
        let (map, mut next) = map_operands(&fx.operands);
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next).expect("fabs lowers");
        match op {
            LoweredOp::FAbs { ty, src, result } => {
                assert_eq!(ty, LoweredType::F32);
                assert_eq!(src, 0);
                assert_eq!(result, 1);
            }
            other => panic!("expected FAbs, got {other:?}"),
        }
    }

    /// Non-float opcodes (here `iadd`) return `None` and leave the
    /// id-counter untouched so the caller can fall through to the next
    /// lowering family.
    #[test]
    fn non_float_opcode_returns_none() {
        let fx = build_func_with_inst(types::I32, 2, |params| {
            binary(Opcode::Iadd, [params[0], params[1]])
        });
        let (map, mut next) = map_operands(&fx.operands);
        let before = next;
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next);
        assert!(op.is_none(), "iadd must not be claimed by lower_float");
        assert_eq!(next, before, "counter must not advance on rejection");
    }

    /// A float opcode on an unsupported type (here a v128 vector
    /// fadd) is rejected — those rows live in `lower_vector`. The
    /// caller falls through cleanly.
    #[test]
    fn vector_fadd_returns_none() {
        let fx = build_func_with_inst(types::F32X4, 2, |params| {
            binary(Opcode::Fadd, [params[0], params[1]])
        });
        let (map, mut next) = map_operands(&fx.operands);
        let before = next;
        let op = lower_float_inst(fx.inst, &fx.func, &map, &mut next);
        assert!(
            op.is_none(),
            "vector fadd must not be claimed by lower_float"
        );
        assert_eq!(next, before);
    }
}
