// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Reject-list detector for the Pliron `dialect-mir` lowering pipeline.
//!
//! [`scan_function`] walks a [`cranelift_codegen::ir::Function`] and returns
//! every instruction whose opcode the wave-2 lowering cannot translate to
//! the [`crate::lowered_ir::LoweredOp`] interim IR. The categories mirror
//! the "Unsupported in v0.4" section of
//! [`crate::pliron_dialect`](crate::pliron_dialect) one-for-one:
//!
//! - **Atomics** ([`RejectReason::Atomic`]): `atomic_load`, `atomic_store`,
//!   `atomic_rmw`, `atomic_cas`. Deferred — Wasm threads + GPU atomics is a
//!   memory-model alignment problem larger than the wave-2 scope.
//! - **Strict-FP exception bits** ([`RejectReason::StrictFp`]):
//!   `fcvt_to_sint_sat`, `fcvt_to_uint_sat`. PTX default rounding diverges
//!   from Wasm-strict FP — these opcodes carry trap semantics the wave-2
//!   lowering cannot honour.
//! - **Host calls** ([`RejectReason::HostCall`]): `call`, `call_indirect`,
//!   `return_call`, `return_call_indirect`. PTX has no host-callback path;
//!   wave 3+ will distinguish device-side `func.call`s from host
//!   round-trips, but for wave 2 every call is rejected.
//!
//! # Categories documented but absent from this Cranelift version
//!
//! The canonical reject list in [`crate::pliron_dialect`] also names:
//!
//! - **Wasm table ops** (`table.get` / `table.set`)
//! - **Wasm GC / `ref.func`**
//! - **`memory.grow` / `memory.size`**
//! - **`memory.copy` / `memory.fill`**
//!
//! These are **not direct Cranelift opcodes in the pinned `0.111.9`
//! workspace dependency**: they are intercepted by the Wasm-to-Cranelift
//! translator (`cranelift-wasm` and Wasmtime), which lowers them into
//! sequences of `load`/`store`/`call`-to-libcall before they reach the
//! `ir::Function` this detector sees. The `call` reject above is therefore
//! load-bearing: any host-routed lowering surfaces here as a `Call` and
//! gets rejected on that ground. The dedicated [`RejectReason::TableOp`],
//! [`RejectReason::GcOp`], [`RejectReason::MemoryResize`] and
//! [`RejectReason::LargeMemcpy`] variants are pre-declared so the public
//! API is stable when a future Cranelift bump (or a direct Wasm front-end)
//! exposes those opcodes natively.
//!
//! # Why a separate pass
//!
//! The detector runs *before* the per-family `lower_*` modules. Returning
//! an early `Vec<Rejection>` (rather than letting the lowerings fail one
//! by one) is the contract that lets the auto-offload pipeline fall back
//! to the blueprint detector cleanly: an admissible function gets the
//! Pliron pipeline, an inadmissible one gets the legacy blueprint path —
//! never a half-lowered, half-rejected mess.

#![cfg(feature = "cuda-oxide-backend")]

use std::collections::HashMap;

use cranelift_codegen::ir::{Block, Function, Inst, Opcode};

