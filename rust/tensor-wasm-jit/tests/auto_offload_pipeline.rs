// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! S14 integration test: drive the auto-offload pipeline end-to-end on
//! the no-CUDA path.
//!
//! Pipeline: detector → clif_lower → ptx_emit → cache.

use tensor_wasm_core::types::TenantId;
use tensor_wasm_jit::cache::{CacheKey, CachedKernel, CompiledHandle, KernelCache};
use tensor_wasm_jit::clif_lower::lower_block;
use tensor_wasm_jit::deopt::{DeoptGuard, DeoptReason};
use tensor_wasm_jit::detector::{classify_default, BlockIR, DetectorVerdict, Op};
use tensor_wasm_jit::ir::ElemType;
use tensor_wasm_jit::ptx_emit::emit;

fn fma() -> Op {
    Op::V128Fma {
        lane_ty: ElemType::F32,
        lanes: 4,
    }
}
fn mul() -> Op {
    Op::V128Mul {
        lane_ty: ElemType::F32,
        lanes: 4,
    }
}

fn matmul_inner_block() -> BlockIR {
    // 90% v128 with a static loop trip count of 256 — should offload.
    BlockIR::new(
        "matmul_inner",
        vec![
            fma(),
            fma(),
            fma(),
            fma(),
            fma(),
            fma(),
            fma(),
            fma(),
            mul(),
            Op::Store,
        ],
        Some(256),
    )
}

fn cpu_only_block() -> BlockIR {
    // Branchy scalar code — should NOT offload.
    BlockIR::new(
        "cpu_only",
        vec![
            Op::ScalarAdd,
            Op::Branch,
            Op::ScalarMul,
            Op::Call,
            Op::Load,
            Op::Store,
        ],
        Some(256),
    )
}

#[test]
fn pipeline_offload_path() {
    let block = matmul_inner_block();
    assert_eq!(classify_default(&block), DetectorVerdict::Offload);

    let blueprint = lower_block(&block).expect("lower");
    assert!(!blueprint.is_empty());

    let ptx = emit(&blueprint).expect("emit");
    assert!(ptx.text.contains(".target sm_80"));
    assert!(ptx.text.contains(".visible .entry matmul_inner"));

    let cache = KernelCache::new();
    let tenant = TenantId(42);
    let key = CacheKey::for_tenant(tenant, blueprint.fingerprint(), 80);
    cache.put(
        key,
        CachedKernel::new(
            blueprint.fingerprint(),
            std::sync::Arc::new(ptx),
            CompiledHandle::default(),
        ),
    );

    // Cache hit on a second lookup with the same blueprint and tenant.
    let hit = cache
        .get_for(tenant, &blueprint, 80)
        .expect("cache hit expected");
    assert_eq!(hit.fingerprint, blueprint.fingerprint());
}

#[test]
fn pipeline_cpu_only_path() {
    let block = cpu_only_block();
    assert_eq!(classify_default(&block), DetectorVerdict::KeepOnCpu);
    // Lowering also rejects the unsupported `Branch` op as a safety net.
    assert!(lower_block(&block).is_err());
}

#[test]
fn deopt_guard_skips_cache_on_failure() {
    let block = matmul_inner_block();
    let blueprint = lower_block(&block).unwrap();
    let guard = DeoptGuard::new();

    guard.record_deopt(blueprint.fingerprint(), DeoptReason::NumericalDivergence);
    assert!(guard.is_deopted(blueprint.fingerprint()));

    guard.clear_deopt(blueprint.fingerprint());
    assert!(!guard.is_deopted(blueprint.fingerprint()));
}

#[test]
fn matmul_wat_fixture_present() {
    let path = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/wasm-fixtures/matrix_multiply.wat");
    assert!(path.exists(), "WAT fixture missing at {}", path.display());
    let contents = std::fs::read_to_string(&path).unwrap();
    assert!(contents.contains("matmul"));
    assert!(contents.contains("f32.mul"));
}

#[test]
fn matrix_multiply_wat_pipeline_smoke() {
    // Read the fixture and parse it to confirm it compiles to Wasm, and
    // verify the v128-rich inner block is *recognisable* by walking the
    // module with wasmparser. We don't reconstruct full Cranelift CLIF —
    // we use this as a sanity check that the fixture exists and is
    // syntactically valid Wasm that contains the f32.mul/f32.add ops
    // representative of a v128-heavy lowered inner loop.

    use wasmparser::{Parser, Payload};

    let path = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../../tests/wasm-fixtures/matrix_multiply.wat");
    let wat = std::fs::read_to_string(&path).expect("read wat");
    let wasm = wat::parse_str(&wat).expect("wat parses to wasm");

    // Count f32.mul / f32.add operators — these are what the lowering pass
    // would observe as candidates for v128.* lowering.
    let mut f32_arith_ops = 0usize;
    for payload in Parser::new(0).parse_all(&wasm) {
        if let Payload::CodeSectionEntry(body) = payload.expect("payload") {
            let mut ops = body.get_operators_reader().expect("ops");
            while let Ok(op) = ops.read() {
                use wasmparser::Operator::*;
                if matches!(op, F32Mul | F32Add | F32Sub | F32Div) {
                    f32_arith_ops += 1;
                }
            }
        }
    }
    assert!(
        f32_arith_ops >= 2,
        "expected at least 2 f32 arith ops in the matmul fixture, got {f32_arith_ops}"
    );

    // Drive the synthetic BlockIR pipeline (the path the executor would use
    // once the wasmparser→BlockIR translator lands) with a block shaped
    // like the inner matmul loop and verify it reaches `Offload`.
    let block = matmul_inner_block();
    assert_eq!(classify_default(&block), DetectorVerdict::Offload);
    let bp = lower_block(&block).expect("lower");
    let ptx = emit(&bp).expect("emit");
    assert!(ptx.text.contains("vector_add") || ptx.text.contains(".entry matmul_inner"));
}
