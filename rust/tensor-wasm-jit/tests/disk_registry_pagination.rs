// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Pagination test for [`DiskRegistry::list_paginated`] (T35, v0.4).
//!
//! Publishes 250 distinct manifests and exercises three contiguous
//! pages of 100 to confirm the listing contract:
//!
//! - `offset=0, limit=100` → 100 entries.
//! - `offset=100, limit=100` → 100 entries.
//! - `offset=200, limit=100` → 50 entries (the residual).
//!
//! Cross-checks: the union of the three pages must contain every
//! published key exactly once. DashMap's iteration order is
//! hash-bucket order (unspecified, stable per process), so we deduce
//! correctness from the set union rather than positional order.
//!
//! Lives behind `kernel-registry` so the default `cargo test -p
//! tensor-wasm-jit` does not pull in the artifact-store / HMAC
//! dependency chain.

#![cfg(feature = "kernel-registry")]

use std::collections::HashSet;

use tensor_wasm_jit::registry::{sign_manifest, DiskRegistry, KernelManifest};

/// Helper: build a v2-signed manifest matching `ptx_text` under `key`.
fn signed_manifest(
    name: &str,
    version: &str,
    sm_version: u32,
    ptx_text: &str,
    key: &[u8; 32],
) -> KernelManifest {
    let digest = *blake3::hash(ptx_text.as_bytes()).as_bytes();
    let mut m = KernelManifest::new(
        name.to_string(),
        version.to_string(),
        sm_version,
        digest,
        [0u8; 32],
        1_700_000_000_000,
        "test-publisher".to_string(),
    );
    m.signature = sign_manifest(&m, key);
    m
}

#[test]
fn pagination_returns_three_pages_summing_to_250() {
    let tmp = tempfile::TempDir::new().expect("tempdir");
    let key = [0xd1u8; 32];
    let reg = DiskRegistry::open(tmp.path().to_path_buf(), key).expect("open");

    // Publish 250 manifests under unique (name, version) pairs so each
    // takes its own keymap slot. The PTX body is deliberately distinct
    // per-i so the BLAKE3 digests differ and the artifact store does
    // not deduplicate (which would invalidate the listing count).
    let total = 250usize;
    for i in 0..total {
        let name = format!("kernel.f32.{}", i);
        let version = "1.0.0";
        let ptx = format!("// kernel-{i}\n");
        let m = signed_manifest(&name, version, 80, &ptx, &key);
        reg.publish(m, ptx).expect("publish ok");
    }

    // Page 1: offset=0, limit=100 → exactly 100 entries.
    let page1 = reg.list_paginated(0, 100);
    assert_eq!(page1.len(), 100, "page 1 length");

    // Page 2: offset=100, limit=100 → exactly 100 entries.
    let page2 = reg.list_paginated(100, 100);
    assert_eq!(page2.len(), 100, "page 2 length");

    // Page 3: offset=200, limit=100 → exactly 50 entries (residual).
    let page3 = reg.list_paginated(200, 100);
    assert_eq!(page3.len(), 50, "page 3 length (residual)");

    // Union: the three pages MUST cover every published key exactly
    // once. We compare on (name, version, sm_version) since the
    // signature byte will differ if anything in the canonical envelope
    // changes; the keymap triple is the stable identity.
    let mut union: HashSet<(String, String, u32)> = HashSet::new();
    for m in page1.iter().chain(page2.iter()).chain(page3.iter()) {
        let key = (m.name.clone(), m.version.clone(), m.sm_version);
        assert!(union.insert(key), "no duplicate keys across pages");
    }
    assert_eq!(union.len(), total, "union covers all 250 published keys");

    // Default list() is a wrapper for list_paginated(0, 100) — verify.
    use tensor_wasm_jit::registry::KernelRegistry;
    let default = KernelRegistry::list(&reg);
    assert_eq!(
        default.len(),
        100,
        "default list returns 100 (default limit)"
    );
}
