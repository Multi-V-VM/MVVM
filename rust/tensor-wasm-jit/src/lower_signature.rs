// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Cranelift [`Signature`] → [`crate::lowered_ir::LoweredSignature`] conversion
//! (wave 2 of the Pliron pipeline, task W2.2).
//!
//! This module bridges the Cranelift ABI surface — `Signature`, `AbiParam`,
//! `ArgumentPurpose`, `CallConv` — to the pure-Rust `LoweredSignature` used
//! by every wave-1 lowering family (`lower_arith`, `lower_float`, etc.). The
//! conversion is intentionally narrow:
//!
//! - Only `ArgumentPurpose::Normal` params/returns are admitted. Struct-return
//!   pointers, VM-context pointers, and struct-by-value args are host-ABI
//!   shapes the wave-1 `LoweredOp` IR has no representation for and they
//!   would not survive PTX codegen.
//! - Only `SystemV` / `Fast` / `Cold` call-convs are admitted. The other
//!   variants (Windows fastcall, Wasm-flavoured, probestack, etc.) carry
//!   host-only fixups (callee-saved sets, shadow space, frame-chain
//!   probing) that the GPU side cannot honour.
//! - Type mapping mirrors [`crate::lowered_ir::LoweredType`] exactly. Types
//!   outside that set (notably `I128`, `F16`, `F128`) surface as a
//!   structured [`SignatureLoweringError::UnsupportedType`].
//!
//! [`Signature`]: cranelift_codegen::ir::Signature

#![cfg(feature = "cuda-oxide-backend")]

use cranelift_codegen::ir::{self, AbiParam, ArgumentPurpose, Signature};
use cranelift_codegen::isa::CallConv;

use crate::lowered_ir::{LoweredSignature, LoweredType};

/// Errors when lowering a Cranelift signature.
#[derive(Debug, Clone, PartialEq, Eq, thiserror::Error)]
pub enum SignatureLoweringError {
    /// A parameter or return type has no `LoweredType` equivalent.
    #[error("unsupported type at {position}: {ty}")]
    UnsupportedType {
        /// `"param N"` or `"return N"`, with `N` the 0-based index.
        position: String,
        /// The offending Cranelift type, formatted via its `Display` impl.
        ty: String,
    },
    /// A parameter or return slot has an `ArgumentPurpose` other than
    /// `Normal` (e.g. struct-return pointer, VM context). These ABI
    /// fixups have no equivalent in the wave-1 `LoweredOp` IR.
    #[error("unsupported argument purpose at {position}: {purpose}")]
    UnsupportedArgumentPurpose {
        /// `"param N"` or `"return N"`.
        position: String,
        /// Cranelift purpose, formatted via its `Display` impl.
        purpose: String,
    },
    /// The calling convention requires a host-only fixup that PTX cannot honor.
    #[error("unsupported call conv: {conv}")]
    UnsupportedCallConv {
        /// The Cranelift call conv display.
        conv: String,
    },
}

/// Convert a Cranelift signature to a [`LoweredSignature`].
///
/// Maps each [`AbiParam::value_type`] through [`cranelift_type_to_lowered`].
/// Rejects sigs with [`AbiParam::purpose`] other than
/// [`ArgumentPurpose::Normal`] — struct-return slots, VM-context pointers,
/// and struct-by-value args aren't representable in the wave-1 `LoweredOp`
/// IR. Rejects call-convs other than [`CallConv::SystemV`], [`CallConv::Fast`],
/// or [`CallConv::Cold`]; kernel candidates aren't expected to use the
/// host-only variants (Windows fastcall, AppleAarch64, Wasm flavours, …).
///
/// # Errors
///
/// - [`SignatureLoweringError::UnsupportedCallConv`] for call-convs outside
///   the `SystemV` / `Fast` / `Cold` allowlist.
/// - [`SignatureLoweringError::UnsupportedArgumentPurpose`] for any param
///   or return whose `purpose` is not `Normal`.
/// - [`SignatureLoweringError::UnsupportedType`] for any param or return
///   whose `value_type` is outside the [`LoweredType`] set (notably
///   `I128`, `F16`, `F128`, or any dynamic vector type).
pub fn lower_signature(sig: &Signature) -> Result<LoweredSignature, SignatureLoweringError> {
    // Call-conv check first so we fail closed before we walk operands.
    if !is_supported_call_conv(sig.call_conv) {
        return Err(SignatureLoweringError::UnsupportedCallConv {
            conv: sig.call_conv.to_string(),
        });
    }

    let mut params = Vec::with_capacity(sig.params.len());
    for (i, p) in sig.params.iter().enumerate() {
        params.push(lower_abi_param(p, "param", i)?);
    }

    let mut returns = Vec::with_capacity(sig.returns.len());
    for (i, r) in sig.returns.iter().enumerate() {
        returns.push(lower_abi_param(r, "return", i)?);
    }

    Ok(LoweredSignature { params, returns })
}

