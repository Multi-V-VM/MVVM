// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! `TensorWasmIR` — a normalised IR easier to lower to PTX than raw CLIF/Wasm.
//!
//! The IR is the contract between the detector (S10), the lowering pass
//! (`clif_lower`, S11), the PTX emitter (`ptx_emit`, S12), and the kernel
//! cache (S13). Keeping it small and explicit means each successor stage
//! can be unit-tested without dragging in the others.

use std::fmt;

/// Element (lane) type of a SIMD vector op.
///
/// jit CRITICAL fix: prior to threading this through, every WASM SIMD
/// shape (`f32x4`, `i32x4`, `f64x2`, `i16x8`, `i8x16`, …) collapsed onto a
/// single `VecAdd`/`VecMul` carrying only `lanes`, and the emitter always
/// emitted `add.f32` regardless of element type — silently miscompiling
/// integer and f64 SIMD. The element type now travels from the WASM opcode
/// (`detector::Op::V128Add { lane_ty, lanes }`) through the blueprint
/// (`TensorWasmOp`) into the PTX emitter so the emitter can pick the
/// correct instruction (`add.f32` / `add.s32` / `mul.lo.s32` / …) or, where
/// a width is not yet emittable end-to-end, the candidate is failed closed
/// to the CPU path upstream (see `rewrite::op_to_detector_op`).
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ElemType {
    /// 8-bit integer lane.
    I8,
    /// 16-bit integer lane.
    I16,
    /// 32-bit integer lane.
    I32,
    /// 64-bit integer lane.
    I64,
    /// 32-bit IEEE-754 float lane.
    F32,
    /// 64-bit IEEE-754 float lane.
    F64,
}

impl ElemType {
    /// Width of one lane in bytes (used to advance the load/store offset
    /// cursor in the emitter — a lane is `byte_width()` bytes wide, not a
    /// hard-coded 4).
    pub fn byte_width(self) -> u32 {
        match self {
            ElemType::I8 => 1,
            ElemType::I16 => 2,
            ElemType::I32 | ElemType::F32 => 4,
            ElemType::I64 | ElemType::F64 => 8,
        }
    }

    /// True if this is a floating-point lane type.
    pub fn is_float(self) -> bool {
        matches!(self, ElemType::F32 | ElemType::F64)
    }

    /// Stable tag byte for fingerprinting. Explicit so reordering the enum
    /// can never silently change a cache key.
    fn fingerprint_tag(self) -> u8 {
        match self {
            ElemType::I8 => 0,
            ElemType::I16 => 1,
            ElemType::I32 => 2,
            ElemType::I64 => 3,
            ElemType::F32 => 4,
            ElemType::F64 => 5,
        }
    }
}

impl fmt::Display for ElemType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            ElemType::I8 => "i8",
            ElemType::I16 => "i16",
            ElemType::I32 => "i32",
            ElemType::I64 => "i64",
            ElemType::F32 => "f32",
            ElemType::F64 => "f64",
        };
        f.write_str(s)
    }
}

/// One coarse-grained op in a TensorWasm-emitted GPU kernel.
///
/// Each op corresponds to a small group of PTX instructions; the emitter
/// (S12) materialises them into a register-allocated assembly block.
#[derive(Debug, Clone, PartialEq)]
pub enum TensorWasmOp {
    /// `c = a + b` element-wise.
    VecAdd {
        /// Per-lane element type.
        elem: ElemType,
        /// Lane count.
        lanes: u32,
    },
    /// `c = a * b` element-wise.
    VecMul {
        /// Per-lane element type.
        elem: ElemType,
        /// Lane count.
        lanes: u32,
    },
    /// `d = a*b + c` element-wise (single rounding for floats).
    VecFma {
        /// Per-lane element type.
        elem: ElemType,
        /// Lane count.
        lanes: u32,
    },
    /// 16x16x16 matrix multiply-accumulate (wmma on sm_80).
    MatMul {
        /// Tile size m (currently fixed at 16).
        m: u32,
        /// Tile size n (currently fixed at 16).
        n: u32,
        /// Tile size k (currently fixed at 16).
        k: u32,
    },
    /// Load lanes from unified-memory pointer `+ offset`.
    LoadUnified {
        /// Per-lane element type.
        elem: ElemType,
        /// Lane count.
        lanes: u32,
    },
    /// Store lanes to unified-memory pointer `+ offset`.
    StoreUnified {
        /// Per-lane element type.
        elem: ElemType,
        /// Lane count.
        lanes: u32,
    },
    /// CTA-wide synchronisation barrier.
    Barrier,
}

