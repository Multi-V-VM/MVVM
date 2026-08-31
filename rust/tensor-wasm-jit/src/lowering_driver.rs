// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Module-level lowering driver (wave-2 task W2.4).
//!
//! Walks a [`cranelift_codegen::ir::Function`], dispatches each instruction
//! to the wave-1 per-family lowerers (L1-L6: arith / float / memory / cf /
//! vector / conv), and assembles a [`LoweredFunction`].
//!
//! # Per-family dispatch
//!
//! The wave-1 lowerings were written *before* this driver. Each has a
//! slightly different signature shape — some take `&mut` views of the value
//! map (the arith family inserts its own result mapping), some take `&`
//! views (float / vector / conv allocate a result id but leave the
//! caller to bind the Cranelift result `Value` to it). The driver papers
//! over those inconsistencies:
//!
//! - [`crate::lower_arith::lower_arith_inst`] — `&mut HashMap`, `&mut next_id`,
//!   `Option<LoweredOp>`. **Self-binds** the result.
//! - [`crate::lower_float::lower_float_inst`] — `&HashMap`, `&mut next_id`,
//!   `Option<LoweredOp>`. Driver binds the result.
//! - [`crate::lower_memory::lower_memory_inst`] — takes a
//!   [`crate::lower_memory::MemLowerContext`], returns
//!   `Result<Option<Vec<LoweredOp>>, MemLowerError>`. Driver binds the
//!   load result (the last emitted op's `result()` if the Cranelift inst
//!   produces a value).
//! - [`crate::lower_cf::lower_cf_inst`] — `&HashMap, &HashMap`,
//!   `Option<LoweredOp>`. No result to bind (cf ops are terminators).
//! - [`crate::lower_vector::lower_vector_inst`] — `&HashMap`, `&mut next_id`,
//!   `Option<LoweredOp>`. Driver binds the result.
//! - [`crate::lower_conv::lower_conv_inst`] — `&HashMap`, `&mut next_id`,
//!   `Option<LoweredOp>`. Driver binds the result.
//!
//! Families are tried in the order arith → float → memory → cf → vector →
//! conv. The first one that returns a successful match wins. If none match,
//! the driver returns [`LoweringError::UnsupportedOpcode`].

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::entity::EntityRef;
use cranelift_codegen::ir as cl;

use crate::lower_arith::lower_arith_inst;
use crate::lower_cf::lower_cf_inst;
use crate::lower_conv::lower_conv_inst;
use crate::lower_float::lower_float_inst;
use crate::lower_memory::{lower_memory_inst, MemLowerContext, MemLowerError};
use crate::lower_signature::lower_signature;
use crate::lower_vector::lower_vector_inst;
use crate::lowered_ir::{LoweredBlock, LoweredFunction, LoweredOp, LoweredValueId};
use crate::lowering_builder::LoweringBuilder;
use crate::lowering_errors::{InstLocation, LoweringError};

