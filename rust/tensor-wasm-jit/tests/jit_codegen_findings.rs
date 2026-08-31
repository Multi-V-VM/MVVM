// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Integration tests for the jit-codegen findings (finding 12).
//!
//! Three test families:
//!
//! 1. **ptxas validation across SIMD widths** — emits PTX for each
//!    element-type/lane shape the emitter now supports and runs it through
//!    the host `ptxas` when `TENSOR_WASM_PTXAS` is set (GPU/toolkit
//!    present). Skipped silently otherwise so CI stays green on hosts
//!    without a CUDA toolkit.
//! 2. **integer SIMD is not offloaded to a float kernel** — a differential
//!    unit check that an `i32x4` blueprint lowers to integer PTX ops, never
//!    `add.f32` / `mul.f32` (the CRITICAL miscompile this work fixes).
//! 3. **adversarial-WASM no-panic** — feeds pseudo-random byte streams
//!    through `rewrite_wasm` and asserts it never panics (returns `Ok`/`Err`
//!    cleanly), exercising the parser/detector/lowering pipeline against
//!    malformed input.

use tensor_wasm_jit::cache::KernelCache;
use tensor_wasm_jit::ir::{ElemType, GridHint, TensorWasmKernelBlueprint, TensorWasmOp};
use tensor_wasm_jit::ptx_emit::emit;
use tensor_wasm_jit::rewrite::{rewrite_wasm, RewriteOptions};

// ---------------------------------------------------------------------------
// 1. ptxas validation across the SIMD widths the emitter supports.
// ---------------------------------------------------------------------------

fn ptxas_path() -> Option<String> {
    std::env::var("TENSOR_WASM_PTXAS").ok()
}

fn run_ptxas_or_skip(ptx: &str, label: &str) {
    let Some(ptxas) = ptxas_path() else {
        eprintln!("TENSOR_WASM_PTXAS not set; skipping ptxas validation for {label}");
        return;
    };
    let mut tmp = std::env::temp_dir();
    tmp.push(format!("tensor_wasm_findings_{label}.ptx"));
    std::fs::write(&tmp, ptx).expect("write tmp ptx");
    let out = std::process::Command::new(&ptxas)
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

fn vec_kernel(elem: ElemType, lanes: u32, op: fn(ElemType, u32) -> TensorWasmOp) -> String {
    let bp = TensorWasmKernelBlueprint::new("kernel")
        .push(TensorWasmOp::LoadUnified { elem, lanes })
        .push(TensorWasmOp::LoadUnified { elem, lanes })
        .push(op(elem, lanes))
        .push(TensorWasmOp::StoreUnified { elem, lanes })
        .with_grid(GridHint {
            total_threads: 256,
            preferred_block_size: 128,
        });
    emit(&bp).expect("emit").text
}

#[test]
fn ptxas_validates_f32x4_add() {
    let ptx = vec_kernel(ElemType::F32, 4, |elem, lanes| TensorWasmOp::VecAdd {
        elem,
        lanes,
    });
    assert!(ptx.contains("add.f32"));
    run_ptxas_or_skip(&ptx, "f32x4_add");
}

#[test]
fn ptxas_validates_i32x4_add() {
    let ptx = vec_kernel(ElemType::I32, 4, |elem, lanes| TensorWasmOp::VecAdd {
        elem,
        lanes,
    });
    assert!(ptx.contains("add.s32"));
    run_ptxas_or_skip(&ptx, "i32x4_add");
}

#[test]
fn ptxas_validates_i32x4_mul() {
    let ptx = vec_kernel(ElemType::I32, 4, |elem, lanes| TensorWasmOp::VecMul {
        elem,
        lanes,
    });
    assert!(ptx.contains("mul.lo.s32"));
    run_ptxas_or_skip(&ptx, "i32x4_mul");
}

// ---------------------------------------------------------------------------
// 2. Integer SIMD is not offloaded to a float kernel (CRITICAL regression).
// ---------------------------------------------------------------------------

/// An `i32x4` blueprint must emit INTEGER PTX — `add.s32` into the `%r`
/// register class with `.u32` loads/stores — and MUST NOT contain any
/// `.f32` arithmetic. This is the end-to-end form of the unit test in
/// `ptx_emit`, asserted here against the public emitter surface.
#[test]
fn integer_simd_emits_integer_ptx_not_float() {
    let i32_ptx = vec_kernel(ElemType::I32, 4, |elem, lanes| TensorWasmOp::VecAdd {
        elem,
        lanes,
    });
    // Integer compute + integer memory ops.
    assert!(i32_ptx.contains("add.s32"), "i32x4.add must emit add.s32");
    assert!(i32_ptx.contains("ld.global.lu.u32"));
    assert!(i32_ptx.contains("st.global.cs.u32"));
    // The float kernel must NOT appear anywhere — that was the miscompile.
    assert!(
        !i32_ptx.contains("add.f32"),
        "i32x4.add must not lower to a float kernel:\n{i32_ptx}"
    );
    assert!(!i32_ptx.contains("ld.global.lu.f32"));

    // And the f32 kernel for the same lane shape is byte-distinct (it uses
    // the float ops), proving the element type genuinely drives codegen.
    let f32_ptx = vec_kernel(ElemType::F32, 4, |elem, lanes| TensorWasmOp::VecAdd {
        elem,
        lanes,
    });
    assert!(f32_ptx.contains("add.f32"));
    assert_ne!(
        i32_ptx, f32_ptx,
        "i32x4 and f32x4 kernels must be distinct, not the same float kernel"
    );
}

/// The blueprint fingerprints for the integer and float kernels must
/// differ so they key to distinct cache slots (no cross-kernel aliasing).
#[test]
fn integer_and_float_blueprints_have_distinct_fingerprints() {
    let i32_bp = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
        elem: ElemType::I32,
        lanes: 4,
    });
    let f32_bp = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
        elem: ElemType::F32,
        lanes: 4,
    });
    assert_ne!(i32_bp.fingerprint(), f32_bp.fingerprint());
}

