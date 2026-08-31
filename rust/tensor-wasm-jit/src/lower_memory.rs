// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Wave-1 memory-family lowering — Cranelift IR memory ops to
//! [`crate::lowered_ir::LoweredOp`].
//!
//! Handles the four memory opcodes from the [Cranelift → dialect-mir
//! mapping table](crate::pliron_dialect#mapping-table):
//!
//! | Cranelift op   | Lowering                                             |
//! |----------------|------------------------------------------------------|
//! | `load`         | [`LoweredOp::Load`] (device-pointer base)            |
//! | `store`        | [`LoweredOp::Store`] (device-pointer base)           |
//! | `stack_load`   | [`LoweredOp::StackAlloc`] (first touch) + `Load`     |
//! | `stack_store`  | [`LoweredOp::StackAlloc`] (first touch) + `Store`    |
//!
//! # Device-pointer translation
//!
//! Cranelift `load`/`store` operate on guest linear-memory offsets. The
//! lowered `Load`/`Store` operate on a *device* pointer. The wave-1 contract
//! here is intentionally simple: the caller supplies a
//! [`MemLowerContext::linear_memory_base`] — the SSA value id of the
//! kernel's device-resident linear-memory base pointer (kernel arg 0 by
//! convention) — and the [`MemLowerContext::value_map`] threads the
//! Cranelift base-value pointer through to a [`LoweredValueId`]. Wave 2
//! refines kernel-arg threading (resolving when the Cranelift base value
//! itself was the linear-memory pointer vs. an interior pointer).
//!
//! # Stack-slot lazy alloca
//!
//! PTX has no stack. Cranelift stack slots become SSA-local
//! [`LoweredOp::StackAlloc`] ops; the downstream `mem2reg` pass promotes
//! them to registers. To keep `LoweredFunction` minimal, the alloca is
//! emitted **lazily** — the first `stack_load`/`stack_store` that touches
//! a slot emits the `StackAlloc` and records the alloca's pointer in
//! [`MemLowerContext::stack_slot_map`]. Subsequent accesses to the same
//! slot reuse the pointer.

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::ir::{self, InstructionData, Opcode};

use crate::lowered_ir::{LoweredOp, LoweredType, LoweredValueId};

/// Errors produced by the memory-family lowering.
#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum MemLowerError {
    /// The Cranelift base-pointer value referenced by a `load`/`store`
    /// has no corresponding [`LoweredValueId`] in
    /// [`MemLowerContext::value_map`]. The caller must populate the
    /// value map before invoking this family — typically by lowering
    /// the producer instruction first or by seeding entry-block params.
    #[error("memory lowering: Cranelift value {0:?} not in value_map")]
    UnmappedValue(ir::Value),

    /// The Cranelift type on a `load` result or `store` operand could
    /// not be mapped to a [`LoweredType`] (e.g. an unsupported vector
    /// shape). The mnemonic of the offending Cranelift type is
    /// captured for triage.
    #[error("memory lowering: unsupported Cranelift type `{0}`")]
    UnsupportedType(String),

    /// The SSA value-id counter overflowed `u32::MAX` (a single function
    /// exceeded ~4 billion lowered values).
    ///
    /// jit LOW fix (finding 7): `fresh_id` previously used `wrapping_add`,
    /// which silently wrapped the counter back to 0 on overflow — aliasing
    /// new SSA values onto already-used ids and miscompiling the function.
    /// It now uses `checked_add(1)` and surfaces this structured error
    /// instead, matching the `lower_arith` family's fail-closed posture.
    #[error("memory lowering: SSA value-id counter overflowed u32::MAX")]
    SsaOverflow,
}