/// Lower a complete Cranelift [`cl::Function`] to a [`LoweredFunction`].
///
/// Walks the function in layout order, dispatches each instruction through
/// the wave-1 per-family lowerers (arith → float → memory → cf → vector →
/// conv), and assembles the resulting [`LoweredOp`]s into per-block
/// [`LoweredBlock`]s.
///
/// # Errors
///
/// - [`LoweringError::UnsupportedOpcode`] when no wave-1 family matches an
///   instruction. (Signature errors are also surfaced via this variant
///   pending the W2.10 `LoweringError::Signature(#[from] ...)` follow-up;
///   they carry the signature error's `Display` text in the `op` field.)
/// - [`LoweringError::UnsupportedType`] when a memory-family op references
///   a Cranelift type [`crate::lowered_ir::LoweredType`] cannot model.
/// - [`LoweringError::UndefinedValue`] when a memory-family op references
///   a Cranelift `Value` not yet in the value-map (driver bug or
///   ill-formed input function).
///
/// # Notes
///
/// Block parameters of *non-entry* blocks are also seeded into the
/// value-map up front — the wave-1 cf lowerings translate `BlockCall` args
/// using the value map of the *call site*, but a downstream consumer that
/// uses a block param as an operand needs that param to be in the map
/// before its block is walked. We allocate ids for every block param in
/// layout order before walking instructions.
pub fn lower_function(func: &cl::Function) -> Result<LoweredFunction, LoweringError> {
    // ---- 1. Lower the signature --------------------------------------
    //
    // Signature errors don't yet have a dedicated `LoweringError` variant
    // (the `From<SignatureLoweringError>` `#[from]` impl is parked behind
    // the W2.10 sync). For now we fold the signature error into
    // `UnsupportedType` at a sentinel location — the message carries the
    // signature error's `Display` text, which itself names the offending
    // position and type.
    let signature =
        lower_signature(&func.signature).map_err(|err| LoweringError::UnsupportedType {
            ty: err.to_string(),
            location: InstLocation {
                block: 0,
                inst_index: 0,
            },
        })?;

    // ---- 2. Reject-list preflight ------------------------------------
    //
    // jit fix (finding 10): the reject-list is now WIRED into every
    // `lower_function` entry path, not a commented-out placeholder. This
    // guarantees no function reaches the per-family lowerings (and the PTX
    // emit / GPU launch behind them) without first passing the reject-list:
    // floats, integer div/rem, loop back-edges, trap/unreachable, atomics,
    // and host calls are refused up front (see `crate::reject_list`). The
    // per-family lowerings remain a second line of defence (they return
    // `None` for opcodes they don't recognise), but the reject-list is the
    // single chokepoint that closes the "an entry path bypasses the
    // soundness gate" hole.
    if let Some(rejection) = crate::reject_list::check_function(func) {
        return Err(LoweringError::Rejected {
            reason: format!("{:?}", rejection.reason),
            location: InstLocation::new(rejection.block, 0),
        });
    }

    // ---- 3. Pre-allocate block ids in layout order -------------------
    let mut builder = LoweringBuilder::new();
    for block in func.layout.blocks() {
        let _ = builder.get_or_alloc_block(block);
    }

    // ---- 4. Seed block params for every block ------------------------
    //
    // Every block param needs a `LoweredValueId` before we walk
    // instructions: branch-arg lowering reads the param ids of the
    // target block at the call site, but a block param also appears as
    // an *operand* of instructions inside its own block. Allocating up
    // front in layout order guarantees both lookups succeed.
    let mut block_params: HashMap<cl::Block, Vec<(LoweredValueId, cl::Value)>> = HashMap::new();
    for block in func.layout.blocks() {
        let mut params = Vec::new();
        for &param in func.dfg.block_params(block) {
            let id = builder.get_or_alloc_value(param);
            params.push((id, param));
        }
        block_params.insert(block, params);
    }

    // ---- 5. Walk each block, dispatch each instruction ---------------
    //
    // Each `LoweredBlock` carries its params (with their freshly-allocated
    // ids and lowered types) and its ops. The cf lowering reads the
    // builder's `block_map()` to resolve branch targets.
    let mut lowered_blocks: Vec<LoweredBlock> = Vec::new();
    for cl_block in func.layout.blocks() {
        let block_id = builder
            .lookup_block(cl_block)
            .expect("block pre-allocated above");
        let mut lblock = LoweredBlock::new(block_id);

        // Carry the param `(id, lowered_type)` pairs into the block.
        let params = block_params
            .get(&cl_block)
            .expect("block params seeded above");
        for (id, cl_value) in params {
            let cl_ty = func.dfg.value_type(*cl_value);
            let lty =
                crate::lower_signature::cranelift_type_to_lowered(cl_ty).ok_or_else(|| {
                    LoweringError::UnsupportedType {
                        ty: cl_ty.to_string(),
                        location: InstLocation::new(cl_block, 0),
                    }
                })?;
            lblock.params.push((*id, lty));
        }

        // Walk instructions in layout order. `enumerate` gives us the
        // 0-based inst index used in diagnostics.
        for (inst_index, inst) in func.layout.block_insts(cl_block).enumerate() {
            let inst_index = inst_index as u32;
            let location = InstLocation::new(cl_block, inst_index);

            dispatch_inst(inst, func, &mut builder, &mut lblock, location)?;
        }

        lowered_blocks.push(lblock);
    }

    // ---- 6. Finalise -------------------------------------------------
    let entry_cl_block = func
        .layout
        .entry_block()
        .ok_or_else(|| LoweringError::MalformedTerminator { block_id: 0 })?;
    let entry = builder
        .lookup_block(entry_cl_block)
        .expect("entry block pre-allocated above");

    Ok(LoweredFunction {
        name: function_name(func),
        signature,
        blocks: lowered_blocks,
        entry,
    })
}

