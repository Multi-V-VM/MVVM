// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Disk-cache GC + key-rotation API.
//!
//! The on-disk L2 cache partitions every file by the fingerprint of the
//! writing HMAC key (sidecar prefix `{fp}-…ptxbin`, blob middle segment
//! `….{fp}.bin`). After rotating to a new key, the previous generation's
//! files linger under the old fingerprint. `KernelCache::gc_disk` sweeps
//! those stale-fingerprint files while unconditionally retaining the
//! active key's entries.

use std::path::PathBuf;
use std::sync::Arc;

use tempfile::TempDir;
use tensor_wasm_core::types::TenantId;
use tensor_wasm_jit::cache::{
    CacheKey, CachedKernel, CompiledHandle, DiskCacheConfig, KernelCache, KeyFingerprint,
};
use tensor_wasm_jit::ptx_emit::EmittedPtx;

fn fixture_ptx(text: &str) -> Arc<EmittedPtx> {
    Arc::new(EmittedPtx {
        text: text.to_string(),
        launch_geometry: (1, 1),
    })
}

/// Every file in `dir` (any extension) sorted for stable assertions.
fn all_files(dir: &PathBuf) -> Vec<String> {
    let mut names: Vec<String> = std::fs::read_dir(dir)
        .expect("read tempdir")
        .filter_map(|e| e.ok())
        .filter(|e| e.file_type().map(|t| t.is_file()).unwrap_or(false))
        .filter_map(|e| e.file_name().to_str().map(|s| s.to_string()))
        .collect();
    names.sort();
    names
}

#[test]
fn gc_removes_stale_fingerprint_files_and_retains_active() {
    let tmp = TempDir::new().expect("tempdir");
    let dir: PathBuf = tmp.path().to_path_buf();

    let key_old = [0x11u8; 32];
    let key_new = [0x22u8; 32];

    let cache_key = CacheKey::for_tenant(TenantId(7), 0xCAFE_BABE, 80);

    // Generation 1: write an entry under the OLD key.
    let cache_old = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key_old,
    });
    cache_old.put(
        cache_key,
        CachedKernel::new(
            0xCAFE_BABE,
            fixture_ptx(".visible .entry old_gen(){}"),
            CompiledHandle::default(),
        ),
    );
    drop(cache_old);

    // Rotate: generation 2 writes the same logical key under the NEW key.
    let cache_new = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key_new,
    });
    cache_new.put(
        cache_key,
        CachedKernel::new(
            0xCAFE_BABE,
            fixture_ptx(".visible .entry new_gen(){}"),
            CompiledHandle::default(),
        ),
    );

    // Before GC: both generations' files coexist. Each generation writes a
    // sidecar (.ptxbin) + an artifact blob (.bin), so 4 files total.
    let before = all_files(&dir);
    assert_eq!(
        before.len(),
        4,
        "two generations should leave 4 files (2 sidecars + 2 blobs); got {before:?}"
    );

    // Both fingerprints are visible on disk.
    let fps = cache_new.disk_key_fingerprints().expect("enumerate fps");
    let fp_old = KeyFingerprint::of_key(&key_old);
    let fp_new = KeyFingerprint::of_key(&key_new);
    assert!(fps.contains(&fp_old), "old fingerprint should be present");
    assert!(fps.contains(&fp_new), "new fingerprint should be present");

    let active = cache_new
        .active_disk_key_fingerprint()
        .expect("disk configured");
    assert_eq!(active, fp_new, "active fingerprint is the new key's");

    // Sweep: retain only the active (new) key. The old generation's 2 files
    // are removed; the new generation's 2 are retained.
    let retain = vec![active.clone()];
    let removed = cache_new.gc_disk(&retain).expect("gc");
    assert_eq!(removed, 2, "exactly the old generation's 2 files removed");

    let after = all_files(&dir);
    assert_eq!(
        after.len(),
        2,
        "only the active generation remains; got {after:?}"
    );
    for name in &after {
        assert!(
            name.contains(active.as_str()),
            "surviving file {name} must carry the active fingerprint {active}"
        );
    }

    // The active entry still round-trips after the sweep (reload to bypass
    // the L1 hit and force an on-disk read).
    drop(cache_new);
    let reloaded = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key_new,
    });
    let got = reloaded
        .get(&cache_key)
        .expect("active entry must survive GC and round-trip from disk");
    assert!(
        got.ptx.text.contains("new_gen"),
        "surviving entry is the new generation's: {:?}",
        got.ptx.text
    );
}

#[test]
fn gc_never_deletes_active_even_if_omitted_from_retain() {
    let tmp = TempDir::new().expect("tempdir");
    let dir: PathBuf = tmp.path().to_path_buf();
    let key = [0x33u8; 32];

    let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key: key,
    });
    cache.put(
        CacheKey::for_tenant(TenantId(1), 0xABCD, 80),
        CachedKernel::new(
            0xABCD,
            fixture_ptx(".visible .entry only(){}"),
            CompiledHandle::default(),
        ),
    );

    let before = all_files(&dir).len();
    assert_eq!(before, 2, "one entry => sidecar + blob");

    // Empty retain set — but the active key's files must NOT be swept.
    let removed = cache.gc_disk(&[]).expect("gc");
    assert_eq!(removed, 0, "active generation must never be swept");
    assert_eq!(all_files(&dir).len(), 2, "active files retained");
}

#[test]
fn gc_on_missing_dir_is_noop() {
    let tmp = TempDir::new().expect("tempdir");
    // Point at a subdir that does not exist yet (no put has run).
    let dir = tmp.path().join("never-created");
    let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir,
        hmac_key: [0x44u8; 32],
    });
    assert_eq!(cache.gc_disk(&[]).expect("gc on missing dir"), 0);
    assert!(cache
        .disk_key_fingerprints()
        .expect("enumerate on missing dir")
        .is_empty());
}

#[test]
fn gc_and_fingerprints_are_noop_without_disk() {
    // No disk persistence configured at all.
    let cache = KernelCache::new();
    assert!(cache.active_disk_key_fingerprint().is_none());
    assert!(cache.disk_key_fingerprints().expect("enumerate").is_empty());
    assert_eq!(cache.gc_disk(&[]).expect("gc"), 0);
}
