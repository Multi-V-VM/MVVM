// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! W3.2 — convert a wave-1 [`LoweredFunction`] into a pliron 0.15
//! `Ptr<Operation>` graph rooted at a `builtin.func` op.
//!
//! # What this module does
//!
//! Wave 3 of the Pliron pipeline: walks a [`LoweredFunction`] (the
//! per-family wave-1 IR contract, defined in [`crate::lowered_ir`]) and
//! emits a pliron 0.15 IR graph rooted at a `builtin.func` operation. The
//! returned [`Ptr<Operation>`](pliron::context::Ptr) is the function op
//! itself — the caller decides whether to wrap it in a `builtin.module`,
//! verify it, or hand it to a further-downstream pass (mem2reg →
//! dialect-llvm → PTX, per RFC 0001).
//!
//! # What this module is NOT (yet)
//!
//! - A fully-wired converter for every [`LoweredOp`] variant. The wave-1
//!   IR has 30+ variants; this initial pass wires up the ones that exercise
//!   pliron's API surface (arith integer, arith float, memory load/store,
//!   control-flow branch/return). The rest return
//!   [`PlironConversionError::NotYetWired`] with a `&'static str` naming
//!   the missing variant so the W3.3 author can grep + extend without
//!   re-deriving the API decisions documented in this file.
//! - A custom dialect registered in the pliron `Context`. The ops emitted
//!   by this module are wired via the [`pliron::derive::pliron_op`] proc
//!   macro and *registered automatically at link time* via the
//!   `linkme`/`inventory` slices pliron exposes — meaning a fresh
//!   `Context::new()` already has the `twasm` dialect's ops available.
//!   No explicit `Dialect::register` call is necessary.
//! - A `dialect-mir` faithful naming. The op names below (e.g.
//!   `twasm.addi`) intentionally use the `twasm.` prefix rather than the
//!   `arith.` / `memref.` / `cf.` names from the [mapping
//!   table](crate::pliron_dialect#mapping-table). The mapping is
//!   semantic-only at this stage: a later W3.x agent can rename to match
//!   cuda-oxide's exact dialect names once the cuda-oxide host crates land
//!   in wave 4. The op names are an internal contract, not part of the
//!   public API of this crate.
//!
//! # API surface and stability
//!
//! The single public entry point is
//! [`lowered_function_to_pliron`]. The returned `Ptr<Operation>` is owned
//! by the supplied `&mut Context` (pliron's arena model). The
//! [`PlironConversionError`] enum is the stable error contract —
//! `NotYetWired` is the variant W3.3 expects to grow zero new instances
//! of as it lands more lowerings.
//!
//! # Pliron 0.15 API notes (for W3.3)
//!
//! The cuda-oxide RFC 0001 mapping table named target ops as
//! `arith.addi`, `memref.load`, etc. — those are MLIR dialect names, NOT
//! ops pliron 0.15 exposes out of the box. Pliron 0.15 ships only a
//! `builtin` dialect (`builtin.module`, `builtin.func`, `builtin.integer`
//! type, `builtin.fp32`/`fp64` types, `builtin.function` function type).
//! Every other op is user-defined via `#[pliron_op(name = "dialect.opname",
//! ...)]`. This module defines the minimum set under `twasm.`.
//!
//! Key API gotchas this module worked around:
//!
//! - [`Operation::new`] is the universal constructor — there's no separate
//!   `OperationBuilder`. Takes `result_types: Vec<Ptr<TypeObj>>`, `operands:
//!   Vec<Value>`, `successors: Vec<Ptr<BasicBlock>>`, `num_regions: usize`.
//! - [`Value`](pliron::value::Value) is `Copy + Hash + Eq` — safe to clone
//!   into a `HashMap<LoweredValueId, Value>`.
//! - [`FuncOp::new`] auto-creates an "entry" block whose args match the
//!   function signature. We reuse it for the wave-1 entry block instead of
//!   creating a fresh one.
//! - Block args are exposed via `BasicBlock::get_argument(idx)` returning
//!   a `Value`. Op results similarly via `Operation::get_result(idx)`.
//! - Branches (the only ops with successors) pass the target
//!   `Ptr<BasicBlock>` via the `successors` arg of `Operation::new`.
//!   Block-argument forwarding (the `args` field on
//!   [`LoweredOp::Br`](crate::lowered_ir::LoweredOp::Br)) is encoded by
//!   adding those values to the *operand* list — wave-1 already orders
//!   them — and the downstream pass interprets the operands as block
//!   arguments via the op's interface contract. Wiring a richer
//!   "branch with block-args" interface (à la MLIR's `BranchOpInterface`)
//!   is left to W3.3 once the cuda-oxide host crates are available.
//! - All ops we emit are `verifier = "succ"` (success). Strict verification
//!   (operand/result count, types) belongs to a separate W3.x pass.

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use pliron::{
    basic_block::BasicBlock,
    builtin::{
        op_interfaces::OneRegionInterface,
        ops::FuncOp,
        types::{FP32Type, FP64Type, FunctionType, IntegerType, Signedness},
    },
    context::{Context, Ptr},
    derive::pliron_op,
    op::Op,
    operation::Operation,
    r#type::{TypeObj, TypePtr},
    value::Value,
};
use thiserror::Error;

use crate::lowered_ir::{LoweredBlockId, LoweredFunction, LoweredOp, LoweredType, LoweredValueId};

// ---------------------------------------------------------------------------
// Custom `twasm.*` dialect ops.
//
// Each op is wired with `pliron_op` so pliron's link-time registration
// (`linkme` on native, `inventory` on wasm) picks it up. Verifier is
// "succ" (success) at this stage — operand/result count + type checks
// belong to a separate pass that the W3.3 agent can add.
//
// One op per LoweredOp variant we wire below. The naming follows the
// mapping table semantics (e.g. `twasm.addi` for `arith.addi`) without
// claiming MLIR dialect equivalence.
// ---------------------------------------------------------------------------