/// Extract a human-readable name from a Cranelift function for use as the
/// `LoweredFunction::name` (and thus the PTX entry symbol).
///
/// Cranelift's `UserFuncName::Testcase` carries a short byte-string that
/// we surface via its `Display` impl (a leading `%` prefix); `User` carries
/// a `(namespace, index)` pair which we render as `user<ns>_<idx>`. Names
/// are best-effort diagnostic — fingerprinting in [`crate::ir`] hashes the
/// [`LoweredFunction`] structure, not its name.
fn function_name(func: &cl::Function) -> String {
    match &func.name {
        cl::UserFuncName::Testcase(t) => {
            // `TestcaseName` doesn't expose its bytes publicly in
            // cranelift-codegen 0.111; its `Display` impl renders the
            // bytes as `%<utf8>`. We strip the leading `%` so the
            // resulting name is a clean identifier.
            let display = t.to_string();
            display.strip_prefix('%').unwrap_or(&display).to_string()
        }
        cl::UserFuncName::User(u) => format!("user{}_{}", u.namespace, u.index),
    }
}

/// Dispatch a single Cranelift instruction through the wave-1 per-family
/// lowerers and append the resulting op(s) to `lblock`.
///
/// Returns [`LoweringError::UnsupportedOpcode`] if none of the six
/// families recognises the instruction, or one of the other
/// [`LoweringError`] variants for memory-family errors.
fn dispatch_inst(
    inst: cl::Inst,
    func: &cl::Function,
    builder: &mut LoweringBuilder,
    lblock: &mut LoweredBlock,
    location: InstLocation,
) -> Result<(), LoweringError> {
    // ---- L1: arith ---------------------------------------------------
    //
    // The arith family takes `&mut HashMap` + `&mut next_id` and inserts
    // its own result mapping. Borrow split: we need to mutate both at
    // once, so we pull them out together via `value_map_mut` and
    // `next_value_id_mut`. Rust's borrow checker forbids two `&mut`s on
    // the same `LoweringBuilder` simultaneously; the helper splits them
    // into a single scope.
    if let Some(op) = arith_attempt(inst, func, builder) {
        lblock.ops.push(op);
        return Ok(());
    }

    // ---- L2: float ---------------------------------------------------
    if let Some(op) = float_attempt(inst, func, builder) {
        bind_result_if_any(func, inst, &op, builder);
        lblock.ops.push(op);
        return Ok(());
    }

    // ---- L3: memory --------------------------------------------------
    //
    // Memory returns `Result<Option<Vec<...>>, MemLowerError>`. Errors
    // surface as structured `LoweringError` variants; an `Ok(None)`
    // means the family didn't match.
    match memory_attempt(inst, func, builder, location.clone())? {
        Some(ops) => {
            // The Cranelift inst's first result (if any) needs binding to
            // the last `result()`-bearing op in the emitted sequence. For
            // a plain `load`, that's the Load itself. For `stack_load`,
            // the emitted sequence is `[StackAlloc, Load]` and the Load's
            // result is the one that maps to the Cranelift load result.
            let cl_results = func.dfg.inst_results(inst);
            if let Some(cl_result_v) = cl_results.first() {
                if let Some(lid) = ops.iter().rev().find_map(LoweredOp::result) {
                    builder.bind_value(*cl_result_v, lid);
                }
            }
            for op in ops {
                lblock.ops.push(op);
            }
            return Ok(());
        }
        None => {}
    }

    // ---- L4: control flow --------------------------------------------
    //
    // The cf family takes a read-only block_map. We snapshot it into a
    // local so the borrow doesn't conflict with the rest of the builder.
    if let Some(op) = cf_attempt(inst, func, builder) {
        // cf ops are terminators — no result mapping needed.
        lblock.ops.push(op);
        return Ok(());
    }

    // ---- L5: vector --------------------------------------------------
    if let Some(op) = vector_attempt(inst, func, builder) {
        bind_result_if_any(func, inst, &op, builder);
        lblock.ops.push(op);
        return Ok(());
    }

    // ---- L6: conversion ---------------------------------------------
    if let Some(op) = conv_attempt(inst, func, builder) {
        bind_result_if_any(func, inst, &op, builder);
        lblock.ops.push(op);
        return Ok(());
    }

    // No family matched — this is the "we forgot to wire it up" path.
    let opcode = func.dfg.insts[inst].opcode();
    Err(LoweringError::UnsupportedOpcode {
        op: format!("{opcode:?}"),
        location,
    })
}

