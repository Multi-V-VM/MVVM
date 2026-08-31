// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Optionally validates the emitted PTX with the host `ptxas` tool.
//!
//! Enabled by setting the env var `TENSOR_WASM_PTXAS=/path/to/ptxas` (or
//! `TENSOR_WASM_PTXAS=ptxas` if it's on PATH). Skipped silently otherwise so the
//! test suite stays green on hosts without a CUDA toolkit.

use std::process::Command;

use tensor_wasm_jit::ir::{ElemType, GridHint, TensorWasmKernelBlueprint, TensorWasmOp};
use tensor_wasm_jit::ptx_emit::{emit, EmitError};

fn ptxas_path() -> Option<String> {
    std::env::var("TENSOR_WASM_PTXAS").ok()
}

fn run_ptxas_or_skip(ptx: &str, label: &str) {
    let Some(ptxas) = ptxas_path() else {
        eprintln!("TENSOR_WASM_PTXAS not set; skipping ptxas validation for {label}");
        return;
    };
    let mut tmp = std::env::temp_dir();
    tmp.push(format!("tensor_wasm_ptx_{}.ptx", label));
    std::fs::write(&tmp, ptx).expect("write tmp ptx");
    let out = Command::new(&ptxas)
        .arg("--gpu-name")
        .arg("sm_80")
        .arg("-o")
        .arg(format!("{}.cubin", tmp.display()))
        .arg(&tmp)
        .output()
        .expect("spawn ptxas");
    assert!(
        out.status.success(),
        "ptxas rejected emitted PTX for {label}\nstderr:\n{}\nptx:\n{}",
        String::from_utf8_lossy(&out.stderr),
        ptx,
    );
    let _ = std::fs::remove_file(&tmp);
    let _ = std::fs::remove_file(format!("{}.cubin", tmp.display()));
}

#[test]
fn emitted_vector_add_validates_with_ptxas() {
    let bp = TensorWasmKernelBlueprint::new("vector_add")
        .push(TensorWasmOp::LoadUnified {
            elem: ElemType::F32,
            lanes: 4,
        })
        .push(TensorWasmOp::LoadUnified {
            elem: ElemType::F32,
            lanes: 4,
        })
        .push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        })
        .push(TensorWasmOp::StoreUnified {
            elem: ElemType::F32,
            lanes: 4,
        })
        .with_grid(GridHint {
            total_threads: 1024,
            preferred_block_size: 128,
        });
    let out = emit(&bp).expect("emit vector_add");
    run_ptxas_or_skip(&out.text, "vector_add");
}

/// MatMul PTX lowering is deferred to v0.4 — the emitter must refuse rather
/// than produce a broken wmma block. This test was previously a positive
/// ptxas-validates assertion; that contract no longer holds. Asserting the
/// explicit "not yet implemented" error guards against accidentally re-
/// introducing the broken emitter, which would silently launch kernels
/// that read undefined `%r1..%r7` and never store their accumulators.
#[test]
fn matmul_emission_is_explicitly_refused() {
    let bp = TensorWasmKernelBlueprint::new("matmul_16x16x16")
        .push(TensorWasmOp::MatMul {
            m: 16,
            n: 16,
            k: 16,
        })
        .with_grid(GridHint {
            total_threads: 256,
            preferred_block_size: 128,
        });
    let err = emit(&bp).expect_err("MatMul emission must fail");
    assert!(matches!(err, EmitError::NotYetImplemented(_)));
}