// ---------------------------------------------------------------------------
// 3. Adversarial-WASM no-panic.
// ---------------------------------------------------------------------------

/// A small, fast, deterministic xorshift PRNG so the test needs no
/// `rand`/proptest dependency and is fully reproducible across runs.
struct XorShift(u64);
impl XorShift {
    fn next_u64(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        x
    }
    fn fill(&mut self, buf: &mut [u8]) {
        for chunk in buf.chunks_mut(8) {
            let bytes = self.next_u64().to_le_bytes();
            for (b, r) in chunk.iter_mut().zip(bytes.iter()) {
                *b = *r;
            }
        }
    }
}

/// Feeding random bytes through `rewrite_wasm` must NEVER panic — it must
/// return `Ok` or a structured `RewriteError`. This exercises the whole
/// parse → detect → lower → emit pipeline against malformed input. The
/// previous element-type-collapsing codegen and the unchecked SSA-id /
/// scratch-size arithmetic were panic/miscompile hazards on adversarial
/// shapes; this fuzz-style smoke test pins the no-panic contract.
#[test]
fn rewrite_does_not_panic_on_random_bytes() {
    let cache = KernelCache::new();
    let opts = RewriteOptions::default();
    let mut rng = XorShift(0x9E37_79B9_7F4A_7C15);

    for _case in 0..512u32 {
        // Vary the length so we hit short truncations and longer streams.
        let len = (rng.next_u64() % 4096) as usize;
        let mut bytes = vec![0u8; len];
        rng.fill(&mut bytes);

        // The only contract: this returns without panicking (a panic would
        // unwind and fail the test). Most inputs fail to parse
        // (RewriteError::Parse); a few may parse as trivial modules. Either
        // outcome is acceptable — we just consume the result.
        let _ = rewrite_wasm(&bytes, &opts, &cache);
    }
}

/// A second adversarial pass that prefixes the valid WASM magic + version
/// header so the parser advances past the header into section parsing —
/// reaching deeper code paths (type/function/code sections) than pure
/// random bytes usually do.
#[test]
fn rewrite_does_not_panic_on_wasm_prefixed_random_bytes() {
    let cache = KernelCache::new();
    let opts = RewriteOptions::default();
    let mut rng = XorShift(0xD1B5_4A32_D192_ED03);

    // `\0asm` + version 1.
    const HEADER: [u8; 8] = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];

    for _case in 0..512u32 {
        let len = (rng.next_u64() % 2048) as usize;
        let mut bytes = Vec::with_capacity(8 + len);
        bytes.extend_from_slice(&HEADER);
        let start = bytes.len();
        bytes.resize(start + len, 0);
        rng.fill(&mut bytes[start..]);

        // A panic here would unwind and fail the test — that is the
        // no-panic contract under test.
        let _ = rewrite_wasm(&bytes, &opts, &cache);
    }
}