/// Lower a single [`AbiParam`] (param or return slot), tagging any error
/// with the caller-supplied positional label (`"param"` / `"return"`) plus
/// the 0-based slot index.
fn lower_abi_param(
    p: &AbiParam,
    kind: &str,
    idx: usize,
) -> Result<LoweredType, SignatureLoweringError> {
    if p.purpose != ArgumentPurpose::Normal {
        return Err(SignatureLoweringError::UnsupportedArgumentPurpose {
            position: format!("{kind} {idx}"),
            purpose: p.purpose.to_string(),
        });
    }
    cranelift_type_to_lowered(p.value_type).ok_or_else(|| SignatureLoweringError::UnsupportedType {
        position: format!("{kind} {idx}"),
        ty: p.value_type.to_string(),
    })
}

/// True for call-convs whose calling-side contract is representable on the
/// GPU. `SystemV` / `Fast` / `Cold` only — the host-only variants
/// (`WindowsFastcall`, `AppleAarch64`, `WasmtimeSystemV`, `Winch`, `Tail`,
/// `Probestack`) all require register-save or probestack fixups PTX has
/// no equivalent for.
fn is_supported_call_conv(conv: CallConv) -> bool {
    matches!(conv, CallConv::SystemV | CallConv::Fast | CallConv::Cold)
}

/// Convert one Cranelift [`Type`] to a [`LoweredType`].
///
/// Returns `None` for types the wave-1 `LoweredOp` IR cannot represent:
///
/// - `I128`, `F16`, `F128` — out of [`LoweredType`]'s scalar range.
/// - Dynamic vectors (`is_dynamic_vector()` true) — vec scaling factor
///   isn't modelled.
/// - Vectors whose lane type is itself unsupported (e.g. `I128x2` if it
///   ever appears).
/// - The `INVALID` sentinel.
/// - Any other future addition to Cranelift's `Type` enum.
///
/// Reference (pointer) types `R32` / `R64` map to [`LoweredType::Ptr`] —
/// the wave-1 IR is pointer-width-agnostic, so both widths collapse to
/// the same opaque variant.
///
/// [`Type`]: cranelift_codegen::ir::Type
pub fn cranelift_type_to_lowered(ty: ir::Type) -> Option<LoweredType> {
    use ir::types as t;

    // Scalar integers first (the common case).
    if ty == t::I8 {
        return Some(LoweredType::I8);
    }
    if ty == t::I16 {
        return Some(LoweredType::I16);
    }
    if ty == t::I32 {
        return Some(LoweredType::I32);
    }
    if ty == t::I64 {
        return Some(LoweredType::I64);
    }
    // I128 is intentionally rejected: PTX has no native 128-bit integer
    // and the wave-1 LoweredType has no I128 variant.

    // Scalar floats.
    if ty == t::F32 {
        return Some(LoweredType::F32);
    }
    if ty == t::F64 {
        return Some(LoweredType::F64);
    }
    // F16 / F128 fall through to the unsupported tail.

    // Reference types — both widths collapse to the opaque `Ptr` variant.
    // The wave-1 IR doesn't distinguish 32-bit vs 64-bit device pointers
    // at the type level; the downstream PTX backend selects the correct
    // address-space register width from target metadata.
    if ty == t::R32 || ty == t::R64 {
        return Some(LoweredType::Ptr);
    }

    // SIMD vectors. Lane type must itself be one of the supported scalars
    // and the lane count must fit in `u8` (Cranelift vectors are at most
    // 256 lanes today). Dynamic vectors are rejected because their lane
    // count isn't statically known and the wave-1 IR doesn't carry the
    // scaling-factor GlobalValue.
    if ty.is_vector() && !ty.is_dynamic_vector() {
        let lane_ty = cranelift_type_to_lowered(ty.lane_type())?;
        // Only scalar lanes are legal — `LoweredType` forbids nested
        // vectors. `is_int`/`is_float` cover I8..I64 / F32..F64.
        if !(lane_ty.is_int() || lane_ty.is_float()) {
            return None;
        }
        let lanes_u32 = ty.lane_count();
        let lanes: u8 = u8::try_from(lanes_u32).ok()?;
        return Some(LoweredType::V128 {
            lane_type: Box::new(lane_ty),
            lanes,
        });
    }

    None
}

#[cfg(test)]
mod tests {
    use super::*;
    use cranelift_codegen::ir::{types, AbiParam, ArgumentPurpose, Signature};
    use cranelift_codegen::isa::CallConv;

    // ---- cranelift_type_to_lowered: one test per branch -----------------

