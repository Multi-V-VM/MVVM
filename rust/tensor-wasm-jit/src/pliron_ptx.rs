// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! W3.3 — pliron → LLVM-dialect → LLVM IR text → PTX scaffolding.
//!
//! # What this module does
//!
//! Wave 3 step 3 in the cuda-oxide / pliron pipeline (RFC 0001 "Pliron lever
//! and the auto-offload pipeline"). The wave-1 driver produces a
//! [`LoweredFunction`](crate::lowered_ir::LoweredFunction); W3.2
//! ([`crate::pliron_lowering`]) lifts that into a `builtin.func` operation
//! whose body contains the custom `twasm.*` ops (`twasm.addi`,
//! `twasm.load`, …). This module takes the next step: rewrite those
//! `twasm.*` ops into `pliron-llvm` 0.15's `llvm.*` dialect ops so the IR
//! is suitable for the LLVM NVPTX backend.
//!
//! # Pipeline stages
//!
//! 1. [`crate::lowered_ir::LoweredFunction`] — wave-1 interim IR (per-family).
//! 2. `builtin.func` containing `twasm.*` ops — W3.2 output.
//! 3. `builtin.func` containing `llvm.*` ops — [`twasm_to_llvm_dialect`] (this module, step 2).
//! 4. LLVM IR textual form — [`llvm_dialect_to_text`] stub (this module, step 3, NotYetWired).
//! 5. PTX — [`llvm_text_to_ptx`] stub (this module, step 4, NotYetWired).
//!
//! [`lowered_function_to_ptx`] is the end-to-end convenience wrapper that
//! threads stages 1 → 5 and returns the PTX string.
//!
//! # Build-environment gating (READ THIS)
//!
//! `pliron-llvm` 0.15 carries a hard transitive dep on `llvm-sys = "221"`,
//! which fails to build unless LLVM 221 is installed system-wide (via
//! `LLVM_SYS_221_PREFIX`). On the default `cuda-oxide-backend` build, we
//! therefore *do not* link in `pliron-llvm`: stage 2
//! ([`twasm_to_llvm_dialect`]) is a stub that returns
//! [`PtxError::NotYetWired("twasm_to_llvm_dialect")`](PtxError::NotYetWired).
//!
//! The full implementation is gated behind the additional `pliron-llvm-backend`
//! feature, which is a strict superset of `cuda-oxide-backend`. W4.x will
//! enable it on a CI image that pre-installs LLVM 221.
//!
//! # pliron-llvm 0.15 API surface notes (for future agents)
//!
//! Mapping table when the full backend is enabled:
//!
//! | twasm.* (W3.2) | llvm.* (pliron-llvm 0.15) |
//! |---|---|
//! | `twasm.addi` | `pliron_llvm::ops::AddOp` (`llvm.add`) |
//! | `twasm.subi` | `pliron_llvm::ops::SubOp` (`llvm.sub`) |
//! | `twasm.muli` | `pliron_llvm::ops::MulOp` (`llvm.mul`) |
//! | `twasm.addf` | `pliron_llvm::ops::FAddOp` (`llvm.fadd`) |
//! | `twasm.subf` | `pliron_llvm::ops::FSubOp` (`llvm.fsub`) |
//! | `twasm.mulf` | `pliron_llvm::ops::FMulOp` (`llvm.fmul`) |
//! | `twasm.load` | `pliron_llvm::ops::LoadOp` (`llvm.load`) |
//! | `twasm.store` | `pliron_llvm::ops::StoreOp` (`llvm.store`) |
//! | `twasm.br` | `pliron_llvm::ops::BrOp` (`llvm.br`) |
//! | `twasm.cond_br` | `pliron_llvm::ops::CondBrOp` (`llvm.cond_br`) |
//! | `twasm.return` | `pliron_llvm::ops::ReturnOp` (`llvm.return`) |
//!
//! Gotchas the W3.3 agent (this file) discovered from reading pliron-llvm's
//! sources (registry copy at `~/.cargo/registry/src/.../pliron-llvm-0.15.0`):
//!
//! - `llvm.add`/`sub`/`mul` *require* an `IntegerOverflowFlagsAttr` to pass
//!   the explicit `Verify` pass (`IntBinArithOpWithOverflowFlag::verify`).
//!   We attach the default (no overflow flags) at construction time. The
//!   ops were declared `verifier = "succ"` at the `pliron_op` macro layer
//!   so building them without the attribute doesn't itself error — but
//!   the dedicated Verify trait *would* fail at a future dialect
//!   validation pass.
//! - `llvm.fadd`/`fsub`/`fmul` similarly require a `FastmathFlagsAttr`;
//!   we set the empty default via `FastMathFlags::set_fast_math_flags`.
//! - `llvm.load`/`llvm.store` require the address operand to be a
//!   `pliron_llvm::types::PointerType`, NOT the i64 we currently emit in
//!   W3.2 for [`LoweredType::Ptr`](crate::lowered_ir::LoweredType::Ptr).
//!   This module inserts an `llvm.inttoptr` cast before the load/store as
//!   a scaffolding shim — a future agent that wires a proper twasm pointer
//!   type can remove the cast.
//! - `llvm.cond_br` uses an operand-segments attribute to separate
//!   `[cond, then_args..., else_args...]`. The `twasm.cond_br` op
//!   produced by W3.2 does *not* track the split; we therefore re-emit
//!   the cond_br with empty then-args / else-args, dropping block-arg
//!   forwarding. Known scaffolding limitation.
//! - Textual LLVM IR emission lives in `pliron_llvm::to_llvm_ir`, which
//!   uses `llvm-sys` to build an `LLVMModule` and then
//!   `LLVMPrintModuleToString`. The `to_llvm_ir` entry points take a
//!   `&LLVMModule` and a pliron `ModuleOp`. Wiring requires the same
//!   LLVM toolchain as the dep, plus a small adapter that wraps a
//!   `builtin.func` in a `builtin.module` first.

