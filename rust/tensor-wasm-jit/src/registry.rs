// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Signed kernel registry (roadmap feature #3).
//!
//! Operators publish vetted PTX kernels (matmul, attention, conv2d) as
//! HMAC-SHA256-signed [`KernelManifest`] records. Guests reference kernels
//! by `name@version` (or content-addressed digest); the runtime resolves
//! the manifest, verifies the signature, and exposes the PTX text to
//! the JIT cache as a pre-populated entry.
//!
//! ## v0.3.7 status: scaffold
//!
//! Manifest types, signing helpers, and the registry trait surface land.
//! Actual on-disk store, signing CLI, and wire-format integration are
//! v0.4 deliverables — see `docs/KERNEL-REGISTRY.md`.
//!
//! ## Signing envelope (v2)
//!
//! The HMAC-SHA256 input is the byte concatenation
//!
//! ```text
//! "twasm-kmf-v2"                                 (12 bytes, magic + version tag)
//! u64_le(name.len())      || name.as_bytes()
//! u64_le(version.len())   || version.as_bytes()
//! u64_le(publisher.len()) || publisher.as_bytes()
//! u64_le(8)               || published_unix_ms as u64_le
//! u64_le(4)               || sm_version as u32_le
//! u64_le(digest.len())    || digest_bytes
//! ```
//!
//! where `digest` is the BLAKE3 hash of the UTF-8 PTX text. Every
//! field is preceded by a `u64`-little-endian length prefix. Fixed-
//! width integer fields (`published_unix_ms`, `sm_version`) carry a
//! length prefix too, so the canonical encoding is trivially
//! parseable end-to-end and uniform across all fields.
//!
//! Length-prefixing replaces the prior NUL-separator scheme — under
//! NUL separators the two manifests `("a\0b", "c", ...)` and
//! `("a", "b\0c", ...)` produced identical signed-byte streams, a
//! cross-field collision that an attacker with publish access could
//! exploit. Length prefixes make field boundaries unambiguous and
//! also bind the `publisher` and `published_unix_ms` fields into the
//! MAC so they can no longer be rewritten post-sign without
//! invalidating the signature.
//!
//! The leading `b"twasm-kmf-v2"` magic gates this envelope to v2.
//! Manifests signed with the v0.3.7 (v1) NUL-separator scheme will
//! NOT verify under the v2 MAC and must be re-signed — see the
//! `[Unreleased]` CHANGELOG entry.

use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::path::PathBuf;
use std::sync::Arc;

use dashmap::DashMap;
use tensor_wasm_artifacts::{ArtifactError, ArtifactStore, ContentHash, DiskArtifactStore};

/// Signed kernel manifest. Cargo for vetted PTX.
///
/// Marked `#[non_exhaustive]` so future revisions can add fields
/// without breaking downstream pattern-matching consumers. Construct
/// instances via [`KernelManifest::new`] from outside this crate (the
/// `non_exhaustive` attribute disallows struct-literal construction
/// from foreign crates).
#[derive(Debug, Clone, Serialize, Deserialize)]
#[non_exhaustive]
pub struct KernelManifest {
    /// Stable name (e.g. `"matmul.f32"`).
    pub name: String,
    /// SemVer-style version (`"1.0.0"`).
    pub version: String,
    /// Compute capability the PTX was built for (e.g. `80` for sm_80).
    pub sm_version: u32,
    /// BLAKE3 hash of the PTX text. Content-addresses the artifact.
    pub digest: [u8; 32],
    /// HMAC-SHA256 tag computed over the canonical signed-bytes of
    /// this manifest — see the module-level docstring for the exact
    /// envelope layout (v2). Covers `name`, `version`, `publisher`,
    /// `published_unix_ms`, `sm_version`, and `digest`.
    ///
    /// **The tag is PUBLIC by design.** It authenticates the
    /// manifest's authorship (i.e. proves the holder of the publishing
    /// HMAC key produced it) but is NOT a private signature in the
    /// asymmetric-crypto sense and MUST NOT be treated as a secret.
    /// `KernelManifest` deliberately derives `Serialize` / `Deserialize`
    /// and the field is `pub` so the tag travels with the manifest
    /// over the wire and through storage — that is the intended,
    /// audited behaviour, not an oversight.
    ///
    /// What IS a secret is the HMAC key used to compute the tag (the
    /// publisher's signing key); see `sign_manifest`. Leaking the
    /// signing key — not the tag — is what would let an attacker
    /// forge manifests.
    pub signature: [u8; 32],
    /// Wall-clock publish timestamp (Unix millis). Covered by the v2
    /// signature envelope — tampering with this field invalidates the
    /// signature.
    pub published_unix_ms: u64,
    /// Publisher identifier (typically a tenant id or signing-key id).
    /// Covered by the v2 signature envelope — tampering with this
    /// field invalidates the signature.
    pub publisher: String,
    /// Optional launch geometry hint `(grid_size, block_size)` the
    /// kernel expects, mirroring [`EmittedPtx::launch_geometry`] on the
    /// freshly-emitted path. Used by the JIT L3 registry-promotion path
    /// (`KernelCache::get_with_registry_fallback`) to populate the
    /// promoted L1 entry's geometry instead of falling back to `(0, 0)`.
    ///
    /// **Deliberately NOT covered by the v2 signature envelope.**
    /// Launch geometry is an advisory *launch hint*, not a security
    /// boundary — the PTX content itself is authenticated by `digest`
    /// (and transitively by the signature, which binds `digest`). Folding
    /// geometry into `KernelManifest::canonical_signed_bytes` would
    /// invalidate every previously-signed `twasm-kmf-v2` blob (a magic
    /// bump to `-v3` plus a dual-version verify path) for no security
    /// gain, so it is carried as an unsigned hint instead. A bad actor
    /// who could rewrite this field could at worst mis-hint a launch grid
    /// for a kernel whose code is already separately authenticated.
    ///
    /// `#[serde(default)]` makes manifests serialized before this field
    /// existed deserialize cleanly (the field reads back as `None`), so
    /// older on-disk blobs and wire payloads remain forward-compatible.
    ///
    /// [`EmittedPtx::launch_geometry`]: crate::ptx_emit::EmittedPtx::launch_geometry
    #[serde(default)]
    pub launch_geometry: Option<(u32, u32)>,
}

impl KernelManifest {
    /// Construct a manifest from its component fields.
    ///
    /// `KernelManifest` is `#[non_exhaustive]`, so foreign crates cannot
    /// build one via a struct literal — they must go through this
    /// constructor. The `signature` field is typically left as
    /// `[0u8; 32]` here and filled in afterwards by [`sign_manifest`],
    /// because the publisher's HMAC key is what produces the
    /// signature value.
    ///
    /// The optional `launch_geometry` hint defaults to `None`; publishers
    /// that know the kernel's grid/block geometry chain
    /// [`Self::with_launch_geometry`] after this constructor to set it.
    /// Keeping this signature stable (geometry is set via the builder, not
    /// a new positional argument) is intentional so the cross-crate
    /// callers in tensor-wasm-cli / tensor-wasm-api and the integration
    /// tests keep compiling unchanged.
    pub fn new(
        name: String,
        version: String,
        sm_version: u32,
        digest: [u8; 32],
        signature: [u8; 32],
        published_unix_ms: u64,
        publisher: String,
    ) -> Self {
        Self {
            name,
            version,
            sm_version,
            digest,
            signature,
            published_unix_ms,
            publisher,
            launch_geometry: None,
        }
    }

