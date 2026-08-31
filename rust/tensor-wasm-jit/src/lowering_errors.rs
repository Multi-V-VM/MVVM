// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Structured errors produced by the Cranelift → [`LoweredFunction`]
//! lowering pipeline (RFC 0001 wave 2).
//!
//! Each variant carries enough context for a maintainer to grep the call
//! site and understand the failure. The boundary into
//! [`crate::pliron_dialect::PlironLoweringError`] is provided so the
//! wave-2 lowering driver (W2.4) and the `CraneliftLowerer` impl (W2.5)
//! can bubble these errors through the trait surface.
//!
//! [`LoweredFunction`]: crate::lowered_ir::LoweredFunction

#![cfg(feature = "cuda-oxide-backend")]

use cranelift_codegen::ir as cl;
use thiserror::Error;

// TODO(W2.2): once `crate::lower_signature::SignatureLoweringError` lands,
// re-introduce the `Signature(#[from] SignatureLoweringError)` variant and
// update the `From<LoweringError> for PlironLoweringError` mapping so
// signature errors also reach the trait surface.

/// Structured errors produced by the Cranelift → `LoweredFunction`
/// lowering pipeline.
///
/// Each variant carries enough context for a maintainer to grep the call
/// site and understand the failure. The wave-2 lowering driver (W2.4)
/// and `CraneliftLowerer` impl (W2.5) return this error type, which is
/// then mapped into [`crate::pliron_dialect::PlironLoweringError`] at
/// the trait boundary.
#[derive(Debug, Clone, PartialEq, Eq, Error)]
pub enum LoweringError {
    /// The opcode has no `LoweredOp` equivalent and isn't on the
    /// reject-list either — this is a "we forgot to wire it up" path.
    /// Distinct from [`LoweringError::Rejected`] because it indicates a
    /// hole, not a deliberate refusal.
    #[error("lowering: unsupported opcode `{op}` (no lower_* family matched)")]
    UnsupportedOpcode {
        /// Cranelift opcode mnemonic.
        op: String,
        /// Block + instruction index inside the function, for diagnostics.
        location: InstLocation,
    },

    /// A Cranelift [`Value`](cl::Value) or [`Block`](cl::Block) reference
    /// resolved to an unsupported type.
    #[error("lowering: unsupported type `{ty}` at {location}")]
    UnsupportedType {
        /// Cranelift type as printed via `Display`.
        ty: String,
        /// Where the unsupported type appears.
        location: InstLocation,
    },

    /// An operand [`Value`](cl::Value) wasn't allocated in the
    /// `LoweringBuilder` before use. Indicates a use-before-def in the
    /// walker (caller bug, not user input).
    #[error("lowering: undefined value `{value:?}` at {location}")]
    UndefinedValue {
        /// The Cranelift `Value` that wasn't in the `value_map`.
        value: cl::Value,
        /// Where the missing operand appeared.
        location: InstLocation,
    },

    /// A branch instruction referred to a [`Block`](cl::Block) that the
    /// walker didn't visit (so the `block_map` has no entry).
    #[error("lowering: branch references unknown block `{block:?}` at {location}")]
    BadBlockReference {
        /// The unresolved block.
        block: cl::Block,
        /// Where the branch appears.
        location: InstLocation,
    },

    /// A `LoweredBlock` has no terminator (or has a terminator that isn't
    /// last).
    #[error("lowering: malformed terminator in block `{block_id}`")]
    MalformedTerminator {
        /// The `LoweredBlockId` of the offending block.
        block_id: crate::lowered_ir::LoweredBlockId,
    },

    /// The function was hit by the reject-list (W2.1). Carries the first
    /// rejection for diagnostics; the full list is available from
    /// [`crate::reject_list`].
    #[error("lowering: function rejected by reject-list: {reason}")]
    Rejected {
        /// One-line description of the rejection reason.
        reason: String,
        /// Where the rejected op appears.
        location: InstLocation,
    },
}