/// Integer addition (`twasm.addi`). Maps from [`LoweredOp::AddI`].
#[pliron_op(name = "twasm.addi", format, verifier = "succ")]
pub struct AddIOp;

/// Integer subtraction (`twasm.subi`). Maps from [`LoweredOp::SubI`].
#[pliron_op(name = "twasm.subi", format, verifier = "succ")]
pub struct SubIOp;

/// Integer multiplication (`twasm.muli`). Maps from [`LoweredOp::MulI`].
#[pliron_op(name = "twasm.muli", format, verifier = "succ")]
pub struct MulIOp;

/// Float addition (`twasm.addf`). Maps from [`LoweredOp::AddF`].
#[pliron_op(name = "twasm.addf", format, verifier = "succ")]
pub struct AddFOp;

/// Float subtraction (`twasm.subf`). Maps from [`LoweredOp::SubF`].
#[pliron_op(name = "twasm.subf", format, verifier = "succ")]
pub struct SubFOp;

/// Float multiplication (`twasm.mulf`). Maps from [`LoweredOp::MulF`].
#[pliron_op(name = "twasm.mulf", format, verifier = "succ")]
pub struct MulFOp;

/// Memory load (`twasm.load`). Maps from [`LoweredOp::Load`].
#[pliron_op(name = "twasm.load", format, verifier = "succ")]
pub struct LoadOp;

/// Memory store (`twasm.store`). Maps from [`LoweredOp::Store`].
#[pliron_op(name = "twasm.store", format, verifier = "succ")]
pub struct StoreOp;

/// Unconditional branch (`twasm.br`). Maps from [`LoweredOp::Br`].
///
/// Successors live on `Operation::successors`; block-argument forwarding
/// is encoded via the operand list.
#[pliron_op(name = "twasm.br", format, verifier = "succ")]
pub struct BrOp;

/// Conditional branch (`twasm.cond_br`). Maps from [`LoweredOp::CondBr`].
///
/// Operands: `[cond, then_args..., else_args...]`. Successors: `[then,
/// else]`. The split-point between then-args and else-args is the same
/// as the wave-1 IR's split.
#[pliron_op(name = "twasm.cond_br", format, verifier = "succ")]
pub struct CondBrOp;

/// Function return (`twasm.return`). Maps from [`LoweredOp::Return`].
#[pliron_op(name = "twasm.return", format, verifier = "succ")]
pub struct ReturnOp;

// jit fix (finding 10): additional LLVM-free `twasm.*` ops. These cover
// integer/structural `LoweredOp` variants whose lowering needs only pliron
// op definitions (no system LLVM / `pliron-llvm-backend`), so they are
// wired here rather than left as `NotYetWired`. The float and vector
// variants stay unwired (the former are reject-listed upstream; the latter
// need a `twasm.vector` type pliron 0.15 does not ship — see the module
// docs and the `NotYetWired` arms below).

/// Conditional select (`twasm.select`). Maps from [`LoweredOp::Select`].
/// Operands `[cond, then_v, else_v]`, one result.
#[pliron_op(name = "twasm.select", format, verifier = "succ")]
pub struct SelectOp;

/// Bit reinterpretation (`twasm.bitcast`). Maps from [`LoweredOp::Bitcast`].
/// One operand, one result of the destination type.
#[pliron_op(name = "twasm.bitcast", format, verifier = "succ")]
pub struct BitcastOp;

/// Integer truncation (`twasm.trunci`). Maps from [`LoweredOp::TruncI`].
#[pliron_op(name = "twasm.trunci", format, verifier = "succ")]
pub struct TruncIOp;

/// Zero-extending integer widening (`twasm.extui`). Maps from
/// [`LoweredOp::ExtendU`].
#[pliron_op(name = "twasm.extui", format, verifier = "succ")]
pub struct ExtendUOp;

/// Sign-extending integer widening (`twasm.extsi`). Maps from
/// [`LoweredOp::ExtendS`].
#[pliron_op(name = "twasm.extsi", format, verifier = "succ")]
pub struct ExtendSOp;

/// Stack allocation (`twasm.alloca`). Maps from [`LoweredOp::StackAlloc`].
/// Zero operands, one pointer-typed result. The element type and byte size
/// are recorded as attributes (see [`set_stack_alloc_attrs`]).
#[pliron_op(name = "twasm.alloca", format, verifier = "succ")]
pub struct AllocaOp;

// ---------------------------------------------------------------------------
// Error type.
// ---------------------------------------------------------------------------

/// Errors produced by [`lowered_function_to_pliron`].
///
/// `NotYetWired` is the explicit "this LoweredOp variant has no emitter
/// yet — a W3.3+ agent should add one" sentinel. `Context` covers
/// pliron-level errors surfaced through `String`-ified messages (pliron's
/// own [`Result`](pliron::result::Result) carries opaque error chains;
/// stringifying at the conversion boundary keeps our public API independent
/// of pliron's internal `Error` shape).
#[derive(Debug, Error)]
pub enum PlironConversionError {
    /// The converter does not yet emit pliron ops for this
    /// [`LoweredOp`] variant. Future agents add a case to
    /// [`emit_op`] and remove this error path.
    #[error("pliron_lowering: variant {variant} not yet wired (TODO)")]
    NotYetWired {
        /// Name of the missing variant (e.g. `"VMin"`, `"Fma"`).
        variant: &'static str,
    },

    /// A pliron API call returned an error. The wrapped `String` is
    /// pliron's `Display`-format error message — opaque on purpose, since
    /// pliron's internal `Error` type isn't part of our public surface.
    #[error("pliron_lowering: pliron context error: {0}")]
    Context(String),

    /// An SSA value id referenced by a [`LoweredOp`] operand was never
    /// defined (no block-arg or earlier op produced it). Indicates a
    /// malformed [`LoweredFunction`]; the wave-1 lowering driver should
    /// have caught this. Surfaced here so the converter is robust against
    /// future driver bugs rather than panicking.
    #[error("pliron_lowering: undefined SSA value id {0}")]
    UndefinedValue(LoweredValueId),

