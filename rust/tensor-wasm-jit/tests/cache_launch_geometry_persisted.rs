// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Regression test: the L2 on-disk cache must round-trip
//! `EmittedPtx::launch_geometry`.
//!
//! Prior to V2 of the on-disk layout, the disk reconstructor unconditionally
//! set `launch_geometry: (0, 0)` because the field was never persisted. Every
//! L2 hit silently dropped the (grid_x, block_x) hint that `ptx_emit`
//! populated for the dispatch path. This test pins V2's persistence path:
//! put a kernel with a recognisable geometry, drop the L1 cache by building
//! a fresh `KernelCache` from the same disk dir, and assert the geometry
//! round-trips through the L2-only read path.

use std::path::PathBuf;
use std::sync::Arc;

use tempfile::TempDir;
use tensor_wasm_core::types::TenantId;
use tensor_wasm_jit::cache::{
    CacheKey, CachedKernel, CompiledHandle, DiskCacheConfig, KernelCache,
};
use tensor_wasm_jit::ptx_emit::EmittedPtx;

#[test]
fn l2_hit_preserves_launch_geometry() {
    let tmp = TempDir::new().expect("tempdir");
    let dir: PathBuf = tmp.path().to_path_buf();
    let hmac_key = [0x77; 32];

    // First cache writes the entry to disk under V2 magic, persisting the
    // launch_geometry alongside the PTX text.
    let cache_a = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key,
    });
    let key = CacheKey::for_tenant(TenantId(42), 0xABCD_1234, 80);
    let ptx = Arc::new(EmittedPtx {
        text: ".visible .entry geo_kernel(){}".to_string(),
        launch_geometry: (256, 32),
    });
    cache_a.put(
        key,
        CachedKernel::new(0xABCD_1234, ptx, CompiledHandle::default()),
    );

    // Fresh cache pointed at the same dir — no L1 entry, so any hit must
    // come from the L2 (disk) path. With V1 the geometry came back as
    // (0, 0); with V2 it must round-trip the value we wrote.
    let cache_b = KernelCache::new().with_disk_persistence(DiskCacheConfig { dir, hmac_key });
    let hit = cache_b
        .get(&key)
        .expect("V2 L2 hit must reconstruct the kernel");
    assert_eq!(
        hit.ptx.launch_geometry,
        (256, 32),
        "launch_geometry must round-trip through the L2 on-disk cache"
    );
    assert!(
        hit.ptx.text.contains("geo_kernel"),
        "PTX text must also round-trip alongside the geometry"
    );
}

#[test]
fn l2_legacy_v1_file_is_treated_as_miss() {
    // A pre-existing pre-T30 raw record (V1 magic "TWJIT-KRNL-v1\0\0\0",
    // or V2 magic "TWJIT-KRNL-v2\0\0\0" — both written by the
    // pre-migration code that wrote the V2 envelope directly to the
    // `.ptxbin` file) must be silently discarded as a miss so the next
    // put rewrites it under the T30 layered `(sidecar -> artifact-store
    // blob)` shape.
    //
    // T30 reads the sidecar magic [`SIDECAR_MAGIC_V1`] (= "TWJIT-IDX-v1")
    // at the head of every `.ptxbin` file. A legacy file's first 16
    // bytes are the kernel-envelope magic ("TWJIT-KRNL-v1" or
    // "TWJIT-KRNL-v2"), so the sidecar-magic check rejects it as
    // "legacy or unknown" and the loader returns a clean miss.
    use std::io::Write;
    let tmp = TempDir::new().expect("tempdir");
    let dir: PathBuf = tmp.path().to_path_buf();
    let hmac_key = [0x55; 32];

    let key = CacheKey::for_tenant(TenantId(1), 0xFEED_FACE, 89);
    // Hash the key the same way DiskCache::path_for does so we land on
    // the exact filename a real put would have produced. Hardcoding the
    // computation here (rather than exposing path_for) keeps the test
    // sealed against cache internals — if path_for ever changes, both
    // the writer and this test fail together. Path shape since T20:
    //   `{key_prefix_hex:16}-{cache_key_hex:32}.ptxbin`
    // where `key_prefix_hex` is the first 8 bytes of blake3(hmac_key).
    let path = {
        let mut hasher = blake3::Hasher::new();
        hasher.update(b"tensor-wasm-jit::DiskCache::path::v1\0");
        hasher.update(&key.tenant_id.to_le_bytes());
        hasher.update(&key.blueprint.to_le_bytes());
        hasher.update(&key.sm_version.to_le_bytes());
        hasher.update(&key.emit_config_hash.to_le_bytes());
        let h = hasher.finalize();
        let cache_key_hex: String = h
            .as_bytes()
            .iter()
            .take(16)
            .map(|b| format!("{b:02x}"))
            .collect();
        let key_fp = blake3::hash(&hmac_key[..]);
        let key_prefix_hex: String = key_fp
            .as_bytes()
            .iter()
            .take(8)
            .map(|b| format!("{b:02x}"))
            .collect();
        dir.join(format!("{key_prefix_hex}-{cache_key_hex}.ptxbin"))
    };

    // Build a V1 record by hand. V1 layout was:
    //   magic(16) || fingerprint(8) || sm_version(4) || ptx_len(8)
    //     || ptx_text(N) || hmac(32)
    // This sits directly at the sidecar's path (the same path post-T30
    // sidecars use), but its leading 16 bytes ("TWJIT-KRNL-v1\0\0\0")
    // do NOT match SIDECAR_MAGIC_V1 ("TWJIT-IDX-v1\0\0\0\0"), so the
    // T30 loader rejects it as legacy/unknown and returns a miss.
    std::fs::create_dir_all(&dir).unwrap();
    let ptx_text = b".visible .entry legacy_kernel(){}";
    let mut buf: Vec<u8> = Vec::new();
    buf.extend_from_slice(b"TWJIT-KRNL-v1\0\0\0");
    buf.extend_from_slice(&key.blueprint.to_le_bytes());
    buf.extend_from_slice(&key.sm_version.to_le_bytes());
    buf.extend_from_slice(&(ptx_text.len() as u64).to_le_bytes());
    buf.extend_from_slice(ptx_text);
    let tag = blake3::keyed_hash(&hmac_key, &buf);
    buf.extend_from_slice(tag.as_bytes());
    let mut f = std::fs::File::create(&path).unwrap();
    f.write_all(&buf).unwrap();
    drop(f);

    let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig { dir, hmac_key });
    assert!(
        cache.get(&key).is_none(),
        "legacy pre-T30 records must be treated as miss so the L1 path \
         re-emits and rewrites under the T30 layered layout (the only way \
         to recover the lost launch_geometry on the cold path)"
    );
}