/// Location of a Cranelift instruction inside a function, used for
/// diagnostics. Cheap to construct and stable across the lowering pass.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstLocation {
    /// Cranelift block id (as displayed: `block0`, `block1`, etc).
    pub block: u32,
    /// Position within the block (0-indexed, layout order).
    pub inst_index: u32,
}

impl std::fmt::Display for InstLocation {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "block{}[{}]", self.block, self.inst_index)
    }
}

impl InstLocation {
    /// Construct from a Cranelift [`Block`](cl::Block) + a 0-indexed
    /// instruction position.
    ///
    /// Cranelift's `Block` exposes its numeric index via the
    /// `EntityRef::index()` trait method, which is what gets rendered as
    /// `block0`/`block1`/... in Cranelift's textual IR. We snapshot that
    /// index into a `u32` so `InstLocation` stays plain-old-data and
    /// doesn't drag Cranelift types into error messages.
    pub fn new(block: cl::Block, inst_index: u32) -> Self {
        use cranelift_codegen::entity::EntityRef;
        Self {
            block: block.index() as u32,
            inst_index,
        }
    }
}

impl From<LoweringError> for crate::pliron_dialect::PlironLoweringError {
    /// Map every `LoweringError` variant into
    /// [`PlironLoweringError::UnsupportedOp`] using the error's `Display`
    /// representation as the `op` description.
    ///
    /// This is intentionally lossy for wave 2: richer mapping (dedicated
    /// `PlironLoweringError` variants per `LoweringError` kind) comes in
    /// wave 3 when `PlironLoweringError` grows the necessary surface.
    /// The point of this `From` impl is to land the boundary now so W2.5
    /// can bubble driver errors into the trait surface without churn
    /// later.
    fn from(err: LoweringError) -> Self {
        Self::UnsupportedOp {
            op: err.to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::pliron_dialect::PlironLoweringError;
    use cranelift_codegen::entity::EntityRef;

    fn loc(block: u32, inst_index: u32) -> InstLocation {
        InstLocation { block, inst_index }
    }

    #[test]
    fn inst_location_display_matches_block_index_format() {
        assert_eq!(loc(5, 3).to_string(), "block5[3]");
        assert_eq!(loc(0, 0).to_string(), "block0[0]");
        assert_eq!(loc(42, 17).to_string(), "block42[17]");
    }

    #[test]
    fn inst_location_new_round_trips_block_index() {
        // Cranelift `Block` numbering is exposed through `EntityRef`; the
        // `new` constructor must snapshot that into the `block` field
        // verbatim.
        let block = cl::Block::new(7);
        let location = InstLocation::new(block, 4);
        assert_eq!(location.block, 7);
        assert_eq!(location.inst_index, 4);
        assert_eq!(location.to_string(), "block7[4]");
    }

    #[test]
    fn inst_location_eq() {
        // PartialEq + Eq let callers compare locations in tests cheaply.
        assert_eq!(loc(1, 2), loc(1, 2));
        assert_ne!(loc(1, 2), loc(1, 3));
        assert_ne!(loc(1, 2), loc(2, 2));
    }

    #[test]
    fn unsupported_opcode_displays_op_and_location() {
        let err = LoweringError::UnsupportedOpcode {
            op: "iadd_imm".to_string(),
            location: loc(2, 5),
        };
        let s = err.to_string();
        assert!(s.contains("iadd_imm"), "got: {s}");
        assert!(s.contains("no lower_* family matched"), "got: {s}");
    }

    #[test]
    fn unsupported_type_displays_ty_and_location() {
        let err = LoweringError::UnsupportedType {
            ty: "i128".to_string(),
            location: loc(0, 1),
        };
        let s = err.to_string();
        assert!(s.contains("i128"), "got: {s}");
        assert!(s.contains("block0[1]"), "got: {s}");
    }

    #[test]
    fn undefined_value_displays_value_and_location() {
        let value = cl::Value::new(13);
        let err = LoweringError::UndefinedValue {
            value,
            location: loc(3, 0),
        };
        let s = err.to_string();
        assert!(s.contains("block3[0]"), "got: {s}");
        // The Debug repr of `Value` typically renders as `v13`, but we
        // only care that *some* rendering of the value is in the string.
        assert!(s.contains("undefined value"), "got: {s}");
    }

    #[test]
    fn bad_block_reference_displays_block_and_location() {
        let block = cl::Block::new(9);
        let err = LoweringError::BadBlockReference {
            block,
            location: loc(1, 7),
        };
        let s = err.to_string();
        assert!(s.contains("block1[7]"), "got: {s}");
        assert!(s.contains("unknown block"), "got: {s}");
    }

    #[test]
    fn malformed_terminator_displays_block_id() {
        let err = LoweringError::MalformedTerminator { block_id: 4 };
        let s = err.to_string();
        assert!(s.contains("malformed terminator"), "got: {s}");
        assert!(s.contains("4"), "got: {s}");
    }

    #[test]
    fn rejected_displays_reason_and_location() {
        let err = LoweringError::Rejected {
            reason: "atomic ops not supported".to_string(),
            location: loc(0, 3),
        };
        let s = err.to_string();
        assert!(s.contains("atomic ops not supported"), "got: {s}");
        assert!(s.contains("rejected by reject-list"), "got: {s}");
    }

    #[test]
    fn lowering_error_eq() {
        // Two errors built from the same fields must compare equal.
        let a = LoweringError::UnsupportedOpcode {
            op: "iadd_imm".to_string(),
            location: loc(2, 5),
        };
        let b = LoweringError::UnsupportedOpcode {
            op: "iadd_imm".to_string(),
            location: loc(2, 5),
        };
        let c = LoweringError::UnsupportedOpcode {
            op: "iadd_imm".to_string(),
            location: loc(2, 6),
        };
        assert_eq!(a, b);
        assert_ne!(a, c);

        // Across variants, different kinds never compare equal even with
        // matching field values.
        let d = LoweringError::MalformedTerminator { block_id: 4 };
        let e = LoweringError::MalformedTerminator { block_id: 4 };
        let f = LoweringError::MalformedTerminator { block_id: 5 };
        assert_eq!(d, e);
        assert_ne!(d, f);
        assert_ne!(a, d);
    }

    #[test]
    fn from_lowering_error_into_pliron_unsupported_op_uses_display() {
        // The boundary into `PlironLoweringError` must produce
        // `UnsupportedOp` and use the source error's Display string as
        // the `op` description.
        let err = LoweringError::UnsupportedOpcode {
            op: "iadd_imm".to_string(),
            location: loc(2, 5),
        };
        let display = err.to_string();
        let pliron: PlironLoweringError = err.into();
        match pliron {
            PlironLoweringError::UnsupportedOp { op } => {
                assert_eq!(op, display);
                assert!(op.contains("iadd_imm"), "got: {op}");
            }
            other => panic!("expected UnsupportedOp, got {other:?}"),
        }
    }

    #[test]
    fn from_lowering_error_into_pliron_for_rejected_variant() {
        // Sanity check: a non-opcode variant also routes to UnsupportedOp
        // for now (richer mapping is wave 3).
        let err = LoweringError::Rejected {
            reason: "atomics".to_string(),
            location: loc(0, 0),
        };
        let display = err.to_string();
        let pliron: PlironLoweringError = err.into();
        match pliron {
            PlironLoweringError::UnsupportedOp { op } => {
                assert_eq!(op, display);
                assert!(op.contains("atomics"), "got: {op}");
            }
            other => panic!("expected UnsupportedOp, got {other:?}"),
        }
    }

    #[test]
    fn inst_location_new_uses_entity_ref_index() {
        // Defence-in-depth: the EntityRef trait is what Cranelift uses to
        // render `blockN`, and `InstLocation::new` must use the same
        // numbering so error messages line up with Cranelift's textual
        // IR.
        let block = cl::Block::new(123);
        assert_eq!(block.index(), 123);
        let location = InstLocation::new(block, 0);
        assert_eq!(location.block, 123);
    }
}