    /// A branch op targets a [`LoweredBlockId`] that the function does
    /// not declare. Same malformed-IR class as [`Self::UndefinedValue`].
    #[error("pliron_lowering: undefined block id {0}")]
    UndefinedBlock(LoweredBlockId),
}

// ---------------------------------------------------------------------------
// Type translation.
// ---------------------------------------------------------------------------

/// Translate a wave-1 [`LoweredType`] into a pliron [`Ptr<TypeObj>`].
///
/// `Ptr`, `Bool`, and `V128` are not yet wired — those require either
/// pointer / opaque types or vector types that pliron 0.15's builtin
/// dialect doesn't ship. Future agents extend this with a custom
/// `twasm.ptr` / `twasm.vector<N x T>` type via `pliron_type`.
fn translate_type(
    ctx: &mut Context,
    ty: &LoweredType,
) -> Result<Ptr<TypeObj>, PlironConversionError> {
    match ty {
        LoweredType::I8 => Ok(IntegerType::get(ctx, 8, Signedness::Signless).into()),
        LoweredType::I16 => Ok(IntegerType::get(ctx, 16, Signedness::Signless).into()),
        LoweredType::I32 => Ok(IntegerType::get(ctx, 32, Signedness::Signless).into()),
        LoweredType::I64 => Ok(IntegerType::get(ctx, 64, Signedness::Signless).into()),
        LoweredType::F32 => Ok(FP32Type::get(ctx).into()),
        LoweredType::F64 => Ok(FP64Type::get(ctx).into()),
        // i64 placeholder for ptr — wave-3 still needs a proper twasm.ptr
        // type, but i64 is the right pointer-width for the sm_80 baseline
        // and lets the converter emit something useful for Load/Store
        // until the dedicated type lands.
        LoweredType::Ptr => Ok(IntegerType::get(ctx, 64, Signedness::Signless).into()),
        // i1 — pliron's builtin has no dedicated bool, MLIR convention is
        // i1. PTX lowers to a `pred` register downstream.
        LoweredType::Bool => Ok(IntegerType::get(ctx, 1, Signedness::Signless).into()),
        LoweredType::V128 { .. } => Err(PlironConversionError::NotYetWired {
            variant: "LoweredType::V128",
        }),
    }
}

/// Attribute key under which the byte offset of a `twasm.load` /
/// `twasm.store` is recorded.
const TWASM_OFFSET_ATTR: &str = "twasm.byte_offset";

/// Record a load/store byte offset on the `twasm.load` / `twasm.store`
/// operation `op`.
///
/// jit fix (finding 10): the wave-1 `LoweredOp::Load` / `Store` byte offset
/// was previously DROPPED (the `offset: _` match), so a non-zero offset
/// produced a load/store against the wrong address — a silent miscompile.
/// We now persist the offset on the op as a `builtin.string` attribute
/// (the `twasm.*` ops carry no dedicated offset operand/attr schema yet, so
/// a string attribute is the schema-free way to thread it through to the
/// downstream `twasm.* → llvm.*` rewrite, where it folds into an LLVM GEP).
///
/// A zero offset is recorded too so the attribute is always present and the
/// downstream rewrite can read it unconditionally. The offset is stored as
/// its decimal text; the downstream consumer parses it back with
/// `str::parse::<i32>()`.
fn set_load_store_offset(ctx: &mut Context, op: Ptr<Operation>, offset: i32) {
    // `Identifier::try_new` only fails on a malformed identifier; the
    // constant key here is a valid identifier, so the `Ok` path always
    // taken. We fall back to skipping the annotation (rather than
    // panicking) on the impossible error path.
    if let Ok(key) = pliron::identifier::Identifier::try_new(TWASM_OFFSET_ATTR.to_string()) {
        let attr = pliron::builtin::attributes::StringAttr::new(offset.to_string());
        op.deref_mut(ctx).attributes.set(key, attr);
    }
}

// ---------------------------------------------------------------------------
// Conversion state.
// ---------------------------------------------------------------------------

/// Per-conversion state: maps wave-1 SSA value ids to pliron [`Value`]s,
/// and wave-1 [`LoweredBlockId`]s to pliron [`Ptr<BasicBlock>`]s.
struct ConversionState {
    values: HashMap<LoweredValueId, Value>,
    blocks: HashMap<LoweredBlockId, Ptr<BasicBlock>>,
}

impl ConversionState {
    fn new() -> Self {
        Self {
            values: HashMap::new(),
            blocks: HashMap::new(),
        }
    }

    fn get_value(&self, id: LoweredValueId) -> Result<Value, PlironConversionError> {
        self.values
            .get(&id)
            .copied()
            .ok_or(PlironConversionError::UndefinedValue(id))
    }

    fn get_block(&self, id: LoweredBlockId) -> Result<Ptr<BasicBlock>, PlironConversionError> {
        self.blocks
            .get(&id)
            .copied()
            .ok_or(PlironConversionError::UndefinedBlock(id))
    }

    fn map_value(&mut self, id: LoweredValueId, value: Value) {
        self.values.insert(id, value);
    }

    fn map_block(&mut self, id: LoweredBlockId, block: Ptr<BasicBlock>) {
        self.blocks.insert(id, block);
    }
}

// ---------------------------------------------------------------------------
// Public entry point.
// ---------------------------------------------------------------------------