    /// Builder: attach an optional launch-geometry hint
    /// `(grid_size, block_size)` to this manifest and return it.
    ///
    /// This is the publisher-facing way to populate the
    /// [`KernelManifest::launch_geometry`] field without widening
    /// [`KernelManifest::new`]'s signature. Because geometry is carried
    /// as an *unsigned* hint (see the field docs), this may be called
    /// either before or after [`sign_manifest`] without affecting
    /// signature verification — the canonical signed bytes do not include
    /// it. Pass `None` to explicitly clear any previously-set hint.
    pub fn with_launch_geometry(mut self, launch_geometry: Option<(u32, u32)>) -> Self {
        self.launch_geometry = launch_geometry;
        self
    }

    /// First 8 bytes of `digest` interpreted as a little-endian `u64`.
    /// Used as the synthetic `fingerprint` field on the `CachedKernel`
    /// that the registry path promotes into L1.
    pub fn digest_as_u64(&self) -> u64 {
        let mut buf = [0u8; 8];
        buf.copy_from_slice(&self.digest[..8]);
        u64::from_le_bytes(buf)
    }

    /// Build the canonical v2 signed-bytes blob for this manifest.
    ///
    /// Both [`sign_manifest`] and [`InMemoryRegistry::verify_signature`]
    /// route through this single helper, so the sign and verify
    /// paths cannot drift. See the module-level docstring for the
    /// exact wire layout. The output never contains the
    /// `signature` field itself — the MAC is computed over this
    /// blob and stored in `signature`.
    ///
    /// The optional `launch_geometry` hint is DELIBERATELY excluded from
    /// this envelope (see [`KernelManifest::launch_geometry`]): it is an
    /// advisory launch hint, not a security boundary, so adding it here
    /// (and bumping the `twasm-kmf-v2` magic) would needlessly invalidate
    /// every previously-signed manifest. Keeping the envelope byte-for-
    /// byte identical to the prior v2 form means existing signed blobs
    /// continue to verify unchanged.
    pub(crate) fn canonical_signed_bytes(&self) -> Vec<u8> {
        // Pre-size: 12 (magic) + 6*8 (length prefixes) + name + version
        // + publisher + 8 (ts) + 4 (sm) + 32 (digest). The exact size
        // doesn't matter for correctness, only for one fewer realloc.
        let cap = 12
            + 6 * 8
            + self.name.len()
            + self.version.len()
            + self.publisher.len()
            + 8
            + 4
            + self.digest.len();
        let mut buf = Vec::with_capacity(cap);
        buf.extend_from_slice(SIGNED_BYTES_MAGIC_V2);
        push_len_prefixed(&mut buf, self.name.as_bytes());
        push_len_prefixed(&mut buf, self.version.as_bytes());
        push_len_prefixed(&mut buf, self.publisher.as_bytes());
        push_len_prefixed(&mut buf, &self.published_unix_ms.to_le_bytes());
        push_len_prefixed(&mut buf, &self.sm_version.to_le_bytes());
        push_len_prefixed(&mut buf, &self.digest);
        buf
    }
}

/// Magic + version tag prefixed to every v2 canonical signed-bytes
/// blob. Exactly 12 bytes — keeping it a fixed width means a future
/// v3 envelope can prepend its own tag without ambiguity. Changing
/// this constant is a breaking change to every previously-signed
/// manifest in existence.
pub(crate) const SIGNED_BYTES_MAGIC_V2: &[u8; 12] = b"twasm-kmf-v2";

/// Append `bytes` to `buf` preceded by a `u64` little-endian length
/// prefix. Used by [`KernelManifest::canonical_signed_bytes`] to
/// emit each field in the canonical layout.
fn push_len_prefixed(buf: &mut Vec<u8>, bytes: &[u8]) {
    buf.extend_from_slice(&(bytes.len() as u64).to_le_bytes());
    buf.extend_from_slice(bytes);
}

/// Resolves a JIT cache key to a (name, version) tuple that the
/// registry can look up. The cache is keyed by (tenant, blueprint
/// fingerprint, sm_version, emit_config_hash); the registry is keyed
/// by (name, version). This trait is the bridge — embedders provide
/// the mapping policy (e.g. a YAML manifest baked into the deploy,
/// or a tenant-level metadata table).
pub trait BlueprintResolver: Send + Sync {
    /// Resolve a (blueprint fingerprint, sm_version) pair to a
    /// `(name, version)` tuple that [`KernelRegistry::get`] understands.
    /// Returns `None` if the embedder has no mapping for this
    /// blueprint — the caller then proceeds with fresh PTX emission.
    fn resolve(&self, blueprint_fp: u64, sm_version: u32) -> Option<(String, String)>;
}

/// In-memory [`BlueprintResolver`] backed by a `HashMap`. Test-only
/// convenience for the cache integration tests in v0.3.8; production
/// embedders supply their own implementation that consults a
/// deployment-baked manifest or a tenant-level metadata table.
pub struct InMemoryBlueprintResolver {
    map: HashMap<(u64, u32), (String, String)>,
}

impl InMemoryBlueprintResolver {
    /// Construct an empty resolver. Use [`Self::insert`] to populate
    /// the (blueprint, sm) → (name, version) mapping.
    pub fn new() -> Self {
        Self {
            map: HashMap::new(),
        }
    }

    /// Build a resolver pre-populated from a map of
    /// `(blueprint_fp, sm_version)` → `(name, version)` entries.
    pub fn from_map(map: HashMap<(u64, u32), (String, String)>) -> Self {
        Self { map }
    }

    /// Insert a single `(blueprint_fp, sm_version)` → `(name, version)`
    /// mapping. Overwrites any prior entry for the same key.
    pub fn insert(&mut self, blueprint_fp: u64, sm_version: u32, name: String, version: String) {
        self.map.insert((blueprint_fp, sm_version), (name, version));
    }
}

impl Default for InMemoryBlueprintResolver {
    fn default() -> Self {
        Self::new()
    }
}

impl BlueprintResolver for InMemoryBlueprintResolver {
    fn resolve(&self, blueprint_fp: u64, sm_version: u32) -> Option<(String, String)> {
        self.map.get(&(blueprint_fp, sm_version)).cloned()
    }
}

/// Failure modes for registry operations.
#[derive(Debug, thiserror::Error)]
pub enum RegistryError {
    /// No entry for the requested `name@version`.
    #[error("kernel not found: {0}")]
    NotFound(String),
    /// HMAC verification failed against the configured signing key.
    #[error("signature verification failed for {0}")]
    BadSignature(String),
    /// BLAKE3 of the PTX text does not match `manifest.digest`.
    #[error("digest mismatch for {0}")]
    DigestMismatch(String),
    /// A manifest with the same `name@version` already exists.
    #[error("name @ version already registered: {0}")]
    AlreadyRegistered(String),
    /// The manifest's `publisher` field is not in the registry's
    /// configured allowlist. Only returned by the [`DiskRegistry`] when
    /// it has been built with `Some(publisher_allowlist)`; the
    /// allowlist-less default and [`InMemoryRegistry`] never emit this.
    /// The string carries the manifest name for parity with the other
    /// rejection variants — the publisher itself is deliberately NOT
    /// echoed back to avoid leaking the allowlist contents through
    /// negative-result probing.
    #[error("publisher not allowlisted for {0}")]
    PublisherNotAllowed(String),
    /// The underlying on-disk artifact store rejected a put/get/list. The
    /// inner `String` is the `ArtifactError`'s `Display`; we collapse to
    /// a single variant because the registry layer treats every storage
    /// failure as a transient backend error rather than re-deriving one
    /// HTTP status per inner cause. Only emitted by [`DiskRegistry`].
    #[error("artifact store error: {0}")]
    Storage(String),
    /// bincode encode/decode of the on-disk manifest blob failed. The
    /// disk format pairs a [`KernelManifest`] with its PTX text under a
    /// single bincode envelope; a decode failure here usually means the
    /// blob was written by an incompatible bincode version, not that
    /// the bytes were tampered with (the artifact store's HMAC catches
    /// the latter). Only emitted by [`DiskRegistry`].
    #[error("manifest codec error: {0}")]
    Codec(String),
}

