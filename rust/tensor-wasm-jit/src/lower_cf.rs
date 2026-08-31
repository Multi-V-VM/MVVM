// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! L4 — control-flow lowering family: Cranelift control-flow ops →
//! [`crate::lowered_ir::LoweredOp`].
//!
//! Wave-1 task L4 of the Pliron pipeline scaffold
//! (see [RFC 0001 "Future possibilities"][rfc] and the
//! [Cranelift → dialect-mir mapping table][map]).
//!
//! [rfc]: ../../../../rfcs/0001-cuda-oxide-integration.md
//! [map]: crate::pliron_dialect#mapping-table
//!
//! Cranelift opcodes handled here:
//!
//! | Cranelift opcode | `LoweredOp` variant |
//! |------------------|---------------------|
//! | `jump`           | [`Br`](crate::lowered_ir::LoweredOp::Br) |
//! | `brif`           | [`CondBr`](crate::lowered_ir::LoweredOp::CondBr) |
//! | `br_table`       | [`Switch`](crate::lowered_ir::LoweredOp::Switch) |
//! | `return`         | [`Return`](crate::lowered_ir::LoweredOp::Return) |
//!
//! # On `brz` / `brnz`
//!
//! The deprecated `brz` / `brnz` (branch-if-zero / branch-if-nonzero) forms
//! were **removed** from Cranelift in the 0.93 series (replaced by the
//! two-successor `brif`). The workspace's pinned `cranelift-codegen = "0.111"`
//! has no such variants in [`InstructionData`][id] or [`Opcode`][op]; this
//! module therefore does not match on them. A future Cranelift bump that
//! re-introduces them (unlikely) would need to extend
//! [`lower_cf_inst`] with the corresponding arm.
//!
//! [id]: cranelift_codegen::ir::InstructionData
//! [op]: cranelift_codegen::ir::Opcode
//!
//! # Block-arg semantics
//!
//! Cranelift's `BlockCall` bundles a successor [`Block`][block] with the
//! [`Value`][val]s passed as that block's parameters. The lowering walks
//! every `BlockCall` reachable from the instruction, translates the
//! Cranelift `Block` through `block_map` to a
//! [`LoweredBlockId`](crate::lowered_ir::LoweredBlockId), and translates
//! each `Value` arg through `value_map` to a
//! [`LoweredValueId`](crate::lowered_ir::LoweredValueId).
//!
//! Both maps are passed by the caller (the function-level driver, not yet
//! written — wave 1 lands per-family lowerers, wave 2 wires them into the
//! [`crate::pliron_dialect::WasmToPliron`] entry point). Any `Block` or
//! `Value` missing from the maps is a caller bug and produces `None`; the
//! driver is responsible for pre-populating the maps in a single pass
//! before invoking the per-instruction lowerers.
//!
//! [block]: cranelift_codegen::ir::Block
//! [val]: cranelift_codegen::ir::Value

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::ir::{Block, Function, Inst, InstructionData, Opcode, Value};

use crate::lowered_ir::{LoweredBlockId, LoweredOp, LoweredValueId};