    #[test]
    fn maps_i8() {
        assert_eq!(cranelift_type_to_lowered(types::I8), Some(LoweredType::I8));
    }

    #[test]
    fn maps_i16() {
        assert_eq!(
            cranelift_type_to_lowered(types::I16),
            Some(LoweredType::I16)
        );
    }

    #[test]
    fn maps_i32() {
        assert_eq!(
            cranelift_type_to_lowered(types::I32),
            Some(LoweredType::I32)
        );
    }

    #[test]
    fn maps_i64() {
        assert_eq!(
            cranelift_type_to_lowered(types::I64),
            Some(LoweredType::I64)
        );
    }

    #[test]
    fn rejects_i128() {
        // I128 is in Cranelift's type system but outside LoweredType's
        // scalar range. The wrapper translates this `None` into an
        // UnsupportedType error.
        assert_eq!(cranelift_type_to_lowered(types::I128), None);
    }

    #[test]
    fn maps_f32() {
        assert_eq!(
            cranelift_type_to_lowered(types::F32),
            Some(LoweredType::F32)
        );
    }

    #[test]
    fn maps_f64() {
        assert_eq!(
            cranelift_type_to_lowered(types::F64),
            Some(LoweredType::F64)
        );
    }

    #[test]
    fn rejects_f16() {
        assert_eq!(cranelift_type_to_lowered(types::F16), None);
    }

    #[test]
    fn rejects_f128() {
        assert_eq!(cranelift_type_to_lowered(types::F128), None);
    }

    #[test]
    fn maps_r32_to_ptr() {
        assert_eq!(
            cranelift_type_to_lowered(types::R32),
            Some(LoweredType::Ptr)
        );
    }

    #[test]
    fn maps_r64_to_ptr() {
        assert_eq!(
            cranelift_type_to_lowered(types::R64),
            Some(LoweredType::Ptr)
        );
    }

    #[test]
    fn maps_i8x16() {
        assert_eq!(
            cranelift_type_to_lowered(types::I8X16),
            Some(LoweredType::V128 {
                lane_type: Box::new(LoweredType::I8),
                lanes: 16,
            })
        );
    }

    #[test]
    fn maps_i16x8() {
        assert_eq!(
            cranelift_type_to_lowered(types::I16X8),
            Some(LoweredType::V128 {
                lane_type: Box::new(LoweredType::I16),
                lanes: 8,
            })
        );
    }

    #[test]
    fn maps_i32x4() {
        assert_eq!(
            cranelift_type_to_lowered(types::I32X4),
            Some(LoweredType::V128 {
                lane_type: Box::new(LoweredType::I32),
                lanes: 4,
            })
        );
    }

    #[test]
    fn maps_i64x2() {
        assert_eq!(
            cranelift_type_to_lowered(types::I64X2),
            Some(LoweredType::V128 {
                lane_type: Box::new(LoweredType::I64),
                lanes: 2,
            })
        );
    }

    #[test]
    fn maps_f32x4() {
        assert_eq!(
            cranelift_type_to_lowered(types::F32X4),
            Some(LoweredType::V128 {
                lane_type: Box::new(LoweredType::F32),
                lanes: 4,
            })
        );
    }

    #[test]
    fn maps_f64x2() {
        assert_eq!(
            cranelift_type_to_lowered(types::F64X2),
            Some(LoweredType::V128 {
                lane_type: Box::new(LoweredType::F64),
                lanes: 2,
            })
        );
    }

    #[test]
    fn rejects_invalid_type() {
        assert_eq!(cranelift_type_to_lowered(types::INVALID), None);
    }

    // ---- lower_signature: empty + happy path + error paths --------------

    #[test]
    fn lowers_empty_signature() {
        let sig = Signature::new(CallConv::SystemV);
        let lowered = lower_signature(&sig).expect("empty sig lowers");
        assert!(lowered.params.is_empty());
        assert!(lowered.returns.is_empty());
        assert_eq!(lowered, LoweredSignature::default());
    }