/// Context threaded through the memory-family lowering.
///
/// Carries the device-pointer base (kernel-arg 0 by convention), the
/// running value-id allocator, and the bookkeeping maps for translating
/// Cranelift [`ir::Value`] / [`ir::StackSlot`] into [`LoweredValueId`].
///
/// The caller owns all four state pieces: this type is a borrow bundle,
/// not an owner. Lifetime `'a` ties the borrows together so callers can
/// thread one context through a block walk without re-binding.
#[derive(Debug)]
pub struct MemLowerContext<'a> {
    /// SSA value id of the kernel's device-resident linear-memory base
    /// pointer. By convention this is the lowered id of kernel arg 0.
    /// Wave-1 lowering does not yet *use* this id (regular `load`/`store`
    /// take their base directly from [`Self::value_map`]); it is plumbed
    /// here so the wave-2 kernel-arg refinement can land without
    /// changing the signature.
    pub linear_memory_base: LoweredValueId,

    /// Map from Cranelift [`ir::Value`] → [`LoweredValueId`]. Populated by
    /// the caller before invoking this family. A miss on a base-pointer
    /// lookup is a [`MemLowerError::UnmappedValue`].
    pub value_map: &'a HashMap<ir::Value, LoweredValueId>,

    /// Map from Cranelift [`ir::StackSlot`] → [`LoweredValueId`] of the
    /// alloca's pointer. Populated lazily by this family the first time
    /// a `stack_load`/`stack_store` references the slot.
    pub stack_slot_map: &'a mut HashMap<ir::StackSlot, LoweredValueId>,

    /// Next free [`LoweredValueId`]. Incremented by [`Self::fresh_id`]
    /// each time a new id is handed out.
    pub next_value_id: &'a mut LoweredValueId,
}

impl<'a> MemLowerContext<'a> {
    /// Allocate and return the next fresh [`LoweredValueId`], advancing
    /// the caller's counter.
    ///
    /// jit LOW fix (finding 7): standardize on `checked_add(1)` (the
    /// `lower_arith` idiom) and return a structured
    /// [`MemLowerError::SsaOverflow`] on overflow rather than `wrapping_add`,
    /// which silently aliased fresh ids onto already-used ones once the
    /// counter wrapped.
    pub fn fresh_id(&mut self) -> Result<LoweredValueId, MemLowerError> {
        let id = *self.next_value_id;
        *self.next_value_id = id.checked_add(1).ok_or(MemLowerError::SsaOverflow)?;
        Ok(id)
    }

    /// Look up the [`LoweredValueId`] for a Cranelift value, returning
    /// [`MemLowerError::UnmappedValue`] if the value has not been
    /// lowered yet.
    pub fn lookup_value(&self, v: ir::Value) -> Result<LoweredValueId, MemLowerError> {
        self.value_map
            .get(&v)
            .copied()
            .ok_or(MemLowerError::UnmappedValue(v))
    }
}