/// Convert a wave-1 [`LoweredFunction`] into a pliron `builtin.func`
/// operation.
///
/// The returned `Ptr<Operation>` is the function op itself. The caller
/// owns deciding whether to wrap it in a `builtin.module`, verify it, or
/// hand it to a further pass.
///
/// # Errors
///
/// - [`PlironConversionError::NotYetWired`] if `func` contains a
///   [`LoweredOp`] variant the converter doesn't yet handle.
/// - [`PlironConversionError::UndefinedValue`] / [`PlironConversionError::UndefinedBlock`]
///   if `func` is malformed (refers to ids that don't exist).
/// - [`PlironConversionError::Context`] for any pliron API-level
///   failure.
pub fn lowered_function_to_pliron(
    ctx: &mut Context,
    func: &LoweredFunction,
) -> Result<Ptr<Operation>, PlironConversionError> {
    // ---- 1. Build the function type ------------------------------------
    let param_types: Vec<Ptr<TypeObj>> = func
        .signature
        .params
        .iter()
        .map(|t| translate_type(ctx, t))
        .collect::<Result<_, _>>()?;
    let return_types: Vec<Ptr<TypeObj>> = func
        .signature
        .returns
        .iter()
        .map(|t| translate_type(ctx, t))
        .collect::<Result<_, _>>()?;
    let func_ty: TypePtr<FunctionType> = FunctionType::get(ctx, param_types.clone(), return_types);

    // ---- 2. Create the FuncOp -----------------------------------------
    //
    // FuncOp::new requires a valid Identifier for the symbol name. Wave-1
    // already enforces non-empty + identifier-shaped names through the
    // detector reject list, but we guard here so a bad name surfaces
    // through our typed error instead of panicking in pliron internals.
    let name_ident: pliron::identifier::Identifier = func
        .name
        .as_str()
        .try_into()
        .map_err(|e| PlironConversionError::Context(format!("invalid function name: {e}")))?;
    let func_op = FuncOp::new(ctx, name_ident, func_ty);
    let func_op_ptr = func_op.get_operation();

    // ---- 3. Set up the block map. --------------------------------------
    //
    // FuncOp::new creates one "entry" block with the signature's args.
    // We reuse it for the wave-1 entry block and create fresh blocks for
    // the rest. We must do this *before* emitting any op, because branches
    // need to resolve target-block pointers.
    let mut state = ConversionState::new();
    let entry_block = func_op.get_entry_block(ctx);
    let entry_lowered = func
        .blocks
        .iter()
        .find(|b| b.id == func.entry)
        .ok_or(PlironConversionError::UndefinedBlock(func.entry))?;

    // Map entry-block param ids to entry block's pliron arguments.
    {
        let entry_block_ref = entry_block.deref(ctx);
        for (idx, (param_id, _)) in entry_lowered.params.iter().enumerate() {
            // Sanity: the entry block's params must match the function
            // signature length. If they don't, surface as Context error
            // rather than panic.
            if idx >= entry_block_ref.get_num_arguments() {
                return Err(PlironConversionError::Context(format!(
                    "entry block has {} params but FuncOp entry block has {} args",
                    entry_lowered.params.len(),
                    entry_block_ref.get_num_arguments()
                )));
            }
            state
                .values
                .insert(*param_id, entry_block_ref.get_argument(idx));
        }
    }
    state.map_block(entry_lowered.id, entry_block);

    // Create fresh BasicBlocks for every non-entry block, mapping their
    // param ids to the new blocks' arguments.
    let region = func_op.get_region(ctx);
    for lowered_block in &func.blocks {
        if lowered_block.id == func.entry {
            continue;
        }
        let arg_types: Vec<Ptr<TypeObj>> = lowered_block
            .params
            .iter()
            .map(|(_, ty)| translate_type(ctx, ty))
            .collect::<Result<_, _>>()?;
        let label = format!("bb{}", lowered_block.id).as_str().try_into().ok();
        let bb = BasicBlock::new(ctx, label, arg_types);
        bb.insert_at_back(region, ctx);
        {
            let bb_ref = bb.deref(ctx);
            for (idx, (param_id, _)) in lowered_block.params.iter().enumerate() {
                state.values.insert(*param_id, bb_ref.get_argument(idx));
            }
        }
        state.map_block(lowered_block.id, bb);
    }

    // ---- 4. Emit ops for each block, in declaration order. -------------
    for lowered_block in &func.blocks {
        let bb = state.get_block(lowered_block.id)?;
        for op in &lowered_block.ops {
            emit_op(ctx, &mut state, bb, op)?;
        }
    }

    Ok(func_op_ptr)
}

// ---------------------------------------------------------------------------
// Op emission.
// ---------------------------------------------------------------------------

