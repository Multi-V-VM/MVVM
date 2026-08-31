// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Reusable Cranelift IR test-fixture builders for the wave-1 lowering
//! passes.
//!
//! Each helper here produces a small [`cranelift_codegen::ir::Function`]
//! with the shape a single `lower_*` pass needs to exercise — an entry
//! block whose params match the requested signature, one (or zero)
//! instructions of interest, and a `return` terminator. The lowering
//! passes (`lower_arith`, `lower_float`, `lower_memory`, `lower_cf`,
//! `lower_vector`, `lower_conv`) consume these in their integration tests
//! so they don't each re-invent the Cranelift-construction boilerplate.
//!
//! # Scope
//!
//! These builders **intentionally** do not use `cranelift-frontend`
//! (which is not in `tensor-wasm-jit`'s `Cargo.toml`). They build
//! functions by hand against the `cranelift-codegen` raw DFG + cursor
//! API. The trade-off: the helpers are limited to single-block bodies
//! with no SSA-from-frontend convenience. That is the right scope for
//! wave-1 unit tests; the multi-block lowering tests in `lower_cf` build
//! their fixtures inline.
//!
//! # Why fixtures here (vs inline in each `lower_*` module)
//!
//! Wave-1 agents L1-L6 wrote their first-cut fixtures inline because
//! this module hadn't landed yet. Wave-2 integration tests (the
//! detector → lowering → blueprint pipeline) need to share fixture
//! shapes across passes, so this module is the long-term home. The
//! inline fixtures in `lower_*` modules can migrate here in a future
//! cleanup pass without changing the test logic — the function shapes
//! these builders produce are the same shapes those tests already
//! construct.

#![cfg(feature = "cuda-oxide-backend")]

use cranelift_codegen::cursor::{Cursor, FuncCursor};
use cranelift_codegen::ir::immediates::Offset32;
use cranelift_codegen::ir::{
    AbiParam, Function, Inst, InstBuilder, MemFlags, Opcode, Signature, Type, UserFuncName,
};
use cranelift_codegen::isa::CallConv;

/// Pointer-typed parameter used for `load` / `store` fixtures.
///
/// Cranelift addresses are integers, not a dedicated pointer type. The
/// memory-lowering passes match on the 64-bit integer width because
/// that's the address width PTX targets on sm_80+ (per
/// [`crate::ptx_emit::DEFAULT_TARGET`]). Using a single named constant
/// here keeps the `function_with_load` / `function_with_store` helpers
/// and their unit tests aligned on the same width.
const PTR_TY: Type = cranelift_codegen::ir::types::I64;

/// Build an empty Cranelift function with the requested signature.
///
/// The returned [`Function`] has:
///
/// - A `SystemV` calling convention. (The actual CC doesn't matter for
///   the IR-shape tests these fixtures support — the lowering passes
///   don't inspect it. SystemV is the workspace default for "any sane
///   CC will do".)
/// - One entry block whose block-param list matches `params` 1:1 (same
///   types, same order).
/// - A terminating `return` of **zero values**. The caller is expected
///   to either accept the empty return (e.g. void-returning functions)
///   or to replace the terminator before lowering when a non-empty
///   return is required. Replacing the terminator is uncommon in the
///   wave-1 tests; most callers either use `function_with_*` (which
///   already inserts a sensible return) or this helper for void
///   fixtures.
///
/// The `name` argument is wrapped via [`UserFuncName::testcase`] so it
/// shows up readably in `Function::display()` output during test
/// failures.
pub fn empty_function(name: &str, params: &[Type], returns: &[Type]) -> Function {
    let mut sig = Signature::new(CallConv::SystemV);
    for p in params {
        sig.params.push(AbiParam::new(*p));
    }
    for r in returns {
        sig.returns.push(AbiParam::new(*r));
    }

    let mut func = Function::with_name_signature(UserFuncName::testcase(name.as_bytes()), sig);
    let block = func.dfg.make_block();
    // Append block params 1:1 with the signature so the entry block is
    // immediately usable by lowering passes that iterate block params.
    let param_types: Vec<Type> = func.signature.params.iter().map(|p| p.value_type).collect();
    for pty in param_types {
        func.dfg.append_block_param(block, pty);
    }
    func.layout.append_block(block);

    // Insert a zero-value `return` so the block has a terminator. Tests
    // that need a non-empty return (e.g. function_with_binary_op) call
    // their own InstBuilder::return_ instead of relying on this stub.
    let mut cursor = FuncCursor::new(&mut func).at_bottom(block);
    cursor.ins().return_(&[]);

    func
}

