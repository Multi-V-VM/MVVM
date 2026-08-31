// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Regression tests for jit S-1 — PTX entry-name validation.
//!
//! `ptx_emit::emit_with` interpolates `blueprint.entry` into the PTX
//! template at multiple sites unescaped. If a tenant-controlled entry like
//! `.entry attacker(...)\n` reaches it, the kernel scope closes and a
//! second adversary kernel is emitted. The validator rejects anything
//! that isn't a well-formed PTX identifier before any output is written.

use tensor_wasm_jit::ir::{ElemType, GridHint, TensorWasmKernelBlueprint, TensorWasmOp};
use tensor_wasm_jit::ptx_emit::{emit, is_valid_ptx_identifier, EmitError, MAX_PTX_IDENTIFIER_LEN};

#[test]
fn validator_accepts_canonical_identifiers() {
    assert!(is_valid_ptx_identifier("func0"));
    assert!(is_valid_ptx_identifier("_kernel"));
    assert!(is_valid_ptx_identifier("my$kernel"));
    assert!(is_valid_ptx_identifier("a"));
    assert!(is_valid_ptx_identifier("A_b_C_1_2_3"));
}

#[test]
fn validator_rejects_invalid_identifiers() {
    // empty
    assert!(!is_valid_ptx_identifier(""));
    // starts with a digit
    assert!(!is_valid_ptx_identifier("0starts_with_digit"));
    // contains whitespace
    assert!(!is_valid_ptx_identifier("has space"));
    // contains closing paren — the injection-shape character
    assert!(!is_valid_ptx_identifier("has)closeparen"));
    // contains newline
    assert!(!is_valid_ptx_identifier("has\nnewline"));
    // contains semicolon
    assert!(!is_valid_ptx_identifier("has;semicolon"));
    // 1025 bytes — one over the cap
    let too_long: String = "a".repeat(MAX_PTX_IDENTIFIER_LEN + 1);
    assert!(!is_valid_ptx_identifier(&too_long));
    // exactly at cap is accepted
    let at_cap: String = "a".repeat(MAX_PTX_IDENTIFIER_LEN);
    assert!(is_valid_ptx_identifier(&at_cap));
}

#[test]
fn emit_rejects_injection_entry() {
    let bp = TensorWasmKernelBlueprint::new(".entry attacker()\n")
        .push(TensorWasmOp::StoreUnified {
            elem: ElemType::F32,
            lanes: 4,
        })
        .with_grid(GridHint {
            total_threads: 1024,
            preferred_block_size: 128,
        });
    let err = emit(&bp).expect_err("injection entry must be rejected");
    assert!(matches!(err, EmitError::InvalidEntryName { .. }));
}

#[test]
fn emit_accepts_canonical_entry() {
    let bp = TensorWasmKernelBlueprint::new("safe_kernel_42")
        .push(TensorWasmOp::StoreUnified {
            elem: ElemType::F32,
            lanes: 4,
        })
        .with_grid(GridHint {
            total_threads: 1024,
            preferred_block_size: 128,
        });
    let out = emit(&bp).expect("canonical entry must succeed");
    assert!(out.text.contains(".visible .entry safe_kernel_42("));
}
