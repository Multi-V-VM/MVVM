// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! T30: regression-pin the migration of the JIT L2 disk cache onto
//! `tensor-wasm-artifacts::DiskArtifactStore`.
//!
//! Three guarantees this file freezes:
//!
//! 1. A `(put, get)` round-trip through `DiskCache` (via the public
//!    `KernelCache::with_disk_persistence` builder) produces both the
//!    artifact-store blob (under the unified store's path layout —
//!    `*.<key-fp>.bin`) AND the sidecar (`*.ptxbin`) that maps the
//!    cache key to the blob's content hash.
//! 2. The inner V2 kernel-manifest envelope (`b"TWJIT-KRNL-v2\0\0\0"`
//!    magic + length-prefixed body) is preserved end-to-end: after
//!    a `put`, decoding the artifact-store blob recovers a buffer
//!    whose first 16 bytes are exactly the V2 magic. This pins the
//!    "cross-version compat" invariant the task spec calls out — even
//!    if a future iteration of the disk store changed its outer
//!    framing, the inner envelope a kernel writer produces would still
//!    decode under the legacy V2 reader.
//! 3. Tampering with the artifact-store blob (a bit flipped inside the
//!    zstd body) makes the next `get` collapse to a miss — exercising
//!    the store's streaming HMAC verification through the cache's
//!    public surface.
//!
//! The test deliberately reaches only through `KernelCache`,
//! `DiskCacheConfig`, and `tensor_wasm_artifacts` — no private symbols
//! of `tensor-wasm-jit` are touched, so the test will continue to
//! compile across any future refactor that keeps the public API stable.

use std::path::PathBuf;
use std::sync::Arc;

use tempfile::TempDir;
use tensor_wasm_artifacts::{
    decode_envelope_from_bytes, ArtifactStore, ContentHash, DiskArtifactStore,
};
use tensor_wasm_core::types::TenantId;
use tensor_wasm_jit::cache::{
    CacheKey, CachedKernel, CompiledHandle, DiskCacheConfig, KernelCache,
};
use tensor_wasm_jit::ptx_emit::EmittedPtx;

/// Recompute the same sidecar magic the disk cache writes. Hardcoded
/// rather than re-exported so the test fails if a future migration
/// changes the magic without updating the regression pin.
const SIDECAR_MAGIC_V1: &[u8; 16] = b"TWJIT-IDX-v1\0\0\0\0";
/// Recompute the same inner-envelope magic the writer stamps inside
/// every artifact-store blob.
const KERNEL_ENVELOPE_MAGIC_V2: &[u8; 16] = b"TWJIT-KRNL-v2\0\0\0";

fn fixture_ptx(text: &str) -> Arc<EmittedPtx> {
    Arc::new(EmittedPtx {
        text: text.to_string(),
        launch_geometry: (16, 64),
    })
}

/// (a) The sidecar lives under `DiskArtifactStore`'s parent dir.
/// (b) The artifact-store blob exists alongside it.
/// (c) The blob's body, once HMAC + zstd verified by the artifact
///     store, starts with the V2 kernel-envelope magic.
#[test]
fn t30_put_writes_sidecar_and_artifact_blob_with_v2_magic() {
    let tmp = TempDir::new().expect("tempdir");
    let dir: PathBuf = tmp.path().to_path_buf();
    let hmac_key = [0x71; 32];

    let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key,
    });
    let key = CacheKey::for_tenant(TenantId(11), 0x1234_5678, 80);
    let kernel = CachedKernel::new(
        0x1234_5678,
        fixture_ptx(".visible .entry t30_round_trip(){}"),
        CompiledHandle::default(),
    );
    cache.put(key, kernel);

    // Layout assertion #1: exactly one `.ptxbin` sidecar.
    let sidecars: Vec<PathBuf> = std::fs::read_dir(&dir)
        .expect("read tempdir")
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|ext| ext == "ptxbin"))
        .collect();
    assert_eq!(
        sidecars.len(),
        1,
        "T30 put must produce exactly one sidecar `.ptxbin`; got {sidecars:?}",
    );

    // Layout assertion #2: the sidecar is 48 bytes (16 B magic + 32 B
    // content hash) and starts with the T30 sidecar magic.
    let sidecar_bytes = std::fs::read(&sidecars[0]).expect("read sidecar");
    assert_eq!(
        sidecar_bytes.len(),
        16 + 32,
        "sidecar must be magic(16) + content_hash(32)"
    );
    assert_eq!(
        &sidecar_bytes[..16],
        SIDECAR_MAGIC_V1,
        "sidecar must lead with the T30 sidecar magic"
    );
    let mut hash_bytes = [0u8; 32];
    hash_bytes.copy_from_slice(&sidecar_bytes[16..]);
    let content_hash = ContentHash::from_bytes(hash_bytes);

    // Layout assertion #3: the artifact-store blob exists under the
    // unified store's filename convention (`{hash}.{key_fp}.bin`).
    let blobs: Vec<PathBuf> = std::fs::read_dir(&dir)
        .expect("read tempdir")
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|ext| ext == "bin"))
        .collect();
    assert_eq!(
        blobs.len(),
        1,
        "T30 put must produce exactly one artifact-store blob; got {blobs:?}",
    );

    // Round-trip assertion: the blob, fetched through a fresh
    // `DiskArtifactStore` rooted at the same dir with the same key,
    // streaming-verifies and decompresses into the V2 kernel envelope.
    let store = DiskArtifactStore::new(dir.clone(), hmac_key);
    let payload = store
        .get(&content_hash)
        .expect("artifact-store get must verify the streaming HMAC and decompress");
    assert!(
        payload.len() >= 16,
        "decoded payload must include at least the inner envelope magic"
    );
    assert_eq!(
        &payload[..16],
        KERNEL_ENVELOPE_MAGIC_V2,
        "inner V2 envelope magic must survive the round trip (cross-version compat)"
    );

    // Cross-check: a vanilla `KernelCache` re-built against the same dir
    // and key reads the kernel back through the L2 path with the
    // launch_geometry preserved (T22 streaming + V2 envelope intact).
    drop(cache);
    let reloaded = KernelCache::new().with_disk_persistence(DiskCacheConfig { dir, hmac_key });
    let hit = reloaded
        .get(&key)
        .expect("L2 round-trip through the unified artifact store must hit");
    assert_eq!(hit.fingerprint, 0x1234_5678);
    assert_eq!(hit.ptx.launch_geometry, (16, 64));
    assert!(hit.ptx.text.contains("t30_round_trip"));
}