/// Build a function `fn(lane_ty, lane_ty) -> lane_ty` whose entry block
/// contains a single binary instruction of `opcode` over the two block
/// params, followed by a return of the result.
///
/// Used by the arith / float / vector lowering tests to exercise
/// per-opcode dispatch. The helper covers any opcode whose
/// `InstructionFormat` is `Binary` (e.g. `iadd`, `isub`, `imul`, `fadd`,
/// `fsub`, `fmul`, `fdiv`, `fma` — wait, `fma` is `Ternary`; the rule of
/// thumb is "two operands, one result, no immediate"). Passing an
/// opcode whose format is not `Binary` will panic in debug builds via
/// `cranelift-codegen`'s own `debug_assert_eq!` inside the
/// `InstBuilder::Binary` shim.
///
/// Returns the constructed [`Function`] and the [`Inst`] handle for the
/// inserted binary op so tests can grep for it without re-walking the
/// layout.
pub fn function_with_binary_op(opcode: Opcode, lane_ty: Type) -> (Function, Inst) {
    let mut func = empty_function_no_return("binop", &[lane_ty, lane_ty], &[lane_ty]);
    let block = func.layout.entry_block().expect("entry block");
    let params: Vec<_> = func.dfg.block_params(block).to_vec();
    let p0 = params[0];
    let p1 = params[1];

    let mut cursor = FuncCursor::new(&mut func).at_bottom(block);
    // Use the low-level `Binary` constructor so the helper can dispatch
    // on a generic Opcode rather than naming one of cranelift's
    // per-opcode shims. `ctrl_typevar = lane_ty` mirrors what
    // `iadd` / `fadd` / etc. set internally.
    let (inst, dfg) = cursor.ins().Binary(opcode, lane_ty, p0, p1);
    let result = dfg.first_result(inst);
    cursor.ins().return_(&[result]);

    (func, inst)
}

/// Build a function `fn(lane_ty) -> lane_ty` whose entry block contains
/// a single unary instruction of `opcode` over the single block param,
/// followed by a return of the result.
///
/// Symmetric counterpart to [`function_with_binary_op`]; covers any
/// opcode whose `InstructionFormat` is `Unary` (e.g. `fneg`, `fabs`,
/// `ineg`, `bnot`). Same debug-assertion caveat applies.
pub fn function_with_unary_op(opcode: Opcode, lane_ty: Type) -> (Function, Inst) {
    let mut func = empty_function_no_return("unop", &[lane_ty], &[lane_ty]);
    let block = func.layout.entry_block().expect("entry block");
    let params: Vec<_> = func.dfg.block_params(block).to_vec();
    let p0 = params[0];

    let mut cursor = FuncCursor::new(&mut func).at_bottom(block);
    let (inst, dfg) = cursor.ins().Unary(opcode, lane_ty, p0);
    let result = dfg.first_result(inst);
    cursor.ins().return_(&[result]);

    (func, inst)
}

/// Build a function `fn(ptr) -> ty` whose entry block contains a single
/// `load.<ty>` instruction reading from `(param0 + offset)`, followed
/// by a return of the loaded value.
///
/// The pointer param is a 64-bit integer ([`PTR_TY`]) — see the
/// constant's rustdoc for why we don't use a dedicated pointer type.
/// `offset` is the byte displacement passed to Cranelift's `load`
/// instruction; the lowering pass extracts it via
/// `InstructionData::Load { offset, .. }`.
///
/// `MemFlags::trusted()` is used so the lowering doesn't have to model
/// trap behaviour for the fixture — these tests are about IR shape, not
/// trap semantics.
pub fn function_with_load(ty: Type, offset: i32) -> (Function, Inst) {
    let mut func = empty_function_no_return("load_fn", &[PTR_TY], &[ty]);
    let block = func.layout.entry_block().expect("entry block");
    let params: Vec<_> = func.dfg.block_params(block).to_vec();
    let p0 = params[0];

    let mut cursor = FuncCursor::new(&mut func).at_bottom(block);
    // Use the low-level `Load` constructor so we get the `Inst` handle
    // back directly. The shorthand `cursor.ins().load(...)` returns a
    // `Value` and would require a follow-up `value_def` lookup.
    let (inst, dfg) = cursor.ins().Load(
        Opcode::Load,
        ty,
        MemFlags::trusted(),
        Offset32::new(offset),
        p0,
    );
    let result = dfg.first_result(inst);
    cursor.ins().return_(&[result]);

    (func, inst)
}