/// Lower a single Cranelift control-flow instruction to a [`LoweredOp`].
///
/// Returns `Some(LoweredOp)` for `jump`, `brif`, `br_table`, and `return`;
/// returns `None` for every other instruction (caller is expected to
/// dispatch to other `lower_*` family modules).
///
/// # Map contracts
///
/// - `value_map`: every Cranelift [`Value`] referenced by the instruction
///   (the condition for `brif`, the switched value for `br_table`, the
///   return values for `return`, and every block-arg appearing inside a
///   `BlockCall`) must already be present. Missing entries return `None`.
/// - `block_map`: every Cranelift [`Block`] referenced as a successor —
///   the `Jump::destination`, both `Brif::blocks`, the `br_table` default
///   and case targets — must already be present. Missing entries return
///   `None`.
///
/// The caller (function-level driver) is responsible for walking the
/// Cranelift function once to pre-populate both maps before invoking the
/// per-instruction lowerers.
///
/// # Notes
///
/// - `br_table` case values are zero-based indices into the jump table's
///   non-default entries, matching Cranelift's "0-based indexing, densely
///   populated" jump-table contract.
/// - The deprecated `brz` / `brnz` forms are not handled — see the
///   module-level docs.
pub fn lower_cf_inst(
    inst: Inst,
    func: &Function,
    value_map: &HashMap<Value, LoweredValueId>,
    block_map: &HashMap<Block, LoweredBlockId>,
) -> Option<LoweredOp> {
    let data = &func.dfg.insts[inst];
    match data {
        // ---- jump ----------------------------------------------------
        InstructionData::Jump { destination, .. } => {
            let target = *block_map.get(&destination.block(&func.dfg.value_lists))?;
            let args =
                map_block_call_args(destination.args_slice(&func.dfg.value_lists), value_map)?;
            Some(LoweredOp::Br { target, args })
        }

        // ---- brif ----------------------------------------------------
        InstructionData::Brif { arg, blocks, .. } => {
            let cond = *value_map.get(arg)?;
            let then_call = &blocks[0];
            let else_call = &blocks[1];

            let then_target = *block_map.get(&then_call.block(&func.dfg.value_lists))?;
            let else_target = *block_map.get(&else_call.block(&func.dfg.value_lists))?;

            let then_args =
                map_block_call_args(then_call.args_slice(&func.dfg.value_lists), value_map)?;
            let else_args =
                map_block_call_args(else_call.args_slice(&func.dfg.value_lists), value_map)?;

            Some(LoweredOp::CondBr {
                cond,
                then_target,
                then_args,
                else_target,
                else_args,
            })
        }

        // ---- br_table ------------------------------------------------
        InstructionData::BranchTable { arg, table, .. } => {
            let value = *value_map.get(arg)?;
            let jt = &func.dfg.jump_tables[*table];

            // `JumpTableData::default_block()` returns the default
            // successor; `as_slice()` excludes it and yields the
            // case-indexed entries in 0..N order.
            let default_call = jt.default_block();
            let default_target = *block_map.get(&default_call.block(&func.dfg.value_lists))?;

            // Block args on `br_table` successors must be empty in
            // well-formed Cranelift IR (the verifier rejects any
            // br_table BlockCall with arguments). We still defensively
            // map them — if a future Cranelift relaxes that and a caller
            // supplies non-empty args, we propagate them rather than
            // silently dropping them. The `LoweredOp::Switch` schema
            // intentionally does not carry per-case args (the dialect-mir
            // `cf.switch` lowering does not either), so we currently
            // ignore them and bail to `None` if any are present. This
            // keeps the conversion total and the schema honest.
            let mut cases: Vec<(u32, LoweredBlockId)> = Vec::with_capacity(jt.as_slice().len());
            for (idx, case_call) in jt.as_slice().iter().enumerate() {
                if !case_call.args_slice(&func.dfg.value_lists).is_empty() {
                    return None;
                }
                let target = *block_map.get(&case_call.block(&func.dfg.value_lists))?;
                // u32 cast: Cranelift jump tables are 0-based and limited
                // in practice to ~2^31 entries by the surrounding
                // `JumpTable` entity, so the cast is lossless. (A
                // pathological 2^32-entry table would already have
                // exhausted the Cranelift entity counter.)
                cases.push((idx as u32, target));
            }

            // Default block must likewise be arg-free.
            if !default_call.args_slice(&func.dfg.value_lists).is_empty() {
                return None;
            }

            Some(LoweredOp::Switch {
                value,
                default_target,
                cases,
            })
        }

        // ---- return --------------------------------------------------
        // Return is encoded as InstructionData::MultiAry { opcode:
        // Opcode::Return, args }. MultiAry is shared with `call`-family
        // ops, so we must discriminate by opcode.
        InstructionData::MultiAry { opcode, args } if *opcode == Opcode::Return => {
            let cranelift_args = args.as_slice(&func.dfg.value_lists);
            let mut values = Vec::with_capacity(cranelift_args.len());
            for v in cranelift_args {
                values.push(*value_map.get(v)?);
            }
            Some(LoweredOp::Return { values })
        }

        _ => None,
    }
}