/// Lower a Cranelift memory-family instruction (load / store /
/// stack_load / stack_store) to one or more [`LoweredOp`]s.
///
/// Returns:
///
/// - `Ok(None)` if `inst` is not a memory-family opcode this lowering
///   handles. The caller routes to a different family (arith, float,
///   vector, cf, conv).
/// - `Ok(Some(ops))` with the lowered ops in emission order. Regular
///   `load`/`store` produce a single op. `stack_load`/`stack_store` may
///   produce two ops on the first touch of a slot (a [`LoweredOp::StackAlloc`]
///   followed by the load/store); on subsequent touches the alloca is
///   reused and the vec has one element.
/// - `Err(MemLowerError)` if the instruction references a value not yet
///   in [`MemLowerContext::value_map`] or has a Cranelift type the wave-1
///   lowering cannot express.
///
/// The caller is responsible for inserting the returned ops into the
/// target [`crate::lowered_ir::LoweredBlock`] in order and for recording
/// the load/stack_load result in `ctx.value_map` against the Cranelift
/// result value id.
pub fn lower_memory_inst(
    inst: ir::Inst,
    func: &ir::Function,
    ctx: &mut MemLowerContext<'_>,
) -> Result<Option<Vec<LoweredOp>>, MemLowerError> {
    let data = &func.dfg.insts[inst];
    match data {
        InstructionData::Load {
            opcode: Opcode::Load,
            arg,
            offset,
            ..
        } => {
            let base = ctx.lookup_value(*arg)?;
            let result_val = func.dfg.first_result(inst);
            let ty = cranelift_type_to_lowered(func.dfg.value_type(result_val))?;
            let result_id = ctx.fresh_id()?;
            // Caller is expected to record `result_val -> result_id` in
            // its value_map after this function returns. We do not do it
            // here because `value_map` is borrowed immutably.
            Ok(Some(vec![LoweredOp::Load {
                ty,
                base,
                offset: (*offset).into(),
                result: result_id,
            }]))
        }
        InstructionData::Store {
            opcode: Opcode::Store,
            args,
            offset,
            ..
        } => {
            // args[0] = value being stored, args[1] = base pointer (see
            // cranelift-codegen's instruction-builder generator).
            let value_v = args[0];
            let base_v = args[1];
            let value = ctx.lookup_value(value_v)?;
            let base = ctx.lookup_value(base_v)?;
            let ty = cranelift_type_to_lowered(func.dfg.value_type(value_v))?;
            Ok(Some(vec![LoweredOp::Store {
                ty,
                value,
                base,
                offset: (*offset).into(),
            }]))
        }
        InstructionData::StackLoad {
            opcode: Opcode::StackLoad,
            stack_slot,
            offset,
        } => {
            let mut emitted = Vec::with_capacity(2);
            let result_val = func.dfg.first_result(inst);
            let ty = cranelift_type_to_lowered(func.dfg.value_type(result_val))?;
            let alloca_ptr = ensure_stack_alloca(*stack_slot, func, ty.clone(), ctx, &mut emitted)?;
            let result_id = ctx.fresh_id()?;
            emitted.push(LoweredOp::Load {
                ty,
                base: alloca_ptr,
                offset: (*offset).into(),
                result: result_id,
            });
            Ok(Some(emitted))
        }
        InstructionData::StackStore {
            opcode: Opcode::StackStore,
            arg,
            stack_slot,
            offset,
        } => {
            let value = ctx.lookup_value(*arg)?;
            let ty = cranelift_type_to_lowered(func.dfg.value_type(*arg))?;
            let mut emitted = Vec::with_capacity(2);
            let alloca_ptr = ensure_stack_alloca(*stack_slot, func, ty.clone(), ctx, &mut emitted)?;
            emitted.push(LoweredOp::Store {
                ty,
                value,
                base: alloca_ptr,
                offset: (*offset).into(),
            });
            Ok(Some(emitted))
        }
        _ => Ok(None),
    }
}

/// Emit a [`LoweredOp::StackAlloc`] for `stack_slot` the first time it
/// is touched, returning the alloca's pointer id. On subsequent calls
/// for the same slot, the cached pointer id is returned and no op is
/// emitted into `out`.
///
/// The element type passed in is whatever the first touch decided —
/// `mem2reg` downstream re-types the alloca's uses so this initial
/// choice is informational, not load-bearing.
fn ensure_stack_alloca(
    stack_slot: ir::StackSlot,
    func: &ir::Function,
    ty: LoweredType,
    ctx: &mut MemLowerContext<'_>,
    out: &mut Vec<LoweredOp>,
) -> Result<LoweredValueId, MemLowerError> {
    if let Some(existing) = ctx.stack_slot_map.get(&stack_slot) {
        return Ok(*existing);
    }
    let bytes = func.sized_stack_slots[stack_slot].size;
    let ptr_id = ctx.fresh_id()?;
    out.push(LoweredOp::StackAlloc {
        ty,
        bytes,
        result: ptr_id,
    });
    ctx.stack_slot_map.insert(stack_slot, ptr_id);
    Ok(ptr_id)
}