/// Emit a single [`LoweredOp`] into `bb`, threading SSA mappings through
/// `state`. Returns `Ok(())` on a successful append.
fn emit_op(
    ctx: &mut Context,
    state: &mut ConversionState,
    bb: Ptr<BasicBlock>,
    op: &LoweredOp,
) -> Result<(), PlironConversionError> {
    match op {
        // ---- Arith integer (depth-wired) -------------------------------
        LoweredOp::AddI {
            ty,
            lhs,
            rhs,
            result,
        } => emit_binary::<AddIOp>(ctx, state, bb, ty, *lhs, *rhs, *result),
        LoweredOp::SubI {
            ty,
            lhs,
            rhs,
            result,
        } => emit_binary::<SubIOp>(ctx, state, bb, ty, *lhs, *rhs, *result),
        LoweredOp::MulI {
            ty,
            lhs,
            rhs,
            result,
        } => emit_binary::<MulIOp>(ctx, state, bb, ty, *lhs, *rhs, *result),

        // ---- Arith float (depth-wired) ---------------------------------
        LoweredOp::AddF {
            ty,
            lhs,
            rhs,
            result,
        } => emit_binary::<AddFOp>(ctx, state, bb, ty, *lhs, *rhs, *result),
        LoweredOp::SubF {
            ty,
            lhs,
            rhs,
            result,
        } => emit_binary::<SubFOp>(ctx, state, bb, ty, *lhs, *rhs, *result),
        LoweredOp::MulF {
            ty,
            lhs,
            rhs,
            result,
        } => emit_binary::<MulFOp>(ctx, state, bb, ty, *lhs, *rhs, *result),

        // ---- Memory (depth-wired) --------------------------------------
        LoweredOp::Load {
            ty,
            base,
            offset,
            result,
        } => {
            // jit fix (finding 10): the byte offset is now HANDLED rather
            // than silently dropped. Previously `offset: _` discarded a
            // non-zero offset and emitted a load from `base+0` — a silent
            // miscompile reading the wrong address. The `twasm.load` op
            // carries no offset attribute schema yet, so the offset is
            // recorded on the op via [`set_load_store_offset`] when it is
            // non-zero, and the zero case (what the wave-1 pointer-load
            // driver produces) emits exactly as before. The non-zero path
            // is preserved on the op for the downstream `twasm.* → llvm.*`
            // rewrite (which will fold it into an LLVM GEP); it is NOT
            // dropped.
            let base_v = state.get_value(*base)?;
            let result_ty = translate_type(ctx, ty)?;
            let pop = Operation::new(
                ctx,
                <LoadOp as Op>::get_concrete_op_info(),
                vec![result_ty],
                vec![base_v],
                vec![],
                0,
            );
            set_load_store_offset(ctx, pop, *offset);
            pop.insert_at_back(bb, ctx);
            let v = pop.deref(ctx).get_result(0);
            state.map_value(*result, v);
            Ok(())
        }
        LoweredOp::Store {
            ty: _,
            value,
            base,
            offset,
        } => {
            // jit fix (finding 10): record the store offset on the op
            // (same rationale as Load above) rather than dropping it.
            let v = state.get_value(*value)?;
            let base_v = state.get_value(*base)?;
            let pop = Operation::new(
                ctx,
                <StoreOp as Op>::get_concrete_op_info(),
                vec![],
                vec![v, base_v],
                vec![],
                0,
            );
            set_load_store_offset(ctx, pop, *offset);
            pop.insert_at_back(bb, ctx);
            Ok(())
        }

        // ---- Control flow (depth-wired) --------------------------------
        LoweredOp::Br { target, args } => {
            let target_bb = state.get_block(*target)?;
            let operand_vals: Vec<Value> = args
                .iter()
                .map(|id| state.get_value(*id))
                .collect::<Result<_, _>>()?;
            let pop = Operation::new(
                ctx,
                <BrOp as Op>::get_concrete_op_info(),
                vec![],
                operand_vals,
                vec![target_bb],
                0,
            );
            pop.insert_at_back(bb, ctx);
            Ok(())
        }
        LoweredOp::CondBr {
            cond,
            then_target,
            then_args,
            else_target,
            else_args,
        } => {
            let cond_v = state.get_value(*cond)?;
            let then_bb = state.get_block(*then_target)?;
            let else_bb = state.get_block(*else_target)?;
            let mut operand_vals = vec![cond_v];
            for id in then_args.iter().chain(else_args.iter()) {
                operand_vals.push(state.get_value(*id)?);
            }
            let pop = Operation::new(
                ctx,
                <CondBrOp as Op>::get_concrete_op_info(),
                vec![],
                operand_vals,
                vec![then_bb, else_bb],
                0,
            );
            pop.insert_at_back(bb, ctx);
            Ok(())
        }
        LoweredOp::Return { values } => {
            let operand_vals: Vec<Value> = values
                .iter()
                .map(|id| state.get_value(*id))
                .collect::<Result<_, _>>()?;
            let pop = Operation::new(
                ctx,
                <ReturnOp as Op>::get_concrete_op_info(),
                vec![],
                operand_vals,
                vec![],
                0,
            );
            pop.insert_at_back(bb, ctx);
            Ok(())
        }

        // ---- LLVM-free ops wired in jit finding 10 ---------------------
        LoweredOp::StackAlloc { ty, bytes, result } => {
            // Zero operands → one pointer-typed result. The element type +
            // byte size ride as attributes (the result type is the pointer
            // width per `translate_type(Ptr)`).
            let ptr_ty = translate_type(ctx, &LoweredType::Ptr)?;
            let pop = Operation::new(
                ctx,
                <AllocaOp as Op>::get_concrete_op_info(),
                vec![ptr_ty],
                vec![],
                vec![],
                0,
            );
            set_stack_alloc_attrs(ctx, pop, ty, *bytes);
            pop.insert_at_back(bb, ctx);
            let v = pop.deref(ctx).get_result(0);
            state.map_value(*result, v);
            Ok(())
        }
        LoweredOp::Select {
            ty,
            cond,
            then_v,
            else_v,
            result,
        } => {
            let cond_v = state.get_value(*cond)?;
            let then_val = state.get_value(*then_v)?;
            let else_val = state.get_value(*else_v)?;
            let result_ty = translate_type(ctx, ty)?;
            let pop = Operation::new(
                ctx,
                <SelectOp as Op>::get_concrete_op_info(),
                vec![result_ty],
                vec![cond_v, then_val, else_val],
                vec![],
                0,
            );
            pop.insert_at_back(bb, ctx);
            let v = pop.deref(ctx).get_result(0);
            state.map_value(*result, v);
            Ok(())
        }
        LoweredOp::Bitcast {
            to_ty, src, result, ..
        } => emit_unary_cast::<BitcastOp>(ctx, state, bb, to_ty, *src, *result),
        LoweredOp::TruncI {
            to_ty, src, result, ..
        } => emit_unary_cast::<TruncIOp>(ctx, state, bb, to_ty, *src, *result),
        LoweredOp::ExtendU {
            to_ty, src, result, ..
        } => emit_unary_cast::<ExtendUOp>(ctx, state, bb, to_ty, *src, *result),
        LoweredOp::ExtendS {
            to_ty, src, result, ..
        } => emit_unary_cast::<ExtendSOp>(ctx, state, bb, to_ty, *src, *result),

        // ---- Still NotYetWired -----------------------------------------
        //
        // These remain unwired and DOCUMENTED as such:
        //
        // * `DivS`/`DivU`/`RemS`/`RemU` — integer div/rem trap on
        //   divide-by-zero in Wasm; the reject-list refuses them upstream
        //   (jit finding 4), so they cannot reach a real lowering until the
        //   trap is emulated. Left unwired deliberately.
        // * `DivF`/`Fma`/`FNeg`/`FAbs` — all float; reject-listed upstream
        //   (jit finding 4) until bit-exact PTX rounding is proven.
        // * `Switch` — needs an LLVM `switch` terminator with a case table;
        //   the `twasm.*` dialect has no switch-with-cases op yet and the
        //   real lowering is the LLVM-backed path (gated, needs LLVM 221).
        // * `VMin`/`VMax`/`VSplat`/`VSelect`/`VAllTrue`/`VAnyTrue` — all
        //   require a `twasm.vector<N x T>` type that pliron 0.15's builtin
        //   dialect does not ship (see `translate_type`'s `V128` arm). A
        //   future agent adds the type via `pliron_type`, then these wire
        //   like the scalar ops above. NOT an LLVM dependency, but a
        //   dialect-type prerequisite this change deliberately scopes out.
        LoweredOp::DivS { .. } => Err(PlironConversionError::NotYetWired { variant: "DivS" }),
        LoweredOp::DivU { .. } => Err(PlironConversionError::NotYetWired { variant: "DivU" }),
        LoweredOp::RemS { .. } => Err(PlironConversionError::NotYetWired { variant: "RemS" }),
        LoweredOp::RemU { .. } => Err(PlironConversionError::NotYetWired { variant: "RemU" }),
        LoweredOp::DivF { .. } => Err(PlironConversionError::NotYetWired { variant: "DivF" }),
        LoweredOp::Fma { .. } => Err(PlironConversionError::NotYetWired { variant: "Fma" }),
        LoweredOp::FNeg { .. } => Err(PlironConversionError::NotYetWired { variant: "FNeg" }),
        LoweredOp::FAbs { .. } => Err(PlironConversionError::NotYetWired { variant: "FAbs" }),
        LoweredOp::Switch { .. } => Err(PlironConversionError::NotYetWired { variant: "Switch" }),
        LoweredOp::VMin { .. } => Err(PlironConversionError::NotYetWired { variant: "VMin" }),
        LoweredOp::VMax { .. } => Err(PlironConversionError::NotYetWired { variant: "VMax" }),
        LoweredOp::VSplat { .. } => Err(PlironConversionError::NotYetWired { variant: "VSplat" }),
        LoweredOp::VSelect { .. } => Err(PlironConversionError::NotYetWired { variant: "VSelect" }),
        LoweredOp::VAllTrue { .. } => Err(PlironConversionError::NotYetWired {
            variant: "VAllTrue",
        }),
        LoweredOp::VAnyTrue { .. } => Err(PlironConversionError::NotYetWired {
            variant: "VAnyTrue",
        }),
    }
}

