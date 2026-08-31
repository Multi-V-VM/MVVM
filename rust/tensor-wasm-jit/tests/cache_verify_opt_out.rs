// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Regression tests for the `verify_on_get` opt-out on the L1 kernel
//! cache.
//!
//! Background: `KernelCache::get` historically recomputed a BLAKE3 over
//! `ptx.text` on every L1 hit (jit S-3 in-memory poisoning defence).
//! That costs ~10 µs over a typical multi-KB PTX blob and dominates on
//! a high-QPS path with multi-MB PTX. The opt-out
//! [`KernelCacheConfig::verify_on_get`] skips the recompute on `get`
//! while still keeping defence-in-depth: an entry whose stored
//! `integrity_hash` is all-zero (the construction signature of a
//! `CachedKernel` built without `CachedKernel::new`) is refused.
//!
//! These tests pin:
//! 1. Default config still recomputes (no behavioural change for the
//!    existing audience).
//! 2. Opting out skips the recompute and increments the counter that
//!    backs `tensor_wasm_jit_cache_verify_skipped_total`.
//! 3. Even with the opt-out, a hand-crafted zero-hash entry is rejected
//!    (closes the construction-bypass primitive).

use std::sync::Arc;

use tensor_wasm_core::types::TenantId;
use tensor_wasm_jit::cache::{
    CacheKey, CachedKernel, CompiledHandle, KernelCache, KernelCacheConfig,
};
use tensor_wasm_jit::ptx_emit::EmittedPtx;

fn fixture_ptx(text: &str) -> Arc<EmittedPtx> {
    Arc::new(EmittedPtx {
        text: text.to_string(),
        launch_geometry: (1, 1),
    })
}

#[test]
fn verify_on_get_default_recomputes() {
    // The default config is `verify_on_get: true`, so every L1 hit
    // recomputes the BLAKE3 and the `verify_skipped_total` counter
    // stays at zero — the dashboard sees "we are paying the
    // full verification cost".
    let cache = KernelCache::new();
    assert!(
        cache.config().verify_on_get,
        "default config must keep verify-on-get enabled"
    );

    let key = CacheKey::for_tenant(TenantId(1), 0xABCD, 80);
    let kernel = CachedKernel::new(
        0xABCD,
        fixture_ptx(".visible .entry recompute_default(){}"),
        CompiledHandle::default(),
    );
    cache.put(key, kernel);

    assert_eq!(
        cache.verify_skipped_total(),
        0,
        "default config must not increment the skip counter"
    );

    for _ in 0..2 {
        let got = cache.get(&key).expect("default get must succeed");
        assert_eq!(got.fingerprint, 0xABCD);
    }

    assert_eq!(
        cache.verify_skipped_total(),
        0,
        "default config must keep the skip counter at zero across hits"
    );
}

#[test]
fn verify_on_get_false_skips_recompute() {
    // Opting out: each L1 hit increments the skip counter that backs
    // `tensor_wasm_jit_cache_verify_skipped_total` and the BLAKE3
    // recompute is not performed.
    let cache = KernelCache::with_config(KernelCacheConfig::default().with_verify_on_get(false));
    assert!(
        !cache.config().verify_on_get,
        "explicit opt-out must persist in the cache config"
    );

    let key = CacheKey::for_tenant(TenantId(2), 0xF00D, 80);
    let kernel = CachedKernel::new(
        0xF00D,
        fixture_ptx(".visible .entry skip_recompute(){}"),
        CompiledHandle::default(),
    );
    cache.put(key, kernel);

    assert_eq!(
        cache.verify_skipped_total(),
        0,
        "no skips recorded before the first get"
    );

    let _ = cache.get(&key).expect("first get must succeed");
    assert_eq!(
        cache.verify_skipped_total(),
        1,
        "first get on opt-out path must increment the skip counter"
    );

    let _ = cache.get(&key).expect("second get must succeed");
    assert_eq!(
        cache.verify_skipped_total(),
        2,
        "second get on opt-out path must increment the skip counter again"
    );
}

#[test]
fn verify_on_get_false_still_rejects_zero_hash() {
    // Defence-in-depth: even with verify-on-get disabled, an entry
    // whose `integrity_hash` is all-zero must be refused on `get`.
    // The zero hash is the construction signature of a `CachedKernel`
    // built without `CachedKernel::new` — i.e. via the doc-hidden
    // struct literal — and accepting it would mean trusting an entry
    // whose `ptx.text` was never tagged.
    //
    // `put` rejects this same shape eagerly, so we bypass it via the
    // doc-hidden test-only insert to install a hostile entry directly
    // into L1 storage.
    let cache = KernelCache::with_config(KernelCacheConfig::default().with_verify_on_get(false));
    let key = CacheKey::for_tenant(TenantId(3), 0xBADD_F00D, 80);
    let bad = CachedKernel {
        fingerprint: 0xBADD_F00D,
        ptx: fixture_ptx(".visible .entry hostile(){}"),
        compiled: CompiledHandle::default(),
        integrity_hash: [0u8; 32],
    };
    cache.__test_only_insert_unchecked(key, bad);

    assert!(
        cache.get(&key).is_none(),
        "verify-on-get=false must still refuse a zero-hash entry"
    );
}
