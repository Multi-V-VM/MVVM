// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Regression: the on-disk L2 cache must partition its filenames by HMAC
//! key fingerprint.
//!
//! Without this partition, two `KernelCache`s pointed at the same
//! directory but configured with *different* HMAC keys would race on the
//! same final filename (the path was derived only from `CacheKey`). The
//! second writer's `tmp.persist` would silently overwrite the first
//! writer's entry, and both readers would then fail the HMAC check on
//! each other's data — every put-then-get round-trip looking like a
//! miss in steady state.
//!
//! The fix (see `DiskCache::path_for`) prefixes the filename with the
//! first 8 bytes of `blake3::hash(hmac_key)` so the two writers land in
//! disjoint paths and each loader only reads back its own entries.

use std::path::PathBuf;
use std::sync::Arc;

use tempfile::TempDir;
use tensor_wasm_core::types::TenantId;
use tensor_wasm_jit::cache::{
    CacheKey, CachedKernel, CompiledHandle, DiskCacheConfig, KernelCache,
};
use tensor_wasm_jit::ptx_emit::EmittedPtx;

fn fixture_ptx(text: &str) -> Arc<EmittedPtx> {
    Arc::new(EmittedPtx {
        text: text.to_string(),
        launch_geometry: (1, 1),
    })
}

/// List every `*.ptxbin` file in `dir`, sorted by filename so the
/// assertions below are stable across filesystems.
fn ptxbin_paths(dir: &PathBuf) -> Vec<PathBuf> {
    let mut paths: Vec<PathBuf> = std::fs::read_dir(dir)
        .expect("read tempdir")
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|ext| ext == "ptxbin"))
        .collect();
    paths.sort();
    paths
}

#[test]
fn two_keys_in_same_dir_produce_distinct_paths_and_dont_collide() {
    let tmp = TempDir::new().expect("tempdir");
    let dir: PathBuf = tmp.path().to_path_buf();

    // Two caches, same directory, *different* HMAC keys.
    let key_a = [0xAAu8; 32];
    let key_b = [0xBBu8; 32];
    let cache_a = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key_a,
    });
    let cache_b = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key_b,
    });

    // Same logical CacheKey put into both — distinct PTX text so we can
    // tell the two entries apart on the read-back side.
    let cache_key = CacheKey::for_tenant(TenantId(7), 0xCAFE_BABE, 80);
    cache_a.put(
        cache_key,
        CachedKernel::new(
            0xCAFE_BABE,
            fixture_ptx(".visible .entry from_key_a(){}"),
            CompiledHandle::default(),
        ),
    );
    cache_b.put(
        cache_key,
        CachedKernel::new(
            0xCAFE_BABE,
            fixture_ptx(".visible .entry from_key_b(){}"),
            CompiledHandle::default(),
        ),
    );

    // Assertion #1: two distinct on-disk files exist. Without the
    // key-fingerprint prefix the second `put` would have overwritten
    // the first via `tmp.persist`, leaving exactly one file.
    let paths = ptxbin_paths(&dir);
    assert_eq!(
        paths.len(),
        2,
        "two HMAC keys in the same dir must produce two on-disk files; \
         got {} (paths = {:?})",
        paths.len(),
        paths,
    );
    assert_ne!(paths[0], paths[1], "the two on-disk paths must be distinct");

    // Assertion #2: each cache reads back only its own entry. We drop
    // the writer caches and reload from disk to make sure the L1 hit
    // doesn't mask a buggy on-disk read.
    drop(cache_a);
    drop(cache_b);
    let reloaded_a = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key_a,
    });
    let reloaded_b = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key_b,
    });
    let got_a = reloaded_a
        .get(&cache_key)
        .expect("cache_a entry should round-trip under its own key");
    let got_b = reloaded_b
        .get(&cache_key)
        .expect("cache_b entry should round-trip under its own key");
    assert!(
        got_a.ptx.text.contains("from_key_a"),
        "cache_a must read back its own entry, got: {:?}",
        got_a.ptx.text,
    );
    assert!(
        got_b.ptx.text.contains("from_key_b"),
        "cache_b must read back its own entry, got: {:?}",
        got_b.ptx.text,
    );

    // Assertion #3: a third reader with an entirely *different* key
    // sees no entry at all (sanity-check that the partition is keyed,
    // not just position-stable).
    let reloaded_c = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir,
        hmac_key: [0xCCu8; 32],
    });
    assert!(
        reloaded_c.get(&cache_key).is_none(),
        "an unrelated HMAC key must see a miss — partitioning is keyed"
    );
}