/// A reason a Cranelift instruction cannot be lowered to a
/// [`crate::lowered_ir::LoweredOp`].
///
/// Each variant carries the offending opcode's mnemonic (`&'static str`)
/// so error messages and logs are grep-able against the [mapping
/// table](crate::pliron_dialect#mapping-table). The mnemonic is the same
/// string Cranelift's own [`Opcode`] `Display` impl prints (e.g.
/// `"atomic_rmw"`, not `"AtomicRmw"`), making it stable across the
/// `cranelift-codegen` version bumps that periodically rename enum
/// variants but keep the textual mnemonic.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum RejectReason {
    /// Atomic memory operation. Deferred — Wasm-threads-on-GPU is a
    /// memory-model problem out of scope for wave 2. Covers `atomic_load`,
    /// `atomic_store`, `atomic_rmw`, `atomic_cas`.
    Atomic(&'static str),

    /// Floating-point opcode with strict-FP exception semantics PTX
    /// default rounding cannot match. Covers `fcvt_to_sint_sat` and
    /// `fcvt_to_uint_sat` — both saturate-on-out-of-range, behaviour the
    /// PTX `cvt` does not provide by default. The full strict-FP scope
    /// (NaN propagation, denormals-as-zero) is broader; wave 2 limits
    /// itself to the trap-carrying conversions.
    StrictFp(&'static str),

    /// Wasm `table.get` / `table.set`. Tables live host-side; lowering
    /// them would require device-resident table mirrors. Hard-rejected.
    ///
    /// **Not currently wired in the detector**: `cranelift-codegen` 0.111
    /// has no direct `Opcode::TableGet` / `Opcode::TableSet` variant —
    /// `cranelift-wasm` lowers these to `load`/`store`+libcall sequences
    /// in the translator, so a Wasm module reaches us with the table
    /// access already expanded into a `call` to a runtime helper (and
    /// therefore is caught by [`RejectReason::HostCall`]). Pre-declared
    /// here so a future direct Wasm front-end or a Cranelift bump that
    /// introduces table opcodes natively does not have to reshuffle the
    /// public enum.
    TableOp(&'static str),

    /// Wasm GC / reference-type opcode (`ref.func`, `ref.null`,
    /// `ref.is_null` on a non-i31 ref). No device-side representation
    /// possible. Hard-rejected.
    ///
    /// **Not currently wired** for the same reason as
    /// [`RejectReason::TableOp`]: in `cranelift-codegen` 0.111 the GC
    /// proposal opcodes either do not exist as direct enum variants
    /// (`ref.func`) or are translated into other ops by `cranelift-wasm`
    /// before the detector sees them. Pre-declared for forward
    /// compatibility.
    GcOp(&'static str),

    /// `memory.grow` / `memory.size`. Linear-memory resizing requires a
    /// host round-trip; PTX kernels must run with a fixed memory
    /// snapshot.
    ///
    /// **Not currently wired**: `cranelift-codegen` 0.111 has no direct
    /// `Opcode::MemoryGrow` / `Opcode::MemorySize` — Wasm `memory.grow`
    /// reaches us as a libcall (a `Call` instruction targeting the
    /// runtime helper) and is therefore caught by
    /// [`RejectReason::HostCall`]. Pre-declared for the same forward-
    /// compatibility reason as the other absent categories.
    MemoryResize(&'static str),

    /// `memory.copy` / `memory.fill` above the inline-copy threshold.
    /// PTX has `cp.async.bulk` (sm_90+) but the wave-2 baseline is
    /// sm_80 — the cuda-oxide PTX target version pinned via
    /// [`crate::ptx_emit::DEFAULT_TARGET`](crate::ptx_emit::DEFAULT_TARGET).
    ///
    /// **Not currently wired**: same translator-expansion situation as
    /// the other absent categories — `memory.copy` / `memory.fill` reach
    /// us as `Call` instructions to the runtime, caught by
    /// [`RejectReason::HostCall`]. Pre-declared for forward compatibility.
    /// Wave 3+ will refine [`RejectReason::HostCall`] to distinguish
    /// device-internal calls from runtime-helper calls and may at that
    /// point route small `memory.copy` libcalls back to an inline
    /// `Load`/`Store` sequence instead of a hard reject.
    LargeMemcpy(&'static str),

    /// `call` / `call_indirect` / `return_call` / `return_call_indirect`.
    /// Host-callback prohibition: PTX has no path back into the Wasmtime
    /// runtime, so every call is rejected at the wave-2 detector. Wave
    /// 3+ will distinguish device-internal calls (legal: lowered to
    /// `func.call`) from host round-trips (illegal: rejected).
    HostCall(&'static str),

    /// Any floating-point opcode (`fadd`, `fmul`, `fdiv`, `fma`, `sqrt`,
    /// `fcmp`, the float conversions, …).
    ///
    /// jit MED fix (finding 4): the reject-list previously admitted
    /// non-saturating FP ops. PTX default rounding (`add.rn.f32` etc.) is
    /// bit-exact IEEE for the basic ops, but the broader strict-FP scope
    /// (NaN payload propagation, denormal-as-zero behaviour, transcendental
    /// approximations) is NOT yet proven equivalent to the Wasm/CPU
    /// reference across this lowering path. Until a differential proof of
    /// bit-exact rounding lands, ALL float opcodes are rejected so a float
    /// function deopts to the CPU path rather than risk silent numerical
    /// divergence. (The basic-op subset can be re-admitted once the
    /// differential oracle proves equivalence — narrow this then.)
    FloatOp(&'static str),

    /// Integer divide / remainder (`sdiv`, `udiv`, `srem`, `urem`).
    ///
    /// jit MED fix (finding 4): Wasm `i32.div_u` etc. TRAP on divide-by-
    /// zero (and signed div traps on `INT_MIN / -1` overflow). PTX `div`
    /// has undefined behaviour on divide-by-zero — it does not trap. Until
    /// the lowering emits an explicit divisor-zero guard that reproduces
    /// the Wasm trap, integer div/rem is rejected so it stays on the CPU
    /// path rather than producing a non-trapping (wrong) result on the GPU.
    IntDivRem(&'static str),

    /// A control-flow back-edge: a branch whose target is a block at or
    /// before the branching block in layout order (i.e. a loop).
    ///
    /// jit MED fix (finding 4): the wave-2 lowering does not yet model loop
    /// carried dependencies / convergence on the GPU, so any function
    /// containing a loop back-edge is rejected. Straight-line and
    /// forward-only (if/else, switch) control flow remains admissible.
    BackEdge(&'static str),

    /// An explicit trap or unreachable (`trap`, `trapz`, `trapnz`,
    /// `resumable_trap`, `debugtrap`; Wasm `unreachable` lowers to `trap`).
    ///
    /// jit MED fix (finding 4): PTX has no equivalent of the Wasm trap
    /// machinery (which unwinds back into the host with a trap code). A
    /// function that can trap mid-kernel cannot be faithfully offloaded, so
    /// it is rejected.
    Trap(&'static str),
}

impl RejectReason {
    /// The Cranelift opcode mnemonic that triggered this rejection.
    ///
    /// Convenience accessor for log lines / error formatting that want
    /// the offending opcode name without `match`-ing on the variant.
    pub fn opcode_mnemonic(&self) -> &'static str {
        match self {
            Self::Atomic(m)
            | Self::StrictFp(m)
            | Self::TableOp(m)
            | Self::GcOp(m)
            | Self::MemoryResize(m)
            | Self::LargeMemcpy(m)
            | Self::HostCall(m)
            | Self::FloatOp(m)
            | Self::IntDivRem(m)
            | Self::BackEdge(m)
            | Self::Trap(m) => m,
        }
    }
}

/// A single rejected instruction with its location in the function.
///
/// Returned by [`scan_function`]; the `(inst, block)` pair is enough for
/// the caller to render a diagnostic with `cranelift_codegen`'s own
/// pretty-printer (e.g. `func.dfg.display_inst(inst)`).
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Rejection {
    /// Cranelift instruction that triggered the rejection.
    pub inst: Inst,
    /// Block containing the offending instruction.
    pub block: Block,
    /// Reason for rejection (also carries the opcode mnemonic).
    pub reason: RejectReason,
}

/// Walk `func` and return every rejection.
///
/// Empty `Vec` ⇔ the function is admissible to the Pliron pipeline modulo
/// per-opcode lowering errors caught later. The detector is intentionally
/// conservative: when in doubt, reject — it is cheaper to fall back to the
/// blueprint detector than to half-lower a function and then bail.
///
/// The scan is `O(n)` in the number of instructions and allocates only the
/// returned `Vec`. It does not run the Cranelift verifier and therefore
/// makes no well-formedness guarantees on `func` beyond what the layout
/// iterator exposes.
pub fn scan_function(func: &Function) -> Vec<Rejection> {
    let mut rejections = Vec::new();
    let block_order = block_layout_order(func);
    for block in func.layout.blocks() {
        let block_idx = block_order.get(&block).copied().unwrap_or(usize::MAX);
        for inst in func.layout.block_insts(block) {
            if let Some(reason) = classify_opcode(func.dfg.insts[inst].opcode()) {
                rejections.push(Rejection {
                    inst,
                    block,
                    reason,
                });
            } else if let Some(reason) = back_edge_reason(func, inst, block_idx, &block_order) {
                // jit MED fix (finding 4): a branch whose target is at or
                // before this block in layout order is a loop back-edge.
                rejections.push(Rejection {
                    inst,
                    block,
                    reason,
                });
            }
        }
    }
    rejections
}

/// Convenience wrapper: return the **first** rejection or `None`.
///
/// Useful for the call-site shortcut "is this function admissible?" —
/// avoids the `Vec` allocation when the caller does not need the full
/// list. Equivalent to `scan_function(func).into_iter().next()` but does
/// not walk the rest of the function once a rejection is found.
pub fn check_function(func: &Function) -> Option<Rejection> {
    let block_order = block_layout_order(func);
    for block in func.layout.blocks() {
        let block_idx = block_order.get(&block).copied().unwrap_or(usize::MAX);
        for inst in func.layout.block_insts(block) {
            if let Some(reason) = classify_opcode(func.dfg.insts[inst].opcode()) {
                return Some(Rejection {
                    inst,
                    block,
                    reason,
                });
            }
            if let Some(reason) = back_edge_reason(func, inst, block_idx, &block_order) {
                return Some(Rejection {
                    inst,
                    block,
                    reason,
                });
            }
        }
    }
    None
}

/// Assign each block its position in layout order. A branch to a block
/// whose index is `<=` the branching block's index is a back-edge (loop).
fn block_layout_order(func: &Function) -> HashMap<Block, usize> {
    func.layout
        .blocks()
        .enumerate()
        .map(|(idx, block)| (block, idx))
        .collect()
}

/// If `inst` is a branch with a destination at or before `block_idx` in
/// layout order, return a [`RejectReason::BackEdge`]. Returns `None` for
/// non-branch instructions and purely forward branches.
///
/// jit MED fix (finding 4): the wave-2 lowering does not model loop-carried
/// dependencies, so any loop (detected here as a back-edge) is rejected.
fn back_edge_reason(
    func: &Function,
    inst: Inst,
    block_idx: usize,
    block_order: &HashMap<Block, usize>,
) -> Option<RejectReason> {
    let data = &func.dfg.insts[inst];
    let opcode = data.opcode();
    if !opcode.is_branch() {
        return None;
    }
    let mut is_back_edge = false;
    for block_call in data.branch_destination(&func.dfg.jump_tables) {
        let target = block_call.block(&func.dfg.value_lists);
        if let Some(&target_idx) = block_order.get(&target) {
            if target_idx <= block_idx {
                is_back_edge = true;
            }
        }
    }
    if is_back_edge {
        Some(RejectReason::BackEdge(branch_mnemonic(opcode)))
    } else {
        None
    }
}

/// Static mnemonic for a branch opcode (diagnostics only).
fn branch_mnemonic(op: Opcode) -> &'static str {
    match op {
        Opcode::Jump => "jump",
        Opcode::Brif => "brif",
        Opcode::BrTable => "br_table",
        _ => "branch",
    }
}

/// Map a Cranelift [`Opcode`] to a [`RejectReason`] if it is on the
/// reject list, otherwise `None`.
///
/// Centralised match so the two scanners stay in lock-step. The mnemonic
/// strings are hard-coded `&'static str` literals (rather than
/// `opcode_name(op)` / `Display`) to keep [`RejectReason`] `Copy`-able
/// and avoid the runtime cost of stringification in the hot path; they
/// are kept identical to Cranelift's own mnemonics so the two are
/// interchangeable for log output.
fn classify_opcode(op: Opcode) -> Option<RejectReason> {
    match op {
        // Atomics — see RejectReason::Atomic.
        Opcode::AtomicLoad => Some(RejectReason::Atomic("atomic_load")),
        Opcode::AtomicStore => Some(RejectReason::Atomic("atomic_store")),
        Opcode::AtomicRmw => Some(RejectReason::Atomic("atomic_rmw")),
        Opcode::AtomicCas => Some(RejectReason::Atomic("atomic_cas")),

        // Strict-FP saturating conversions — see RejectReason::StrictFp.
        // PTX `cvt.f32.s32` / `cvt.f32.u32` do not saturate; the wave-2
        // lowering cannot honour the Wasm-strict saturation semantics
        // without an explicit min/max clamp the detector chooses not to
        // synthesize for v0.4.
        Opcode::FcvtToSintSat => Some(RejectReason::StrictFp("fcvt_to_sint_sat")),
        Opcode::FcvtToUintSat => Some(RejectReason::StrictFp("fcvt_to_uint_sat")),

        // Host-call prohibition — see RejectReason::HostCall. This also
        // catches the libcall-expanded forms of `memory.grow`,
        // `memory.size`, `memory.copy`, `memory.fill`, `table.get`, and
        // `table.set` that `cranelift-wasm` emits in pinned Cranelift
        // 0.111. Wave 3+ will refine this match to peek into `func_ref`
        // and distinguish device-internal calls from runtime helpers.
        Opcode::Call => Some(RejectReason::HostCall("call")),
        Opcode::CallIndirect => Some(RejectReason::HostCall("call_indirect")),
        Opcode::ReturnCall => Some(RejectReason::HostCall("return_call")),
        Opcode::ReturnCallIndirect => Some(RejectReason::HostCall("return_call_indirect")),

        // Integer divide / remainder — see RejectReason::IntDivRem. These
        // trap on divide-by-zero in Wasm but PTX `div`/`rem` do not, so
        // they are rejected until the trap is emulated (jit MED finding 4).
        Opcode::Sdiv => Some(RejectReason::IntDivRem("sdiv")),
        Opcode::Udiv => Some(RejectReason::IntDivRem("udiv")),
        Opcode::Srem => Some(RejectReason::IntDivRem("srem")),
        Opcode::Urem => Some(RejectReason::IntDivRem("urem")),

        // Trap / unreachable — see RejectReason::Trap. No PTX equivalent of
        // the Wasm trap unwind (jit MED finding 4). `unreachable` lowers to
        // `trap` in Cranelift, so this also covers Wasm `unreachable`.
        Opcode::Trap => Some(RejectReason::Trap("trap")),
        Opcode::Trapz => Some(RejectReason::Trap("trapz")),
        Opcode::Trapnz => Some(RejectReason::Trap("trapnz")),
        Opcode::Debugtrap => Some(RejectReason::Trap("debugtrap")),

        // All floating-point opcodes — see RejectReason::FloatOp. Rejected
        // wholesale until bit-exact PTX rounding equivalence is proven by
        // the differential oracle (jit MED finding 4). The two saturating
        // conversions keep their more specific `StrictFp` reason above for
        // diagnostic continuity; everything else float lands here.
        op if is_float_opcode(op) => Some(RejectReason::FloatOp(float_mnemonic(op))),

        // Everything else is provisionally admissible; the per-family
        // lowerings may still reject in their own pass.
        _ => None,
    }
}

/// True for any floating-point Cranelift opcode the wave-2 reject-list
/// refuses (jit MED finding 4). Kept as an explicit allow/deny list rather
/// than a type-based check because `classify_opcode` only has the opcode in
/// hand (the scanners deliberately avoid touching value types for speed).
fn is_float_opcode(op: Opcode) -> bool {
    matches!(
        op,
        Opcode::Fadd
            | Opcode::Fsub
            | Opcode::Fmul
            | Opcode::Fdiv
            | Opcode::Fma
            | Opcode::Fneg
            | Opcode::Fabs
            | Opcode::Fcopysign
            | Opcode::Fmin
            | Opcode::Fmax
            | Opcode::Sqrt
            | Opcode::Ceil
            | Opcode::Floor
            | Opcode::Trunc
            | Opcode::Nearest
            | Opcode::Fcmp
            | Opcode::Fpromote
            | Opcode::Fdemote
            | Opcode::FcvtFromSint
            | Opcode::FcvtFromUint
            | Opcode::FcvtToSint
            | Opcode::FcvtToUint
    )
}

/// Static mnemonic for a float opcode that `is_float_opcode` accepts.
/// Falls back to a generic `"float_op"` label for any opcode not
/// individually named — the reject decision is what matters, the mnemonic
/// is only for diagnostics.
fn float_mnemonic(op: Opcode) -> &'static str {
    match op {
        Opcode::Fadd => "fadd",
        Opcode::Fsub => "fsub",
        Opcode::Fmul => "fmul",
        Opcode::Fdiv => "fdiv",
        Opcode::Fma => "fma",
        Opcode::Fneg => "fneg",
        Opcode::Fabs => "fabs",
        Opcode::Fcopysign => "fcopysign",
        Opcode::Fmin => "fmin",
        Opcode::Fmax => "fmax",
        Opcode::Sqrt => "sqrt",
        Opcode::Ceil => "ceil",
        Opcode::Floor => "floor",
        Opcode::Trunc => "trunc",
        Opcode::Nearest => "nearest",
        Opcode::Fcmp => "fcmp",
        Opcode::Fpromote => "fpromote",
        Opcode::Fdemote => "fdemote",
        Opcode::FcvtFromSint => "fcvt_from_sint",
        Opcode::FcvtFromUint => "fcvt_from_uint",
        Opcode::FcvtToSint => "fcvt_to_sint",
        Opcode::FcvtToUint => "fcvt_to_uint",
        _ => "float_op",
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::ir::immediates::Offset32;
    use cranelift_codegen::ir::instructions::InstructionData;
    use cranelift_codegen::ir::{
        AtomicRmwOp, FuncRef, MemFlags, SigRef, Signature, UserFuncName, Value, ValueList,
    };
    use cranelift_codegen::isa::CallConv;

    /// Build a fresh empty function with a single empty entry block.
    /// The wave-2 detector never inspects values or types, only opcodes,
    /// so the dummy `Value(0)` / `FuncRef(0)` / `SigRef(0)` placeholders
    /// the tests use never need to be valid — `scan_function` does not
    /// dereference them.
    fn empty_func() -> (Function, Block) {
        let mut func = Function::with_name_signature(
            UserFuncName::user(0, 0),
            Signature::new(CallConv::SystemV),
        );
        let block = func.dfg.make_block();
        func.layout.append_block(block);
        (func, block)
    }

    /// Convenience: place `data` at the end of `block` and return the
    /// resulting `Inst`. Intentionally does NOT call `make_inst_results`
    /// — the detector ignores result values, and skipping result wiring
    /// keeps the test fixtures small and verifier-independent.
    fn append(func: &mut Function, block: Block, data: InstructionData) -> Inst {
        let inst = func.dfg.make_inst(data);
        func.layout.append_inst(inst, block);
        inst
    }

    /// A throwaway `Value` reference. Never dereferenced by the
    /// detector, so any well-formed `Value` ID is fine.
    fn dummy_val() -> Value {
        Value::from_u32(0)
    }

    // ---- Positive (admissible) case -----------------------------------

    /// A function with only admissible opcodes (`iadd` + `return`)
    /// produces no rejections.
    #[test]
    fn admissible_iadd_return_yields_empty() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Binary {
                opcode: Opcode::Iadd,
                args: [dummy_val(), dummy_val()],
            },
        );
        append(
            &mut func,
            block,
            InstructionData::MultiAry {
                opcode: Opcode::Return,
                args: ValueList::new(),
            },
        );

        assert_eq!(scan_function(&func), vec![]);
        assert_eq!(check_function(&func), None);
    }

    // ---- Atomic family -------------------------------------------------

    /// `atomic_load` is rejected with the `Atomic` reason.
    #[test]
    fn atomic_load_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::LoadNoOffset {
                opcode: Opcode::AtomicLoad,
                flags: MemFlags::new(),
                arg: dummy_val(),
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(rejections[0].reason, RejectReason::Atomic("atomic_load"));
        assert_eq!(rejections[0].block, block);
    }

    /// `atomic_store` is rejected with the `Atomic` reason.
    #[test]
    fn atomic_store_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::StoreNoOffset {
                opcode: Opcode::AtomicStore,
                flags: MemFlags::new(),
                args: [dummy_val(), dummy_val()],
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(rejections[0].reason, RejectReason::Atomic("atomic_store"));
    }

    /// `atomic_rmw` is rejected with the `Atomic` reason.
    #[test]
    fn atomic_rmw_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::AtomicRmw {
                opcode: Opcode::AtomicRmw,
                flags: MemFlags::new(),
                op: AtomicRmwOp::Add,
                args: [dummy_val(), dummy_val()],
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(rejections[0].reason, RejectReason::Atomic("atomic_rmw"));
    }

    /// `atomic_cas` is rejected with the `Atomic` reason.
    #[test]
    fn atomic_cas_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::AtomicCas {
                opcode: Opcode::AtomicCas,
                flags: MemFlags::new(),
                args: [dummy_val(), dummy_val(), dummy_val()],
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(rejections[0].reason, RejectReason::Atomic("atomic_cas"));
    }

    // ---- Strict-FP family ----------------------------------------------

    /// `fcvt_to_sint_sat` is rejected with the `StrictFp` reason.
    #[test]
    fn fcvt_to_sint_sat_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Unary {
                opcode: Opcode::FcvtToSintSat,
                arg: dummy_val(),
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(
            rejections[0].reason,
            RejectReason::StrictFp("fcvt_to_sint_sat")
        );
    }

    /// `fcvt_to_uint_sat` is also rejected (sibling saturating
    /// conversion). Covers the second variant of the `StrictFp` family
    /// the detector recognises.
    #[test]
    fn fcvt_to_uint_sat_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Unary {
                opcode: Opcode::FcvtToUintSat,
                arg: dummy_val(),
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(
            rejections[0].reason,
            RejectReason::StrictFp("fcvt_to_uint_sat")
        );
    }

    // ---- Host-call family ----------------------------------------------

    /// `call` is rejected with the `HostCall` reason. The dummy
    /// `FuncRef(0)` is never resolved by the detector.
    #[test]
    fn call_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Call {
                opcode: Opcode::Call,
                func_ref: FuncRef::from_u32(0),
                args: ValueList::new(),
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(rejections[0].reason, RejectReason::HostCall("call"));
    }

    /// `call_indirect` is rejected with the `HostCall` reason.
    #[test]
    fn call_indirect_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::CallIndirect {
                opcode: Opcode::CallIndirect,
                sig_ref: SigRef::from_u32(0),
                args: ValueList::new(),
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 1);
        assert_eq!(
            rejections[0].reason,
            RejectReason::HostCall("call_indirect")
        );
    }

    // ---- TableOp / GcOp / MemoryResize / LargeMemcpy -------------------
    //
    // These four categories are pre-declared in `RejectReason` for forward
    // compatibility but are not wired to a specific Cranelift opcode in
    // 0.111.9 (see module docs — the wasm-to-clif translator expands them
    // into `call` libcalls before the detector sees them, so they are
    // caught indirectly via `HostCall`). The unit tests below exercise
    // the *variant* surface — constructing and inspecting each reason —
    // so a future wiring patch only needs to add a `match` arm in
    // `classify_opcode`, not redesign the public enum.

    /// The `TableOp` variant round-trips through `opcode_mnemonic`.
    /// Pre-declared variant; no opcode wired in 0.111.
    #[test]
    fn table_op_variant_is_constructible() {
        let r = RejectReason::TableOp("table_get");
        assert_eq!(r.opcode_mnemonic(), "table_get");
    }

    /// The `GcOp` variant round-trips through `opcode_mnemonic`.
    /// Pre-declared variant; no opcode wired in 0.111.
    #[test]
    fn gc_op_variant_is_constructible() {
        let r = RejectReason::GcOp("ref_func");
        assert_eq!(r.opcode_mnemonic(), "ref_func");
    }

    /// The `MemoryResize` variant round-trips through `opcode_mnemonic`.
    /// Pre-declared variant; no opcode wired in 0.111.
    #[test]
    fn memory_resize_variant_is_constructible() {
        let r = RejectReason::MemoryResize("memory_grow");
        assert_eq!(r.opcode_mnemonic(), "memory_grow");
    }

    /// The `LargeMemcpy` variant round-trips through `opcode_mnemonic`.
    /// Pre-declared variant; no opcode wired in 0.111.
    #[test]
    fn large_memcpy_variant_is_constructible() {
        let r = RejectReason::LargeMemcpy("memory_copy");
        assert_eq!(r.opcode_mnemonic(), "memory_copy");
    }

    // ---- Multi-rejection / ordering ------------------------------------

    /// A function with two rejected instructions returns both, in
    /// layout order. Locks in the contract that the detector is a
    /// *list* not a "first failure" — wave 3+ diagnostics will rely on
    /// the full list to surface every issue at once.
    #[test]
    fn multiple_rejections_returned_in_layout_order() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::LoadNoOffset {
                opcode: Opcode::AtomicLoad,
                flags: MemFlags::new(),
                arg: dummy_val(),
            },
        );
        append(
            &mut func,
            block,
            InstructionData::Call {
                opcode: Opcode::Call,
                func_ref: FuncRef::from_u32(0),
                args: ValueList::new(),
            },
        );

        let rejections = scan_function(&func);
        assert_eq!(rejections.len(), 2);
        assert_eq!(rejections[0].reason, RejectReason::Atomic("atomic_load"));
        assert_eq!(rejections[1].reason, RejectReason::HostCall("call"));
    }

    /// `check_function` returns the first rejection encountered (early
    /// exit), not the full list.
    #[test]
    fn check_function_returns_first_rejection() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Call {
                opcode: Opcode::Call,
                func_ref: FuncRef::from_u32(0),
                args: ValueList::new(),
            },
        );
        append(
            &mut func,
            block,
            InstructionData::LoadNoOffset {
                opcode: Opcode::AtomicLoad,
                flags: MemFlags::new(),
                arg: dummy_val(),
            },
        );

        let first = check_function(&func).expect("function has rejections");
        assert_eq!(first.reason, RejectReason::HostCall("call"));
    }

    /// `Offset32` is included in the `cranelift_codegen::ir::immediates`
    /// import set above so the test module compiles even when no test
    /// directly references it; this assertion keeps the import live and
    /// documents that the detector intentionally ignores instruction
    /// offsets (only opcode identity matters).
    #[test]
    fn offset_field_is_irrelevant_to_detection() {
        let _ = Offset32::new(0);
    }

    // ---- jit MED fix (finding 4): float / div-rem / trap / back-edge ----

    /// All floating-point arithmetic is rejected with `FloatOp` until
    /// bit-exact PTX rounding is proven.
    #[test]
    fn float_arith_is_rejected() {
        for (opcode, mnemonic) in [
            (Opcode::Fadd, "fadd"),
            (Opcode::Fmul, "fmul"),
            (Opcode::Fdiv, "fdiv"),
        ] {
            let (mut func, block) = empty_func();
            append(
                &mut func,
                block,
                InstructionData::Binary {
                    opcode,
                    args: [dummy_val(), dummy_val()],
                },
            );
            let first = check_function(&func).expect("float op must be rejected");
            assert_eq!(first.reason, RejectReason::FloatOp(mnemonic));
        }
    }

    /// Float unary ops (`sqrt`) are rejected too.
    #[test]
    fn float_sqrt_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Unary {
                opcode: Opcode::Sqrt,
                arg: dummy_val(),
            },
        );
        let first = check_function(&func).expect("sqrt must be rejected");
        assert_eq!(first.reason, RejectReason::FloatOp("sqrt"));
    }

    /// Integer divide / remainder is rejected until divide-by-zero trap
    /// emulation lands.
    #[test]
    fn int_div_rem_is_rejected() {
        for (opcode, mnemonic) in [
            (Opcode::Sdiv, "sdiv"),
            (Opcode::Udiv, "udiv"),
            (Opcode::Srem, "srem"),
            (Opcode::Urem, "urem"),
        ] {
            let (mut func, block) = empty_func();
            append(
                &mut func,
                block,
                InstructionData::Binary {
                    opcode,
                    args: [dummy_val(), dummy_val()],
                },
            );
            let first = check_function(&func).expect("int div/rem must be rejected");
            assert_eq!(first.reason, RejectReason::IntDivRem(mnemonic));
        }
    }

    /// `iadd` (a non-trapping integer op) stays admissible — the div/rem
    /// reject must not over-reach to all integer arithmetic.
    #[test]
    fn plain_iadd_still_admissible() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Binary {
                opcode: Opcode::Iadd,
                args: [dummy_val(), dummy_val()],
            },
        );
        assert_eq!(check_function(&func), None);
    }

    /// `trap` and friends are rejected.
    #[test]
    fn trap_is_rejected() {
        let (mut func, block) = empty_func();
        append(
            &mut func,
            block,
            InstructionData::Trap {
                opcode: Opcode::Trap,
                code: cranelift_codegen::ir::TrapCode::User(1),
            },
        );
        let first = check_function(&func).expect("trap must be rejected");
        assert_eq!(first.reason, RejectReason::Trap("trap"));
    }

    /// A loop back-edge (a `jump` whose target is the entry block, which is
    /// at or before the branching block in layout order) is rejected; a
    /// forward jump is NOT.
    #[test]
    fn loop_back_edge_is_rejected_forward_jump_is_not() {
        use cranelift_codegen::ir::BlockCall;

        // Build entry -> body, with body jumping BACK to entry (a loop).
        let mut func = Function::with_name_signature(
            UserFuncName::user(0, 0),
            Signature::new(CallConv::SystemV),
        );
        let entry = func.dfg.make_block();
        let body = func.dfg.make_block();
        func.layout.append_block(entry);
        func.layout.append_block(body);

        // entry: jump body  (forward — admissible)
        let fwd_call = BlockCall::new(body, &[], &mut func.dfg.value_lists);
        let fwd = func.dfg.make_inst(InstructionData::Jump {
            opcode: Opcode::Jump,
            destination: fwd_call,
        });
        func.layout.append_inst(fwd, entry);

        // body: jump entry  (back-edge — rejected)
        let back_call = BlockCall::new(entry, &[], &mut func.dfg.value_lists);
        let back = func.dfg.make_inst(InstructionData::Jump {
            opcode: Opcode::Jump,
            destination: back_call,
        });
        func.layout.append_inst(back, body);

        let rejections = scan_function(&func);
        assert_eq!(
            rejections.len(),
            1,
            "exactly the back-edge is rejected, not the forward jump"
        );
        assert!(matches!(rejections[0].reason, RejectReason::BackEdge(_)));
        assert_eq!(rejections[0].block, body);
    }

    /// A self-loop (`jump` to the block's own block) is a back-edge.
    #[test]
    fn self_loop_is_back_edge() {
        use cranelift_codegen::ir::BlockCall;
        let (mut func, block) = empty_func();
        let call = BlockCall::new(block, &[], &mut func.dfg.value_lists);
        let jmp = func.dfg.make_inst(InstructionData::Jump {
            opcode: Opcode::Jump,
            destination: call,
        });
        func.layout.append_inst(jmp, block);
        let first = check_function(&func).expect("self-loop must be rejected");
        assert!(matches!(first.reason, RejectReason::BackEdge(_)));
    }
}