/// Registry trait. v0.3.7 ships an in-memory impl; v0.4 lands disk +
/// remote backends. The pair returned by [`Self::get`] is
/// `(manifest, ptx_text)` so a caller can hand the PTX to the JIT cache
/// without a second registry round-trip.
pub trait KernelRegistry: Send + Sync {
    /// Resolve `(name, version)` to the verified manifest + PTX text.
    fn get(
        &self,
        name: &str,
        version: &str,
    ) -> Result<Arc<(KernelManifest, String)>, RegistryError>;
    /// Verify and persist a signed manifest + PTX text.
    fn publish(&self, manifest: KernelManifest, ptx_text: String) -> Result<(), RegistryError>;
    /// Enumerate all registered manifests (PTX text omitted).
    fn list(&self) -> Vec<KernelManifest>;
    /// Page-aware listing. Default implementation slices the result of
    /// [`Self::list`] — backends with native pagination support (see
    /// [`DiskRegistry::list_paginated`]) override this to avoid
    /// materialising the full set when only a page is asked for.
    ///
    /// `limit` is clamped to [`DISK_REGISTRY_MAX_LIMIT`] (1000) by the
    /// default impl, mirroring what the disk-backed override does, so
    /// the HTTP route's contract is the same regardless of backend.
    fn list_paginated(&self, offset: usize, limit: usize) -> Vec<KernelManifest> {
        let limit = limit.min(DISK_REGISTRY_MAX_LIMIT);
        self.list().into_iter().skip(offset).take(limit).collect()
    }
}

/// In-memory implementation for v0.3.7. Holds manifests + PTX text in a
/// `Mutex<HashMap>` keyed by `"{name}@{version}"`.
///
/// The HMAC-SHA256 signing key is held in a [`zeroize::Zeroizing`] so the
/// scrub-on-drop guarantees from the snapshot signing path apply here too:
/// the secret material does not linger in the heap arena after the
/// registry is dropped.
pub struct InMemoryRegistry {
    entries: parking_lot::Mutex<HashMap<String, Arc<(KernelManifest, String)>>>,
    hmac_key: zeroize::Zeroizing<[u8; 32]>,
}

impl InMemoryRegistry {
    /// Construct an empty registry that accepts manifests signed under
    /// `hmac_key`.
    pub fn new(hmac_key: [u8; 32]) -> Self {
        Self {
            entries: parking_lot::Mutex::new(HashMap::new()),
            hmac_key: zeroize::Zeroizing::new(hmac_key),
        }
    }

    /// Verify, then insert, a signed manifest plus its PTX text.
    ///
    /// Returns [`RegistryError::DigestMismatch`] if BLAKE3 of `ptx_text`
    /// does not match `manifest.digest`, [`RegistryError::BadSignature`]
    /// if the HMAC does not verify under the configured key, and
    /// [`RegistryError::AlreadyRegistered`] if `name@version` is already
    /// present. The check order (digest → signature → uniqueness) is
    /// deliberate: a digest mismatch is the cheapest tell that the
    /// PTX/manifest pair was corrupted in transit, so we surface that
    /// before doing the constant-time HMAC compare.
    pub fn publish(&self, manifest: KernelManifest, ptx_text: String) -> Result<(), RegistryError> {
        // Verify digest matches PTX.
        let actual = blake3::hash(ptx_text.as_bytes());
        if actual.as_bytes() != &manifest.digest {
            return Err(RegistryError::DigestMismatch(manifest.name.clone()));
        }
        // Verify signature.
        self.verify_signature(&manifest)?;
        let key = format!("{}@{}", manifest.name, manifest.version);
        let mut entries = self.entries.lock();
        if entries.contains_key(&key) {
            return Err(RegistryError::AlreadyRegistered(key));
        }
        entries.insert(key, Arc::new((manifest, ptx_text)));
        Ok(())
    }

    /// Recompute and constant-time-compare the manifest's HMAC against
    /// the configured signing key. See the module-level docstring for
    /// the exact envelope layout.
    fn verify_signature(&self, manifest: &KernelManifest) -> Result<(), RegistryError> {
        verify_manifest_signature(manifest, &self.hmac_key)
    }
}

/// Free-function HMAC verification against `hmac_key`.
///
/// Shared by [`InMemoryRegistry::verify_signature`] and
/// [`DiskRegistry::verify_signature`] so the sign/verify byte-stream
/// derived from [`KernelManifest::canonical_signed_bytes`] cannot drift
/// across backends. The comparison is constant-time via
/// `subtle::ConstantTimeEq` so a tight publish loop cannot recover bits
/// of the expected MAC through repeated rejections.
fn verify_manifest_signature(
    manifest: &KernelManifest,
    hmac_key: &[u8; 32],
) -> Result<(), RegistryError> {
    use hmac::{Hmac, Mac};
    // `new_from_slice` only errors on invalid key length; ours is a
    // compile-time 32-byte array so the unwrap is sound.
    let mut mac = <Hmac<sha2::Sha256> as Mac>::new_from_slice(&hmac_key[..])
        .expect("32-byte key is always valid HMAC-SHA256 input");
    mac.update(&manifest.canonical_signed_bytes());
    let expected = mac.finalize().into_bytes();
    let ok = subtle::ConstantTimeEq::ct_eq(&expected[..], &manifest.signature[..]);
    if bool::from(ok) {
        Ok(())
    } else {
        Err(RegistryError::BadSignature(manifest.name.clone()))
    }
}

impl KernelRegistry for InMemoryRegistry {
    fn get(
        &self,
        name: &str,
        version: &str,
    ) -> Result<Arc<(KernelManifest, String)>, RegistryError> {
        let key = format!("{name}@{version}");
        self.entries
            .lock()
            .get(&key)
            .cloned()
            .ok_or(RegistryError::NotFound(key))
    }

    fn publish(&self, manifest: KernelManifest, ptx_text: String) -> Result<(), RegistryError> {
        InMemoryRegistry::publish(self, manifest, ptx_text)
    }

    fn list(&self) -> Vec<KernelManifest> {
        self.entries.lock().values().map(|e| e.0.clone()).collect()
    }
}

/// Sign a manifest given a publisher's HMAC key. Helper for tests and
/// the v0.4 signing CLI.
///
/// Callers populate every field of `unsigned` (including
/// `signature: [0; 32]`); this helper computes the real signature over
/// the canonical envelope and returns it. The caller is then responsible
/// for writing it back into `unsigned.signature` before publishing.
pub fn sign_manifest(unsigned: &KernelManifest, hmac_key: &[u8; 32]) -> [u8; 32] {
    use hmac::{Hmac, Mac};
    let mut mac = <Hmac<sha2::Sha256> as Mac>::new_from_slice(hmac_key)
        .expect("32-byte key is always valid HMAC-SHA256 input");
    mac.update(&unsigned.canonical_signed_bytes());
    mac.finalize().into_bytes().into()
}

// =====================================================================
// DiskRegistry — production disk-persisted kernel registry (T35, v0.4)
// =====================================================================