/// Translate a slice of Cranelift block-call [`Value`] args to a vector of
/// [`LoweredValueId`]s. Returns `None` if any arg is missing from
/// `value_map`.
///
/// Internal helper for [`lower_cf_inst`]; kept at module scope (rather
/// than nested in the match arm) so the `?`-propagation reads cleanly and
/// each arm stays grep-able.
fn map_block_call_args(
    cranelift_args: &[Value],
    value_map: &HashMap<Value, LoweredValueId>,
) -> Option<Vec<LoweredValueId>> {
    let mut out = Vec::with_capacity(cranelift_args.len());
    for v in cranelift_args {
        out.push(*value_map.get(v)?);
    }
    Some(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    use cranelift_codegen::ir::{
        types, AbiParam, BlockCall, Function, InstructionData, JumpTableData, Opcode, Signature,
        UserFuncName, ValueList,
    };
    use cranelift_codegen::isa::CallConv;

    /// Build a minimal `Function` with one empty entry block. The block is
    /// returned for the caller to populate. The function's signature is
    /// `() -> ()`; tests that need params/returns extend it directly.
    fn fresh_function() -> Function {
        let sig = Signature::new(CallConv::SystemV);
        Function::with_name_signature(UserFuncName::default(), sig)
    }

    /// Allocate a fresh block in the function. The DFG owns it; the
    /// layout is **not** touched (we never need to query the layout in
    /// these tests, just the per-instruction data).
    fn new_block(func: &mut Function) -> Block {
        func.dfg.make_block()
    }

    /// Materialise a Cranelift `Value` by appending an i32 block param to
    /// the given block. Returns the new value. Using block params (rather
    /// than synthetic insts) keeps the fixture self-contained — we never
    /// have to construct a producing instruction.
    fn fresh_i32_value(func: &mut Function, block: Block) -> Value {
        func.dfg.append_block_param(block, types::I32)
    }

    /// Build a `BlockCall` targeting `block` with the given args, using
    /// the function's value-list pool.
    fn new_block_call(func: &mut Function, block: Block, args: &[Value]) -> BlockCall {
        BlockCall::new(block, args, &mut func.dfg.value_lists)
    }

    #[test]
    fn lowers_jump_no_args() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);
        let target = new_block(&mut func);

        let bc = new_block_call(&mut func, target, &[]);
        let inst = func.dfg.make_inst(InstructionData::Jump {
            opcode: Opcode::Jump,
            destination: bc,
        });

        let value_map = HashMap::new();
        let mut block_map = HashMap::new();
        block_map.insert(entry, 0u32);
        block_map.insert(target, 1u32);

        let op = lower_cf_inst(inst, &func, &value_map, &block_map).expect("jump must lower");
        match op {
            LoweredOp::Br { target, args } => {
                assert_eq!(target, 1);
                assert!(args.is_empty());
            }
            other => panic!("expected LoweredOp::Br, got {other:?}"),
        }
    }

    #[test]
    fn lowers_jump_with_block_args() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);
        let target = new_block(&mut func);

        // Two i32 params on entry — these are the values being passed
        // through the jump as block args to `target`.
        let v0 = fresh_i32_value(&mut func, entry);
        let v1 = fresh_i32_value(&mut func, entry);

        let bc = new_block_call(&mut func, target, &[v0, v1]);
        let inst = func.dfg.make_inst(InstructionData::Jump {
            opcode: Opcode::Jump,
            destination: bc,
        });

        let mut value_map = HashMap::new();
        value_map.insert(v0, 10u32);
        value_map.insert(v1, 20u32);
        let mut block_map = HashMap::new();
        block_map.insert(entry, 0u32);
        block_map.insert(target, 7u32);

        let op =
            lower_cf_inst(inst, &func, &value_map, &block_map).expect("jump-with-args must lower");
        match op {
            LoweredOp::Br { target, args } => {
                assert_eq!(target, 7);
                assert_eq!(args, vec![10, 20]);
            }
            other => panic!("expected LoweredOp::Br, got {other:?}"),
        }
    }

    #[test]
    fn lowers_brif() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);
        let then_blk = new_block(&mut func);
        let else_blk = new_block(&mut func);

        let cond = fresh_i32_value(&mut func, entry);
        let then_arg = fresh_i32_value(&mut func, entry);
        let else_arg = fresh_i32_value(&mut func, entry);

        let then_call = new_block_call(&mut func, then_blk, &[then_arg]);
        let else_call = new_block_call(&mut func, else_blk, &[else_arg]);

        let inst = func.dfg.make_inst(InstructionData::Brif {
            opcode: Opcode::Brif,
            arg: cond,
            blocks: [then_call, else_call],
        });

        let mut value_map = HashMap::new();
        value_map.insert(cond, 100u32);
        value_map.insert(then_arg, 101u32);
        value_map.insert(else_arg, 102u32);
        let mut block_map = HashMap::new();
        block_map.insert(entry, 0u32);
        block_map.insert(then_blk, 1u32);
        block_map.insert(else_blk, 2u32);

        let op = lower_cf_inst(inst, &func, &value_map, &block_map).expect("brif must lower");
        match op {
            LoweredOp::CondBr {
                cond,
                then_target,
                then_args,
                else_target,
                else_args,
            } => {
                assert_eq!(cond, 100);
                assert_eq!(then_target, 1);
                assert_eq!(then_args, vec![101]);
                assert_eq!(else_target, 2);
                assert_eq!(else_args, vec![102]);
            }
            other => panic!("expected LoweredOp::CondBr, got {other:?}"),
        }
    }

    #[test]
    fn lowers_br_table_with_cases_and_default() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);
        let default_blk = new_block(&mut func);
        let case0 = new_block(&mut func);
        let case1 = new_block(&mut func);
        let case2 = new_block(&mut func);

        let switched = fresh_i32_value(&mut func, entry);

        // Default block call comes first; the rest are the indexed
        // cases. `br_table` BlockCalls must carry no args in
        // well-formed Cranelift IR — we pass `&[]` to match.
        let default_call = new_block_call(&mut func, default_blk, &[]);
        let c0 = new_block_call(&mut func, case0, &[]);
        let c1 = new_block_call(&mut func, case1, &[]);
        let c2 = new_block_call(&mut func, case2, &[]);

        let jt = func.create_jump_table(JumpTableData::new(default_call, &[c0, c1, c2]));

        let inst = func.dfg.make_inst(InstructionData::BranchTable {
            opcode: Opcode::BrTable,
            arg: switched,
            table: jt,
        });

        let mut value_map = HashMap::new();
        value_map.insert(switched, 42u32);
        let mut block_map = HashMap::new();
        block_map.insert(entry, 0u32);
        block_map.insert(default_blk, 100u32);
        block_map.insert(case0, 200u32);
        block_map.insert(case1, 201u32);
        block_map.insert(case2, 202u32);

        let op = lower_cf_inst(inst, &func, &value_map, &block_map).expect("br_table must lower");
        match op {
            LoweredOp::Switch {
                value,
                default_target,
                cases,
            } => {
                assert_eq!(value, 42);
                assert_eq!(default_target, 100);
                assert_eq!(cases, vec![(0u32, 200u32), (1u32, 201u32), (2u32, 202u32)]);
            }
            other => panic!("expected LoweredOp::Switch, got {other:?}"),
        }
    }

    #[test]
    fn lowers_return_no_values() {
        let mut func = fresh_function();
        let _entry = new_block(&mut func);

        let args = ValueList::default();
        let inst = func.dfg.make_inst(InstructionData::MultiAry {
            opcode: Opcode::Return,
            args,
        });

        let value_map = HashMap::new();
        let block_map = HashMap::new();

        let op = lower_cf_inst(inst, &func, &value_map, &block_map).expect("return must lower");
        match op {
            LoweredOp::Return { values } => assert!(values.is_empty()),
            other => panic!("expected LoweredOp::Return, got {other:?}"),
        }
    }

    #[test]
    fn lowers_return_with_values() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);

        // Two i32 return values. Use block params on entry as their
        // SSA defs (sufficient for lowering: the lowering only needs
        // the `Value` identity, not the producing op).
        func.signature.returns.push(AbiParam::new(types::I32));
        func.signature.returns.push(AbiParam::new(types::I32));
        let v0 = fresh_i32_value(&mut func, entry);
        let v1 = fresh_i32_value(&mut func, entry);

        let mut args = ValueList::default();
        args.push(v0, &mut func.dfg.value_lists);
        args.push(v1, &mut func.dfg.value_lists);

        let inst = func.dfg.make_inst(InstructionData::MultiAry {
            opcode: Opcode::Return,
            args,
        });

        let mut value_map = HashMap::new();
        value_map.insert(v0, 1000u32);
        value_map.insert(v1, 2000u32);
        let block_map = HashMap::new();

        let op = lower_cf_inst(inst, &func, &value_map, &block_map).expect("return must lower");
        match op {
            LoweredOp::Return { values } => assert_eq!(values, vec![1000, 2000]),
            other => panic!("expected LoweredOp::Return, got {other:?}"),
        }
    }

    /// A non-control-flow op (e.g. `iadd` via `Binary`) must return
    /// `None` — the dispatch table relies on this to skip to the next
    /// `lower_*` family.
    #[test]
    fn non_cf_op_returns_none() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);
        let a = fresh_i32_value(&mut func, entry);
        let b = fresh_i32_value(&mut func, entry);

        let inst = func.dfg.make_inst(InstructionData::Binary {
            opcode: Opcode::Iadd,
            args: [a, b],
        });

        assert!(lower_cf_inst(inst, &func, &HashMap::new(), &HashMap::new()).is_none());
    }

    /// `call` shares the `MultiAry`-adjacent `Call` format but the
    /// MultiAry arm of the lowerer must only match opcode `Return`. A
    /// `MultiAry { opcode: Opcode::ReturnCall, .. }` would be tail-call
    /// shaped — verify the discriminant guard rejects it. Today
    /// `return_call` is not a control-flow lowering target either, so
    /// `None` is the contractually correct answer.
    #[test]
    fn return_call_is_not_lowered_here() {
        let mut func = fresh_function();
        let _entry = new_block(&mut func);

        let args = ValueList::default();
        let inst = func.dfg.make_inst(InstructionData::MultiAry {
            opcode: Opcode::ReturnCall,
            args,
        });

        assert!(lower_cf_inst(inst, &func, &HashMap::new(), &HashMap::new()).is_none());
    }

    /// Missing entries in `block_map` make the lowering bail with
    /// `None`. This locks in the "caller must pre-populate" contract.
    #[test]
    fn missing_block_returns_none() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);
        let target = new_block(&mut func);

        let bc = new_block_call(&mut func, target, &[]);
        let inst = func.dfg.make_inst(InstructionData::Jump {
            opcode: Opcode::Jump,
            destination: bc,
        });

        let value_map = HashMap::new();
        let mut block_map = HashMap::new();
        block_map.insert(entry, 0u32);
        // Note: `target` is intentionally absent.

        assert!(lower_cf_inst(inst, &func, &value_map, &block_map).is_none());
    }

    /// Missing entries in `value_map` make the lowering bail with
    /// `None`. Locks in the value-side of the same contract.
    #[test]
    fn missing_value_returns_none() {
        let mut func = fresh_function();
        let entry = new_block(&mut func);
        let then_blk = new_block(&mut func);
        let else_blk = new_block(&mut func);

        let cond = fresh_i32_value(&mut func, entry);
        let then_call = new_block_call(&mut func, then_blk, &[]);
        let else_call = new_block_call(&mut func, else_blk, &[]);

        let inst = func.dfg.make_inst(InstructionData::Brif {
            opcode: Opcode::Brif,
            arg: cond,
            blocks: [then_call, else_call],
        });

        // `cond` intentionally absent.
        let value_map = HashMap::new();
        let mut block_map = HashMap::new();
        block_map.insert(entry, 0u32);
        block_map.insert(then_blk, 1u32);
        block_map.insert(else_blk, 2u32);

        assert!(lower_cf_inst(inst, &func, &value_map, &block_map).is_none());
    }
}