/// Build a function `fn(ty, ptr)` whose entry block contains a single
/// `store.<ty>` instruction writing `param0` to `(param1 + offset)`,
/// followed by an empty return.
///
/// Parameter order is `(value, pointer)` to match Cranelift's own
/// `store(MemFlags, x, p, Offset)` builder signature — the convention
/// the lowering passes already match against. `MemFlags::trusted()` is
/// used for the same reason as in [`function_with_load`].
pub fn function_with_store(ty: Type, offset: i32) -> (Function, Inst) {
    let mut func = empty_function_no_return("store_fn", &[ty, PTR_TY], &[]);
    let block = func.layout.entry_block().expect("entry block");
    let params: Vec<_> = func.dfg.block_params(block).to_vec();
    let p0 = params[0]; // value
    let p1 = params[1]; // pointer

    let mut cursor = FuncCursor::new(&mut func).at_bottom(block);
    let (inst, _dfg) = cursor.ins().Store(
        Opcode::Store,
        ty,
        MemFlags::trusted(),
        Offset32::new(offset),
        p0,
        p1,
    );
    cursor.ins().return_(&[]);

    (func, inst)
}

/// Internal helper: build a function with the given signature and an
/// entry block sized to the signature, but **without** the trailing
/// `return` that [`empty_function`] inserts. The `function_with_*`
/// builders use this so they can append their own terminator after
/// inserting the op of interest.
fn empty_function_no_return(name: &str, params: &[Type], returns: &[Type]) -> Function {
    let mut sig = Signature::new(CallConv::SystemV);
    for p in params {
        sig.params.push(AbiParam::new(*p));
    }
    for r in returns {
        sig.returns.push(AbiParam::new(*r));
    }
    let mut func = Function::with_name_signature(UserFuncName::testcase(name.as_bytes()), sig);
    let block = func.dfg.make_block();
    let param_types: Vec<Type> = func.signature.params.iter().map(|p| p.value_type).collect();
    for pty in param_types {
        func.dfg.append_block_param(block, pty);
    }
    func.layout.append_block(block);
    func
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::ir::types::{F32, I32, I64};

    /// One block, params match signature, terminator is a zero-value
    /// return.
    #[test]
    fn empty_function_shape() {
        let func = empty_function("noop", &[I32, F32], &[]);
        // Exactly one block.
        assert_eq!(func.layout.blocks().count(), 1);
        let block = func.layout.entry_block().expect("entry block");
        // Block params match the signature.
        assert_eq!(func.dfg.num_block_params(block), 2);
        let ptys: Vec<Type> = func
            .dfg
            .block_params(block)
            .iter()
            .map(|v| func.dfg.value_type(*v))
            .collect();
        assert_eq!(ptys, vec![I32, F32]);
        // Final instruction is a return.
        let last = func.layout.last_inst(block).expect("terminator");
        assert_eq!(func.dfg.insts[last].opcode(), Opcode::Return);
    }

    /// `empty_function` honours a non-empty `returns` even though the
    /// inserted `return` has zero values. Callers are responsible for
    /// replacing the terminator when the signature demands a value; the
    /// helper documents that contract and we just lock in the signature
    /// shape here.
    #[test]
    fn empty_function_signature_records_returns() {
        let func = empty_function("with_ret", &[I32], &[I64]);
        assert_eq!(func.signature.params.len(), 1);
        assert_eq!(func.signature.returns.len(), 1);
        assert_eq!(func.signature.params[0].value_type, I32);
        assert_eq!(func.signature.returns[0].value_type, I64);
    }

    /// Binary builder: one block, two params, terminator is `return`,
    /// inserted instruction is the requested opcode.
    #[test]
    fn function_with_binary_op_iadd_shape() {
        let (func, inst) = function_with_binary_op(Opcode::Iadd, I32);
        assert_eq!(func.layout.blocks().count(), 1);
        let block = func.layout.entry_block().unwrap();
        assert_eq!(func.dfg.num_block_params(block), 2);
        assert_eq!(func.dfg.insts[inst].opcode(), Opcode::Iadd);
        // The instruction's operands are the two block params (in
        // order) — verifies the helper wired param0 to lhs and param1
        // to rhs, the convention the arith lowering expects.
        let args = func.dfg.inst_args(inst);
        let params = func.dfg.block_params(block);
        assert_eq!(args[0], params[0]);
        assert_eq!(args[1], params[1]);
        // Terminator returns the binary result.
        let last = func.layout.last_inst(block).unwrap();
        assert_eq!(func.dfg.insts[last].opcode(), Opcode::Return);
    }

    /// Spot-check that `function_with_binary_op` is opcode-agnostic by
    /// constructing an `fadd` over `F32` lanes.
    #[test]
    fn function_with_binary_op_fadd_shape() {
        let (func, inst) = function_with_binary_op(Opcode::Fadd, F32);
        assert_eq!(func.dfg.insts[inst].opcode(), Opcode::Fadd);
        let block = func.layout.entry_block().unwrap();
        let ptys: Vec<Type> = func
            .dfg
            .block_params(block)
            .iter()
            .map(|v| func.dfg.value_type(*v))
            .collect();
        assert_eq!(ptys, vec![F32, F32]);
    }

    /// Unary builder: one block, one param, terminator is `return`,
    /// inserted instruction is the requested opcode.
    #[test]
    fn function_with_unary_op_fneg_shape() {
        let (func, inst) = function_with_unary_op(Opcode::Fneg, F32);
        assert_eq!(func.layout.blocks().count(), 1);
        let block = func.layout.entry_block().unwrap();
        assert_eq!(func.dfg.num_block_params(block), 1);
        assert_eq!(func.dfg.insts[inst].opcode(), Opcode::Fneg);
        let args = func.dfg.inst_args(inst);
        assert_eq!(args[0], func.dfg.block_params(block)[0]);
        let last = func.layout.last_inst(block).unwrap();
        assert_eq!(func.dfg.insts[last].opcode(), Opcode::Return);
    }

    /// Load builder: one block, one pointer param, load opcode at the
    /// expected offset, terminator returns the loaded value.
    #[test]
    fn function_with_load_shape() {
        let (func, inst) = function_with_load(I32, 16);
        assert_eq!(func.layout.blocks().count(), 1);
        let block = func.layout.entry_block().unwrap();
        assert_eq!(func.dfg.num_block_params(block), 1);
        let ptys: Vec<Type> = func
            .dfg
            .block_params(block)
            .iter()
            .map(|v| func.dfg.value_type(*v))
            .collect();
        assert_eq!(ptys, vec![PTR_TY]);
        assert_eq!(func.dfg.insts[inst].opcode(), Opcode::Load);
        // Offset is reachable via the InstructionData accessor — the
        // lowering pass will read it through the same path.
        let offset = func.dfg.insts[inst]
            .load_store_offset()
            .expect("load instruction missing offset");
        assert_eq!(offset, 16);
        let last = func.layout.last_inst(block).unwrap();
        assert_eq!(func.dfg.insts[last].opcode(), Opcode::Return);
    }

    /// Store builder: one block, two params (value + pointer), store
    /// opcode at the expected offset, terminator is a zero-value
    /// return.
    #[test]
    fn function_with_store_shape() {
        let (func, inst) = function_with_store(I32, 32);
        assert_eq!(func.layout.blocks().count(), 1);
        let block = func.layout.entry_block().unwrap();
        assert_eq!(func.dfg.num_block_params(block), 2);
        let ptys: Vec<Type> = func
            .dfg
            .block_params(block)
            .iter()
            .map(|v| func.dfg.value_type(*v))
            .collect();
        assert_eq!(ptys, vec![I32, PTR_TY]);
        assert_eq!(func.dfg.insts[inst].opcode(), Opcode::Store);
        let offset = func.dfg.insts[inst]
            .load_store_offset()
            .expect("store instruction missing offset");
        assert_eq!(offset, 32);
        let last = func.layout.last_inst(block).unwrap();
        assert_eq!(func.dfg.insts[last].opcode(), Opcode::Return);
    }
}