/// Bind the Cranelift result `Value` of `inst` (if any) to the lowered op's
/// `result()` id.
///
/// Called for families that allocate a result id but leave the binding to
/// the caller (float / vector / conv). A no-op when the Cranelift inst
/// produces no result or when the lowered op carries no result.
fn bind_result_if_any(
    func: &cl::Function,
    inst: cl::Inst,
    op: &LoweredOp,
    builder: &mut LoweringBuilder,
) {
    if let Some(lid) = op.result() {
        if let Some(&cl_result_v) = func.dfg.inst_results(inst).first() {
            builder.bind_value(cl_result_v, lid);
        }
    }
}

// ---- Per-family attempt helpers --------------------------------------
//
// Each `*_attempt` helper exists to keep the borrows on `LoweringBuilder`
// scoped to a single statement. Without these, the dispatch function
// would have to juggle simultaneous `&mut`/`&` borrows of the same
// builder across all six families in one big match expression.

fn arith_attempt(
    inst: cl::Inst,
    func: &cl::Function,
    builder: &mut LoweringBuilder,
) -> Option<LoweredOp> {
    // arith needs `&mut HashMap` + `&mut next_id`. The borrow checker
    // refuses to hand out two `&mut` views into the same struct at
    // once, so we go through a single split helper that returns both
    // borrows from one call.
    //
    // `LoweringBuilder` doesn't expose such a helper directly, but
    // `value_map_mut()` borrows `&mut self` and so does
    // `next_value_id_mut()`. We sidestep this by calling them in
    // sequence within a single `unsafe`-free block: we lift the
    // `next_id` value into a local, run the family, then write the
    // updated counter back.
    let mut next_id_local: LoweredValueId = *builder.next_value_id_mut();
    let result = lower_arith_inst(inst, func, builder.value_map_mut(), &mut next_id_local);
    *builder.next_value_id_mut() = next_id_local;
    result
}

fn float_attempt(
    inst: cl::Inst,
    func: &cl::Function,
    builder: &mut LoweringBuilder,
) -> Option<LoweredOp> {
    let mut next_id_local: LoweredValueId = *builder.next_value_id_mut();
    let result = lower_float_inst(inst, func, builder.value_map(), &mut next_id_local);
    *builder.next_value_id_mut() = next_id_local;
    result
}