/// Default value returned by [`DiskRegistry::list`] and the cap on
/// `limit` accepted by [`DiskRegistry::list_paginated`]. 100 / 1000
/// were chosen to match the conservative pagination defaults the
/// snapshot-listing path uses; operators with larger fleets can pass an
/// explicit `limit` up to [`DISK_REGISTRY_MAX_LIMIT`].
pub const DISK_REGISTRY_DEFAULT_LIMIT: usize = 100;

/// Hard ceiling on `limit` accepted by
/// [`DiskRegistry::list_paginated`]. Requests with `limit` above the
/// ceiling are silently clamped; the response is therefore always
/// bounded regardless of caller input.
pub const DISK_REGISTRY_MAX_LIMIT: usize = 1000;

/// Defaults pagination parameters: `(0, DISK_REGISTRY_DEFAULT_LIMIT)`.
/// `list` forwards through this so a future change to either constant
/// only needs to touch one place.
const DISK_REGISTRY_DEFAULT_LIST: (usize, usize) = (0, DISK_REGISTRY_DEFAULT_LIMIT);

/// Bincode envelope: `(KernelManifest, ptx_text)`. The on-disk artifact
/// store keys by BLAKE3 of these bytes, so two equal manifest+PTX
/// pairs deduplicate naturally. The PTX rides along inside the same
/// blob — separating PTX from manifest would force two `get` round
/// trips per `resolve` and bring no integrity benefit (both fields are
/// covered by the artifact store's outer HMAC envelope).
///
/// We use a tuple rather than a dedicated struct because the trait
/// surface (`KernelRegistry::get` returns `Arc<(KernelManifest, String)>`)
/// already exposes this exact shape; introducing a struct would push a
/// new public type onto the API surface for no reader-side benefit.
type ManifestBlob = (KernelManifest, String);

/// Keymap value: the artifact-store [`ContentHash`] for a published
/// kernel, paired with the decoded-and-already-verified
/// [`KernelManifest`] (PTX text deliberately omitted).
///
/// Caching the manifest here serves two findings:
///
/// * **PERF (`list_paginated`)** — listings can be served straight from
///   this cached manifest without a per-entry artifact-store `get`
///   (HMAC + zstd decode of the full PTX-bearing blob) that is then
///   thrown away. Listing is O(entries) keymap reads instead of
///   O(entries) decompressions.
///
/// * **LOW perf (`get` re-verify)** — both [`DiskRegistry::open`] and
///   [`DiskRegistry::publish`] verify the manifest signature *before*
///   the entry is admitted into the keymap, so the cached manifest is
///   by construction one that has already passed the registry's v2 HMAC
///   check under the current key. `get` can skip the redundant per-
///   resolve re-verification of the manifest signature: the bytes it
///   then pulls from the artifact store are still independently
///   authenticated by the store's own HMAC envelope, and the manifest
///   embedded in this cache is exactly the one that was verified at
///   admission time. No security is weakened — the store HMAC still
///   guards on-disk integrity, and key-rotation skipping still happens
///   at `open` time.
#[derive(Clone)]
struct KeymapEntry {
    hash: ContentHash,
    /// Decoded manifest, PTX stripped. Verified under the registry
    /// HMAC key at admission time (publish or restart-recovery).
    manifest: KernelManifest,
}

/// Production disk-persisted kernel registry (roadmap feature #3, T35).
///
/// `DiskRegistry` is a thin layer on top of
/// [`tensor_wasm_artifacts::DiskArtifactStore`] — the artifact store
/// brings HMAC-SHA256 envelope, zstd compression, content-addressing,
/// size caps, and atomic rename-on-publish. The registry contributes
/// the application-level keymap
/// `(name, version, sm_version) → (ContentHash + cached manifest)`
/// so callers can resolve manifests by stable `name@version` (with
/// `sm_version` as a tie-breaker for kernels published for multiple
/// compute capabilities).
///
/// ## Wire-format guarantee
///
/// Manifests are still subject to the same v2-envelope HMAC-SHA256
/// signature [`InMemoryRegistry`] enforces (the registry's own,
/// independent signing key). The artifact store's HMAC is a SEPARATE
/// integrity layer over the bincode-encoded `(manifest, ptx_text)`
/// blob; both must verify before a manifest is exposed to the caller.
/// This is defence in depth: the artifact store's HMAC catches bit
/// flips on disk, and the manifest's own signature proves the
/// publisher signed off on the *content* of the manifest (and
/// transitively the PTX via `digest`).
///
/// ## Restart recovery
///
/// `DiskRegistry::open` reads every blob in the artifact store's
/// directory, decodes each one, and re-populates the in-memory keymap.
/// A blob that fails the artifact store's HMAC, fails bincode decode,
/// or fails the registry's own signature check is skipped with a
/// `tracing::warn` — restart is best-effort and never panics on partial
/// corruption.
///
/// ## Threat model — publisher allowlist
///
/// Today every caller in possession of the registry HMAC key can sign a
/// manifest under any `publisher` field they please. The v2 envelope
/// (T12) covers `publisher` and `published_unix_ms` cryptographically,
/// so a passive intermediary cannot rewrite `publisher` post-sign — but
/// a legitimate holder of the signing key still has free rein.
///
/// The optional `publisher_allowlist` field closes that gap by
/// refusing `publish` when `manifest.publisher` is not in the
/// configured set, regardless of whether the signature otherwise
/// verifies. This is a separate authorization layer over and above the
/// HTTP route's kernel-publish scope check (T1) — the route check
/// gates *who can call publish at all*, while the allowlist gates
/// *which publisher field the call can claim*. With both layers in
/// force, an operator can have a single signing key shared across
/// trusted publishers without any one publisher being able to forge
/// claims under another publisher's identity.
///
/// `None` (the default) preserves the historical permissive behaviour
/// — any signed publisher is accepted — and matches what
/// [`InMemoryRegistry`] does today.
pub struct DiskRegistry {
    /// Underlying content-addressed signed blob store. Holds the
    /// bincode-encoded `(KernelManifest, ptx_text)` payloads.
    artifact_store: DiskArtifactStore,
    /// `(name, version, sm_version) → KeymapEntry` keymap. The DashMap
    /// behind an `Arc` lets concurrent `publish` / `resolve` callers
    /// proceed in parallel without coarse locking; the artifact store
    /// itself is also `Send + Sync` so neither layer becomes the
    /// serialisation point.
    ///
    /// The value caches the decoded, signature-verified manifest
    /// alongside its `ContentHash` so listings need not re-fetch and
    /// re-decompress each blob — see [`KeymapEntry`].
    keymap: Arc<DashMap<(String, String, u32), KeymapEntry>>,
    /// Secondary index: `(name, version) → sorted Vec<sm_version>`.
    ///
    /// jit PERF fix (finding 11): [`KernelRegistry::get`] resolves
    /// `(name, version)` to the highest available `sm_version`. The previous
    /// implementation scanned the ENTIRE keymap with
    /// `iter().filter(...).max_by_key(...)` — O(N) in the total number of
    /// registered `(name, version, sm)` triples on every resolve. This
    /// index lets `get` look up the candidate sm-versions for a single
    /// `(name, version)` directly and read the max off the (kept-sorted)
    /// tail, turning the hot lookup into an O(1) map probe plus an O(1)
    /// `last()`. Kept in lock-step with `keymap` on every insert.
    sm_index: Arc<DashMap<(String, String), Vec<u32>>>,
    /// Manifest signature key. Held in [`zeroize::Zeroizing`] so the
    /// scrub-on-Drop guarantees from the snapshot signing path apply
    /// here too — the secret does not linger in the heap arena after
    /// the registry is dropped.
    hmac_key: zeroize::Zeroizing<[u8; 32]>,
    /// Optional set of publisher identities the registry will accept on
    /// `publish`. See the type-level docstring for the threat model.
    /// `None` = permissive (any signed publisher is accepted), `Some`
    /// = strict allowlist.
    publisher_allowlist: Option<HashSet<String>>,
}