/// Translate a Cranelift [`ir::Type`] to a [`LoweredType`] for the
/// purposes of memory-family lowering.
///
/// Wave-1 supports the scalar integer + float types and the opaque
/// pointer width. Vector loads are deferred to wave-2 (the v0.4 detector
/// already rejects candidates that need them — see the "Unsupported in
/// v0.4" notes in [`crate::pliron_dialect`]).
fn cranelift_type_to_lowered(ty: ir::Type) -> Result<LoweredType, MemLowerError> {
    use cranelift_codegen::ir::types;
    Ok(match ty {
        types::I8 => LoweredType::I8,
        types::I16 => LoweredType::I16,
        types::I32 => LoweredType::I32,
        types::I64 => LoweredType::I64,
        types::F32 => LoweredType::F32,
        types::F64 => LoweredType::F64,
        other => return Err(MemLowerError::UnsupportedType(other.to_string())),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::ir::immediates::Offset32;
    use cranelift_codegen::ir::{
        types, AbiParam, Function, MemFlags, Signature, StackSlotData, StackSlotKind, UserFuncName,
    };
    use cranelift_codegen::isa::CallConv;

    /// Helper: build a fresh Function with a single block, an `i64` base
    /// pointer block param, and an `i32` second block param (for the
    /// value-to-store cases). Returns the function plus the two values
    /// and an empty value map seeded with them.
    fn skeleton() -> (
        Function,
        ir::Value,
        ir::Value,
        HashMap<ir::Value, LoweredValueId>,
    ) {
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I64)); // ptr-ish base
        sig.params.push(AbiParam::new(types::I32)); // store value
        let mut func = Function::with_name_signature(UserFuncName::default(), sig);
        let block = func.dfg.make_block();
        let base_v = func.dfg.append_block_param(block, types::I64);
        let val_v = func.dfg.append_block_param(block, types::I32);
        let mut map = HashMap::new();
        map.insert(base_v, 100);
        map.insert(val_v, 101);
        (func, base_v, val_v, map)
    }

    #[test]
    fn lower_load_produces_single_load_op() {
        let (mut func, base_v, _val_v, value_map) = skeleton();
        // Build `v_res = load.i32 base_v + 8`.
        let inst = func.dfg.make_inst(InstructionData::Load {
            opcode: Opcode::Load,
            arg: base_v,
            flags: MemFlags::new(),
            offset: Offset32::new(8),
        });
        func.dfg.make_inst_results(inst, types::I32);

        let mut stack_map = HashMap::new();
        let mut next_id: LoweredValueId = 200;
        let mut ctx = MemLowerContext {
            linear_memory_base: 100,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let out = lower_memory_inst(inst, &func, &mut ctx)
            .expect("lowering must succeed")
            .expect("inst is a memory op");
        assert_eq!(out.len(), 1);
        match &out[0] {
            LoweredOp::Load {
                ty,
                base,
                offset,
                result,
            } => {
                assert_eq!(*ty, LoweredType::I32);
                assert_eq!(*base, 100);
                assert_eq!(*offset, 8);
                assert_eq!(*result, 200);
            }
            other => panic!("expected Load, got {other:?}"),
        }
        assert_eq!(next_id, 201);
    }

    /// jit LOW fix (finding 7): when the SSA value-id counter is at
    /// `u32::MAX`, allocating a fresh id for a load result must return the
    /// structured `SsaOverflow` error rather than wrapping the counter back
    /// to 0 (which would alias the new value onto id 0).
    #[test]
    fn fresh_id_overflow_is_structured_error_not_wrap() {
        let (mut func, base_v, _val_v, value_map) = skeleton();
        let inst = func.dfg.make_inst(InstructionData::Load {
            opcode: Opcode::Load,
            arg: base_v,
            flags: MemFlags::new(),
            offset: Offset32::new(0),
        });
        func.dfg.make_inst_results(inst, types::I32);

        let mut stack_map = HashMap::new();
        let mut next_id: LoweredValueId = u32::MAX;
        let mut ctx = MemLowerContext {
            linear_memory_base: 100,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let err = lower_memory_inst(inst, &func, &mut ctx)
            .expect_err("counter at u32::MAX must overflow, not wrap");
        assert_eq!(err, MemLowerError::SsaOverflow);
    }

    #[test]
    fn lower_store_produces_single_store_op() {
        let (mut func, base_v, val_v, value_map) = skeleton();
        // Build `store.i32 flags, val_v, base_v, -4`.
        let inst = func.dfg.make_inst(InstructionData::Store {
            opcode: Opcode::Store,
            args: [val_v, base_v],
            flags: MemFlags::new(),
            offset: Offset32::new(-4),
        });
        // Store has no result, but make_inst_results is safe with 0 results.
        func.dfg.make_inst_results(inst, types::I32);

        let mut stack_map = HashMap::new();
        let mut next_id: LoweredValueId = 300;
        let mut ctx = MemLowerContext {
            linear_memory_base: 100,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let out = lower_memory_inst(inst, &func, &mut ctx)
            .expect("lowering must succeed")
            .expect("inst is a memory op");
        assert_eq!(out.len(), 1);
        match &out[0] {
            LoweredOp::Store {
                ty,
                value,
                base,
                offset,
            } => {
                assert_eq!(*ty, LoweredType::I32);
                assert_eq!(*value, 101);
                assert_eq!(*base, 100);
                assert_eq!(*offset, -4);
            }
            other => panic!("expected Store, got {other:?}"),
        }
        // Store does not produce a fresh id.
        assert_eq!(next_id, 300);
    }

    #[test]
    fn lower_stack_load_first_touch_emits_alloca_then_load() {
        let (mut func, _base_v, _val_v, value_map) = skeleton();
        let ss =
            func.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 16, 0));
        let inst = func.dfg.make_inst(InstructionData::StackLoad {
            opcode: Opcode::StackLoad,
            stack_slot: ss,
            offset: Offset32::new(0),
        });
        func.dfg.make_inst_results(inst, types::I32);

        let mut stack_map = HashMap::new();
        let mut next_id: LoweredValueId = 400;
        let mut ctx = MemLowerContext {
            linear_memory_base: 100,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let out = lower_memory_inst(inst, &func, &mut ctx)
            .expect("lowering must succeed")
            .expect("inst is a memory op");
        assert_eq!(out.len(), 2, "first touch emits alloca + load");
        match &out[0] {
            LoweredOp::StackAlloc { ty, bytes, result } => {
                assert_eq!(*ty, LoweredType::I32);
                assert_eq!(*bytes, 16);
                assert_eq!(*result, 400);
            }
            other => panic!("expected StackAlloc, got {other:?}"),
        }
        match &out[1] {
            LoweredOp::Load {
                ty,
                base,
                offset,
                result,
            } => {
                assert_eq!(*ty, LoweredType::I32);
                assert_eq!(*base, 400, "Load reuses the alloca's pointer id");
                assert_eq!(*offset, 0);
                assert_eq!(*result, 401);
            }
            other => panic!("expected Load, got {other:?}"),
        }
        assert_eq!(stack_map.get(&ss).copied(), Some(400));
        assert_eq!(next_id, 402);
    }

    #[test]
    fn lower_stack_load_second_touch_reuses_alloca() {
        let (mut func, _base_v, _val_v, value_map) = skeleton();
        let ss =
            func.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 8, 0));
        let inst = func.dfg.make_inst(InstructionData::StackLoad {
            opcode: Opcode::StackLoad,
            stack_slot: ss,
            offset: Offset32::new(0),
        });
        func.dfg.make_inst_results(inst, types::I32);

        let mut stack_map = HashMap::new();
        // Pre-seed: pretend the alloca was already emitted with id 999.
        stack_map.insert(ss, 999);
        let mut next_id: LoweredValueId = 500;
        let mut ctx = MemLowerContext {
            linear_memory_base: 100,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let out = lower_memory_inst(inst, &func, &mut ctx)
            .expect("lowering must succeed")
            .expect("inst is a memory op");
        assert_eq!(out.len(), 1, "subsequent touches skip the alloca");
        match &out[0] {
            LoweredOp::Load { base, result, .. } => {
                assert_eq!(*base, 999);
                assert_eq!(*result, 500);
            }
            other => panic!("expected Load, got {other:?}"),
        }
        assert_eq!(next_id, 501);
    }

    #[test]
    fn lower_stack_store_first_touch_emits_alloca_then_store() {
        let (mut func, _base_v, val_v, value_map) = skeleton();
        let ss =
            func.create_sized_stack_slot(StackSlotData::new(StackSlotKind::ExplicitSlot, 4, 0));
        let inst = func.dfg.make_inst(InstructionData::StackStore {
            opcode: Opcode::StackStore,
            arg: val_v,
            stack_slot: ss,
            offset: Offset32::new(0),
        });
        func.dfg.make_inst_results(inst, types::I32);

        let mut stack_map = HashMap::new();
        let mut next_id: LoweredValueId = 600;
        let mut ctx = MemLowerContext {
            linear_memory_base: 100,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let out = lower_memory_inst(inst, &func, &mut ctx)
            .expect("lowering must succeed")
            .expect("inst is a memory op");
        assert_eq!(out.len(), 2, "first touch emits alloca + store");
        match &out[0] {
            LoweredOp::StackAlloc { ty, bytes, result } => {
                assert_eq!(*ty, LoweredType::I32);
                assert_eq!(*bytes, 4);
                assert_eq!(*result, 600);
            }
            other => panic!("expected StackAlloc, got {other:?}"),
        }
        match &out[1] {
            LoweredOp::Store {
                ty,
                value,
                base,
                offset,
            } => {
                assert_eq!(*ty, LoweredType::I32);
                assert_eq!(*value, 101);
                assert_eq!(*base, 600);
                assert_eq!(*offset, 0);
            }
            other => panic!("expected Store, got {other:?}"),
        }
        // Store does not consume a fresh id, but the alloca did.
        assert_eq!(next_id, 601);
    }

    #[test]
    fn lower_returns_none_for_non_memory_op() {
        // Build an iconst instruction — not in this family.
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I32));
        let mut func = Function::with_name_signature(UserFuncName::default(), sig);
        let block = func.dfg.make_block();
        let _arg = func.dfg.append_block_param(block, types::I32);
        let inst = func.dfg.make_inst(InstructionData::UnaryImm {
            opcode: Opcode::Iconst,
            imm: 42i64.into(),
        });
        func.dfg.make_inst_results(inst, types::I32);

        let value_map: HashMap<ir::Value, LoweredValueId> = HashMap::new();
        let mut stack_map = HashMap::new();
        let mut next_id: LoweredValueId = 700;
        let mut ctx = MemLowerContext {
            linear_memory_base: 0,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let out = lower_memory_inst(inst, &func, &mut ctx).expect("lowering must succeed");
        assert!(out.is_none(), "iconst is not a memory op");
        assert_eq!(next_id, 700);
    }

    #[test]
    fn unmapped_base_returns_error() {
        // Build a load whose base value is *not* in the value_map.
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I64));
        let mut func = Function::with_name_signature(UserFuncName::default(), sig);
        let block = func.dfg.make_block();
        let base_v = func.dfg.append_block_param(block, types::I64);
        let inst = func.dfg.make_inst(InstructionData::Load {
            opcode: Opcode::Load,
            arg: base_v,
            flags: MemFlags::new(),
            offset: Offset32::new(0),
        });
        func.dfg.make_inst_results(inst, types::I32);

        let value_map: HashMap<ir::Value, LoweredValueId> = HashMap::new();
        let mut stack_map = HashMap::new();
        let mut next_id: LoweredValueId = 800;
        let mut ctx = MemLowerContext {
            linear_memory_base: 0,
            value_map: &value_map,
            stack_slot_map: &mut stack_map,
            next_value_id: &mut next_id,
        };
        let err = lower_memory_inst(inst, &func, &mut ctx).expect_err("unmapped base must error");
        assert_eq!(err, MemLowerError::UnmappedValue(base_v));
    }

    #[test]
    fn cranelift_type_to_lowered_supports_scalars() {
        assert_eq!(
            cranelift_type_to_lowered(types::I8).unwrap(),
            LoweredType::I8
        );
        assert_eq!(
            cranelift_type_to_lowered(types::I32).unwrap(),
            LoweredType::I32
        );
        assert_eq!(
            cranelift_type_to_lowered(types::F64).unwrap(),
            LoweredType::F64
        );
    }

    #[test]
    fn cranelift_type_to_lowered_rejects_vector() {
        let err = cranelift_type_to_lowered(types::I32X4).unwrap_err();
        assert!(
            matches!(err, MemLowerError::UnsupportedType(_)),
            "vector types are deferred to wave-2"
        );
    }
}