fn memory_attempt(
    inst: cl::Inst,
    func: &cl::Function,
    builder: &mut LoweringBuilder,
    location: InstLocation,
) -> Result<Option<Vec<LoweredOp>>, LoweringError> {
    // jit MED fix (finding 6): use the builder's split-borrow accessor
    // instead of cloning the whole `value_map` on every memory
    // instruction. The previous code cloned `value_map` (O(values)) once
    // per memory instruction (O(memory-instructions)) — i.e. O(V·M) total —
    // and lifted the stack-slot map into a local that then had to be
    // reinserted (jit finding 3's workaround). With the split borrow the
    // memory context borrows the live maps directly: no per-instruction
    // allocation, and any stack slot the memory lowering allocates is
    // written straight into the builder's own map, so the one-slot-one-
    // alloca property (jit finding 3) holds with no reinsert step.
    let result = {
        let (value_map, stack_slot_map, next_value_id) = builder.split_borrow_for_memory();
        let mut ctx = MemLowerContext {
            // The wave-1 memory lowering does not actually read this
            // field yet; 0 is a placeholder. Wave 2's kernel-arg
            // refinement will populate it properly.
            linear_memory_base: 0,
            value_map,
            stack_slot_map,
            next_value_id,
        };
        lower_memory_inst(inst, func, &mut ctx)
    };

    match result {
        Ok(some_ops) => Ok(some_ops),
        Err(MemLowerError::UnmappedValue(v)) => {
            Err(LoweringError::UndefinedValue { value: v, location })
        }
        Err(MemLowerError::UnsupportedType(ty)) => {
            Err(LoweringError::UnsupportedType { ty, location })
        }
        // Fail closed instead of allowing the lowering value allocator to
        // wrap and alias an existing SSA id. Upstream 0.3.8 added this
        // MemLowerError variant but omitted it from this dispatcher, which
        // made the advertised cuda-oxide-backend feature uncompilable.
        Err(MemLowerError::SsaOverflow) => Err(LoweringError::Rejected {
            reason: "memory lowering exhausted the u32 SSA value-id space".to_owned(),
            location,
        }),
    }
}

fn cf_attempt(
    inst: cl::Inst,
    func: &cl::Function,
    builder: &mut LoweringBuilder,
) -> Option<LoweredOp> {
    // `lower_cf_inst` takes `&HashMap, &HashMap`. We can hand it the
    // builder's accessor refs directly.
    lower_cf_inst(inst, func, builder.value_map(), builder.block_map())
}

fn vector_attempt(
    inst: cl::Inst,
    func: &cl::Function,
    builder: &mut LoweringBuilder,
) -> Option<LoweredOp> {
    let mut next_id_local: LoweredValueId = *builder.next_value_id_mut();
    let result = lower_vector_inst(inst, func, builder.value_map(), &mut next_id_local);
    *builder.next_value_id_mut() = next_id_local;
    result
}

fn conv_attempt(
    inst: cl::Inst,
    func: &cl::Function,
    builder: &mut LoweringBuilder,
) -> Option<LoweredOp> {
    let mut next_id_local: LoweredValueId = *builder.next_value_id_mut();
    let result = lower_conv_inst(inst, func, builder.value_map(), &mut next_id_local);
    *builder.next_value_id_mut() = next_id_local;
    result
}