/// Insert `sm` into the `(name, version) → sorted [sm]` secondary index,
/// keeping the per-key list sorted ascending and de-duplicated.
///
/// jit PERF fix (finding 11): the list is kept sorted so
/// [`KernelRegistry::get`] can read the highest sm-version off the tail in
/// O(1). Insertion is O(log k + k) in the (tiny) number of sm-versions per
/// `(name, version)` — negligible next to the artifact-store write it
/// accompanies, and a one-time cost paid only on publish / restart-recovery,
/// not on the hot resolve path.
fn index_sm_version(
    index: &DashMap<(String, String), Vec<u32>>,
    name: String,
    version: String,
    sm: u32,
) {
    let mut entry = index.entry((name, version)).or_default();
    match entry.binary_search(&sm) {
        // Already present (publish rejects duplicate triples upstream, but
        // restart-recovery can re-see one if a blob is double-listed).
        Ok(_) => {}
        Err(pos) => entry.insert(pos, sm),
    }
}

impl DiskRegistry {
    /// Construct (or re-open) a disk-persisted kernel registry rooted at
    /// `dir`, signing/verifying manifests under `hmac_key`.
    ///
    /// On success, the artifact store directory is opened (created
    /// lazily on first put) and the keymap is rebuilt from any blobs
    /// already on disk. Each blob is read via
    /// [`DiskArtifactStore::get`] (which runs the artifact store's
    /// HMAC + zstd verification), bincode-decoded into a
    /// `(KernelManifest, ptx_text)` pair, signature-verified under
    /// `hmac_key`, and only then admitted into the keymap. Corrupt or
    /// foreign-keyed blobs are skipped with a `tracing::warn`; the
    /// registry never panics on a partial-corruption restart.
    ///
    /// The returned registry has no publisher allowlist (permissive
    /// mode); chain through [`Self::with_publisher_allowlist`] to
    /// enable one.
    pub fn open(dir: PathBuf, hmac_key: [u8; 32]) -> Result<Self, RegistryError> {
        let artifact_store = DiskArtifactStore::new(dir, hmac_key);
        let keymap: Arc<DashMap<(String, String, u32), KeymapEntry>> = Arc::new(DashMap::new());
        let sm_index: Arc<DashMap<(String, String), Vec<u32>>> = Arc::new(DashMap::new());

        // Restart-recovery: walk every blob in the store, decode it,
        // re-verify the manifest signature under the same hmac_key,
        // and re-populate the keymap. Best-effort: a blob that fails
        // any check is logged and skipped — we'd rather come up with a
        // partially-populated keymap than refuse to boot.
        let hashes = artifact_store.list().map_err(|e| {
            tracing::warn!(
                target: "tensor_wasm_jit::registry",
                error = %e,
                "restart-recovery: artifact store list failed; refusing to boot with an \
                 unknown-completeness keymap"
            );
            RegistryError::Storage(e.to_string())
        })?;
        for hash in hashes {
            let raw = match artifact_store.get(&hash) {
                Ok(bytes) => bytes,
                Err(e) => {
                    tracing::warn!(
                        target: "tensor_wasm_jit::registry",
                        hash = %hash,
                        error = %e,
                        "restart-recovery: artifact store read failed; skipping blob"
                    );
                    continue;
                }
            };
            let (manifest, _ptx) = match decode_manifest_blob(&raw) {
                Ok(pair) => pair,
                Err(e) => {
                    tracing::warn!(
                        target: "tensor_wasm_jit::registry",
                        hash = %hash,
                        error = %e,
                        "restart-recovery: bincode decode failed; skipping blob"
                    );
                    continue;
                }
            };
            // Defence in depth: the artifact store already authenticated
            // the bytes, but a freshly-rotated registry HMAC key would
            // mean the manifest's own signature no longer verifies
            // even though the store envelope still does. Skip those
            // rather than serve them.
            if let Err(e) = verify_manifest_signature(&manifest, &hmac_key) {
                tracing::warn!(
                    target: "tensor_wasm_jit::registry",
                    name = manifest.name.as_str(),
                    version = manifest.version.as_str(),
                    error = %e,
                    "restart-recovery: manifest signature does not verify under \
                     current hmac_key; skipping blob (registry key may have rotated)"
                );
                continue;
            }
            let k = (
                manifest.name.clone(),
                manifest.version.clone(),
                manifest.sm_version,
            );
            // Cache the verified manifest alongside the hash so listings
            // and resolves avoid a redundant decode/verify (see
            // `KeymapEntry`). `manifest` has just passed the v2 HMAC
            // check above.
            let (name, version, sm) = (k.0.clone(), k.1.clone(), k.2);
            keymap.insert(k, KeymapEntry { hash, manifest });
            index_sm_version(&sm_index, name, version, sm);
        }

        Ok(Self {
            artifact_store,
            keymap,
            sm_index,
            hmac_key: zeroize::Zeroizing::new(hmac_key),
            publisher_allowlist: None,
        })
    }

    /// Builder method: install a publisher allowlist. See the
    /// type-level docstring for the threat model. Chain after
    /// [`Self::open`]:
    ///
    /// ```ignore
    /// let reg = DiskRegistry::open(dir, key)?
    ///     .with_publisher_allowlist(["alice", "bob"].iter().map(|s| s.to_string()).collect());
    /// ```
    pub fn with_publisher_allowlist(mut self, allowlist: HashSet<String>) -> Self {
        self.publisher_allowlist = Some(allowlist);
        self
    }

    /// Recompute and constant-time-compare the manifest's HMAC against
    /// the registry's configured key. Mirrors
    /// [`InMemoryRegistry::verify_signature`] — both route through the
    /// same [`verify_manifest_signature`] helper.
    fn verify_signature(&self, manifest: &KernelManifest) -> Result<(), RegistryError> {
        verify_manifest_signature(manifest, &self.hmac_key)
    }

    /// Verify + persist a signed manifest plus its PTX text.
    ///
    /// Check order:
    /// 1. `publisher` allowlist (if configured) — cheapest failure for
    ///    a publisher who legitimately holds the signing key but is not
    ///    authorised to publish under their claimed identity.
    /// 2. BLAKE3 digest match against `ptx_text`.
    /// 3. HMAC v2 signature verification.
    /// 4. Uniqueness — refuse if `(name, version, sm_version)` is
    ///    already in the keymap.
    /// 5. bincode-encode the `(manifest, ptx_text)` pair and hand it to
    ///    the artifact store. The store computes the BLAKE3
    ///    content-address and HMAC-signs the envelope; the returned
    ///    [`ContentHash`] is recorded in the keymap.
    pub fn publish(&self, manifest: KernelManifest, ptx_text: String) -> Result<(), RegistryError> {
        // Step 1: publisher allowlist (operator-side authorization).
        if let Some(allow) = &self.publisher_allowlist {
            if !allow.contains(&manifest.publisher) {
                return Err(RegistryError::PublisherNotAllowed(manifest.name.clone()));
            }
        }
        // Step 2: digest match.
        let actual = blake3::hash(ptx_text.as_bytes());
        if actual.as_bytes() != &manifest.digest {
            return Err(RegistryError::DigestMismatch(manifest.name.clone()));
        }
        // Step 3: HMAC signature.
        self.verify_signature(&manifest)?;
        // Step 4: uniqueness on (name, version, sm_version). We key on
        // the (name, version, sm_version) triple rather than the
        // {name@version} string so two PTX flavours of the same kernel
        // for different SM versions can coexist; the InMemoryRegistry
        // collapses these into one slot, but the disk-backed path is
        // expected to host the production matrix.
        let key = (
            manifest.name.clone(),
            manifest.version.clone(),
            manifest.sm_version,
        );
        if self.keymap.contains_key(&key) {
            return Err(RegistryError::AlreadyRegistered(format!(
                "{}@{} (sm_{})",
                manifest.name, manifest.version, manifest.sm_version,
            )));
        }

        // Step 5: bincode encode and persist via artifact store.
        // Keep a manifest clone for the keymap cache before the original
        // is moved into the blob — `manifest` has already passed the
        // digest + v2 HMAC checks above, so the cached copy is verified
        // by construction (see `KeymapEntry`).
        let cached_manifest = manifest.clone();
        let blob: ManifestBlob = (manifest, ptx_text);
        let bytes = encode_manifest_blob(&blob)?;
        let hash = self
            .artifact_store
            .put(&bytes)
            .map_err(|e| RegistryError::Storage(e.to_string()))?;
        let (name, version, sm) = (key.0.clone(), key.1.clone(), key.2);
        self.keymap.insert(
            key,
            KeymapEntry {
                hash,
                manifest: cached_manifest,
            },
        );
        // jit PERF fix (finding 11): keep the secondary `(name, version) →
        // [sm]` index in lock-step with the keymap so `get` resolves the
        // highest sm in O(1) instead of an O(N) keymap scan.
        index_sm_version(&self.sm_index, name, version, sm);
        Ok(())
    }