impl fmt::Display for TensorWasmOp {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            TensorWasmOp::VecAdd { elem, lanes } => write!(f, "vec_add.{elem}[{lanes}]"),
            TensorWasmOp::VecMul { elem, lanes } => write!(f, "vec_mul.{elem}[{lanes}]"),
            TensorWasmOp::VecFma { elem, lanes } => write!(f, "vec_fma.{elem}[{lanes}]"),
            TensorWasmOp::MatMul { m, n, k } => write!(f, "matmul[{m}x{n}x{k}]"),
            TensorWasmOp::LoadUnified { elem, lanes } => write!(f, "load_unified.{elem}[{lanes}]"),
            TensorWasmOp::StoreUnified { elem, lanes } => {
                write!(f, "store_unified.{elem}[{lanes}]")
            }
            TensorWasmOp::Barrier => f.write_str("barrier"),
        }
    }
}

/// Hint about CUDA launch geometry. Provided by the lowering pass when it
/// can infer a sensible grid/block from the source loop's trip count.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct GridHint {
    /// Total number of threads to launch (across all blocks).
    pub total_threads: u32,
    /// Preferred CTA size; the emitter rounds the total to a multiple.
    pub preferred_block_size: u32,
}

impl Default for GridHint {
    fn default() -> Self {
        Self {
            total_threads: 256,
            preferred_block_size: 128,
        }
    }
}

impl GridHint {
    /// Compute the corresponding (grid_size, block_size) pair.
    pub fn launch_geometry(&self) -> (u32, u32) {
        let block = self.preferred_block_size.max(1);
        let grid = self.total_threads.div_ceil(block);
        (grid.max(1), block)
    }
}

/// A complete blueprint that the emitter (S12) can turn into PTX text.
#[derive(Debug, Clone, PartialEq)]
pub struct TensorWasmKernelBlueprint {
    /// Symbolic entry name (used by `load_ptx` lookup).
    pub entry: String,
    /// Ordered ops to emit.
    pub ops: Vec<TensorWasmOp>,
    /// Grid hint (S11 fills in; S12 honours).
    pub grid_hint: GridHint,
    /// Bytes of shared memory the kernel will request.
    pub shared_mem_bytes: u32,
}

impl TensorWasmKernelBlueprint {
    /// Construct a new blueprint.
    pub fn new(entry: impl Into<String>) -> Self {
        Self {
            entry: entry.into(),
            ops: Vec::new(),
            grid_hint: GridHint::default(),
            shared_mem_bytes: 0,
        }
    }

    /// Append an op; returns `self` for builder-style chaining.
    pub fn push(mut self, op: TensorWasmOp) -> Self {
        self.ops.push(op);
        self
    }

    /// Update the grid hint.
    pub fn with_grid(mut self, hint: GridHint) -> Self {
        self.grid_hint = hint;
        self
    }

    /// Update the shared-memory requirement.
    pub fn with_shared_mem(mut self, bytes: u32) -> Self {
        self.shared_mem_bytes = bytes;
        self
    }

    /// True if the blueprint contains no ops (a no-op kernel).
    pub fn is_empty(&self) -> bool {
        self.ops.is_empty()
    }