/// Helper: emit a one-operand, one-result cast op of concrete pliron-Op
/// type `O` whose result type is `to_ty`. Shared by Bitcast / TruncI /
/// ExtendU / ExtendS (jit finding 10) — all single-source conversions.
fn emit_unary_cast<O: Op>(
    ctx: &mut Context,
    state: &mut ConversionState,
    bb: Ptr<BasicBlock>,
    to_ty: &LoweredType,
    src: LoweredValueId,
    result: LoweredValueId,
) -> Result<(), PlironConversionError> {
    let src_v = state.get_value(src)?;
    let result_ty = translate_type(ctx, to_ty)?;
    let pop = Operation::new(
        ctx,
        <O as Op>::get_concrete_op_info(),
        vec![result_ty],
        vec![src_v],
        vec![],
        0,
    );
    pop.insert_at_back(bb, ctx);
    let v = pop.deref(ctx).get_result(0);
    state.map_value(result, v);
    Ok(())
}

/// Record the element type + byte size of a `twasm.alloca` as string
/// attributes (jit finding 10). The `twasm.alloca` op carries no dedicated
/// schema for these yet, so — like the load/store offset — they ride as
/// `builtin.string` attributes for the downstream rewrite to consume.
fn set_stack_alloc_attrs(ctx: &mut Context, op: Ptr<Operation>, ty: &LoweredType, bytes: u32) {
    if let Ok(key) = pliron::identifier::Identifier::try_new("twasm.alloca_bytes".to_string()) {
        let attr = pliron::builtin::attributes::StringAttr::new(bytes.to_string());
        op.deref_mut(ctx).attributes.set(key, attr);
    }
    if let Ok(key) = pliron::identifier::Identifier::try_new("twasm.alloca_elem".to_string()) {
        let attr = pliron::builtin::attributes::StringAttr::new(format!("{ty}"));
        op.deref_mut(ctx).attributes.set(key, attr);
    }
}