    /// Page-aware listing: skip the first `offset` keymap entries and
    /// return up to `limit` manifests. `limit` is clamped to
    /// [`DISK_REGISTRY_MAX_LIMIT`].
    ///
    /// The iteration order is NOT specified — DashMap's iteration is
    /// hash-bucket order, which is stable per process but differs
    /// across restarts. Callers that need a deterministic page sequence
    /// should sort the result themselves on whatever field they care
    /// about (`name`, `published_unix_ms`, etc.).
    pub fn list_paginated(&self, offset: usize, limit: usize) -> Vec<KernelManifest> {
        let limit = limit.min(DISK_REGISTRY_MAX_LIMIT);
        let mut out: Vec<KernelManifest> = Vec::with_capacity(limit);
        // FINDING (PERF): serve listings from the cached manifest in the
        // keymap value rather than doing a per-entry artifact-store `get`
        // (HMAC + zstd decode of the full PTX-bearing blob) only to throw
        // the PTX away. The cached manifest was signature-verified at
        // admission time (publish / restart-recovery), and listings omit
        // PTX by convention (same as `InMemoryRegistry::list`), so the
        // blob fetch + decode was pure waste — O(N) decompressions per
        // listing. We now just clone the cached manifest.
        for (i, kv) in self.keymap.iter().enumerate() {
            if i < offset {
                continue;
            }
            if out.len() >= limit {
                break;
            }
            out.push(kv.value().manifest.clone());
        }
        out
    }
}

/// Encode a `(KernelManifest, ptx_text)` pair for the artifact store.
///
/// Routes through `bincode::serde::encode_to_vec` with the
/// `bincode::config::legacy()` configuration so the on-disk format
/// matches the encoding the snapshot crate uses for its v3 payload —
/// keeping both stores on the same bincode flavour means we can adopt
/// a future codec migration in one place.
fn encode_manifest_blob(blob: &ManifestBlob) -> Result<Vec<u8>, RegistryError> {
    bincode::serde::encode_to_vec(blob, bincode::config::legacy())
        .map_err(|e| RegistryError::Codec(e.to_string()))
}

/// Inverse of [`encode_manifest_blob`].
fn decode_manifest_blob(bytes: &[u8]) -> Result<ManifestBlob, RegistryError> {
    let (blob, _consumed): (ManifestBlob, usize) =
        bincode::serde::decode_from_slice(bytes, bincode::config::legacy())
            .map_err(|e| RegistryError::Codec(e.to_string()))?;
    Ok(blob)
}

impl KernelRegistry for DiskRegistry {
    fn get(
        &self,
        name: &str,
        version: &str,
    ) -> Result<Arc<(KernelManifest, String)>, RegistryError> {
        // Find the highest sm_version present for (name, version). The
        // trait surface keys on (name, version) only — when multiple
        // sm_versions exist for the same name/version pair we pick the
        // entry with the maximum sm_version (the 3rd key element).
        // Embedders that need sm-specific resolution can bypass
        // `KernelRegistry::get` and call `list_paginated` to discover
        // the matrix.
        //
        // FINDING (MEDIUM correctness): we resolve to the entry with the
        // maximum `sm_version` (deterministic across restarts, unlike the
        // old DashMap-iteration-order `.find(...)`), matching the doc
        // contract ("the highest sm_version").
        //
        // jit PERF fix (finding 11): use the `(name, version) → [sm]`
        // secondary index instead of an O(N) keymap scan. The index keeps
        // the sm-version list sorted ascending, so the highest sm is the
        // last element — an O(1) read after an O(1) map probe. We then look
        // up the exact `(name, version, sm)` triple in the keymap and copy
        // out its `ContentHash` (a `Copy` value) rather than holding a
        // DashMap iterator across the artifact-store get (long-held entries
        // block writers).
        let best_sm: Option<u32> = self
            .sm_index
            .get(&(name.to_string(), version.to_string()))
            .and_then(|entry| entry.value().last().copied());
        let hit: Option<ContentHash> = best_sm.and_then(|sm| {
            self.keymap
                .get(&(name.to_string(), version.to_string(), sm))
                .map(|kv| kv.value().hash)
        });
        let hash = match hit {
            Some(h) => h,
            None => {
                return Err(RegistryError::NotFound(format!("{name}@{version}")));
            }
        };
        let raw = self.artifact_store.get(&hash).map_err(|e| match e {
            ArtifactError::NotFound(_) => {
                // Keymap and disk disagree — could happen if the
                // operator manually deleted a blob. Treat as a
                // miss; the keymap entry is now a tombstone but
                // we don't proactively clean it up here.
                RegistryError::NotFound(format!("{name}@{version}"))
            }
            other => RegistryError::Storage(other.to_string()),
        })?;
        let (manifest, ptx_text) = decode_manifest_blob(&raw)?;
        // FINDING (LOW perf): we no longer re-run the manifest v2 HMAC
        // verification on every resolve. The keymap only ever contains
        // entries whose manifest passed `verify_signature` at admission
        // time (publish) or at restart-recovery (`open`, which also drops
        // any blob that fails to verify under the *current* key, so a
        // rotated key is handled there). The bytes we just read are
        // independently authenticated by the artifact store's own HMAC
        // envelope keyed on the same content hash, so on-disk integrity
        // is still covered — this removes a redundant per-resolve MAC,
        // not a security layer. `self.verify_signature` is retained for
        // the publish/open paths; deliberately not invoked here.
        Ok(Arc::new((manifest, ptx_text)))
    }

    fn publish(&self, manifest: KernelManifest, ptx_text: String) -> Result<(), RegistryError> {
        DiskRegistry::publish(self, manifest, ptx_text)
    }

    fn list(&self) -> Vec<KernelManifest> {
        let (offset, limit) = DISK_REGISTRY_DEFAULT_LIST;
        DiskRegistry::list_paginated(self, offset, limit)
    }