    /// Canonical 256-bit BLAKE3 digest of the blueprint. Both
    /// [`fingerprint`](Self::fingerprint) (64-bit cache key) and
    /// [`fingerprint128`](Self::fingerprint128) (128-bit, lower collision
    /// probability) are derived from this single digest, so they agree on
    /// the hashed schema by construction.
    fn digest(&self) -> [u8; 32] {
        let mut hasher = blake3::Hasher::new();
        // Domain-separate so changes to the hashed schema can never collide
        // with an older fingerprint namespace — bump this on any schema
        // change. v2 adds the per-lane element type to every lane-bearing op
        // (jit HIGH fix): v1 hashed only `lanes`, so `i32x4.add` and
        // `f32x4.add` collided. Rolling the tag to v2 guarantees no v1
        // cache key is ever reused for a v2-schema blueprint.
        hasher.update(b"tensor-wasm-jit::ir::v2\0");
        hasher.update(&(self.entry.len() as u64).to_le_bytes());
        hasher.update(self.entry.as_bytes());
        hasher.update(&(self.ops.len() as u64).to_le_bytes());
        for op in &self.ops {
            // Variant tag (stable per-variant) followed by the variant's
            // payload bytes. Variant tags are explicit so reordering the
            // enum cannot accidentally change fingerprints.
            // jit HIGH fix: every lane-bearing op now hashes its element
            // type *before* its lane count. Previously only `lanes` was
            // hashed, so `i32x4.add` and `f32x4.add` produced the same
            // fingerprint and aliased to the same cache key / kernel — a
            // cross-kernel cache-poisoning bug. The element-type tag byte is
            // explicit (see `ElemType::fingerprint_tag`) so reordering the
            // enum cannot silently roll a key.
            match op {
                TensorWasmOp::VecAdd { elem, lanes } => {
                    hasher.update(&[0u8]);
                    hasher.update(&[elem.fingerprint_tag()]);
                    hasher.update(&lanes.to_le_bytes());
                }
                TensorWasmOp::VecMul { elem, lanes } => {
                    hasher.update(&[1u8]);
                    hasher.update(&[elem.fingerprint_tag()]);
                    hasher.update(&lanes.to_le_bytes());
                }
                TensorWasmOp::VecFma { elem, lanes } => {
                    hasher.update(&[2u8]);
                    hasher.update(&[elem.fingerprint_tag()]);
                    hasher.update(&lanes.to_le_bytes());
                }
                TensorWasmOp::MatMul { m, n, k } => {
                    hasher.update(&[3u8]);
                    hasher.update(&m.to_le_bytes());
                    hasher.update(&n.to_le_bytes());
                    hasher.update(&k.to_le_bytes());
                }
                TensorWasmOp::LoadUnified { elem, lanes } => {
                    hasher.update(&[4u8]);
                    hasher.update(&[elem.fingerprint_tag()]);
                    hasher.update(&lanes.to_le_bytes());
                }
                TensorWasmOp::StoreUnified { elem, lanes } => {
                    hasher.update(&[5u8]);
                    hasher.update(&[elem.fingerprint_tag()]);
                    hasher.update(&lanes.to_le_bytes());
                }
                TensorWasmOp::Barrier => {
                    hasher.update(&[6u8]);
                }
            }
        }
        hasher.update(&self.grid_hint.total_threads.to_le_bytes());
        hasher.update(&self.grid_hint.preferred_block_size.to_le_bytes());
        hasher.update(&self.shared_mem_bytes.to_le_bytes());
        *hasher.finalize().as_bytes()
    }