/// Tampering with the artifact-store blob (bit-flip in the zstd body)
/// must make `get` collapse to a miss. This exercises the store's
/// streaming HMAC verification end-to-end through `KernelCache::get`.
#[test]
fn t30_tampered_artifact_blob_is_treated_as_miss() {
    let tmp = TempDir::new().expect("tempdir");
    let dir: PathBuf = tmp.path().to_path_buf();
    let hmac_key = [0x44; 32];

    let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig {
        dir: dir.clone(),
        hmac_key,
    });
    let key = CacheKey::for_tenant(TenantId(3), 0xC0FFEE, 80);
    cache.put(
        key,
        CachedKernel::new(
            0xC0FFEE,
            fixture_ptx(".visible .entry tamper_target(){}"),
            CompiledHandle::default(),
        ),
    );

    // Find the one artifact-store blob and flip a byte inside its
    // zstd body. The artifact store's outer envelope is
    //   magic(16) || version(4) || content_hash(32) || zstd(payload) || hmac_tag(32)
    // so we land the flip safely between the 52-byte header prefix
    // and the 32-byte HMAC tail.
    const ARTIFACT_HEADER_LEN: usize = 16 + 4 + 32;
    const ARTIFACT_HMAC_LEN: usize = 32;
    let blobs: Vec<PathBuf> = std::fs::read_dir(&dir)
        .expect("read tempdir")
        .filter_map(|e| e.ok())
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|ext| ext == "bin"))
        .collect();
    assert_eq!(blobs.len(), 1);
    let mut bytes = std::fs::read(&blobs[0]).expect("read blob");
    assert!(
        bytes.len() > ARTIFACT_HEADER_LEN + ARTIFACT_HMAC_LEN,
        "blob must have non-empty zstd body"
    );
    let mid = ARTIFACT_HEADER_LEN + (bytes.len() - ARTIFACT_HEADER_LEN - ARTIFACT_HMAC_LEN) / 2;
    bytes[mid] ^= 0xFF;
    std::fs::write(&blobs[0], &bytes).expect("rewrite tampered blob");

    // Defence-in-depth check: decode_envelope_from_bytes returns
    // BadHmac for a tampered envelope, confirming the streaming MAC
    // covers this byte range.
    let direct = decode_envelope_from_bytes(&bytes, &hmac_key);
    assert!(
        direct.is_err(),
        "tampered envelope must fail HMAC verification directly via \
         decode_envelope_from_bytes (defence-in-depth probe)"
    );

    // The cache must then collapse to a miss when the user calls `get`
    // through a fresh `KernelCache` (so no L1 entry masks the L2 path).
    drop(cache);
    let reloaded = KernelCache::new().with_disk_persistence(DiskCacheConfig { dir, hmac_key });
    assert!(
        reloaded.get(&key).is_none(),
        "tampered artifact-store blob must surface as a clean cache miss"
    );
}