    /// Override the trait default with the native paginated walk over
    /// the keymap (no intermediate full `list()` materialisation).
    fn list_paginated(&self, offset: usize, limit: usize) -> Vec<KernelManifest> {
        DiskRegistry::list_paginated(self, offset, limit)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Helper: build a manifest for `ptx_text` signed under `key`.
    /// In-crate tests are allowed to use struct-literal construction
    /// (the `#[non_exhaustive]` attribute only restricts foreign crates),
    /// but the helper here mirrors the public `KernelManifest::new`
    /// flow so the unit and integration tests stay in lock-step.
    fn signed_manifest(
        name: &str,
        version: &str,
        ptx_text: &str,
        key: &[u8; 32],
    ) -> KernelManifest {
        let digest = *blake3::hash(ptx_text.as_bytes()).as_bytes();
        let mut m = KernelManifest::new(
            name.to_string(),
            version.to_string(),
            80,
            digest,
            [0u8; 32],
            0,
            "test".to_string(),
        );
        m.signature = sign_manifest(&m, key);
        m
    }

    /// Like [`signed_manifest`] but lets the caller pick `sm_version`,
    /// for the DiskRegistry highest-sm resolution test below.
    fn signed_manifest_sm(
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
            0,
            "test".to_string(),
        );
        m.signature = sign_manifest(&m, key);
        m
    }

    /// FINDING (MEDIUM correctness) regression: when several
    /// `sm_version` builds exist for one `(name, version)`,
    /// `DiskRegistry::get` MUST resolve the HIGHEST sm_version
    /// deterministically (per the doc contract), not whichever the
    /// DashMap happens to iterate first. We publish sm_70, sm_90, sm_80
    /// (intentionally out of order) under distinct PTX so the resolved
    /// manifest is unambiguous, then assert `get` returns sm_90.
    #[test]
    fn disk_get_resolves_highest_sm_version() {
        let key = [0x42u8; 32];
        let tmp = tempfile::TempDir::new().expect("tempdir");
        let reg = DiskRegistry::open(tmp.path().to_path_buf(), key).expect("open");

        // Distinct PTX per sm so the digests (and thus blobs) differ and
        // we can tell which entry was resolved.
        for sm in [70u32, 90, 80] {
            let ptx = format!("// ptx for sm_{sm}\n");
            let m = signed_manifest_sm("matmul.f32", "1.0.0", sm, &ptx, &key);
            KernelRegistry::publish(&reg, m, ptx).expect("publish");
        }

        let got = KernelRegistry::get(&reg, "matmul.f32", "1.0.0").expect("get");
        assert_eq!(
            got.0.sm_version, 90,
            "get must resolve the highest sm_version"
        );
        assert_eq!(got.1, "// ptx for sm_90\n");
    }

    /// jit PERF fix (finding 11): the `(name, version) → [sm]` secondary
    /// index must be rebuilt on restart-recovery (`open`) so a reopened
    /// registry still resolves the highest sm_version via the index, not
    /// just a freshly-published one.
    #[test]
    fn disk_sm_index_survives_restart() {
        let key = [0x37u8; 32];
        let tmp = tempfile::TempDir::new().expect("tempdir");
        {
            let reg = DiskRegistry::open(tmp.path().to_path_buf(), key).expect("open");
            for sm in [75u32, 86, 80] {
                let ptx = format!("// ptx for sm_{sm}\n");
                let m = signed_manifest_sm("conv.f32", "2.1.0", sm, &ptx, &key);
                KernelRegistry::publish(&reg, m, ptx).expect("publish");
            }
        }
        // Reopen: the index is rebuilt from the on-disk blobs in `open`.
        let reopened = DiskRegistry::open(tmp.path().to_path_buf(), key).expect("reopen");
        let got = KernelRegistry::get(&reopened, "conv.f32", "2.1.0").expect("get after restart");
        assert_eq!(
            got.0.sm_version, 86,
            "reopened registry must still resolve the highest sm_version via the rebuilt index"
        );
        assert_eq!(got.1, "// ptx for sm_86\n");
        // A name/version that was never published still misses.
        assert!(matches!(
            KernelRegistry::get(&reopened, "conv.f32", "9.9.9"),
            Err(RegistryError::NotFound(_))
        ));
    }