#![cfg(feature = "cuda-oxide-backend")]
#![allow(dead_code)]

use pliron::{
    context::{Context, Ptr},
    operation::Operation,
};
use thiserror::Error;

use crate::pliron_lowering::PlironConversionError;

// ---------------------------------------------------------------------------
// Error type.
// ---------------------------------------------------------------------------

/// Errors produced by the W3.3 pliron → LLVM-dialect → PTX pipeline.
///
/// Stage tags carried by [`Self::NotYetWired`] use a `&'static str` to make
/// the missing-stage source clear in test failures and trace logs.
#[derive(Debug, Error)]
pub enum PtxError {
    /// A `twasm.*` op encountered during rewriting has no equivalent in
    /// pliron-llvm's `llvm.*` dialect (yet). The wrapped `String` is the
    /// op's `OpId` (`"twasm.fma"`, `"twasm.vmax"`, …).
    #[error("pliron_ptx: pliron op {0} has no LLVM dialect equivalent")]
    UnsupportedOp(String),

    /// A pipeline stage hasn't been wired to a real implementation yet.
    /// Stage tags currently in use:
    ///
    /// - `"twasm_to_llvm_dialect"` (stage 2, unless `pliron-llvm-backend` is enabled).
    /// - `"llvm_dialect_to_text"` (stage 3).
    /// - `"llvm_text_to_ptx"` (stage 4).
    #[error("pliron_ptx: stage {0} not yet wired (TODO)")]
    NotYetWired(&'static str),

    /// pliron-llvm or the underlying pliron API surfaced an error during
    /// rewriting. The wrapped `String` is the `Display`-format error
    /// message; we don't propagate pliron's `Error` type to keep our
    /// public API independent of pliron's internal error shape.
    #[error("pliron_ptx: pliron-llvm error: {0}")]
    Lowering(String),

    /// W3.2's converter (the upstream stage 1 → 2) failed.
    #[error(transparent)]
    Pliron(#[from] PlironConversionError),
}

// ---------------------------------------------------------------------------
// Stage 2: twasm.* → llvm.* dialect.
// ---------------------------------------------------------------------------
//
// The "real" implementation lives in the `with_llvm` submodule, which is
// only compiled under the `pliron-llvm-backend` feature. The base
// implementation is a stub that returns NotYetWired — this is what the
// default `cuda-oxide-backend` build sees.

#[cfg(not(feature = "pliron-llvm-backend"))]
/// Rewrite a `builtin.func` whose body uses W3.2's `twasm.*` ops into a
/// new `builtin.func` whose body uses pliron-llvm 0.15's `llvm.*` ops.
///
/// # This build configuration
///
/// `pliron-llvm-backend` is *not* enabled in this build, so this is a
/// stub that returns [`PtxError::NotYetWired("twasm_to_llvm_dialect")`](PtxError::NotYetWired).
/// See the module-level docs for the build-environment rationale.
///
/// # Errors
///
/// Always [`PtxError::NotYetWired("twasm_to_llvm_dialect")`](PtxError::NotYetWired)
/// under this build configuration.
pub fn twasm_to_llvm_dialect(
    _ctx: &mut Context,
    _func: Ptr<Operation>,
) -> Result<Ptr<Operation>, PtxError> {
    Err(PtxError::NotYetWired("twasm_to_llvm_dialect"))
}

#[cfg(feature = "pliron-llvm-backend")]
pub use with_llvm::twasm_to_llvm_dialect;

#[cfg(feature = "pliron-llvm-backend")]
mod with_llvm {
    //! Real W3.3 stage 2 implementation. Compiled only when
    //! `pliron-llvm-backend` is enabled (requires a system LLVM 221).

    use std::collections::HashMap;

    use pliron::{
        basic_block::BasicBlock,
        builtin::{op_interfaces::SymbolOpInterface, ops::FuncOp, types::FunctionType},
        context::{Context, Ptr},
        op::Op,
        operation::Operation,
        r#type::{TypeObj, TypePtr, Typed},
        value::Value,
    };
    use pliron_llvm::{
        attributes::FastmathFlagsAttr,
        op_interfaces::{
            FastMathFlags, FloatBinArithOpWithFastMathFlags, IntBinArithOpWithOverflowFlag,
        },
        ops::{
            AddOp, BrOp, CondBrOp, FAddOp, FMulOp, FSubOp, IntToPtrOp, LoadOp, MulOp, ReturnOp,
            StoreOp, SubOp,
        },
        types::PointerType,
    };

    use super::PtxError;

    /// Rewrite a `builtin.func` whose body uses W3.2's `twasm.*` ops into
    /// a new `builtin.func` whose body uses pliron-llvm 0.15's `llvm.*`
    /// ops.
    ///
    /// The returned `Ptr<Operation>` is a *new* `builtin.func` — the
    /// input is left unmodified. Supported `twasm.*` op set: addi/subi/muli,
    /// addf/subf/mulf, load, store, br, cond_br, return. Any other op
    /// surfaces [`PtxError::UnsupportedOp`].
    ///
    /// See the module-level docs for the known scaffolding limitations
    /// (cond_br segment loss, ptr-as-i64 cast shim, no verifier pass).
    ///
    /// # Errors
    ///
    /// - [`PtxError::UnsupportedOp`] for an unrecognized op.
    /// - [`PtxError::Lowering`] for any pliron API call that fails.
    pub fn twasm_to_llvm_dialect(
        ctx: &mut Context,
        func: Ptr<Operation>,
    ) -> Result<Ptr<Operation>, PtxError> {
        let func_op = Operation::get_op_dyn(func, ctx)
            .downcast::<FuncOp>()
            .ok_or_else(|| {
                PtxError::Lowering(format!(
                    "twasm_to_llvm_dialect: expected builtin.func, got {}",
                    Operation::get_opid(func, ctx)
                ))
            })?;

        // Build a new FuncOp with the same signature. FuncOp::new
        // auto-creates an "entry" block whose args match the signature.
        let func_ty_ptr = func_op.get_type(ctx);
        let func_ty = TypePtr::<FunctionType>::from_ptr(func_ty_ptr, ctx)
            .map_err(|e| PtxError::Lowering(format!("FuncOp type: {e}")))?;
        let name = func_op.get_symbol_name(ctx);
        let new_func = FuncOp::new(ctx, name, func_ty);
        let new_func_ptr = new_func.get_operation();

        let mut state = RewriteState::new();
        let input_region = func_op.get_region(ctx);
        let output_region = new_func.get_region(ctx);

        let input_blocks: Vec<Ptr<BasicBlock>> = {
            use pliron::linked_list::ContainsLinkedList;
            input_region.deref(ctx).iter(ctx).collect()
        };
        let new_entry = new_func.get_entry_block(ctx);
        let input_entry = *input_blocks
            .first()
            .ok_or_else(|| PtxError::Lowering("input function has no blocks".to_string()))?;

        // Map entry block arguments 1:1 (same types since signatures match).
        {
            let in_bb = input_entry.deref(ctx);
            let out_bb = new_entry.deref(ctx);
            for idx in 0..in_bb.get_num_arguments() {
                state
                    .values
                    .insert(in_bb.get_argument(idx), out_bb.get_argument(idx));
            }
        }
        state.blocks.insert(input_entry, new_entry);

        // Create fresh blocks for non-entry blocks.
        for &in_bb_ptr in input_blocks.iter().skip(1) {
            let arg_types: Vec<Ptr<TypeObj>> = {
                let bb_ref = in_bb_ptr.deref(ctx);
                (0..bb_ref.get_num_arguments())
                    .map(|i| bb_ref.get_argument(i).get_type(ctx))
                    .collect()
            };
            let out_bb = BasicBlock::new(ctx, None, arg_types);
            out_bb.insert_at_back(output_region, ctx);
            {
                let in_ref = in_bb_ptr.deref(ctx);
                let out_ref = out_bb.deref(ctx);
                for idx in 0..in_ref.get_num_arguments() {
                    state
                        .values
                        .insert(in_ref.get_argument(idx), out_ref.get_argument(idx));
                }
            }
            state.blocks.insert(in_bb_ptr, out_bb);
        }

        // Walk ops and rewrite.
        for in_bb_ptr in input_blocks {
            let out_bb = *state.blocks.get(&in_bb_ptr).expect("block mapped above");
            let ops_in_block: Vec<Ptr<Operation>> = {
                use pliron::linked_list::ContainsLinkedList;
                in_bb_ptr.deref(ctx).iter(ctx).collect()
            };
            for in_op in ops_in_block {
                rewrite_op(ctx, &mut state, out_bb, in_op)?;
            }
        }

        Ok(new_func_ptr)
    }

    /// Per-conversion state: pliron Values + BasicBlocks from the input
    /// function map to fresh Values + BasicBlocks in the output function.
    struct RewriteState {
        values: HashMap<Value, Value>,
        blocks: HashMap<Ptr<BasicBlock>, Ptr<BasicBlock>>,
    }

    impl RewriteState {
        fn new() -> Self {
            Self {
                values: HashMap::new(),
                blocks: HashMap::new(),
            }
        }

        fn map_value(&self, v: Value) -> Result<Value, PtxError> {
            self.values.get(&v).copied().ok_or_else(|| {
                PtxError::Lowering(format!("twasm_to_llvm_dialect: unmapped value {v:?}"))
            })
        }

        fn map_block(&self, b: Ptr<BasicBlock>) -> Result<Ptr<BasicBlock>, PtxError> {
            self.blocks.get(&b).copied().ok_or_else(|| {
                PtxError::Lowering(format!("twasm_to_llvm_dialect: unmapped block {b:?}"))
            })
        }
    }

    /// Rewrite a single input twasm.* op into the corresponding llvm.* op,
    /// appending the new op to `out_bb` and updating `state`'s value map.
    fn rewrite_op(
        ctx: &mut Context,
        state: &mut RewriteState,
        out_bb: Ptr<BasicBlock>,
        in_op: Ptr<Operation>,
    ) -> Result<(), PtxError> {
        let opid = Operation::get_opid(in_op, ctx).to_string();

        let (in_operands, in_successors, in_results, in_result_types): (
            Vec<Value>,
            Vec<Ptr<BasicBlock>>,
            Vec<Value>,
            Vec<Ptr<TypeObj>>,
        ) = {
            let r = in_op.deref(ctx);
            (
                r.operands().collect(),
                r.successors().collect(),
                r.results().collect(),
                r.result_types().collect(),
            )
        };

        let new_operands: Vec<Value> = in_operands
            .iter()
            .map(|v| state.map_value(*v))
            .collect::<Result<_, _>>()?;
        let new_successors: Vec<Ptr<BasicBlock>> = in_successors
            .iter()
            .map(|b| state.map_block(*b))
            .collect::<Result<_, _>>()?;

        let new_op_ptr: Ptr<Operation> = match opid.as_str() {
            "twasm.addi" => emit_int_binop::<AddOp>(ctx, &new_operands)?,
            "twasm.subi" => emit_int_binop::<SubOp>(ctx, &new_operands)?,
            "twasm.muli" => emit_int_binop::<MulOp>(ctx, &new_operands)?,

            "twasm.addf" => emit_float_binop::<FAddOp>(ctx, &new_operands)?,
            "twasm.subf" => emit_float_binop::<FSubOp>(ctx, &new_operands)?,
            "twasm.mulf" => emit_float_binop::<FMulOp>(ctx, &new_operands)?,

            "twasm.load" => {
                let base = new_operands.first().copied().ok_or_else(|| {
                    PtxError::Lowering("twasm.load missing base operand".to_string())
                })?;
                let res_ty = *in_result_types.first().ok_or_else(|| {
                    PtxError::Lowering("twasm.load missing result type".to_string())
                })?;
                let ptr_val = cast_int_to_ptr(ctx, out_bb, base);
                let load = LoadOp::new(ctx, ptr_val, res_ty);
                let load_ptr = load.get_operation();
                load_ptr.insert_at_back(out_bb, ctx);
                load_ptr
            }
            "twasm.store" => {
                let value = new_operands.first().copied().ok_or_else(|| {
                    PtxError::Lowering("twasm.store missing value operand".to_string())
                })?;
                let base = new_operands.get(1).copied().ok_or_else(|| {
                    PtxError::Lowering("twasm.store missing base operand".to_string())
                })?;
                let ptr_val = cast_int_to_ptr(ctx, out_bb, base);
                let store = StoreOp::new(ctx, value, ptr_val);
                let store_ptr = store.get_operation();
                store_ptr.insert_at_back(out_bb, ctx);
                store_ptr
            }

            "twasm.br" => {
                let dest = new_successors
                    .first()
                    .copied()
                    .ok_or_else(|| PtxError::Lowering("twasm.br missing successor".to_string()))?;
                let br = BrOp::new(ctx, dest, new_operands.clone());
                let br_ptr = br.get_operation();
                br_ptr.insert_at_back(out_bb, ctx);
                br_ptr
            }
            "twasm.cond_br" => {
                let cond = new_operands.first().copied().ok_or_else(|| {
                    PtxError::Lowering("twasm.cond_br missing condition".to_string())
                })?;
                let then_dest = new_successors.first().copied().ok_or_else(|| {
                    PtxError::Lowering("twasm.cond_br missing then successor".to_string())
                })?;
                let else_dest = new_successors.get(1).copied().ok_or_else(|| {
                    PtxError::Lowering("twasm.cond_br missing else successor".to_string())
                })?;
                let cb = CondBrOp::new(ctx, cond, then_dest, vec![], else_dest, vec![]);
                let cb_ptr = cb.get_operation();
                cb_ptr.insert_at_back(out_bb, ctx);
                cb_ptr
            }
            "twasm.return" => {
                if new_operands.len() > 1 {
                    return Err(PtxError::UnsupportedOp(format!(
                        "{opid} with {} results (only 0 or 1 supported)",
                        new_operands.len()
                    )));
                }
                let value = new_operands.first().copied();
                let ret = ReturnOp::new(ctx, value);
                let ret_ptr = ret.get_operation();
                ret_ptr.insert_at_back(out_bb, ctx);
                ret_ptr
            }

            other => return Err(PtxError::UnsupportedOp(other.to_string())),
        };

        let new_results: Vec<Value> = new_op_ptr.deref(ctx).results().collect();
        for (old, new) in in_results.iter().zip(new_results.iter()) {
            state.values.insert(*old, *new);
        }

        Ok(())
    }

    /// Emit a `llvm.add` / `llvm.sub` / `llvm.mul` op with the empty
    /// default `IntegerOverflowFlagsAttr`.
    fn emit_int_binop<O>(ctx: &mut Context, operands: &[Value]) -> Result<Ptr<Operation>, PtxError>
    where
        O: Op + IntBinArithOpWithOverflowFlag,
    {
        let lhs = *operands
            .first()
            .ok_or_else(|| PtxError::Lowering("integer binop missing lhs operand".to_string()))?;
        let rhs = *operands
            .get(1)
            .ok_or_else(|| PtxError::Lowering("integer binop missing rhs operand".to_string()))?;
        let op = O::new_with_overflow_flag(ctx, lhs, rhs, Default::default());
        Ok(op.get_operation())
    }

    /// Emit a `llvm.fadd` / `llvm.fsub` / `llvm.fmul` op with the empty
    /// default `FastmathFlagsAttr`.
    fn emit_float_binop<O>(
        ctx: &mut Context,
        operands: &[Value],
    ) -> Result<Ptr<Operation>, PtxError>
    where
        O: Op + FloatBinArithOpWithFastMathFlags,
    {
        let lhs = *operands
            .first()
            .ok_or_else(|| PtxError::Lowering("float binop missing lhs operand".to_string()))?;
        let rhs = *operands
            .get(1)
            .ok_or_else(|| PtxError::Lowering("float binop missing rhs operand".to_string()))?;
        let op = O::new_with_fast_math_flags(ctx, lhs, rhs, FastmathFlagsAttr::default());
        Ok(op.get_operation())
    }

    /// Insert an `llvm.inttoptr` op casting `int_val` (assumed to be an
    /// i64 from W3.2's `LoweredType::Ptr` lowering) into an
    /// `llvm.PointerType` at the end of `bb`. Returns the result Value.
    fn cast_int_to_ptr(ctx: &mut Context, bb: Ptr<BasicBlock>, int_val: Value) -> Value {
        let ptr_ty: Ptr<TypeObj> = PointerType::get(ctx).into();
        let cast_op = Operation::new(
            ctx,
            <IntToPtrOp as Op>::get_concrete_op_info(),
            vec![ptr_ty],
            vec![int_val],
            vec![],
            0,
        );
        cast_op.insert_at_back(bb, ctx);
        cast_op.deref(ctx).get_result(0)
    }
}

// ---------------------------------------------------------------------------
// Stage 3: llvm.* dialect → LLVM IR textual form (stub).
// ---------------------------------------------------------------------------

/// Stage 3: serialize the pliron LLVM-dialect IR to textual LLVM IR.
///
/// Currently returns [`PtxError::NotYetWired`]. The implementation will
/// call into `pliron_llvm::to_llvm_ir`, which uses `llvm-sys` to build an
/// `LLVMModule` and then `LLVMPrintModuleToString`. That codepath also
/// requires a system LLVM 221 install (same constraint as
/// `pliron-llvm-backend`); wiring happens in W4.x.
///
/// # Errors
///
/// Always [`PtxError::NotYetWired("llvm_dialect_to_text")`](PtxError::NotYetWired).
pub fn llvm_dialect_to_text(_ctx: &Context, _func: Ptr<Operation>) -> Result<String, PtxError> {
    Err(PtxError::NotYetWired("llvm_dialect_to_text"))
}

// ---------------------------------------------------------------------------
// Stage 4: LLVM IR text → PTX (stub).
// ---------------------------------------------------------------------------

/// Stage 4: emit PTX from textual LLVM IR. Requires the LLVM NVPTX
/// backend (typically via `llvm-sys` / `inkwell` with the `nvptx` target
/// enabled).
///
/// Currently returns [`PtxError::NotYetWired`]. The W4.x author wires the
/// actual llvm-sys backend call when the CUDA toolchain build environment
/// is available.
///
/// # Errors
///
/// Always [`PtxError::NotYetWired("llvm_text_to_ptx")`](PtxError::NotYetWired).
pub fn llvm_text_to_ptx(_llvm_ir: &str) -> Result<String, PtxError> {
    Err(PtxError::NotYetWired("llvm_text_to_ptx"))
}

// ---------------------------------------------------------------------------
// End-to-end pipeline.
// ---------------------------------------------------------------------------

/// End-to-end convenience: [`LoweredFunction`](crate::lowered_ir::LoweredFunction)
/// → twasm.* (W3.2) → llvm.* (W3.3 step 2) → LLVM IR text (W4.x) → PTX (W4.x).
///
/// Runs stages 1 and 2 for real (stage 2 only when `pliron-llvm-backend`
/// is enabled); stages 3 and 4 surface [`PtxError::NotYetWired`].
///
/// # Errors
///
/// - Whatever [`crate::pliron_lowering::lowered_function_to_pliron`]
///   surfaces (W3.2 stage).
/// - [`PtxError::UnsupportedOp`] / [`PtxError::Lowering`] from
///   [`twasm_to_llvm_dialect`] when `pliron-llvm-backend` is enabled.
/// - [`PtxError::NotYetWired("twasm_to_llvm_dialect")`](PtxError::NotYetWired)
///   when `pliron-llvm-backend` is *not* enabled.
/// - [`PtxError::NotYetWired("llvm_dialect_to_text")`](PtxError::NotYetWired)
///   when stage 2 succeeds (stage 3 is always stub for now).
pub fn lowered_function_to_ptx(
    func: &crate::lowered_ir::LoweredFunction,
) -> Result<String, PtxError> {
    let mut ctx = Context::new();
    let twasm_func = crate::pliron_lowering::lowered_function_to_pliron(&mut ctx, func)?;
    let llvm_func = twasm_to_llvm_dialect(&mut ctx, twasm_func)?;
    let llvm_text = llvm_dialect_to_text(&ctx, llvm_func)?;
    llvm_text_to_ptx(&llvm_text)
}

// ---------------------------------------------------------------------------
// Tests.
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lowered_ir::{
        LoweredBlock, LoweredFunction, LoweredOp, LoweredSignature, LoweredType,
    };

    /// Build a single-block AddI function: `fn add(i32, i32) -> i32`.
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

    /// Stage 2 test — depends on the build configuration.
    ///
    /// When `pliron-llvm-backend` is enabled, the rewriter produces an
    /// `llvm.add` op. When it's not (the default `cuda-oxide-backend`
    /// build), stage 2 is a stub returning
    /// `NotYetWired("twasm_to_llvm_dialect")`. The test asserts the right
    /// outcome for whichever flavour was built.
    #[test]
    fn twasm_to_llvm_dialect_addi() {
        use crate::pliron_lowering::lowered_function_to_pliron;
        let mut ctx = Context::new();
        let func = addi_fn();
        let twasm_func =
            lowered_function_to_pliron(&mut ctx, &func).expect("W3.2 conversion should succeed");

        let result = twasm_to_llvm_dialect(&mut ctx, twasm_func);

        #[cfg(feature = "pliron-llvm-backend")]
        {
            let llvm_func = result.expect("W3.3 stage 2 should succeed for AddI");
            assert_eq!(
                Operation::get_opid(llvm_func, &ctx).to_string(),
                "builtin.func"
            );
            // The entry block's first op should be llvm.add.
            use pliron::linked_list::ContainsLinkedList;
            let region = llvm_func.deref(&ctx).get_region(0);
            let entry = region.deref(&ctx).get_head().expect("entry block");
            let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
            assert_eq!(entry_ops.len(), 2, "expected llvm.add + llvm.return");
            assert_eq!(
                Operation::get_opid(entry_ops[0], &ctx).to_string(),
                "llvm.add",
                "expected llvm.add, got {}",
                Operation::get_opid(entry_ops[0], &ctx)
            );
            assert_eq!(
                Operation::get_opid(entry_ops[1], &ctx).to_string(),
                "llvm.return"
            );
        }

        #[cfg(not(feature = "pliron-llvm-backend"))]
        {
            // Stage 2 is a stub — assert the documented NotYetWired tag.
            match result {
                Err(PtxError::NotYetWired("twasm_to_llvm_dialect")) => {}
                Err(other) => panic!("expected NotYetWired, got {other:?}"),
                Ok(_) => panic!("expected NotYetWired, got Ok (built without pliron-llvm-backend)"),
            }
        }
    }

    #[test]
    fn llvm_text_to_ptx_is_not_yet_wired() {
        let err = llvm_text_to_ptx("define i32 @foo(i32 %0) { ret i32 %0 }")
            .expect_err("stage 4 should be NotYetWired");
        match err {
            PtxError::NotYetWired(stage) => assert_eq!(stage, "llvm_text_to_ptx"),
            other => panic!("expected NotYetWired, got {other:?}"),
        }
    }

    /// End-to-end test. The exact `NotYetWired` tag that surfaces depends
    /// on whether `pliron-llvm-backend` is enabled:
    ///
    /// - Without the feature: stage 2 is the first stub, surfaces
    ///   `NotYetWired("twasm_to_llvm_dialect")`.
    /// - With the feature: stage 2 runs for real, stage 3 trips with
    ///   `NotYetWired("llvm_dialect_to_text")`.
    #[test]
    fn lowered_function_to_ptx_round_trips_to_not_yet_wired() {
        let func = addi_fn();
        let err =
            lowered_function_to_ptx(&func).expect_err("end-to-end should surface NotYetWired");
        match err {
            PtxError::NotYetWired(stage) => {
                #[cfg(feature = "pliron-llvm-backend")]
                assert_eq!(stage, "llvm_dialect_to_text");
                #[cfg(not(feature = "pliron-llvm-backend"))]
                assert_eq!(stage, "twasm_to_llvm_dialect");
            }
            other => panic!("expected NotYetWired, got {other:?}"),
        }
    }

    /// Exercises the float-binop path when `pliron-llvm-backend` is
    /// enabled. Without it, falls back to NotYetWired (same as the AddI
    /// stub test).
    #[test]
    #[cfg(feature = "pliron-llvm-backend")]
    fn twasm_addf_lowers_to_llvm_fadd() {
        use crate::pliron_lowering::lowered_function_to_pliron;
        let mut ctx = Context::new();
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

        let twasm_func =
            lowered_function_to_pliron(&mut ctx, &f).expect("W3.2 should succeed for AddF");
        let llvm_func = twasm_to_llvm_dialect(&mut ctx, twasm_func)
            .expect("W3.3 stage 2 should succeed for AddF");
        use pliron::linked_list::ContainsLinkedList;
        let region = llvm_func.deref(&ctx).get_region(0);
        let entry = region.deref(&ctx).get_head().expect("entry block");
        let entry_ops: Vec<_> = entry.deref(&ctx).iter(&ctx).collect();
        assert_eq!(
            Operation::get_opid(entry_ops[0], &ctx).to_string(),
            "llvm.fadd"
        );
    }
}