/// Helper: emit a two-operand, one-result op of concrete pliron-Op type
/// `O`. Shared between the AddI/SubI/MulI/AddF/SubF/MulF arms because
/// they all have the identical shape (binary, type-preserving).
fn emit_binary<O: Op>(
    ctx: &mut Context,
    state: &mut ConversionState,
    bb: Ptr<BasicBlock>,
    ty: &LoweredType,
    lhs: LoweredValueId,
    rhs: LoweredValueId,
    result: LoweredValueId,
) -> Result<(), PlironConversionError> {
    let lhs_v = state.get_value(lhs)?;
    let rhs_v = state.get_value(rhs)?;
    let result_ty = translate_type(ctx, ty)?;
    let pop = Operation::new(
        ctx,
        <O as Op>::get_concrete_op_info(),
        vec![result_ty],
        vec![lhs_v, rhs_v],
        vec![],
        0,
    );
    pop.insert_at_back(bb, ctx);
    let v = pop.deref(ctx).get_result(0);
    state.map_value(result, v);
    Ok(())
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lowered_ir::{LoweredBlock, LoweredSignature};
    use pliron::linked_list::ContainsLinkedList;

    /// Build a single-block function: `(i32, i32) -> i32` returning
    /// `lhs + rhs`.
    fn addi_fn() -> LoweredFunction {
        let mut f = LoweredFunction::new(
            "add",
            LoweredSignature {
                params: vec![LoweredType::I32, LoweredType::I32],
                returns: vec![LoweredType::I32],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        b0.params = vec![(1, LoweredType::I32), (2, LoweredType::I32)];
        b0.ops.push(LoweredOp::AddI {
            ty: LoweredType::I32,
            lhs: 1,
            rhs: 2,
            result: 3,
        });
        b0.ops.push(LoweredOp::Return { values: vec![3] });
        f.blocks.push(b0);
        f.entry = 0;
        f
    }

    /// Build a single-block function: `(f32, f32) -> f32` returning
    /// `lhs + rhs`.
    fn addf_fn() -> LoweredFunction {
        let mut f = LoweredFunction::new(
            "addf",
            LoweredSignature {
                params: vec![LoweredType::F32, LoweredType::F32],
                returns: vec![LoweredType::F32],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        b0.params = vec![(1, LoweredType::F32), (2, LoweredType::F32)];
        b0.ops.push(LoweredOp::AddF {
            ty: LoweredType::F32,
            lhs: 1,
            rhs: 2,
            result: 3,
        });
        b0.ops.push(LoweredOp::Return { values: vec![3] });
        f.blocks.push(b0);
        f.entry = 0;
        f
    }

    #[test]
    fn convert_addi_single_block() {
        let mut ctx = Context::new();
        let func = addi_fn();
        let op =
            lowered_function_to_pliron(&mut ctx, &func).expect("AddI conversion should succeed");

        // The returned op should be a builtin.func.
        let op_ref = op.deref(&ctx);
        let opid = Operation::get_opid(op, &ctx);
        assert_eq!(
            opid.to_string(),
            "builtin.func",
            "expected builtin.func, got {opid}"
        );

        // It should have one region with one block (the entry).
        assert_eq!(op_ref.regions().count(), 1);
        let region = op_ref.get_region(0);
        let block_count = region.deref(&ctx).iter(&ctx).count();
        assert_eq!(block_count, 1, "expected 1 block, got {block_count}");

        // The entry block should contain exactly 2 ops: addi + return.
        let entry = region.deref(&ctx).get_head().expect("entry block");
        let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
        assert_eq!(
            entry_ops.len(),
            2,
            "expected 2 ops, got {}",
            entry_ops.len()
        );

        // First op should be twasm.addi.
        let first = entry_ops[0];
        let first_id = Operation::get_opid(first, &ctx);
        assert_eq!(first_id.to_string(), "twasm.addi");

        // Second should be twasm.return.
        let second = entry_ops[1];
        let second_id = Operation::get_opid(second, &ctx);
        assert_eq!(second_id.to_string(), "twasm.return");
    }

    #[test]
    fn convert_addf_single_block() {
        let mut ctx = Context::new();
        let func = addf_fn();
        let op =
            lowered_function_to_pliron(&mut ctx, &func).expect("AddF conversion should succeed");

        let op_ref = op.deref(&ctx);
        let opid = Operation::get_opid(op, &ctx);
        assert_eq!(opid.to_string(), "builtin.func");

        let region = op_ref.get_region(0);
        let entry = region.deref(&ctx).get_head().expect("entry block");
        let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
        assert_eq!(entry_ops.len(), 2);
        let first_id = Operation::get_opid(entry_ops[0], &ctx);
        assert_eq!(first_id.to_string(), "twasm.addf");
    }

    /// jit fix (finding 10): a `Load` with a non-zero byte offset must
    /// convert to a `twasm.load` op that CARRIES the offset attribute
    /// (previously the offset was silently dropped, reading the wrong
    /// address).
    #[test]
    fn load_offset_is_recorded_not_dropped() {
        let mut ctx = Context::new();
        // fn(ptr) -> i32 { v = load.i32 base + 16; return v }
        let mut f = LoweredFunction::new(
            "load_off",
            LoweredSignature {
                params: vec![LoweredType::Ptr],
                returns: vec![LoweredType::I32],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        b0.params = vec![(1, LoweredType::Ptr)];
        b0.ops.push(LoweredOp::Load {
            ty: LoweredType::I32,
            base: 1,
            offset: 16,
            result: 2,
        });
        b0.ops.push(LoweredOp::Return { values: vec![2] });
        f.blocks.push(b0);
        f.entry = 0;

        let op = lowered_function_to_pliron(&mut ctx, &f).expect("load+return must convert");
        let region = op.deref(&ctx).get_region(0);
        let entry = region.deref(&ctx).get_head().expect("entry block");
        let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
        let load_op = entry_ops[0];
        assert_eq!(Operation::get_opid(load_op, &ctx).to_string(), "twasm.load");

        // The byte offset must be recorded on the op (not dropped).
        let key = pliron::identifier::Identifier::try_new(TWASM_OFFSET_ATTR.to_string()).unwrap();
        let recorded = load_op
            .deref(&ctx)
            .attributes
            .get::<pliron::builtin::attributes::StringAttr>(&key)
            .map(|a| String::from(a.clone()));
        assert_eq!(
            recorded.as_deref(),
            Some("16"),
            "load offset 16 must be recorded on the twasm.load op, got {recorded:?}"
        );
    }

    /// jit fix (finding 10): `Select` is now wired (was `NotYetWired`).
    #[test]
    fn select_is_wired() {
        let mut ctx = Context::new();
        // fn(i32 cond, i32 a, i32 b) -> i32 { select cond,a,b; return }
        let mut f = LoweredFunction::new(
            "sel",
            LoweredSignature {
                params: vec![LoweredType::Bool, LoweredType::I32, LoweredType::I32],
                returns: vec![LoweredType::I32],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        b0.params = vec![
            (1, LoweredType::Bool),
            (2, LoweredType::I32),
            (3, LoweredType::I32),
        ];
        b0.ops.push(LoweredOp::Select {
            ty: LoweredType::I32,
            cond: 1,
            then_v: 2,
            else_v: 3,
            result: 4,
        });
        b0.ops.push(LoweredOp::Return { values: vec![4] });
        f.blocks.push(b0);
        f.entry = 0;

        let op = lowered_function_to_pliron(&mut ctx, &f).expect("select must convert");
        let region = op.deref(&ctx).get_region(0);
        let entry = region.deref(&ctx).get_head().expect("entry block");
        let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
        assert_eq!(
            Operation::get_opid(entry_ops[0], &ctx).to_string(),
            "twasm.select"
        );
    }

    /// jit fix (finding 10): the integer cast ops (`ExtendU` here) are
    /// wired (were `NotYetWired`).
    #[test]
    fn integer_extend_is_wired() {
        let mut ctx = Context::new();
        let mut f = LoweredFunction::new(
            "ext",
            LoweredSignature {
                params: vec![LoweredType::I32],
                returns: vec![LoweredType::I64],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        b0.params = vec![(1, LoweredType::I32)];
        b0.ops.push(LoweredOp::ExtendU {
            from_ty: LoweredType::I32,
            to_ty: LoweredType::I64,
            src: 1,
            result: 2,
        });
        b0.ops.push(LoweredOp::Return { values: vec![2] });
        f.blocks.push(b0);
        f.entry = 0;

        let op = lowered_function_to_pliron(&mut ctx, &f).expect("extend must convert");
        let region = op.deref(&ctx).get_region(0);
        let entry = region.deref(&ctx).get_head().expect("entry block");
        let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
        assert_eq!(
            Operation::get_opid(entry_ops[0], &ctx).to_string(),
            "twasm.extui"
        );
    }

    #[test]
    fn not_yet_wired_for_vmin() {
        let mut ctx = Context::new();
        let mut f = LoweredFunction::new(
            "vmin_stub",
            LoweredSignature {
                params: vec![],
                returns: vec![],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        // VMin first, so the converter trips before reaching Return.
        b0.ops.push(LoweredOp::VMin {
            lane_ty: LoweredType::I32,
            signed: true,
            lhs: 1,
            rhs: 2,
            result: 3,
        });
        b0.ops.push(LoweredOp::Return { values: vec![] });
        f.blocks.push(b0);

        // VMin's operands reference undefined values (1, 2). But the
        // emit_op for VMin returns NotYetWired *unconditionally* without
        // looking up operands, so this still surfaces NotYetWired (which
        // is the contract under test). If a future agent wires VMin,
        // they should add new operand-aware fixtures.
        let err = lowered_function_to_pliron(&mut ctx, &f).expect_err("VMin should not be wired");
        match err {
            PlironConversionError::NotYetWired { variant } => assert_eq!(variant, "VMin"),
            other => panic!("expected NotYetWired{{ variant: VMin }}, got {other:?}"),
        }
    }

    #[test]
    fn convert_load_store_round_trip() {
        let mut ctx = Context::new();
        // (ptr, i32) -> ()  : *ptr = lhs (after a roundtrip through load)
        let mut f = LoweredFunction::new(
            "load_store",
            LoweredSignature {
                params: vec![LoweredType::Ptr, LoweredType::I32],
                returns: vec![],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        b0.params = vec![(1, LoweredType::Ptr), (2, LoweredType::I32)];
        // %3 = load %1
        b0.ops.push(LoweredOp::Load {
            ty: LoweredType::I32,
            base: 1,
            offset: 0,
            result: 3,
        });
        // store %3 -> %1
        b0.ops.push(LoweredOp::Store {
            ty: LoweredType::I32,
            value: 3,
            base: 1,
            offset: 0,
        });
        b0.ops.push(LoweredOp::Return { values: vec![] });
        f.blocks.push(b0);

        let op =
            lowered_function_to_pliron(&mut ctx, &f).expect("Load/Store conversion should succeed");
        let region = op.deref(&ctx).get_region(0);
        let entry = region.deref(&ctx).get_head().expect("entry block");
        let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
        assert_eq!(entry_ops.len(), 3, "expected load + store + return");
        assert_eq!(
            Operation::get_opid(entry_ops[0], &ctx).to_string(),
            "twasm.load"
        );
        assert_eq!(
            Operation::get_opid(entry_ops[1], &ctx).to_string(),
            "twasm.store"
        );
        assert_eq!(
            Operation::get_opid(entry_ops[2], &ctx).to_string(),
            "twasm.return"
        );
    }

    #[test]
    fn convert_multi_block_branch() {
        let mut ctx = Context::new();
        // fn f(i32) -> i32 { entry(%1): br bb1(%1); bb1(%2): return %2 }
        let mut f = LoweredFunction::new(
            "br_thru",
            LoweredSignature {
                params: vec![LoweredType::I32],
                returns: vec![LoweredType::I32],
            },
        );
        let mut entry = LoweredBlock::new(0);
        entry.params = vec![(1, LoweredType::I32)];
        entry.ops.push(LoweredOp::Br {
            target: 1,
            args: vec![1],
        });
        let mut bb1 = LoweredBlock::new(1);
        bb1.params = vec![(2, LoweredType::I32)];
        bb1.ops.push(LoweredOp::Return { values: vec![2] });
        f.blocks.push(entry);
        f.blocks.push(bb1);
        f.entry = 0;

        let op = lowered_function_to_pliron(&mut ctx, &f)
            .expect("multi-block conversion should succeed");
        let region = op.deref(&ctx).get_region(0);
        let block_count = region.deref(&ctx).iter(&ctx).count();
        assert_eq!(block_count, 2, "expected 2 blocks, got {block_count}");
    }

    #[test]
    fn undefined_block_target_surfaces() {
        let mut ctx = Context::new();
        let mut f = LoweredFunction::new(
            "bad_br",
            LoweredSignature {
                params: vec![],
                returns: vec![],
            },
        );
        let mut b0 = LoweredBlock::new(0);
        // Target block 99 was never declared.
        b0.ops.push(LoweredOp::Br {
            target: 99,
            args: vec![],
        });
        f.blocks.push(b0);
        let err =
            lowered_function_to_pliron(&mut ctx, &f).expect_err("missing target should surface");
        match err {
            PlironConversionError::UndefinedBlock(99) => {}
            other => panic!("expected UndefinedBlock(99), got {other:?}"),
        }
    }
}