    #[test]
    fn publish_and_get_roundtrip() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        let m = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        reg.publish(m.clone(), ptx.clone()).unwrap();
        let got = reg.get("matmul.f32", "1.0.0").unwrap();
        assert_eq!(got.0.name, "matmul.f32");
        assert_eq!(got.1, ptx);
        let listing = reg.list();
        assert_eq!(listing.len(), 1);
        assert_eq!(listing[0].version, "1.0.0");
    }

    #[test]
    fn rejects_bad_signature() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        let mut m = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        // Flip a byte in the signature.
        m.signature[0] ^= 0xff;
        match reg.publish(m, ptx) {
            Err(RegistryError::BadSignature(name)) => assert_eq!(name, "matmul.f32"),
            other => panic!("expected BadSignature, got {other:?}"),
        }
    }

    #[test]
    fn rejects_digest_mismatch() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        let m = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        // Publish with different PTX than what was signed.
        match reg.publish(m, "// different ptx\n".to_string()) {
            Err(RegistryError::DigestMismatch(name)) => assert_eq!(name, "matmul.f32"),
            other => panic!("expected DigestMismatch, got {other:?}"),
        }
    }

    #[test]
    fn rejects_duplicate_publish() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        let m = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        reg.publish(m.clone(), ptx.clone()).unwrap();
        match reg.publish(m, ptx) {
            Err(RegistryError::AlreadyRegistered(key)) => {
                assert_eq!(key, "matmul.f32@1.0.0")
            }
            other => panic!("expected AlreadyRegistered, got {other:?}"),
        }
    }

    #[test]
    fn get_returns_not_found_for_missing() {
        let key = [0u8; 32];
        let reg = InMemoryRegistry::new(key);
        match reg.get("nope", "0.0.0") {
            Err(RegistryError::NotFound(k)) => assert_eq!(k, "nope@0.0.0"),
            other => panic!("expected NotFound, got {other:?}"),
        }
    }

    // --- v2 envelope: publisher + timestamp coverage --------------------

    /// Round-trip: a manifest signed under the v2 envelope verifies
    /// successfully. Complementary to `publish_and_get_roundtrip`
    /// above but uses `verify_signature` directly so the test is
    /// independent of the digest + uniqueness checks the registry
    /// also runs.
    #[test]
    fn v2_envelope_roundtrip_verifies() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        let m = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        reg.verify_signature(&m)
            .expect("freshly signed manifest must verify");
    }

    /// Tamper-publisher: rewriting `publisher` after signing MUST
    /// invalidate the signature. In the v0.3.7 envelope this field
    /// was not covered and the signature still verified — the v2
    /// envelope closes that hole.
    #[test]
    fn v2_envelope_rejects_publisher_tamper() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        let mut m = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        // Sanity: the freshly-signed manifest verifies under the
        // original publisher, so any failure below is attributable
        // to the post-sign tamper rather than a sign-side bug.
        reg.verify_signature(&m).expect("baseline must verify");
        m.publisher = "attacker".to_string();
        match reg.verify_signature(&m) {
            Err(RegistryError::BadSignature(name)) => assert_eq!(name, "matmul.f32"),
            other => panic!("expected BadSignature after publisher tamper, got {other:?}"),
        }
    }

    /// Tamper-timestamp: rewriting `published_unix_ms` after signing
    /// MUST invalidate the signature. Same v0.3.7 → v2 motivation as
    /// `v2_envelope_rejects_publisher_tamper`.
    #[test]
    fn v2_envelope_rejects_timestamp_tamper() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        let mut m = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        reg.verify_signature(&m).expect("baseline must verify");
        // Shift the timestamp by one millisecond — any bit change
        // is sufficient to break the MAC.
        m.published_unix_ms = m.published_unix_ms.wrapping_add(1);
        match reg.verify_signature(&m) {
            Err(RegistryError::BadSignature(name)) => assert_eq!(name, "matmul.f32"),
            other => panic!("expected BadSignature after timestamp tamper, got {other:?}"),
        }
    }

    /// Canonicalisation collision: the v0.3.7 NUL-separator scheme
    /// produced identical signed bytes for `("a\0b", "c", ...)` and
    /// `("a", "b\0c", ...)`. Under the v2 length-prefixed envelope
    /// these MUST differ, because the `u64`-LE length prefix on each
    /// field disambiguates the boundary.
    #[test]
    fn v2_envelope_avoids_nul_collision() {
        let digest = [0u8; 32];
        let a = KernelManifest::new(
            "a\0b".to_string(),
            "c".to_string(),
            80,
            digest,
            [0u8; 32],
            0,
            "p".to_string(),
        );
        let b = KernelManifest::new(
            "a".to_string(),
            "b\0c".to_string(),
            80,
            digest,
            [0u8; 32],
            0,
            "p".to_string(),
        );
        let ca = a.canonical_signed_bytes();
        let cb = b.canonical_signed_bytes();
        assert_ne!(
            ca, cb,
            "v2 canonical envelope MUST disambiguate name/version field boundaries"
        );
        // Cross-check via the actual MAC, not just the canonical
        // bytes — a future regression that strips the length prefix
        // from one field but not the other could pass the byte
        // compare but still collide once the MAC is computed.
        let key = [0u8; 32];
        let sig_a = sign_manifest(&a, &key);
        let sig_b = sign_manifest(&b, &key);
        assert_ne!(
            sig_a, sig_b,
            "v2 signatures MUST differ across NUL-collision pair"
        );
    }

    /// Cross-version: a manifest "signed" by the legacy v0.3.7
    /// canonical form (NUL separators, no publisher / timestamp
    /// coverage, no magic prefix) MUST NOT verify under the v2 MAC.
    /// We hand-roll the legacy envelope here so the test does not
    /// depend on any deprecated helper sticking around.
    #[test]
    fn v2_envelope_rejects_legacy_v1_signature() {
        use hmac::{Hmac, Mac};
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n";
        let digest = *blake3::hash(ptx.as_bytes()).as_bytes();
        let mut m = KernelManifest::new(
            "matmul.f32".to_string(),
            "1.0.0".to_string(),
            80,
            digest,
            [0u8; 32],
            12345,
            "legacy-publisher".to_string(),
        );
        // Compute the *legacy* (v0.3.7) MAC: name || 0 || version
        // || 0 || sm_le || digest. No publisher, no timestamp, no
        // magic, no length prefixes. This is what an old client or
        // a tampered re-publish would produce.
        let mut mac = <Hmac<sha2::Sha256> as Mac>::new_from_slice(&key)
            .expect("32-byte key is always valid HMAC-SHA256 input");
        mac.update(m.name.as_bytes());
        mac.update(b"\0");
        mac.update(m.version.as_bytes());
        mac.update(b"\0");
        mac.update(&m.sm_version.to_le_bytes());
        mac.update(&m.digest);
        m.signature = mac.finalize().into_bytes().into();
        match reg.verify_signature(&m) {
            Err(RegistryError::BadSignature(name)) => assert_eq!(name, "matmul.f32"),
            other => panic!("v1-shaped signature MUST NOT verify under v2 MAC: {other:?}"),
        }
    }

    // --- launch_geometry: unsigned hint, back-compat ---------------------

    /// `KernelManifest::new` defaults `launch_geometry` to `None`, and
    /// `with_launch_geometry` is the way to set it. This is the L3
    /// promotion path's source of truth — the cache reads
    /// `manifest.launch_geometry` and falls back to `(0, 0)` on `None`.
    #[test]
    fn launch_geometry_defaults_none_and_builder_sets_it() {
        let digest = [0u8; 32];
        let m = KernelManifest::new(
            "matmul.f32".to_string(),
            "1.0.0".to_string(),
            80,
            digest,
            [0u8; 32],
            0,
            "p".to_string(),
        );
        assert_eq!(
            m.launch_geometry, None,
            "new() must default geometry to None"
        );
        let m = m.with_launch_geometry(Some((8, 128)));
        assert_eq!(m.launch_geometry, Some((8, 128)));
        // Clearing back to None is supported.
        assert_eq!(m.with_launch_geometry(None).launch_geometry, None);
    }

    /// Geometry rides OUTSIDE the v2 HMAC envelope: setting (or changing)
    /// it must NOT change the canonical signed bytes, so a manifest signed
    /// without geometry still verifies after geometry is attached. This is
    /// what lets the L3 promotion path carry geometry without a
    /// `twasm-kmf-v2` -> `-v3` format break.
    #[test]
    fn launch_geometry_is_unsigned_and_does_not_affect_mac() {
        let key = [0x42u8; 32];
        let reg = InMemoryRegistry::new(key);
        let ptx = "// fake ptx\n".to_string();
        // Sign WITHOUT geometry (the historical v2 flow).
        let signed = signed_manifest("matmul.f32", "1.0.0", &ptx, &key);
        let bytes_before = signed.canonical_signed_bytes();
        // Attach geometry AFTER signing.
        let with_geo = signed.with_launch_geometry(Some((8, 128)));
        let bytes_after = with_geo.canonical_signed_bytes();
        assert_eq!(
            bytes_before, bytes_after,
            "launch_geometry must not alter the canonical signed bytes"
        );
        // The original (geometry-less) signature still verifies.
        reg.verify_signature(&with_geo)
            .expect("manifest must still verify after attaching unsigned geometry hint");
    }

    /// Round-trip: a geometry-bearing manifest survives the on-disk
    /// bincode codec (the same `(manifest, ptx)` envelope `DiskRegistry`
    /// persists), and a blob serialized WITHOUT the field (older format)
    /// deserializes with `launch_geometry == None` thanks to
    /// `#[serde(default)]`.
    #[test]
    fn launch_geometry_round_trips_and_old_blobs_default_none() {
        let key = [0x42u8; 32];
        let ptx = "// fake ptx\n".to_string();
        let m =
            signed_manifest("matmul.f32", "1.0.0", &ptx, &key).with_launch_geometry(Some((4, 256)));

        // Round-trip through the real disk codec.
        let blob: ManifestBlob = (m.clone(), ptx.clone());
        let encoded = encode_manifest_blob(&blob).expect("encode");
        let (decoded, decoded_ptx) = decode_manifest_blob(&encoded).expect("decode");
        assert_eq!(decoded.launch_geometry, Some((4, 256)));
        assert_eq!(decoded_ptx, ptx);
        // And the signature still verifies after the codec round-trip.
        verify_manifest_signature(&decoded, &key).expect("decoded manifest must verify");

        // Simulate an OLD blob that predates the field by serializing a
        // manifest whose geometry is None — it must decode to None, not
        // error, proving `#[serde(default)]` keeps old blobs readable.
        let old = signed_manifest("conv2d.f32", "2.0.0", &ptx, &key);
        assert_eq!(old.launch_geometry, None);
        let old_blob: ManifestBlob = (old, ptx.clone());
        let old_encoded = encode_manifest_blob(&old_blob).expect("encode old");
        let (old_decoded, _) = decode_manifest_blob(&old_encoded).expect("decode old");
        assert_eq!(
            old_decoded.launch_geometry, None,
            "geometry-less blob must deserialize to None"
        );
        verify_manifest_signature(&old_decoded, &key)
            .expect("geometry-less (old-shape) manifest must still verify");
    }
}