// Silence the unused-import lint for `EntityRef`; it's pulled in for
// readability when reading `block.index()` in adjacent code paths, but the
// driver itself only calls `EntityRef` transitively through
// `InstLocation::new`.
#[allow(dead_code)]
fn _entity_ref_witness() -> &'static str {
    let _: fn(cl::Block) -> usize = |b| b.index();
    "ok"
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lowered_ir::{LoweredOp, LoweredType};
    use crate::lowering_test_support::function_with_binary_op;
    use cranelift_codegen::cursor::{Cursor, FuncCursor};
    use cranelift_codegen::ir::{
        types, AbiParam, BlockCall, Function, InstBuilder, InstructionData, Opcode, Signature,
        UserFuncName,
    };
    use cranelift_codegen::isa::CallConv;

    /// `lower_function` on `fn(i32, i32) -> i32 { iadd; return }` produces
    /// one block, two ops `[AddI, Return]`, and the signature `(I32, I32)
    /// -> I32`.
    #[test]
    fn lowers_single_block_iadd_return() {
        let (func, _inst) = function_with_binary_op(Opcode::Iadd, types::I32);
        let lowered = lower_function(&func).expect("must lower");

        assert_eq!(lowered.blocks.len(), 1, "one block expected");
        assert_eq!(
            lowered.signature.params,
            vec![LoweredType::I32, LoweredType::I32]
        );
        assert_eq!(lowered.signature.returns, vec![LoweredType::I32]);

        let block = &lowered.blocks[0];
        assert_eq!(block.ops.len(), 2, "AddI + Return");
        assert!(matches!(block.ops[0], LoweredOp::AddI { .. }));
        assert!(matches!(block.ops[1], LoweredOp::Return { .. }));
        // Well-formed: ends in a terminator.
        assert!(block.is_well_formed());
        assert!(lowered.is_well_formed());
    }

    /// Same shape, but `fadd` over `F32` → ops `[AddF, Return]`.
    #[test]
    fn lowers_single_block_fadd_return() {
        let (func, _inst) = function_with_binary_op(Opcode::Fadd, types::F32);
        let lowered = lower_function(&func).expect("must lower");

        assert_eq!(lowered.blocks.len(), 1);
        assert_eq!(
            lowered.signature.params,
            vec![LoweredType::F32, LoweredType::F32]
        );
        assert_eq!(lowered.signature.returns, vec![LoweredType::F32]);

        let block = &lowered.blocks[0];
        assert_eq!(block.ops.len(), 2);
        match &block.ops[0] {
            LoweredOp::AddF { ty, .. } => assert_eq!(*ty, LoweredType::F32),
            other => panic!("expected AddF, got {other:?}"),
        }
        assert!(matches!(block.ops[1], LoweredOp::Return { .. }));
    }

    /// An opcode no family handles must surface as `UnsupportedOpcode`.
    ///
    /// We use `Opcode::Iconst` (a `UnaryImm` form) — it's intentionally
    /// outside every wave-1 family's match set (arith handles binary ints,
    /// not constants; conv handles type changes, not literals; the others
    /// don't touch immediates).
    #[test]
    fn errors_on_unsupported_opcode_iconst() {
        let mut sig = Signature::new(CallConv::SystemV);
        sig.returns.push(AbiParam::new(types::I32));
        let mut func =
            Function::with_name_signature(UserFuncName::testcase("iconst_fn".as_bytes()), sig);
        let block = func.dfg.make_block();
        func.layout.append_block(block);

        let mut cursor = FuncCursor::new(&mut func).at_bottom(block);
        let v = cursor.ins().iconst(types::I32, 7);
        cursor.ins().return_(&[v]);

        let err = lower_function(&func).expect_err("iconst is not in any wave-1 family");
        match err {
            LoweringError::UnsupportedOpcode { op, .. } => {
                assert!(
                    op.contains("Iconst"),
                    "expected Iconst in the error op, got {op:?}",
                );
            }
            other => panic!("expected UnsupportedOpcode, got {other:?}"),
        }
    }

    /// Multi-block function: entry block jumps to a second block which
    /// returns. The lowered function must have two blocks, both with
    /// well-formed terminators, and the entry id must match the entry
    /// block's allocated id.
    #[test]
    fn lowers_multi_block_jump_then_return() {
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I32));
        sig.returns.push(AbiParam::new(types::I32));
        let mut func =
            Function::with_name_signature(UserFuncName::testcase("two_block".as_bytes()), sig);

        // Two blocks. Entry takes one i32 param; the second block also
        // takes one i32 param (which we'll forward via the jump's block
        // args). The second block returns the value it received.
        let entry = func.dfg.make_block();
        let entry_param = func.dfg.append_block_param(entry, types::I32);
        let next = func.dfg.make_block();
        let next_param = func.dfg.append_block_param(next, types::I32);

        func.layout.append_block(entry);
        func.layout.append_block(next);

        // Entry: jump next(entry_param).
        let block_call = BlockCall::new(next, &[entry_param], &mut func.dfg.value_lists);
        let jump_inst = func.dfg.make_inst(InstructionData::Jump {
            opcode: Opcode::Jump,
            destination: block_call,
        });
        func.dfg.make_inst_results(jump_inst, types::INVALID);
        func.layout.append_inst(jump_inst, entry);

        // Next: return next_param.
        {
            let mut cursor = FuncCursor::new(&mut func).at_bottom(next);
            cursor.ins().return_(&[next_param]);
        }

        let lowered = lower_function(&func).expect("multi-block must lower");
        assert_eq!(lowered.blocks.len(), 2, "two blocks expected");

        // Entry block must have an op `Br { target: <next-id> }`.
        let entry_lblock = &lowered.blocks[0];
        assert_eq!(entry_lblock.ops.len(), 1, "entry has one op (Br)");
        match &entry_lblock.ops[0] {
            LoweredOp::Br { target, args } => {
                assert_eq!(*target, lowered.blocks[1].id);
                assert_eq!(args.len(), 1);
            }
            other => panic!("expected Br, got {other:?}"),
        }

        // Next block must have a Return op.
        let next_lblock = &lowered.blocks[1];
        assert_eq!(next_lblock.ops.len(), 1, "next has one op (Return)");
        assert!(matches!(next_lblock.ops[0], LoweredOp::Return { .. }));

        // Well-formed overall.
        assert!(lowered.is_well_formed());
        // The entry id of the lowered function must equal the first block's id.
        assert_eq!(lowered.entry, entry_lblock.id);
    }

    /// An empty `Function::new()` has no entry block → must surface as
    /// `MalformedTerminator { block_id: 0 }` (the closest existing
    /// variant — wave 2 doesn't yet carry a dedicated "no entry block"
    /// error).
    #[test]
    fn errors_on_function_without_entry_block() {
        // SystemV is the workspace default that `lower_signature` accepts,
        // so the signature step passes; the failure surfaces at the
        // entry-block lookup.
        let func = Function::new();
        let err = lower_function(&func).expect_err("no entry block");
        match err {
            LoweringError::MalformedTerminator { block_id } => {
                assert_eq!(block_id, 0);
            }
            other => panic!("expected MalformedTerminator, got {other:?}"),
        }
    }

    /// Signature errors surface through the driver. An I128 param can't
    /// be lowered; the driver wraps the signature error's display string
    /// in an `UnsupportedType` variant at a sentinel location.
    #[test]
    fn errors_on_unsupported_signature_type() {
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I128));
        let func = Function::with_name_signature(UserFuncName::default(), sig);

        let err = lower_function(&func).expect_err("I128 param must reject");
        match err {
            LoweringError::UnsupportedType { ty, .. } => {
                assert!(
                    ty.contains("i128"),
                    "expected i128 in the error display, got {ty:?}",
                );
            }
            other => panic!("expected UnsupportedType, got {other:?}"),
        }
    }

    /// Sanity: the `function_name` helper handles `UserFuncName::Testcase`
    /// and `UserFuncName::User` shapes.
    #[test]
    fn function_name_renders_testcase_and_user() {
        let f1 = Function::with_name_signature(
            UserFuncName::testcase("k1".as_bytes()),
            Signature::new(CallConv::SystemV),
        );
        assert_eq!(function_name(&f1), "k1");

        let f2 = Function::with_name_signature(
            UserFuncName::user(2, 5),
            Signature::new(CallConv::SystemV),
        );
        assert_eq!(function_name(&f2), "user2_5");
    }
}