    #[test]
    fn lowers_i32_f32_to_i64() {
        // fn(I32, F32) -> I64
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I32));
        sig.params.push(AbiParam::new(types::F32));
        sig.returns.push(AbiParam::new(types::I64));

        let lowered = lower_signature(&sig).expect("happy-path sig lowers");
        assert_eq!(
            lowered,
            LoweredSignature {
                params: vec![LoweredType::I32, LoweredType::F32],
                returns: vec![LoweredType::I64],
            }
        );
    }

    #[test]
    fn rejects_i128_param_at_position_zero() {
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I128));
        sig.returns.push(AbiParam::new(types::I32));

        let err = lower_signature(&sig).expect_err("I128 must be rejected");
        match err {
            SignatureLoweringError::UnsupportedType { position, ty } => {
                assert_eq!(position, "param 0");
                // Cranelift's Display for I128 is "i128".
                assert_eq!(ty, types::I128.to_string());
            }
            other => panic!("expected UnsupportedType, got {other:?}"),
        }
    }

    #[test]
    fn rejects_i128_return_at_position_one() {
        // Use a non-zero return index to confirm the positional label
        // tracks the slot index, not just "0".
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I32));
        sig.returns.push(AbiParam::new(types::I32));
        sig.returns.push(AbiParam::new(types::I128));

        let err = lower_signature(&sig).expect_err("I128 return must be rejected");
        match err {
            SignatureLoweringError::UnsupportedType { position, ty } => {
                assert_eq!(position, "return 1");
                assert_eq!(ty, types::I128.to_string());
            }
            other => panic!("expected UnsupportedType, got {other:?}"),
        }
    }

    #[test]
    fn lowers_v128_return_with_lane_info() {
        // fn() -> F32X4 — confirms vector returns survive with lane info.
        let mut sig = Signature::new(CallConv::SystemV);
        sig.returns.push(AbiParam::new(types::F32X4));

        let lowered = lower_signature(&sig).expect("vector return lowers");
        assert_eq!(
            lowered,
            LoweredSignature {
                params: vec![],
                returns: vec![LoweredType::V128 {
                    lane_type: Box::new(LoweredType::F32),
                    lanes: 4,
                }],
            }
        );
    }

    #[test]
    fn lowers_v128_param() {
        // fn(I32X4) -> () — vector params too.
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I32X4));

        let lowered = lower_signature(&sig).expect("vector param lowers");
        assert_eq!(
            lowered.params,
            vec![LoweredType::V128 {
                lane_type: Box::new(LoweredType::I32),
                lanes: 4,
            }]
        );
        assert!(lowered.returns.is_empty());
    }

    #[test]
    fn rejects_struct_return_purpose() {
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params
            .push(AbiParam::special(types::I64, ArgumentPurpose::StructReturn));

        let err = lower_signature(&sig).expect_err("StructReturn must be rejected");
        match err {
            SignatureLoweringError::UnsupportedArgumentPurpose { position, purpose } => {
                assert_eq!(position, "param 0");
                assert_eq!(purpose, ArgumentPurpose::StructReturn.to_string());
            }
            other => panic!("expected UnsupportedArgumentPurpose, got {other:?}"),
        }
    }

    #[test]
    fn rejects_vmcontext_purpose() {
        let mut sig = Signature::new(CallConv::SystemV);
        sig.params.push(AbiParam::new(types::I32));
        sig.params
            .push(AbiParam::special(types::I64, ArgumentPurpose::VMContext));

        let err = lower_signature(&sig).expect_err("VMContext must be rejected");
        match err {
            SignatureLoweringError::UnsupportedArgumentPurpose { position, .. } => {
                assert_eq!(position, "param 1");
            }
            other => panic!("expected UnsupportedArgumentPurpose, got {other:?}"),
        }
    }

    #[test]
    fn accepts_systemv_fast_cold_callconv() {
        for cc in [CallConv::SystemV, CallConv::Fast, CallConv::Cold] {
            let sig = Signature::new(cc);
            assert!(
                lower_signature(&sig).is_ok(),
                "callconv {cc:?} should be accepted",
            );
        }
    }

    #[test]
    fn rejects_windows_fastcall_callconv() {
        let sig = Signature::new(CallConv::WindowsFastcall);
        let err = lower_signature(&sig).expect_err("WindowsFastcall must be rejected");
        match err {
            SignatureLoweringError::UnsupportedCallConv { conv } => {
                assert_eq!(conv, CallConv::WindowsFastcall.to_string());
            }
            other => panic!("expected UnsupportedCallConv, got {other:?}"),
        }
    }

    #[test]
    fn rejects_wasmtime_systemv_callconv() {
        let sig = Signature::new(CallConv::WasmtimeSystemV);
        assert!(matches!(
            lower_signature(&sig),
            Err(SignatureLoweringError::UnsupportedCallConv { .. })
        ));
    }

    #[test]
    fn callconv_check_runs_before_param_check() {
        // Even with a valid I128 (which would otherwise yield
        // UnsupportedType), the call-conv error wins.
        let mut sig = Signature::new(CallConv::WindowsFastcall);
        sig.params.push(AbiParam::new(types::I128));
        assert!(matches!(
            lower_signature(&sig),
            Err(SignatureLoweringError::UnsupportedCallConv { .. })
        ));
    }

    #[test]
    fn error_display_includes_position_and_type() {
        let err = SignatureLoweringError::UnsupportedType {
            position: "param 0".to_string(),
            ty: "i128".to_string(),
        };
        let s = err.to_string();
        assert!(s.contains("param 0"));
        assert!(s.contains("i128"));
    }
}