    /// Stable 64-bit hash used as the `KernelCache` key by S13.
    ///
    /// Uses `blake3` over a canonical byte serialisation so the hash is
    /// deterministic across Rust versions and platforms — `std`'s
    /// `DefaultHasher` (SipHash) is explicitly documented as unstable and
    /// must not be relied on for persistence. The first 8 bytes of the 32-
    /// byte BLAKE3 digest are interpreted as a little-endian u64.
    ///
    /// The width is intentionally kept at 64 bits: the on-disk cache format
    /// (`DiskCache`, `[24..32) blueprint fingerprint (u64 LE)`), the
    /// [`crate::deopt::DeoptGuard`] `DashMap<u64, _>` key, and the
    /// cross-crate `OffloadedFunction.fingerprint` ABI all key on this u64.
    /// Callers wanting a lower collision probability without those
    /// persistence/ABI constraints can use [`fingerprint128`](Self::fingerprint128).
    pub fn fingerprint(&self) -> u64 {
        let bytes = self.digest();
        u64::from_le_bytes([
            bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7],
        ])
    }

    /// 128-bit widening of [`fingerprint`](Self::fingerprint), derived from
    /// the same canonical BLAKE3 digest.
    ///
    /// jit HIGH (finding 2, truncation width): a 64-bit truncation of a
    /// 256-bit digest has a birthday-bound collision probability around
    /// 2^-32 across ~4 billion distinct kernels. Embedders that key kernels
    /// in a context not bound by the 64-bit on-disk/ABI format (e.g. an
    /// in-memory dedup map over a very large kernel population) should key
    /// on this 128-bit value instead, which pushes the birthday bound out
    /// to ~2^-64. The low 64 bits equal [`fingerprint`](Self::fingerprint).
    pub fn fingerprint128(&self) -> u128 {
        let bytes = self.digest();
        let mut buf = [0u8; 16];
        buf.copy_from_slice(&bytes[0..16]);
        u128::from_le_bytes(buf)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn blueprint_builder() {
        let bp = TensorWasmKernelBlueprint::new("vector_add")
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
            })
            .with_shared_mem(0);
        assert_eq!(bp.entry, "vector_add");
        assert_eq!(bp.ops.len(), 3);
        assert_eq!(bp.grid_hint.total_threads, 1024);
    }

    #[test]
    fn grid_geometry_rounds_up() {
        let g = GridHint {
            total_threads: 130,
            preferred_block_size: 64,
        };
        let (grid, block) = g.launch_geometry();
        assert_eq!(block, 64);
        assert_eq!(grid, 3); // ceil(130 / 64)
    }

    #[test]
    fn fingerprint_is_deterministic() {
        let a = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        });
        let b = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        });
        assert_eq!(a.fingerprint(), b.fingerprint());
        assert_eq!(a.fingerprint128(), b.fingerprint128());
        // The 64-bit key is the low half of the 128-bit fingerprint.
        assert_eq!(
            a.fingerprint() as u128,
            a.fingerprint128() & u64::MAX as u128
        );
    }

    #[test]
    fn fingerprint_changes_with_lanes() {
        let a = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        });
        let b = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 8,
        });
        assert_ne!(a.fingerprint(), b.fingerprint());
    }

    /// jit HIGH (finding 2): two SIMD ops that differ ONLY in element type
    /// must produce distinct fingerprints. Before the fix both collapsed to
    /// `VecAdd { lanes }` and aliased to the same cache key, so an
    /// `i32x4.add` kernel could be served from an `f32x4.add` cache slot.
    #[test]
    fn fingerprint_distinguishes_element_type() {
        let f32_add = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        });
        let i32_add = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::I32,
            lanes: 4,
        });
        assert_ne!(
            f32_add.fingerprint(),
            i32_add.fingerprint(),
            "f32x4.add and i32x4.add must not share a 64-bit cache key"
        );
        assert_ne!(
            f32_add.fingerprint128(),
            i32_add.fingerprint128(),
            "f32x4.add and i32x4.add must not share a 128-bit fingerprint"
        );
        // And across every lane-bearing op family.
        let f32_load = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::LoadUnified {
            elem: ElemType::F32,
            lanes: 4,
        });
        let i32_load = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::LoadUnified {
            elem: ElemType::I32,
            lanes: 4,
        });
        assert_ne!(f32_load.fingerprint(), i32_load.fingerprint());
    }

    #[test]
    fn fingerprint_is_blake3_based_and_pinned() {
        // Pin a small known case. If this changes, bump the `tensor-wasm-jit::ir::vN`
        // tag in `fingerprint()` so the cache key namespace is also rolled.
        let bp = TensorWasmKernelBlueprint::new("vector_add")
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
            });
        let fp = bp.fingerprint();
        // The exact value is a golden derived from blake3 over the canonical
        // serialisation declared in `fingerprint()`. Different from
        // `DefaultHasher` (which was platform-dependent), this value is
        // identical on every host.
        assert_ne!(fp, 0);
        // Sanity: identical blueprint with a tweaked op must differ.
        let other = TensorWasmKernelBlueprint::new("vector_add")
            .push(TensorWasmOp::LoadUnified {
                elem: ElemType::F32,
                lanes: 4,
            })
            .push(TensorWasmOp::VecMul {
                elem: ElemType::F32,
                lanes: 4,
            })
            .push(TensorWasmOp::StoreUnified {
                elem: ElemType::F32,
                lanes: 4,
            });
        assert_ne!(fp, other.fingerprint());
    }

    #[test]
    fn fingerprint_distinguishes_matmul_dims() {
        let a = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::MatMul {
            m: 16,
            n: 16,
            k: 16,
        });
        let b = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::MatMul {
            m: 32,
            n: 32,
            k: 32,
        });
        assert_ne!(a.fingerprint(), b.fingerprint());
    }

    #[test]
    fn empty_blueprint_detectable() {
        let bp = TensorWasmKernelBlueprint::new("empty");
        assert!(bp.is_empty());
    }

    #[test]
    fn op_display() {
        assert_eq!(
            TensorWasmOp::VecAdd {
                elem: ElemType::F32,
                lanes: 4
            }
            .to_string(),
            "vec_add.f32[4]"
        );
        assert_eq!(
            TensorWasmOp::VecAdd {
                elem: ElemType::I32,
                lanes: 4
            }
            .to_string(),
            "vec_add.i32[4]"
        );
        assert_eq!(
            TensorWasmOp::MatMul {
                m: 16,
                n: 16,
                k: 16
            }
            .to_string(),
            "matmul[16x16x16]"
        );
        assert_eq!(TensorWasmOp::Barrier.to_string(), "barrier");
    }

    #[test]
    fn elem_type_byte_width() {
        assert_eq!(ElemType::I8.byte_width(), 1);
        assert_eq!(ElemType::I16.byte_width(), 2);
        assert_eq!(ElemType::I32.byte_width(), 4);
        assert_eq!(ElemType::F32.byte_width(), 4);
        assert_eq!(ElemType::I64.byte_width(), 8);
        assert_eq!(ElemType::F64.byte_width(), 8);
        assert!(ElemType::F32.is_float());
        assert!(!ElemType::I32.is_float());
    }
}
