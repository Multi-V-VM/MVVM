// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Compiled-kernel cache.
//!
//! The cache is keyed by `(blueprint_fingerprint, sm_version)`. LRU eviction
//! caps the entry count. On a cache hit the PTX text is reused without
//! re-emitting (cheap) and without re-compiling via `ptxas`/`cust` (expensive
//! — 10-50 ms for non-trivial kernels).
//!
//! Storage: a [`dashmap::DashMap`] holds the actual `(CacheKey, CachedKernel)`
//! entries — `get` / `put` / `len` go straight through the lock-free
//! per-shard locks so the hot path is uncontended under multi-threaded
//! dispatch. A separate `parking_lot::Mutex<LruCache<CacheKey, ()>>` is the
//! eviction queue — touched on `put` (insert/promote) and on `get` (promote)
//! and used to compute which key to evict when the soft cap is exceeded.
//! Splitting storage from policy means lookups never block on the eviction
//! mutex, only inserts that need to evict do. The `get` read path is
//! lock-free on contention; LRU promotion is best-effort under load —
//! contended promotions are skipped, so eviction order is approximately
//! (not strictly) LRU when many readers race.
//!
//! `Mutex` poisoning recovery: every lock acquisition uses `into_inner` on a
//! poisoned guard (after emitting a `tracing::error!`) rather than the prior
//! `.expect("cache poisoned")` panic — a single panic on any thread used to
//! poison the entire cache for the rest of the process.

use std::num::NonZeroUsize;
use std::path::PathBuf;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use dashmap::DashMap;
use lru::LruCache;
use parking_lot::Mutex;
use tensor_wasm_artifacts::{ArtifactError, ArtifactStore, ContentHash, DiskArtifactStore};
use tensor_wasm_core::types::TenantId;
use zeroize::Zeroizing;

use crate::ir::TensorWasmKernelBlueprint;
use crate::ptx_emit::EmittedPtx;
#[cfg(feature = "kernel-registry")]
use crate::registry::{BlueprintResolver, KernelRegistry};

/// Cache key.
///
/// `tenant_id` is the first field so that it dominates the derived `Hash`
/// (field-order) and lexicographic `Ord` orderings. Keeping the cache keyed
/// by tenant is the only thing preventing tenant A from looking up — and on
/// the CUDA path executing — a compiled kernel that tenant B installed
/// (exec S-7, cross-tenant confused-deputy). Every host-side `get` / `put`
/// MUST therefore include the calling tenant; constructing a key without
/// one (e.g. directly from guest-supplied bytes) is the bug we are fixing.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct CacheKey {
    /// Owning tenant. Cache lookups MUST be scoped to the caller's tenant —
    /// see the type-level docs. Use [`CacheKey::for_tenant`] to construct.
    pub tenant_id: u64,
    /// `TensorWasmKernelBlueprint::fingerprint()`.
    pub blueprint: u64,
    /// CUDA compute capability (e.g. 80 for sm_80, 89 for sm_89).
    pub sm_version: u32,
    /// Hash of the full [`crate::ptx_emit::EmitConfig`] used at emit time
    /// (jit S-2). `sm_version` covers the compute capability number but NOT
    /// the architecture suffix (e.g. `"sm_80"` vs `"sm_80a"`), the PTX
    /// language version, or the `launch_bounds` flag — two `EmitConfig`s
    /// that differ in any of those produce non-interchangeable PTX. Without
    /// this field two such configs would collide on the same key and the
    /// second caller would silently get the first caller's PTX.
    ///
    /// Callers that don't have an `EmitConfig` (rewriter pre-population,
    /// benches) pass `0`. Construct via [`CacheKey::for_tenant`] (defaults
    /// to `0`) or [`CacheKey::for_tenant_with_emit_config`] (computes a
    /// stable hash over the config).
    pub emit_config_hash: u64,
}

impl CacheKey {
    /// Construct a tenant-scoped cache key with no `EmitConfig` hash.
    ///
    /// Equivalent to passing `emit_config_hash: 0` — appropriate for the
    /// rewriter and bench paths that use the default emitter config. The
    /// `tenant_id` MUST come from trusted store state (e.g.
    /// `InstanceState::tenant_id`), never from guest-supplied fingerprint
    /// bytes. See the [`CacheKey`] docs for the confused-deputy primitive
    /// this guards against.
    pub fn for_tenant(tenant_id: TenantId, blueprint: u64, sm_version: u32) -> Self {
        Self {
            tenant_id: tenant_id.get(),
            blueprint,
            sm_version,
            emit_config_hash: 0,
        }
    }

    /// Construct a tenant-scoped cache key that also covers the emitter
    /// config. Use this when the lookup must distinguish between
    /// PTX-version variants, target-architecture suffixes, or
    /// launch-bounds settings (jit S-2).
    ///
    /// Cost note: each call builds a fresh `blake3::Hasher` and finalises
    /// over a handful of bytes. The amortised wall cost is ~µs on a
    /// modern x86-64 box — negligible compared to a kernel dispatch but
    /// non-zero on the hot path. Callers that resolve the same
    /// `EmitConfig` for every lookup (the typical pattern: emit-config is
    /// pinned at instance-spawn time) should hash it once at spawn and
    /// reuse the [`CacheKey`] rather than re-deriving it for every
    /// dispatch. The hasher itself is intentionally inline here (not
    /// memoised) so the function stays pure and `Send`-friendly for the
    /// rewriter's `rayon::par_iter` callers.
    pub fn for_tenant_with_emit_config(
        tenant_id: TenantId,
        blueprint: u64,
        sm_version: u32,
        cfg: &crate::ptx_emit::EmitConfig,
    ) -> Self {
        let emit_config_hash = blake3::Hasher::new()
            .update(b"tensor-wasm-jit::EmitConfig::v1\0")
            .update(cfg.target.as_bytes())
            .update(b"\0")
            .update(cfg.ptx_version.as_bytes())
            .update(b"\0")
            .update(&[u8::from(cfg.launch_bounds)])
            .finalize();
        let bytes = emit_config_hash.as_bytes();
        let mut buf = [0u8; 8];
        buf.copy_from_slice(&bytes[..8]);
        Self {
            tenant_id: tenant_id.get(),
            blueprint,
            sm_version,
            emit_config_hash: u64::from_le_bytes(buf),
        }
    }
}

/// Default cache capacity (kernels).
///
/// Memory-ceiling note: each entry holds an `Arc<EmittedPtx>` whose `text`
/// is the emitted PTX string. Typical kernels emit ~5-15 KB of PTX (a
/// vector-add lands around 2 KB; a small fused matmul around 12 KB), so at
/// 256 entries the steady-state L1 footprint is on the order of ~2.5 MB
/// (~10 KB PTX × 256) plus the per-entry `BLAKE3` hash (32 B) and the
/// LRU policy queue (one `CacheKey` per entry, 24 B). Hostile or
/// pathological blueprints could push individual entries into the
/// multi-MB range — a deliberately unrolled blueprint emitting 10 MB of
/// PTX would push the 256-slot cache to ~2.5 GB. Operators expecting
/// adversarial workloads should clamp this via
/// [`KernelCache::with_capacity`] (lower) and pair it with the on-disk
/// L2 cache so cold lookups still hit a persisted path. The cache does
/// NOT enforce a per-entry byte limit; the count cap is the only knob.
pub const DEFAULT_CAPACITY: usize = 256;

/// Construction-time configuration for [`KernelCache`].
///
/// Holds the small set of policy knobs the cache supports today: capacity
/// (count cap) and whether to recompute the per-entry BLAKE3 integrity
/// hash on every `get`. Future knobs (per-byte cap, eviction policy
/// choice) will land here without breaking the existing
/// [`KernelCache::with_capacity`] / [`KernelCache::with_disk_persistence`]
/// shorthands — those construct an equivalent `KernelCacheConfig` under
/// the hood.
///
/// Construct via [`KernelCacheConfig::default`] (which mirrors the
/// historical defaults — capacity [`DEFAULT_CAPACITY`], verify-on-get
/// `true`) and refine with the `with_*` builders, then hand the config
/// to [`KernelCache::with_config`].
#[derive(Clone)]
pub struct KernelCacheConfig {
    /// Soft maximum L1 entry count. Clamped to `>= 1` inside the cache.
    pub capacity: usize,
    /// Optional soft maximum on the *total* bytes of cached PTX text held
    /// in L1, summed across every live entry (each entry contributes
    /// `cached.ptx.text.len()`). When `Some(cap)`, [`KernelCache::put`]
    /// evicts LRU entries — using the same eviction queue the count cap
    /// drives — until the running byte total fits under `cap`, *after*
    /// admitting the new entry. The count cap ([`Self::capacity`]) still
    /// applies independently; whichever cap binds first wins.
    ///
    /// Why this exists: the count cap alone is a DoS vector. A 256-slot
    /// cache fed adversarial multi-MB PTX blueprints (a deliberately
    /// unrolled kernel can emit 10 MB of PTX) reaches ~2.5 GB of resident
    /// L1 — see the memory-ceiling note on [`DEFAULT_CAPACITY`]. The byte
    /// cap bounds the worst case regardless of per-entry size.
    ///
    /// A single entry larger than `cap` is still admitted (the cache never
    /// refuses an insert outright — it would otherwise wedge the dispatch
    /// path) but it will evict every other entry first; the byte total may
    /// transiently exceed `cap` by at most one such oversized entry.
    ///
    /// Default `None` (preserves the historical count-only behaviour). Set
    /// via [`Self::with_max_total_bytes`]. Track the live total via
    /// [`KernelCache::total_bytes`].
    pub max_total_bytes: Option<u64>,
    /// When `true` (the default), [`KernelCache::get`] recomputes a
    /// BLAKE3 over the cached `ptx.text` on every L1 hit and compares
    /// the result against the entry's stored `integrity_hash` (jit S-3
    /// in-mem poisoning defence). When `false`, the recompute is skipped
    /// — the cache still refuses entries whose stored hash is all-zero
    /// (the construction signal for "built without [`CachedKernel::new`]")
    /// as defence-in-depth.
    ///
    /// The recompute costs ~10 µs over a typical multi-KB PTX blob;
    /// skipping it shaves that off every L1 hit at the cost of widening
    /// the in-memory poisoning window from "one `get` call" to "the
    /// lifetime of the entry in L1". Operators on a high-QPS path with
    /// multi-MB PTX where the recompute dominates can opt out, but the
    /// safe default is verify-on-get.
    pub verify_on_get: bool,

    #[cfg(feature = "kernel-registry")]
    /// Optional registry consulted on L1+L2 miss. v0.4 path: caller resolves
    /// (tenant, blueprint, sm_version) → (name, version) via an external
    /// lookup, then `KernelCache::get_with_registry_fallback` consults the
    /// registry by that pair. v0.3.8 ships a `resolve_by_blueprint_hint`
    /// trait method on the cache config for the resolver step; the
    /// in-memory test impl resolves blueprint fingerprint → name@version
    /// directly via a `HashMap`.
    pub registry: Option<Arc<dyn KernelRegistry>>,
}

impl Default for KernelCacheConfig {
    fn default() -> Self {
        Self {
            capacity: DEFAULT_CAPACITY,
            max_total_bytes: None,
            verify_on_get: true,
            #[cfg(feature = "kernel-registry")]
            registry: None,
        }
    }
}

impl KernelCacheConfig {
    /// Override the L1 entry count cap. Clamped to `>= 1` at cache
    /// construction time; values below 1 are silently raised.
    #[must_use]
    pub fn with_capacity(mut self, capacity: usize) -> Self {
        self.capacity = capacity;
        self
    }

    /// Set the soft total-bytes cap on resident L1 PTX. `None` (the
    /// default) preserves the historical count-only behaviour; `Some(cap)`
    /// makes [`KernelCache::put`] evict LRU entries until the running PTX
    /// byte total fits under `cap`. See the field-level docs on
    /// [`Self::max_total_bytes`] for the DoS-mitigation rationale and the
    /// single-oversized-entry caveat.
    #[must_use]
    pub fn with_max_total_bytes(mut self, max_total_bytes: u64) -> Self {
        self.max_total_bytes = Some(max_total_bytes);
        self
    }

    /// Toggle the per-`get` BLAKE3 recompute. Default `true`; setting
    /// `false` is the high-QPS opt-out. See the field-level docs on
    /// [`Self::verify_on_get`] for the threat-model trade-off.
    #[must_use]
    pub fn with_verify_on_get(mut self, on: bool) -> Self {
        self.verify_on_get = on;
        self
    }

    /// Attach a [`KernelRegistry`] as the L3 fallback consulted by
    /// [`KernelCache::get_with_registry_fallback`] on an L1+L2 miss.
    /// Default is `None` (registry path disabled). See the field-level
    /// docs on [`Self::registry`] for the resolution contract.
    #[cfg(feature = "kernel-registry")]
    #[must_use]
    pub fn with_registry(mut self, reg: Arc<dyn KernelRegistry>) -> Self {
        self.registry = Some(reg);
        self
    }
}

// Manual `Debug` impl: `Arc<dyn KernelRegistry>` does not implement
// `Debug` (the trait deliberately does not require it so embedder
// backends like `InMemoryRegistry` — whose interior `Mutex<HashMap>`
// is awkward to debug-format — stay easy to write). Render the
// registry field as a presence-only marker so `{:?}` on a cache
// config still surfaces whether the L3 path is wired without forcing
// every registry impl to derive `Debug`.
impl std::fmt::Debug for KernelCacheConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut d = f.debug_struct("KernelCacheConfig");
        d.field("capacity", &self.capacity);
        d.field("max_total_bytes", &self.max_total_bytes);
        d.field("verify_on_get", &self.verify_on_get);
        #[cfg(feature = "kernel-registry")]
        d.field(
            "registry",
            &self.registry.as_ref().map(|_| "<dyn KernelRegistry>"),
        );
        d.finish()
    }
}

/// Cached PTX module entry.
///
/// `integrity_hash` is a BLAKE3 over `ptx.text` computed at construction
/// time and re-verified on every [`KernelCache::get`] (jit S-3). It defends
/// against in-memory poisoning by a sibling holder of a mutable reference
/// and forms the basis of the integrity tag stored in the on-disk
/// persistence layer (see [`DiskCacheConfig`]). The field is `pub` so
/// downstream tests and observability can inspect the recorded hash, but
/// construction via [`CachedKernel::new`] is the only path that produces a
/// correct-by-construction value — `KernelCache::put` calls `verify`
/// before accepting the entry, so a hand-crafted `CachedKernel` with a
/// mismatched hash will be rejected.
#[derive(Debug, Clone)]
pub struct CachedKernel {
    /// The blueprint that produced this PTX (for diagnostics).
    pub fingerprint: u64,
    /// The emitted PTX text.
    pub ptx: Arc<EmittedPtx>,
    /// The cuda module handle is only meaningful when the `cuda` feature is
    /// on; for the stub path we keep `()`.
    pub compiled: CompiledHandle,
    /// BLAKE3 over `ptx.text` (jit S-3). Recomputed and compared on every
    /// `get`. See the type-level doc for the threat model.
    ///
    /// The field is `pub` for backward compatibility with v0.1 struct-literal
    /// callers and so the forged-blob regression tests can hand-craft a
    /// `CachedKernel` with a deliberately wrong hash and assert
    /// [`KernelCache::put`] rejects it. It is hidden from generated docs and
    /// excluded from the stable surface; prefer [`Self::integrity_hash`] for
    /// read access and [`Self::new`] for construction.
    #[doc(hidden)]
    pub integrity_hash: [u8; 32],
}

impl CachedKernel {
    /// Borrow the stored BLAKE3 integrity hash over `ptx.text`. Prefer this
    /// over reaching for the (hidden) field directly — the field is
    /// retained as `pub` for source compatibility only and may be sealed
    /// to `pub(crate)` in a future minor release.
    #[must_use]
    pub fn integrity_hash(&self) -> &[u8; 32] {
        &self.integrity_hash
    }

    /// Construct a `CachedKernel`, computing `integrity_hash` from
    /// `ptx.text`. This is the only way to obtain a correct-by-
    /// construction value; the older struct-literal pattern still works
    /// (the field is `pub` for backward compatibility) but the literal
    /// must provide a matching hash or `KernelCache::put` will reject it.
    pub fn new(fingerprint: u64, ptx: Arc<EmittedPtx>, compiled: CompiledHandle) -> Self {
        let h = blake3::hash(ptx.text.as_bytes());
        Self {
            fingerprint,
            ptx,
            compiled,
            integrity_hash: *h.as_bytes(),
        }
    }

    /// Re-compute the integrity hash and compare it against the stored
    /// value. Returns `true` iff the PTX text has not been tampered with
    /// since [`Self::new`] ran.
    #[must_use]
    pub fn verify_integrity(&self) -> bool {
        let h = blake3::hash(self.ptx.text.as_bytes());
        h.as_bytes() == &self.integrity_hash
    }
}

/// Compiled-module handle. On CUDA hosts this would hold
/// `cust::module::Module`; for the no-CUDA path it is just `()`.
#[derive(Debug, Clone, Default)]
pub struct CompiledHandle {
    #[allow(dead_code)]
    private: (),
}

/// Thread-safe LRU cache of compiled kernels backed by [`dashmap::DashMap`].
///
/// `get` and `put` are O(1) and (under typical concurrent workloads) lock-
/// free except for the per-shard `dashmap` lock and the eviction-policy
/// `parking_lot::Mutex`. The eviction lock is taken only on `put`s that
/// would push the cache over capacity; reads never touch it.
#[derive(Clone)]
pub struct KernelCache {
    /// Lock-free storage of the cached values themselves.
    ///
    /// T20 perf: values are held as `Arc<CachedKernel>` so a cache hit only
    /// bumps the strong-count refcount instead of cloning the wrapper
    /// (32-byte integrity hash + fingerprint + an inner `Arc<EmittedPtx>`
    /// refcount bump). The inner PTX text was already `Arc`-shared; this
    /// extension closes the matching gap on the outer wrapper.
    storage: Arc<DashMap<CacheKey, Arc<CachedKernel>>>,
    /// LRU policy: keys ordered by recency. `Mutex` (parking_lot) for
    /// fast, panic-safe contention. The value side is `()` — the real value
    /// lives in `storage`.
    lru: Arc<Mutex<LruCache<CacheKey, ()>>>,
    /// Construction-time policy bag (capacity, verify-on-get). Held by
    /// value (cheap to clone, `Copy`-ish payload) so the `get` hot path
    /// can read `verify_on_get` without an extra `Arc` deref.
    config: KernelCacheConfig,
    /// Optional L2 on-disk cache. When present (configured via
    /// [`KernelCache::with_disk_persistence`]), `put` writes each entry to
    /// disk under an HMAC-keyed integrity tag, and `get` falls through to
    /// disk on an L1 miss — verifying the HMAC before deserialising. See
    /// [`DiskCacheConfig`] for the threat model that motivates the design
    /// (jit S-3).
    disk: Option<Arc<DiskCache>>,
    /// Running sum of the PTX-text bytes of every entry currently resident
    /// in `storage` (each entry contributes `cached.ptx.text.len()`).
    /// Maintained incrementally on `put` (add the inserted entry, subtract
    /// every eviction) so the byte cap in
    /// [`KernelCacheConfig::max_total_bytes`] can be enforced without
    /// re-summing the whole map. Exposed via [`Self::total_bytes`] for the
    /// Prometheus gauge `tensor_wasm_jit_cache_bytes`. `Arc`-shared so
    /// cache clones agree on the total.
    ///
    /// Accuracy note: this is a best-effort accounting that tracks the
    /// entries this cache instance inserted and evicted. The `verify_on_get`
    /// failure path and L2 disk-hit promotion also keep it in step. Under
    /// pathological concurrent races on the same key it may drift by a
    /// bounded amount, but it is never used as a correctness gate — only to
    /// drive best-effort byte-based eviction — so a transient skew at most
    /// delays an eviction by one `put`.
    total_bytes: Arc<AtomicU64>,
    /// Cumulative count of `get` calls that skipped the BLAKE3 recompute
    /// because [`KernelCacheConfig::verify_on_get`] was `false`.
    /// Exposed via [`Self::verify_skipped_total`] so operators can wire
    /// it to the Prometheus counter
    /// `tensor_wasm_jit_cache_verify_skipped_total`. Wrapped in an `Arc`
    /// so clones of `KernelCache` share the same counter (the storage
    /// and LRU policy are likewise `Arc`-shared).
    verify_skipped_total: Arc<AtomicU64>,
    /// Cumulative count of `get` calls that returned an entry (L1 hit, L2
    /// disk hit, or L3 registry hit). Exposed via [`Self::cache_hits_total`]
    /// for the Prometheus counter `tensor_wasm_jit_cache_hits_total`.
    /// `Arc`-shared so cache clones agree on the count.
    cache_hits_total: Arc<AtomicU64>,
    /// Cumulative count of `get` calls that returned `None` (full miss after
    /// L1, L2, and any registry fallback). Exposed via
    /// [`Self::cache_misses_total`] for the Prometheus counter
    /// `tensor_wasm_jit_cache_misses_total`. `Arc`-shared so cache clones
    /// agree on the count.
    cache_misses_total: Arc<AtomicU64>,
    /// Cumulative count of entries refused on `get` because their
    /// `integrity_hash` failed verification (L1 recompute mismatch, an
    /// all-zero hash on the verify-skip path, or an L2 disk integrity
    /// failure). These rejections also bump `cache_misses_total`, but this
    /// dedicated counter lets operators distinguish ordinary cache misses
    /// from *tamper attempts* — a rising
    /// `tensor_wasm_jit_cache_integrity_reject_total` is an alarm signal,
    /// not just a cold cache. Exposed via [`Self::integrity_reject_total`].
    /// `Arc`-shared so cache clones agree on the count.
    integrity_reject_total: Arc<AtomicU64>,
}

impl KernelCache {
    /// Construct with default capacity.
    pub fn new() -> Self {
        Self::with_config(KernelCacheConfig::default())
    }

    /// Construct with explicit capacity. Anything below 1 is clamped to 1.
    pub fn with_capacity(cap: usize) -> Self {
        Self::with_config(KernelCacheConfig::default().with_capacity(cap))
    }

    /// Construct from a full [`KernelCacheConfig`]. Anything below 1 in
    /// `config.capacity` is clamped to 1.
    pub fn with_config(mut config: KernelCacheConfig) -> Self {
        config.capacity = config.capacity.max(1);
        // The eviction queue is sized to `cap` so the LRU crate's internal
        // bucket pre-allocation is bounded (sizing it to `usize::MAX` triggers
        // a hashbrown capacity-overflow panic). Storage eviction is still
        // driven from the `storage.len() > capacity` check in `put`; both
        // sides agree on the same `cap` so they stay in sync.
        let nz = NonZeroUsize::new(config.capacity).expect(">0 (clamped above)");
        Self {
            storage: Arc::new(DashMap::with_capacity(config.capacity)),
            lru: Arc::new(Mutex::new(LruCache::new(nz))),
            config,
            disk: None,
            total_bytes: Arc::new(AtomicU64::new(0)),
            verify_skipped_total: Arc::new(AtomicU64::new(0)),
            cache_hits_total: Arc::new(AtomicU64::new(0)),
            cache_misses_total: Arc::new(AtomicU64::new(0)),
            integrity_reject_total: Arc::new(AtomicU64::new(0)),
        }
    }

    /// Enable the on-disk L2 cache, persisting entries to `cfg.dir` under
    /// an HMAC-keyed integrity tag (jit S-3).
    ///
    /// Construct via:
    /// ```
    /// # // Hidden doctest stub — real deployments must read the key from
    /// # // an out-of-process secret store (Vault, KMS, sealed file under
    /// # // mode 0400, etc.) and never embed it in source. This stub lets
    /// # // the doctest compile without shipping a fake key literal that
    /// # // operators might copy-paste verbatim.
    /// # fn load_hmac_key_from_secret_store() -> Result<[u8; 32], Box<dyn std::error::Error>> {
    /// #     Ok([0u8; 32])
    /// # }
    /// # fn main() -> Result<(), Box<dyn std::error::Error>> {
    /// use std::path::PathBuf;
    /// use tensor_wasm_jit::cache::{KernelCache, DiskCacheConfig};
    /// let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig {
    ///     dir: PathBuf::from("/var/cache/tensor-wasm/kernels"),
    ///     hmac_key: load_hmac_key_from_secret_store()?, /* loaded from secrets at startup */
    /// });
    /// # let _ = cache;
    /// # Ok(())
    /// # }
    /// ```
    ///
    /// jit S-3 (T13): the example deliberately routes the key through a
    /// `load_hmac_key_from_secret_store()` stub rather than an inline
    /// literal — operators have a habit of copy-pasting rustdoc examples
    /// verbatim, and an embedded `[0xAB; 32]` (or similar) would survive
    /// into production deployments as a fixed, attacker-known key.
    ///
    /// The directory is created lazily on the first `put`. The HMAC key
    /// MUST be stable across process restarts AND treated as a server-
    /// side secret — possession of the key lets an attacker forge cache
    /// entries that the loader will accept as authentic (and that would
    /// then be handed to `cust::module::Module::from_ptx` as trusted GPU
    /// code).
    #[must_use]
    pub fn with_disk_persistence(mut self, cfg: DiskCacheConfig) -> Self {
        // Share the integrity-rejection counter with the disk layer so an
        // L2 tamper rejection bumps the same
        // `tensor_wasm_jit_cache_integrity_reject_total` as an L1 one.
        self.disk = Some(Arc::new(DiskCache::new(
            cfg,
            Arc::clone(&self.integrity_reject_total),
        )));
        self
    }

    /// Byte cost a single entry contributes to [`Self::total_bytes`]: the
    /// length of its PTX text. The 32-byte integrity hash, fingerprint, and
    /// LRU bookkeeping are fixed-size per entry and already bounded by the
    /// count cap, so the byte cap deliberately accounts only the variable-
    /// size payload that the DoS note on [`DEFAULT_CAPACITY`] flags.
    fn entry_bytes(kernel: &CachedKernel) -> u64 {
        kernel.ptx.text.len() as u64
    }

    /// Remove a key from storage *and* decrement the byte total by the
    /// removed entry's payload size. Centralised so every eviction site
    /// keeps [`Self::total_bytes`] in step with `storage`. Returns the
    /// removed entry (if any) for callers that want it.
    fn evict_key(&self, key: &CacheKey) -> Option<Arc<CachedKernel>> {
        if let Some((_, removed)) = self.storage.remove(key) {
            self.total_bytes
                .fetch_sub(Self::entry_bytes(&removed), Ordering::Relaxed);
            Some(removed)
        } else {
            None
        }
    }

    /// Byte-cap eviction (DoS mitigation — see
    /// [`KernelCacheConfig::max_total_bytes`]). Reuse the SAME LRU policy
    /// queue that drives the count cap: pop least-recently-used keys and
    /// evict them until the running PTX-byte total fits under `cap`. We
    /// never evict down to an empty cache — the loop stops once at most one
    /// entry (the most-recently-used, i.e. typically the entry just
    /// inserted) remains, so a single entry larger than `cap` is still
    /// served rather than wedging dispatch.
    ///
    /// A no-op when no byte cap is configured. Factored out of [`Self::put`]
    /// so EVERY L1 insert site — `put` and the L2 disk-hit promotion in
    /// [`Self::get`] — binds the byte cap, not just `put`. Holds the `lru`
    /// lock across the whole eviction burst (one acquisition for the loop)
    /// exactly as the count-cap loop does; `evict_key` keeps
    /// [`Self::total_bytes`] in step with each storage removal.
    fn enforce_byte_cap(&self) {
        if let Some(cap) = self.config.max_total_bytes {
            if self.total_bytes.load(Ordering::Relaxed) > cap {
                let mut lru = self.lru.lock();
                while self.total_bytes.load(Ordering::Relaxed) > cap && self.storage.len() > 1 {
                    match lru.pop_lru() {
                        Some((evict_key, ())) => {
                            self.evict_key(&evict_key);
                        }
                        None => break,
                    }
                }
            }
        }
    }

    /// Insert (or replace) a kernel. If the insert pushes the cache over
    /// the count cap (or, when configured, the byte cap), evicts LRU
    /// entries from storage and the policy queue until both caps are
    /// satisfied.
    ///
    /// jit S-3: the kernel's `integrity_hash` is recomputed and compared
    /// against the stored hash; a mismatch is treated as a programmer
    /// error (`CachedKernel` constructed via struct-literal with a wrong
    /// hash), logged at `error!`, and the entry is dropped rather than
    /// admitted. Use [`CachedKernel::new`] for the correct-by-
    /// construction path. The on-disk L2 also writes the entry when
    /// configured.
    pub fn put(&self, key: CacheKey, kernel: CachedKernel) {
        if !kernel.verify_integrity() {
            tracing::error!(
                target: "tensor_wasm_jit::cache",
                fingerprint = kernel.fingerprint,
                tenant = key.tenant_id,
                "refusing to cache kernel whose integrity hash does not match \
                 its PTX text -- likely a struct-literal construction with a \
                 stale hash; use CachedKernel::new"
            );
            return;
        }
        if let Some(disk) = &self.disk {
            if let Err(e) = disk.put(&key, &kernel) {
                tracing::warn!(
                    target: "tensor_wasm_jit::cache",
                    fingerprint = kernel.fingerprint,
                    error = %e,
                    "disk-cache put failed; entry remains in L1 only"
                );
            }
        }
        // T20 perf: storage holds `Arc<CachedKernel>` so cache hits return a
        // refcount bump rather than a wrapper-clone. Account the new entry's
        // bytes; if `insert` replaced an existing entry under the same key,
        // subtract the displaced entry's bytes so the running total reflects
        // a replace rather than an add.
        let new_bytes = Self::entry_bytes(&kernel);
        let replaced = self.storage.insert(key, Arc::new(kernel));
        self.total_bytes.fetch_add(new_bytes, Ordering::Relaxed);
        if let Some(old) = replaced {
            self.total_bytes
                .fetch_sub(Self::entry_bytes(&old), Ordering::Relaxed);
        }
        // `LruCache::push` returns `Some((evicted_key, ()))` when sizing the
        // LRU triggers eviction of an older entry. We use this as the
        // authoritative signal for storage eviction so the two stay in sync.
        // Sized to `cap`, the LRU evicts exactly when we want storage to,
        // and we get the evicted key directly without a second pop_lru call.
        let evicted = self.lru.lock().push(key, ());
        if let Some((evicted_key, ())) = evicted {
            if evicted_key != key {
                // Don't remove if `push` returned the just-inserted key
                // (which happens when the cache already held it — push acts
                // as a replace and returns the old `(K, V)`). `evict_key`
                // keeps `total_bytes` in step with the storage removal.
                self.evict_key(&evicted_key);
            }
        }
        // Safety net for the rare burst case where two concurrent `put`s
        // both insert without either triggering LRU's internal eviction
        // (because they raced on the lock). Drive storage back under cap.
        //
        // Hold the `lru` lock across the entire eviction loop instead of
        // taking and releasing it on every iteration: a tight burst of
        // overflow eviction used to lock-unlock the mutex `N` times,
        // letting unrelated readers/writers contend on each pass. One
        // acquisition costs the same as one iteration's lock+unlock but
        // dispatches the whole burst in a single critical section.
        if self.storage.len() > self.config.capacity {
            let mut lru = self.lru.lock();
            while self.storage.len() > self.config.capacity {
                match lru.pop_lru() {
                    Some((evict_key, ())) => {
                        self.evict_key(&evict_key);
                    }
                    None => {
                        tracing::error!(
                            target: "tensor_wasm_jit::cache",
                            storage_len = self.storage.len(),
                            capacity = self.config.capacity,
                            "cache storage exceeds capacity but eviction queue is empty"
                        );
                        break;
                    }
                }
            }
        }
        // Byte-cap eviction (DoS mitigation — see
        // `KernelCacheConfig::max_total_bytes`). Shared with the L2
        // disk-promotion path so both insert sites bind the byte cap.
        self.enforce_byte_cap();
    }

    /// Look up a kernel; best-effort touches the LRU position.
    ///
    /// T20 perf: returns `Option<Arc<CachedKernel>>` rather than the
    /// previous `Option<CachedKernel>` so a cache hit shares the wrapper
    /// allocation via refcount bump instead of cloning the 32-byte
    /// integrity hash + fingerprint + inner-`Arc` refcount bump. The
    /// inner PTX text was already shared; this closes the matching
    /// gap on the outer wrapper.
    pub fn get(&self, key: &CacheKey) -> Option<Arc<CachedKernel>> {
        // Promote in the policy queue if we can grab the lock uncontended;
        // otherwise skip promotion this time so the read path stays
        // contention-free. Eviction order is approximate (not strict) LRU
        // under load — an acceptable trade for a lock-free hot path.
        // The storage read is the single source of truth for "is this
        // cached", so skipping promotion never affects correctness.
        if let Some(mut lru) = self.lru.try_lock() {
            // `get` on LruCache promotes if present.
            let _ = lru.get(key);
        }
        if let Some(entry) = self.storage.get(key) {
            // T20 perf: clone the Arc (refcount bump) rather than the
            // inner `CachedKernel` value.
            let kernel: Arc<CachedKernel> = Arc::clone(entry.value());
            drop(entry); // release shard lock before the hash recompute
                         // jit S-3: verify the in-memory entry hasn't been tampered
                         // with since `put`. A mismatch should be impossible (the
                         // `CachedKernel` is owned by the cache and never handed out
                         // mutably) but failing closed costs ~µs over the public
                         // PTX bytes and definitively closes the in-mem poisoning
                         // path the audit flagged.
                         //
                         // The recompute can be opted out of via
                         // [`KernelCacheConfig::verify_on_get`] for high-QPS callers
                         // where the ~10 µs BLAKE3 cost over multi-MB PTX dominates.
                         // Even on the opt-out path we keep a cheap defence-in-depth
                         // check: refuse entries whose `integrity_hash` is all-zero,
                         // because that is the signature of a `CachedKernel`
                         // constructed via the `#[doc(hidden)]` struct-literal path
                         // without [`CachedKernel::new`] having computed a real hash.
                         // A real BLAKE3 over any PTX text is overwhelmingly unlikely
                         // to collide with the zero hash (probability `2^-256`), so
                         // the rejection is unambiguous.
            if self.config.verify_on_get {
                if !kernel.verify_integrity() {
                    tracing::error!(
                        target: "tensor_wasm_jit::cache",
                        fingerprint = kernel.fingerprint,
                        tenant = key.tenant_id,
                        "L1 cache entry failed integrity verification on get; \
                         evicting and refusing to return it"
                    );
                    self.evict_key(key);
                    self.integrity_reject_total.fetch_add(1, Ordering::Relaxed);
                    self.cache_misses_total.fetch_add(1, Ordering::Relaxed);
                    return None;
                }
            } else {
                // Defence-in-depth on the opt-out path: reject the
                // "constructed-without-`new`" signal (all-zero hash) so
                // hand-crafted `CachedKernel`s with a zeroed hash cannot
                // slip through. Counter increment records that the user
                // chose to skip the full recompute on this hit.
                self.verify_skipped_total.fetch_add(1, Ordering::Relaxed);
                if kernel.integrity_hash == [0u8; 32] {
                    tracing::error!(
                        target: "tensor_wasm_jit::cache",
                        fingerprint = kernel.fingerprint,
                        tenant = key.tenant_id,
                        "L1 cache entry has zero integrity_hash on verify-skip get; \
                         likely a struct-literal CachedKernel built without \
                         CachedKernel::new — evicting and refusing to return it"
                    );
                    self.evict_key(key);
                    self.integrity_reject_total.fetch_add(1, Ordering::Relaxed);
                    self.cache_misses_total.fetch_add(1, Ordering::Relaxed);
                    return None;
                }
            }
            self.cache_hits_total.fetch_add(1, Ordering::Relaxed);
            return Some(kernel);
        }
        // jit S-3 + audit P-4: L1 miss falls through to the optional L2
        // on-disk cache. The disk path HMAC-verifies the entry before
        // deserialising, so a tampered file on disk is rejected with the
        // same "no such entry" outcome as a real miss.
        if let Some(disk) = &self.disk {
            match disk.get(key) {
                Ok(Some(kernel)) => {
                    // Promote the disk hit into L1 so subsequent lookups
                    // stay on the fast path. Storage owns an `Arc<CachedKernel>`;
                    // wrap once here and clone the Arc for the return value
                    // so the caller and the cache share the allocation.
                    let arc = Arc::new(kernel);
                    let promoted_bytes = Self::entry_bytes(&arc);
                    let replaced = self.storage.insert(*key, Arc::clone(&arc));
                    self.total_bytes
                        .fetch_add(promoted_bytes, Ordering::Relaxed);
                    if let Some(old) = replaced {
                        self.total_bytes
                            .fetch_sub(Self::entry_bytes(&old), Ordering::Relaxed);
                    }
                    // Drive count-cap eviction via the policy queue exactly as
                    // `put` does, keeping `total_bytes` in step through
                    // `evict_key`.
                    if let Some((evicted_key, ())) = self.lru.lock().push(*key, ()) {
                        if evicted_key != *key {
                            self.evict_key(&evicted_key);
                        }
                    }
                    // Byte-cap parity: `put` enforces BOTH the count cap and the
                    // byte cap on every insert, but this promotion path used to
                    // run only count-cap eviction — so an L2 disk hit could push
                    // `total_bytes` past `config.max_total_bytes` and leave it
                    // over the cap until the next `put`. Run the shared byte-cap
                    // eviction here too so both caps bind on every L1 insert.
                    self.enforce_byte_cap();
                    self.cache_hits_total.fetch_add(1, Ordering::Relaxed);
                    return Some(arc);
                }
                Ok(None) => {}
                Err(e) => {
                    tracing::warn!(
                        target: "tensor_wasm_jit::cache",
                        tenant = key.tenant_id,
                        error = %e,
                        "disk-cache get failed; treating as miss"
                    );
                }
            }
        }
        self.cache_misses_total.fetch_add(1, Ordering::Relaxed);
        None
    }

    /// Look up by blueprint + sm_version for a given tenant; convenience
    /// wrapper around [`Self::get`].
    ///
    /// T20 perf: returns `Option<Arc<CachedKernel>>` to mirror
    /// [`Self::get`]; callers that only need to peek at fields go
    /// through auto-deref unchanged.
    pub fn get_for(
        &self,
        tenant_id: TenantId,
        blueprint: &TensorWasmKernelBlueprint,
        sm_version: u32,
    ) -> Option<Arc<CachedKernel>> {
        self.get(&CacheKey::for_tenant(
            tenant_id,
            blueprint.fingerprint(),
            sm_version,
        ))
    }

    /// L1 → L2 → L3(registry) resolution path. v0.3.8 scaffold: invokes the
    /// registered resolver + registry on every miss; v0.4 may add a
    /// resolver-level cache to amortise the (blueprint → name@version)
    /// translation.
    #[cfg(feature = "kernel-registry")]
    pub fn get_with_registry_fallback(
        &self,
        key: &CacheKey,
        resolver: &dyn BlueprintResolver,
    ) -> Option<Arc<CachedKernel>> {
        // L1 + L2 — `get` now already returns `Arc<CachedKernel>` (T20).
        if let Some(hit) = self.get(key) {
            return Some(hit);
        }
        // L3
        let registry = self.config.registry.as_ref()?;
        let (name, version) = resolver.resolve(key.blueprint, key.sm_version)?;
        let entry = registry.get(&name, &version).ok()?;
        // Promote into L1 via the standard put path (which re-checks
        // integrity) so subsequent calls hit fast.
        //
        // Perf: `entry` is an `Arc<(KernelManifest, String)>` owned by the
        // registry, so the PTX `String` cannot be moved out — exactly one
        // owned copy into `EmittedPtx` is the irreducible minimum on this
        // path. That copy is wrapped in an `Arc<EmittedPtx>` once; the L1
        // promotion and the handle returned to the caller then share it via
        // refcount bump, so the PTX bytes are never re-copied. The single
        // `CachedKernel::new` below also hashes the PTX exactly once (its
        // BLAKE3 integrity tag); the cheap wrapper `clone()` we hand to `put`
        // copies only the 32-byte hash + fingerprint + an `Arc` refcount
        // bump, not the PTX text.
        let emitted = Arc::new(crate::ptx_emit::EmittedPtx {
            text: entry.1.clone(),
            // Thread the real launch geometry through from the manifest.
            // `KernelManifest::launch_geometry` is an optional, unsigned
            // hint (publishers set it via `KernelManifest::with_launch_geometry`);
            // when present we promote it onto the L1 entry so a registry-sourced
            // kernel carries the same geometry an L1/L2 hit would. When the
            // manifest omits it (older blobs, or publishers that never set it) we
            // fall back to `(0, 0)`, exactly as before — geometry is a launch
            // hint, not a correctness invariant for resolution. The earlier
            // `(0, 0)` hard-code lost geometry on every L3 hit (the bug the V2
            // disk envelope fixed on L2); this closes the L3 gap without a signed-
            // format break, since the hint rides outside the v2 HMAC envelope.
            launch_geometry: entry.0.launch_geometry.unwrap_or((0, 0)),
        });
        // Fingerprint MUST match the cache key under which this entry is
        // inserted. `put` keys L1 entries by `CacheKey` (whose `blueprint` is the
        // blueprint fingerprint) and the L2 disk reader reconstructs with
        // `fingerprint == key.blueprint` (see `DiskCache::get`). Using
        // `manifest.digest_as_u64()` (the PTX digest) here made the promoted
        // entry's `fingerprint` disagree with `key.blueprint`, breaking
        // diagnostics/log correlation and the `OffloadedFunction.fingerprint`
        // contract for registry-sourced entries. Key on `key.blueprint` so L1,
        // L2, and L3 entries are all fingerprinted consistently.
        let cached = CachedKernel::new(key.blueprint, emitted, CompiledHandle::default());
        // Best-effort L1 promote; `put` is infallible-by-design (any
        // integrity-mismatch case logs + drops the entry rather than
        // returning an error), so there is nothing to propagate even if
        // the registry-derived `cached` were somehow rejected — the
        // caller still gets the verified `Arc<CachedKernel>` from this
        // call, the next call simply pays another L3 round-trip.
        //
        // `put` keeps its by-value `CachedKernel` signature, so we hand it a
        // wrapper clone (cheap — see above); the returned `Arc<CachedKernel>`
        // shares the same inner `Arc<EmittedPtx>` as the L1 copy.
        self.put(*key, cached.clone());
        Some(Arc::new(cached))
    }

    /// Number of entries currently held.
    pub fn len(&self) -> usize {
        self.storage.len()
    }

    /// True if empty.
    pub fn is_empty(&self) -> bool {
        self.storage.is_empty()
    }

    /// Configured capacity.
    pub fn capacity(&self) -> usize {
        self.config.capacity
    }

    /// Running sum of resident PTX-text bytes across all L1 entries (each
    /// entry contributes `cached.ptx.text.len()`). This is the quantity the
    /// optional [`KernelCacheConfig::max_total_bytes`] cap bounds. Surface
    /// on the Prometheus gauge `tensor_wasm_jit_cache_bytes` so operators
    /// can watch the L1 footprint against the configured byte cap. Always
    /// present (returns `0` for an empty cache, and tracks the live total
    /// whether or not a byte cap is configured).
    pub fn total_bytes(&self) -> u64 {
        self.total_bytes.load(Ordering::Relaxed)
    }

    /// The configured byte cap, if any (mirror of
    /// [`KernelCacheConfig::max_total_bytes`]). `None` means count-only
    /// eviction. Useful for diagnostic endpoints that surface both the
    /// live [`Self::total_bytes`] and the ceiling it is measured against.
    pub fn max_total_bytes(&self) -> Option<u64> {
        self.config.max_total_bytes
    }

    /// Fingerprint of the *active* on-disk HMAC key — the partition prefix
    /// every file this cache writes lands under — or `None` if no L2 disk
    /// cache is configured.
    ///
    /// Pass this into the `retain` set of [`Self::gc_disk`] to keep the
    /// live generation (though `gc_disk` retains it unconditionally as a
    /// safety net).
    pub fn active_disk_key_fingerprint(&self) -> Option<KeyFingerprint> {
        self.disk.as_ref().map(|d| d.active_key_fingerprint())
    }

    /// Enumerate the distinct HMAC-key fingerprints present in the on-disk
    /// L2 cache directory (one per key generation that has written at least
    /// one file). Returns an empty `Vec` when no disk cache is configured or
    /// the directory does not yet exist.
    ///
    /// Use this to discover stale generations before a [`Self::gc_disk`]
    /// sweep — anything in this list that is not the active fingerprint and
    /// not a key you still want to honour is a rotation leftover.
    pub fn disk_key_fingerprints(&self) -> std::io::Result<Vec<KeyFingerprint>> {
        match &self.disk {
            Some(d) => d.key_fingerprints_on_disk(),
            None => Ok(Vec::new()),
        }
    }

    /// Garbage-collect the on-disk L2 cache after an HMAC-key rotation:
    /// remove every persisted entry whose key fingerprint is NOT in
    /// `retain`, returning the number of files removed. A no-op returning
    /// `Ok(0)` when no disk cache is configured.
    ///
    /// The *active* key's fingerprint is always retained — even if the
    /// caller omits it from `retain` — so a sweep can never delete the live
    /// generation's entries. Only stale-fingerprint files are removed, and
    /// only files matching the cache's own naming scheme are considered
    /// (unrelated files in the directory are left untouched).
    ///
    /// # Example
    ///
    /// ```
    /// # fn load_hmac_key_from_secret_store() -> Result<[u8; 32], Box<dyn std::error::Error>> {
    /// #     Ok([0u8; 32])
    /// # }
    /// # fn main() -> Result<(), Box<dyn std::error::Error>> {
    /// use std::path::PathBuf;
    /// use tensor_wasm_jit::cache::{KernelCache, DiskCacheConfig};
    ///
    /// // After rotating to a fresh HMAC key, stand up a cache under it…
    /// let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig {
    ///     dir: PathBuf::from("/var/cache/tensor-wasm/kernels"),
    ///     hmac_key: load_hmac_key_from_secret_store()?,
    /// });
    ///
    /// // …then sweep every previous generation's files. Retaining only the
    /// // active key (which `gc_disk` keeps regardless) drops all the rest.
    /// let active = cache.active_disk_key_fingerprint().into_iter().collect::<Vec<_>>();
    /// let removed = cache.gc_disk(&active)?;
    /// println!("swept {removed} stale-fingerprint cache files");
    /// # let _ = removed;
    /// # Ok(())
    /// # }
    /// ```
    pub fn gc_disk(&self, retain: &[KeyFingerprint]) -> std::io::Result<usize> {
        match &self.disk {
            Some(d) => d.gc(retain),
            None => Ok(0),
        }
    }

    /// Borrow the construction-time [`KernelCacheConfig`]. Useful for
    /// tests and diagnostic endpoints that want to surface whether
    /// `verify_on_get` is on for this cache instance.
    pub fn config(&self) -> &KernelCacheConfig {
        &self.config
    }

    /// Cumulative count of L1 `get` hits that skipped the BLAKE3
    /// integrity recompute because [`KernelCacheConfig::verify_on_get`]
    /// is `false`. Surface this on the Prometheus counter
    /// `tensor_wasm_jit_cache_verify_skipped_total` so operators can see
    /// how often the cache is trusting an L1 entry without re-hashing
    /// the PTX. The counter is always present (returns `0` when
    /// `verify_on_get` is on) so dashboards can scrape it unconditionally.
    pub fn verify_skipped_total(&self) -> u64 {
        self.verify_skipped_total.load(Ordering::Relaxed)
    }

    /// Cumulative L1/L2/L3 cache hit count. Incremented once per `get`
    /// call that returns `Some`. Surface on the Prometheus counter
    /// `tensor_wasm_jit_cache_hits_total` so operators can compute the
    /// hit ratio against [`Self::cache_misses_total`]. (T20 perf.)
    pub fn cache_hits_total(&self) -> u64 {
        self.cache_hits_total.load(Ordering::Relaxed)
    }

    /// Cumulative cache miss count. Incremented once per `get` call that
    /// returns `None` — including the rare path where an L1 entry failed
    /// integrity verification and was evicted. Surface on the Prometheus
    /// counter `tensor_wasm_jit_cache_misses_total`. (T20 perf.)
    pub fn cache_misses_total(&self) -> u64 {
        self.cache_misses_total.load(Ordering::Relaxed)
    }

    /// Cumulative count of entries refused on `get` because they failed
    /// integrity verification — an L1 BLAKE3 recompute mismatch, an
    /// all-zero `integrity_hash` on the verify-skip path, or an L2 disk
    /// integrity failure (artifact-store HMAC/content-hash mismatch,
    /// inner-envelope magic/header/length/UTF-8 corruption). These also
    /// bump [`Self::cache_misses_total`], but this dedicated counter lets
    /// operators alarm on *tamper attempts* specifically rather than
    /// drown them in ordinary cold-cache misses. Surface on the
    /// Prometheus counter `tensor_wasm_jit_cache_integrity_reject_total`.
    pub fn integrity_reject_total(&self) -> u64 {
        self.integrity_reject_total.load(Ordering::Relaxed)
    }

    /// Test-only insert that skips the `put`-side integrity check.
    ///
    /// `put` rejects any `CachedKernel` whose stored `integrity_hash`
    /// does not match a fresh BLAKE3 over its `ptx.text` (jit S-3).
    /// That is the correct production behaviour, but the
    /// `verify_on_get=false` regression test in
    /// `tests/cache_verify_opt_out.rs` needs to install a hand-crafted
    /// zero-hash entry to confirm the opt-out path still rejects it on
    /// `get`. This entry-point exists for that test only — it is
    /// `#[doc(hidden)]` (excluded from generated docs and rustdoc search)
    /// and named with a `__test_only_` prefix to broadcast "do NOT call
    /// this from production code". Integration tests under
    /// `crates/.../tests/` compile as external consumers of the library and
    /// therefore cannot see `cfg(test)` items, so this cannot be gated on
    /// `cfg(test)`. It is instead gated behind the dedicated
    /// `__unstable-test-internals` cargo feature (jit M2): without that
    /// feature the symbol does not exist, so a downstream crate cannot use
    /// it to install a `CachedKernel` with a forged/zeroed integrity hash
    /// and thereby bypass the S-3 integrity check. The double-underscore
    /// feature name signals it is unstable and not covered by semver.
    ///
    /// Production code MUST go through [`Self::put`].
    #[doc(hidden)]
    #[cfg(feature = "__unstable-test-internals")]
    pub fn __test_only_insert_unchecked(&self, key: CacheKey, kernel: CachedKernel) {
        // T20 perf: storage holds `Arc<CachedKernel>`; wrap on insert.
        let bytes = Self::entry_bytes(&kernel);
        let replaced = self.storage.insert(key, Arc::new(kernel));
        self.total_bytes.fetch_add(bytes, Ordering::Relaxed);
        if let Some(old) = replaced {
            self.total_bytes
                .fetch_sub(Self::entry_bytes(&old), Ordering::Relaxed);
        }
        let _ = self.lru.lock().push(key, ());
    }
}

// ---------------------------------------------------------------------------
// Disk persistence (audit P-4 + jit S-3)
// ---------------------------------------------------------------------------

/// Configuration for the on-disk L2 cache.
///
/// The cache directory must be writable by the runtime user and SHOULD be
/// owned exclusively by that user (mode 0700 on Unix) — operators on a
/// hardened deployment can additionally `chattr +i` files after first
/// write so a parallel attacker process cannot tamper with cached
/// entries even with the same UID.
///
/// `hmac_key` is the secret that gates load: the writer HMACs each
/// persisted entry with the key, and the reader rejects any file whose
/// recomputed HMAC does not match. Without the key (or with a different
/// key) the loader treats every existing entry as a miss, so rotating
/// the key invalidates the disk cache without requiring an `rm -rf`.
///
/// The caller-supplied `hmac_key` field is plain `[u8; 32]` for ergonomic
/// construction; once handed to [`KernelCache::with_disk_persistence`] the
/// bytes are copied into a private `Zeroizing<[u8; 32]>` inside
/// `DiskCache` which wipes them on drop. The caller's own copy is the
/// caller's responsibility (e.g. construct via `Zeroizing::new` upstream
/// and let it drop after the call).
///
/// jit S-3 hardening (T13): `Debug` is implemented manually so the
/// `hmac_key` bytes are redacted in any `{:?}` formatting, panic message,
/// or `tracing` field expansion — the derived `Debug` would have dumped
/// the raw 32-byte array. `Drop` zeroizes `hmac_key` on drop so the
/// construction-time copy does not survive in freed memory.
#[derive(Clone)]
pub struct DiskCacheConfig {
    /// Directory where the L2 cache files live. Created lazily on first
    /// `put`.
    pub dir: PathBuf,
    /// 32-byte HMAC-keyed-BLAKE3 key. MUST be process-stable across the
    /// cache's lifetime and SHOULD be treated as a server-side secret.
    /// The long-lived copy held by the cache is zeroized on drop; this
    /// field is the construction-time hand-off only.
    ///
    /// jit S-3 (T13): redacted in the manual [`std::fmt::Debug`] impl
    /// below and zeroized in [`Drop`] so the construction-time copy
    /// does not linger in freed memory after the value is moved into
    /// the long-lived `DiskCache`.
    pub hmac_key: [u8; 32],
}

// Manual `Debug` so `hmac_key` never appears verbatim in formatted output
// (jit S-3 T13). Any `{:?}` print, panic message, or `tracing::error!`
// field expansion that includes a `DiskCacheConfig` would otherwise dump
// the raw key bytes into the log stream — which is the exact server-side
// secret the disk cache is supposed to protect.
impl std::fmt::Debug for DiskCacheConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DiskCacheConfig")
            .field("dir", &self.dir)
            .field("hmac_key", &"<redacted 32 bytes>")
            .finish()
    }
}

// Zeroize the in-place HMAC key bytes when the config is dropped so the
// construction-time copy does not survive in freed memory once
// [`KernelCache::with_disk_persistence`] has consumed the value (jit S-3
// T13). The long-lived copy in [`DiskCache`] already lives inside
// `Zeroizing<[u8; 32]>`; this `Drop` plugs the matching gap for the
// caller-side hand-off struct.
impl Drop for DiskCacheConfig {
    fn drop(&mut self) {
        use zeroize::Zeroize;
        self.hmac_key.zeroize();
    }
}

/// 16-hex-char fingerprint of an HMAC key, as it appears in on-disk
/// filenames.
///
/// The disk layout partitions every file by the first 8 bytes of
/// `blake3::hash(hmac_key)` rendered as 16 lowercase hex chars — the
/// sidecar prefix in `{fp}-{cache_key}.ptxbin` and the middle segment in
/// the artifact store's `{content_hash}.{fp}.bin` blobs both use the same
/// fingerprint. Rotating the HMAC key changes the fingerprint, so a
/// rotation leaves the previous generation's files behind under their old
/// fingerprint, trivially sweepable by [`KernelCache::gc_disk`].
///
/// This is *not* secret: it is `blake3(key)` truncated, already publicly
/// observable to anyone with directory-list access (it is in the
/// filename). It is a partitioning tag, not a confidentiality boundary —
/// the HMAC trailer on each blob is what actually gates load.
///
/// Obtain the fingerprint of the currently-active key via
/// [`KernelCache::active_disk_key_fingerprint`], and the set present on
/// disk via [`KernelCache::disk_key_fingerprints`].
#[derive(Debug, Clone, PartialEq, Eq, Hash, PartialOrd, Ord)]
pub struct KeyFingerprint(pub String);

impl KeyFingerprint {
    /// The 16-hex-char fingerprint of `key` — the same value the disk
    /// layout stamps into filenames. Equivalent to the artifact store's
    /// own `key_fingerprint_hex`; kept in lock-step so a `KernelCache` and
    /// its underlying [`DiskArtifactStore`] always agree on the partition.
    #[must_use]
    pub fn of_key(key: &[u8; 32]) -> Self {
        let h = blake3::hash(&key[..]);
        let mut buf = [0u8; 16];
        hex::encode_to_slice(&h.as_bytes()[..8], &mut buf).expect("16 byte buf for 8 byte input");
        // `buf` is ASCII hex by construction, so the UTF-8 check never fails.
        Self(std::str::from_utf8(&buf).expect("hex is utf8").to_string())
    }

    /// Borrow the underlying hex string.
    #[must_use]
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl std::fmt::Display for KeyFingerprint {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

/// Disk-backed L2 cache.
///
/// ## T30 layering (current)
///
/// Stores each kernel as two cooperating on-disk artefacts under
/// [`DiskCacheConfig::dir`]:
///
/// 1. A *sidecar* file at [`Self::path_for`] (`*.ptxbin`) containing a
///    fixed-size record that maps the [`CacheKey`] to the
///    [`ContentHash`] of the underlying blob (16-byte magic
///    [`SIDECAR_MAGIC_V1`] + 32-byte content hash).
/// 2. A *blob* file under the
///    [`tensor_wasm_artifacts::DiskArtifactStore`] layout
///    (`*.<key-fp>.bin`) holding the streaming-encoded HMAC-SHA256 +
///    zstd envelope around the V2 kernel-manifest framing.
///
/// The V2 kernel-manifest framing (16-byte `TWJIT-KRNL-v2\0\0\0` magic +
/// length-prefixed body: blueprint, sm_version, grid_x, block_x,
/// ptx_len, ptx bytes) is built ABOVE the artifact store: a `put` first
/// serialises the kernel into a V2 envelope `Vec<u8>`, then hands that
/// to [`DiskArtifactStore::put`] which streams it through an HMAC-tee +
/// zstd encoder onto disk. A `get` reverses the layering: look up the
/// sidecar to get the [`ContentHash`], call [`DiskArtifactStore::get`]
/// which streaming-verifies the outer HMAC + decompresses, then parse
/// the inner V2 envelope to recover the kernel.
///
/// ## Why two files
///
/// [`DiskArtifactStore`] is content-addressed: lookups go through a
/// [`ContentHash`] derived from the payload bytes, not through a caller
/// key. The kernel cache needs lookups by tenant-scoped [`CacheKey`],
/// so the sidecar maps `CacheKey -> ContentHash` while the blob owns
/// the bytes. This preserves the streaming HMAC + zstd properties of
/// the store (T22) without baking the cache-key shape into the store
/// itself. Filenames stay partitioned by HMAC-key fingerprint on both
/// sides — the sidecar via `path_for`'s `{key_prefix}-…` prefix, the
/// blob via [`DiskArtifactStore`]'s own `{hash}.{key_fp}.bin` shape —
/// so two stores in the same directory under different keys cannot
/// race on the same path.
///
/// ## V2 envelope (current writer, body inside the artifact store)
///
/// ```text
/// [0..16)   magic = "TWJIT-KRNL-v2\0\0"
/// [16..24)  tenant_id (u64 LE)            <- tenant-binding defence-in-depth
/// [24..32)  blueprint fingerprint (u64 LE)
/// [32..36)  sm_version (u32 LE)
/// [36..40)  launch_geometry.grid_x (u32 LE)
/// [40..44)  launch_geometry.block_x (u32 LE)
/// [44..52)  ptx length (u64 LE)
/// [52..52+ptx_len)  PTX text (UTF-8, NOT null-terminated)
/// ```
///
/// ## Tenant binding (defence-in-depth)
///
/// The `tenant_id` word at `[16..24)` was added so the *persisted*
/// envelope binds the owning tenant, not merely the filename path
/// (`path_for` already folds `tenant_id` into the path hash, but the
/// path alone is the only thing scoping a blob to a tenant). Without
/// this, an attacker who can drop a well-formed, HMAC-valid sidecar +
/// blob at *another* tenant's `path_for` location would have it served
/// to that tenant on `get`. The reader now cross-checks
/// `tenant_on_disk == key.tenant_id` alongside the existing
/// fingerprint / sm_version defence-in-depth check and rejects a
/// mismatch as a miss.
///
/// The 16-byte magic stays `TWJIT-KRNL-v2\0\0\0` (the cross-version
/// compat invariant pinned by the artifact-store round-trip test); the
/// tenant word is an in-place, length-extending revision of the V2 body.
/// A genuinely pre-tenant V2 blob fed to this reader misaligns every
/// field and is rejected cleanly by the tenant / fingerprint / sm_version
/// cross-check (worst case a clean miss + rewrite on the next `put`) —
/// the L2 cache is regenerable, so no migration step is required.
///
/// The HMAC trailer that pre-T30 V2 sat at the end of the file is gone
/// from this layer — the artifact store provides streaming HMAC over
/// the entire envelope (T22), so a second per-envelope MAC would be
/// redundant.
///
/// ## V1 magic (read-only)
///
/// Pre-T30 V1/V2 files (magic `"TWJIT-KRNL-v1\0\0\0"` or
/// `"TWJIT-KRNL-v2\0\0\0"` at offset 0 of a `.ptxbin` file written by
/// the legacy raw-file writer) sit at a different path shape than the
/// new T30 sidecar and are silently invisible to the new reader: they
/// no longer occupy the post-T30 sidecar path, so a fresh `get` of
/// the corresponding key returns a clean miss and the next `put` rewrites
/// the entry as `(sidecar, blob)` under the unified store. This matches
/// the `cache.l2.miss.legacy_magic` behaviour of pre-T30 V1 detection
/// without any decoder support for the legacy raw layout.
struct DiskCache {
    /// Directory the cache writes to. Cloned out of the supplied
    /// [`DiskCacheConfig`] so we can drop the original config (and its
    /// plain `[u8; 32]` copy of the key) and keep only the zeroizing
    /// copy below as the long-lived owner.
    dir: PathBuf,
    /// 32-byte HMAC-keyed-BLAKE3 key, wiped on drop. `Zeroizing` Derefs
    /// to `[u8; 32]` so existing `&self.hmac_key[..]`-style call sites
    /// keep compiling. The artifact store holds an independent copy
    /// (also zeroising) — the duplication is intentional so the
    /// sidecar's path-prefix fingerprint and the store's own
    /// per-blob path-prefix fingerprint agree byte-for-byte without
    /// either side reaching into the other's private field.
    hmac_key: Zeroizing<[u8; 32]>,
    /// Cached 16-hex-char fingerprint of `hmac_key` — the `{key_prefix}`
    /// segment every `path_for` filename leads with.
    ///
    /// Perf: the HMAC-key fingerprint is `blake3(hmac_key)` truncated, which
    /// is constant for the cache's lifetime (the key never changes after
    /// construction). `path_for` used to recompute it via
    /// `blake3::hash(&self.hmac_key[..])` on *every* disk op; hoisting it
    /// here computes the digest exactly once in `new`. The per-cache-key
    /// digest in `path_for` genuinely varies per call and stays inline.
    /// This is *not* secret (it is already in every filename), so caching
    /// the rendered hex rather than the key bytes leaks nothing.
    key_prefix_hex: String,
    /// Underlying streaming content-addressed signed blob store.
    /// Holds the v2-envelope-wrapped kernel payloads. Wrapped in an
    /// `Arc` so the disk cache is cheaply clonable — `KernelCache`
    /// already holds the cache behind `Arc<DiskCache>`, this is the
    /// only field whose backend benefits from sharing.
    store: Arc<DiskArtifactStore>,
    /// Shared clone of the owning [`KernelCache`]'s integrity-rejection
    /// counter (backs `tensor_wasm_jit_cache_integrity_reject_total`).
    /// Bumped on the L2 read path when an entry is refused for a genuine
    /// integrity failure (artifact-store HMAC/content-hash mismatch or an
    /// inner-envelope magic mismatch) — i.e. a tamper signal, distinct
    /// from a benign miss (missing sidecar / legacy magic) which leaves
    /// this untouched.
    integrity_reject_total: Arc<AtomicU64>,
}

/// V2 magic for the inner kernel-manifest envelope wrapped inside each
/// artifact-store blob. Unchanged across the T30 migration so cross-
/// version compat is preserved: an older reader extracting this body
/// from any future archive can still parse it.
const DISK_CACHE_MAGIC_V2: &[u8; 16] = b"TWJIT-KRNL-v2\0\0\0";
/// V2 header: magic + tenant_id + fingerprint + sm_version + grid_x +
/// block_x + ptx_len. The `tenant_id` word is the tenant-binding
/// defence-in-depth field — see the [`DiskCache`] type-level "Tenant
/// binding" note.
const DISK_CACHE_HEADER_LEN_V2: usize = 16 + 8 + 8 + 4 + 4 + 4 + 8;

/// 16-byte sidecar magic. Stamped at the head of every `*.ptxbin`
/// sidecar so the reader can tell a T30 sidecar apart from any legacy
/// pre-T30 raw-V2 file that happens to live under the same filename
/// scheme. Pre-T30 files start with `TWJIT-KRNL-v2\0\0\0`; T30 sidecars
/// start with this distinct magic, so a stale legacy file is treated
/// as a miss and the next `put` overwrites it.
const SIDECAR_MAGIC_V1: &[u8; 16] = b"TWJIT-IDX-v1\0\0\0\0";
/// Sidecar total length: 16-byte magic + 32-byte BLAKE3 content hash.
const SIDECAR_LEN_V1: usize = 16 + 32;

impl DiskCache {
    fn new(mut cfg: DiskCacheConfig, integrity_reject_total: Arc<AtomicU64>) -> Self {
        // Move the key bytes into a `Zeroizing` newtype so the long-lived
        // copy is wiped on `DiskCache::drop`. We cannot partially move
        // fields out of `cfg` directly because `DiskCacheConfig`
        // implements `Drop` (jit S-3 T13) — the language forbids
        // partial moves out of a `Drop` type. Instead we `mem::take`
        // each field, leaving the source struct in a default-valued
        // state before its `Drop::drop` runs and zeroizes the (now
        // already-defaulted) `hmac_key` array a second time.
        let dir = std::mem::take(&mut cfg.dir);
        // Wrap the moved-out key in `Zeroizing` *immediately* (jit L4) so the
        // stack copy is wiped when this function returns, rather than left as
        // a plain `[u8; 32]` lingering in the frame after both consumers have
        // taken their own copies.
        let key_bytes = Zeroizing::new(std::mem::take(&mut cfg.hmac_key));
        // The artifact store gets its own copy of the same key so its
        // streaming HMAC matches the one the sidecar's path-prefix
        // fingerprint will agree with. Both copies are wrapped in
        // `Zeroizing` (the artifact store wraps internally) so neither
        // construction-time bytes linger after drop. `*key_bytes` derefs the
        // `Zeroizing` to hand the store its by-value `[u8; 32]`.
        let store = Arc::new(DiskArtifactStore::new(dir.clone(), *key_bytes));
        // Perf: the HMAC-key fingerprint (`blake3(hmac_key)` truncated to 8
        // bytes → 16 hex chars) is constant for the cache's lifetime, so
        // compute it once here rather than on every `path_for` call. See the
        // `key_prefix_hex` field doc.
        let key_fp = blake3::hash(&key_bytes[..]);
        let mut key_prefix_buf = [0u8; 16];
        hex::encode_to_slice(&key_fp.as_bytes()[..8], &mut key_prefix_buf)
            .expect("16 byte buf for 8 byte input");
        let key_prefix_hex = std::str::from_utf8(&key_prefix_buf)
            .expect("hex is utf8")
            .to_string();
        Self {
            dir,
            hmac_key: key_bytes,
            key_prefix_hex,
            store,
            integrity_reject_total,
        }
    }

    /// Build the on-disk path for a key. Hash the full key (including
    /// tenant_id and emit_config_hash) so two tenants cannot collide
    /// on the same blueprint+sm and so the file name itself does not
    /// leak the blueprint fingerprint to anyone with directory-list
    /// access.
    ///
    /// The filename is also prefixed with the first 8 bytes of
    /// `blake3::hash(hmac_key)` so two `KernelCache`s pointed at the
    /// same directory but configured with different HMAC keys produce
    /// *disjoint* paths. Without this prefix the two writers would
    /// race on the same final path (`tmp.persist` overwrites whichever
    /// landed first) and both readers would then fail the HMAC check
    /// on each other's writes — every put-then-get round-trip would
    /// look like a miss in steady state.
    ///
    /// The 8-byte HMAC-key fingerprint is NOT the key itself: it's
    /// `blake3::hash(key)` truncated, which is already publicly
    /// observable (anyone with directory-list access can read the
    /// filename). The actual MAC trailer still gates load — partitioning
    /// just avoids the inter-key collision, it is not a confidentiality
    /// boundary.
    ///
    /// Format: `{key_prefix:016x}-{cache_key_hex}.ptxbin`. The key
    /// prefix leads so `ls`-style directory listings group entries by
    /// the writing key — handy for operators rotating keys (each
    /// rotation lands under a new prefix and the old generation is
    /// trivially `rm`-able by prefix glob).
    fn path_for(&self, key: &CacheKey) -> PathBuf {
        let mut hasher = blake3::Hasher::new();
        hasher.update(b"tensor-wasm-jit::DiskCache::path::v1\0");
        hasher.update(&key.tenant_id.to_le_bytes());
        hasher.update(&key.blueprint.to_le_bytes());
        hasher.update(&key.sm_version.to_le_bytes());
        hasher.update(&key.emit_config_hash.to_le_bytes());
        let h = hasher.finalize();
        // First 16 bytes (32 hex chars) is plenty of entropy for filenames.
        //
        // T20 perf: render the digest into a fixed 32-byte stack buffer via
        // `hex::encode_to_slice` rather than 16× `format!("{b:02x}")` —
        // the per-byte `format!` path used to allocate 16 transient `String`s
        // (plus the `.collect()` target) on every disk-cache op. The slice
        // form writes ASCII hex directly into the buffer with no heap
        // traffic; `str::from_utf8` then borrows it as a `&str` for the
        // final `format!`-built filename.
        let digest = h.as_bytes();
        let mut cache_key_hex_buf = [0u8; 32];
        hex::encode_to_slice(&digest[..16], &mut cache_key_hex_buf)
            .expect("32 byte buf for 16 byte input");
        let cache_key_hex = std::str::from_utf8(&cache_key_hex_buf).expect("hex is utf8");
        // Perf: the HMAC-key fingerprint prefix is constant for the cache's
        // lifetime, so it was hoisted to a `key_prefix_hex` field computed
        // once in `DiskCache::new` rather than re-hashing `blake3(hmac_key)`
        // on every disk op. The per-cache-key digest above genuinely varies
        // per call and stays inline. See the `key_prefix_hex` field doc.
        let key_prefix_hex = &self.key_prefix_hex;
        self.dir
            .join(format!("{key_prefix_hex}-{cache_key_hex}.ptxbin"))
    }

    /// Encode a kernel into the inner V2 envelope (the same byte layout
    /// pre-T30 used at the head of every `.ptxbin` file, minus the
    /// per-envelope HMAC trailer — the streaming HMAC is now the
    /// artifact store's job). The result is what gets handed to
    /// [`DiskArtifactStore::put`].
    fn encode_v2_envelope(key: &CacheKey, kernel: &CachedKernel) -> Vec<u8> {
        let ptx_bytes = kernel.ptx.text.as_bytes();
        let (grid_x, block_x) = kernel.ptx.launch_geometry;
        let mut buf = Vec::with_capacity(DISK_CACHE_HEADER_LEN_V2 + ptx_bytes.len());
        buf.extend_from_slice(DISK_CACHE_MAGIC_V2);
        // jit L (tenant-isolation defence-in-depth): bind the owning tenant
        // into the persisted envelope, not just the filename path. The reader
        // asserts this matches the requesting key so a sidecar+blob planted at
        // another tenant's `path_for` location is rejected. See the `DiskCache`
        // "Tenant binding" doc.
        buf.extend_from_slice(&key.tenant_id.to_le_bytes());
        buf.extend_from_slice(&key.blueprint.to_le_bytes());
        buf.extend_from_slice(&key.sm_version.to_le_bytes());
        // jit S-3 follow-up: persist `launch_geometry` so L2 hits round-trip
        // the (grid_x, block_x) hint that `ptx_emit` populates. Prior to V2
        // the reconstructor defaulted this to (0, 0) on every disk hit.
        buf.extend_from_slice(&grid_x.to_le_bytes());
        buf.extend_from_slice(&block_x.to_le_bytes());
        buf.extend_from_slice(&(ptx_bytes.len() as u64).to_le_bytes());
        buf.extend_from_slice(ptx_bytes);
        buf
    }

    /// Write a kernel via the layered `(sidecar -> blob)` representation:
    ///
    /// 1. Build the inner V2 envelope `Vec<u8>` (cross-version-compat
    ///    format, T12-aligned).
    /// 2. Hand the envelope bytes to [`DiskArtifactStore::put`] which
    ///    streams HMAC + zstd onto disk under a content-addressed path
    ///    and returns the [`ContentHash`] of the envelope.
    /// 3. Write a small sidecar at [`Self::path_for`] mapping the
    ///    [`CacheKey`] to that [`ContentHash`] via an atomic temp-then-
    ///    rename, so a partial write never strands a half-formed
    ///    sidecar that a concurrent reader might trip over.
    ///
    /// The artifact store inherits T22's streaming property: the
    /// envelope's bytes are not re-buffered into another `Vec` before
    /// HMAC + zstd — the store's `put` tees through a `MacWriter` /
    /// zstd encoder pipeline straight to the file. The only buffered
    /// allocation here is the v2 envelope itself, which is bounded by
    /// the kernel's PTX size and lives just long enough to be consumed
    /// by `store.put`.
    fn put(&self, key: &CacheKey, kernel: &CachedKernel) -> std::io::Result<()> {
        use std::io::Write;
        std::fs::create_dir_all(&self.dir)?;
        // Step 1: build the inner V2 envelope (no per-envelope MAC —
        // the artifact store's HMAC covers it transitively).
        let envelope = Self::encode_v2_envelope(key, kernel);
        // Step 2: stream the envelope through the artifact store. The
        // store handles atomic temp-then-rename of the blob itself.
        let hash = self.store.put(&envelope).map_err(|e| {
            // Wrap as `io::Error` so the existing `Result<(), io::Error>`
            // signature of `DiskCache::put` (and the warn-level log path
            // in `KernelCache::put`) stays unchanged.
            std::io::Error::other(e.to_string())
        })?;
        // Step 3: stamp the sidecar atomically. The sidecar is small
        // (48 bytes) and lives at a path derived from the cache key, so
        // the lookup side only needs one read to find the content
        // hash and one more (through the artifact store) to fetch the
        // verified envelope bytes.
        let mut sidecar = Vec::with_capacity(SIDECAR_LEN_V1);
        sidecar.extend_from_slice(SIDECAR_MAGIC_V1);
        sidecar.extend_from_slice(hash.as_bytes());
        debug_assert_eq!(sidecar.len(), SIDECAR_LEN_V1);
        let sidecar_path = self.path_for(key);
        let mut tmp = tempfile::NamedTempFile::new_in(&self.dir)?;
        tmp.as_file_mut().write_all(&sidecar)?;
        tmp.persist(&sidecar_path).map_err(std::io::Error::other)?;
        Ok(())
    }

    /// Read and verify a kernel from disk. Returns `Ok(None)` on a
    /// genuine miss (sidecar does not exist), `Err` on I/O failure,
    /// and `Ok(None)` (with a warn-level log) on any integrity failure
    /// — magic mismatch on the sidecar, blob lookup failure,
    /// artifact-store HMAC mismatch, or envelope header mismatch. The
    /// loader treats every integrity failure as "no such entry" so a
    /// poisoned file behaves identically to a fresh cache.
    fn get(&self, key: &CacheKey) -> std::io::Result<Option<CachedKernel>> {
        // ---- Sidecar lookup. ----
        let sidecar_path = self.path_for(key);
        let sidecar = match std::fs::read(&sidecar_path) {
            Ok(b) => b,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => return Ok(None),
            Err(e) => return Err(e),
        };
        if sidecar.len() != SIDECAR_LEN_V1 {
            tracing::warn!(
                target: "tensor_wasm_jit::cache",
                file = %sidecar_path.display(),
                len = sidecar.len(),
                "disk-cache sidecar wrong length; treating as miss"
            );
            return Ok(None);
        }
        if &sidecar[..16] != SIDECAR_MAGIC_V1 {
            // Either a legacy pre-T30 raw-V2 record sitting at the same
            // path (TWJIT-KRNL-v2 magic) or unrelated garbage. Either
            // way the new reader does not understand it — treat as a
            // miss so the next `put` rewrites it under the T30 layout.
            tracing::info!(
                target: "tensor_wasm_jit::cache",
                file = %sidecar_path.display(),
                "cache.l2.miss.legacy_or_unknown_magic: sidecar will be rewritten on next put"
            );
            return Ok(None);
        }
        let mut hash_bytes = [0u8; 32];
        hash_bytes.copy_from_slice(&sidecar[16..48]);
        let content_hash = ContentHash::from_bytes(hash_bytes);

        // ---- Streaming-verified blob fetch via the artifact store. ----
        //
        // The store handles HMAC-SHA256 verification (constant-time),
        // zstd decompression with a [`MAX_DECOMPRESSED_LEN`] cap, and
        // content-hash defence-in-depth. Any failure (NotFound,
        // BadHmac, HashMismatch, …) collapses to a miss here, mirroring
        // the pre-T30 reader's "log + return Ok(None)" convention so
        // the call site's `cache.misses_total` counter still ticks
        // correctly on integrity rejection.
        let envelope = match self.store.get(&content_hash) {
            Ok(bytes) => bytes,
            Err(ArtifactError::NotFound(_)) => {
                tracing::warn!(
                    target: "tensor_wasm_jit::cache",
                    file = %sidecar_path.display(),
                    "disk-cache sidecar references missing artifact blob; treating as miss"
                );
                return Ok(None);
            }
            Err(e) => {
                // Anything that is not a clean NotFound — BadHmac,
                // HashMismatch, decompression-bomb cap, … — is a genuine
                // integrity failure (tamper signal), so bump the dedicated
                // integrity-rejection counter in addition to the call
                // site's miss counter.
                self.integrity_reject_total.fetch_add(1, Ordering::Relaxed);
                tracing::warn!(
                    target: "tensor_wasm_jit::cache",
                    file = %sidecar_path.display(),
                    error = %e,
                    "disk-cache artifact-store read failed; treating as miss"
                );
                return Ok(None);
            }
        };

        // ---- Parse the inner V2 envelope. ----
        if envelope.len() < DISK_CACHE_HEADER_LEN_V2 {
            tracing::warn!(
                target: "tensor_wasm_jit::cache",
                file = %sidecar_path.display(),
                len = envelope.len(),
                "disk-cache V2 envelope too short; treating as miss"
            );
            return Ok(None);
        }
        if &envelope[..16] != DISK_CACHE_MAGIC_V2 {
            // The artifact store already HMAC-verified the blob, so a bad
            // envelope magic here means the *verified* bytes are not a
            // kernel envelope — corruption/tamper inside the signed
            // payload. Count it as an integrity rejection.
            self.integrity_reject_total.fetch_add(1, Ordering::Relaxed);
            tracing::warn!(
                target: "tensor_wasm_jit::cache",
                file = %sidecar_path.display(),
                "disk-cache V2 envelope magic mismatch; treating as miss"
            );
            return Ok(None);
        }
        // Header integrity is implied by the artifact store's HMAC, but
        // cross-check tenant_id, fingerprint and sm_version against the
        // requested key as defence-in-depth — a sidecar that points at a
        // foreign blob (e.g. someone hand-edited the sidecar's content hash)
        // would still survive the store's MAC because each blob carries
        // its own self-consistent envelope; only this final check
        // refuses the mismatch.
        let mut tenant_bytes = [0u8; 8];
        tenant_bytes.copy_from_slice(&envelope[16..24]);
        let mut bp_bytes = [0u8; 8];
        bp_bytes.copy_from_slice(&envelope[24..32]);
        let mut sm_bytes = [0u8; 4];
        sm_bytes.copy_from_slice(&envelope[32..36]);
        let mut grid_x_bytes = [0u8; 4];
        grid_x_bytes.copy_from_slice(&envelope[36..40]);
        let mut block_x_bytes = [0u8; 4];
        block_x_bytes.copy_from_slice(&envelope[40..44]);
        let mut len_bytes = [0u8; 8];
        len_bytes.copy_from_slice(&envelope[44..52]);
        let tenant_on_disk = u64::from_le_bytes(tenant_bytes);
        let fingerprint_on_disk = u64::from_le_bytes(bp_bytes);
        let sm_version_on_disk = u32::from_le_bytes(sm_bytes);
        let grid_x_on_disk = u32::from_le_bytes(grid_x_bytes);
        let block_x_on_disk = u32::from_le_bytes(block_x_bytes);
        let ptx_len_on_disk = u64::from_le_bytes(len_bytes) as usize;
        // jit L (tenant-isolation defence-in-depth): the persisted tenant_id
        // MUST match the requesting key. The filename path already partitions
        // by tenant (path_for folds tenant_id into the hash), but binding the
        // tenant *inside* the signed envelope closes the gap where a planted
        // sidecar+blob at another tenant's path would otherwise be served to
        // that tenant. A genuine pre-tenant V2 blob also lands here (its
        // misaligned fields will not satisfy this check) and is rejected as a
        // clean miss to be rewritten on the next put.
        if tenant_on_disk != key.tenant_id
            || fingerprint_on_disk != key.blueprint
            || sm_version_on_disk != key.sm_version
        {
            // Fires only on a HMAC-verified blob whose header does not match
            // the requested key — a hand-edited sidecar pointing at a
            // foreign blob, or a blob planted under a foreign tenant's path.
            // Integrity rejection.
            self.integrity_reject_total.fetch_add(1, Ordering::Relaxed);
            tracing::warn!(
                target: "tensor_wasm_jit::cache",
                file = %sidecar_path.display(),
                tenant = key.tenant_id,
                tenant_on_disk,
                "disk-cache V2 envelope header key mismatch (tenant/fingerprint/sm); treating as miss"
            );
            return Ok(None);
        }
        let ptx_start = DISK_CACHE_HEADER_LEN_V2;
        let ptx_end = ptx_start.saturating_add(ptx_len_on_disk);
        if ptx_end > envelope.len() {
            // Declared length overruns the verified envelope — structural
            // corruption inside the signed payload. Integrity rejection.
            self.integrity_reject_total.fetch_add(1, Ordering::Relaxed);
            tracing::warn!(
                target: "tensor_wasm_jit::cache",
                file = %sidecar_path.display(),
                "disk-cache declared ptx_len overruns envelope; treating as miss"
            );
            return Ok(None);
        }
        let ptx_text = match std::str::from_utf8(&envelope[ptx_start..ptx_end]) {
            Ok(s) => s.to_string(),
            Err(_) => {
                // Non-UTF-8 PTX in a verified blob — corruption/tamper.
                self.integrity_reject_total.fetch_add(1, Ordering::Relaxed);
                tracing::warn!(
                    target: "tensor_wasm_jit::cache",
                    file = %sidecar_path.display(),
                    "disk-cache PTX bytes are not valid UTF-8; treating as miss"
                );
                return Ok(None);
            }
        };
        // Reconstruct the kernel via the integrity-aware constructor so
        // the L1 cache accepts it without further verification work.
        // V2 persists the emit-time `launch_geometry` hint; reading it
        // back here closes the lost-geometry bug (previously this defaulted
        // to (0, 0) and the dispatch path silently fell back to guest-
        // declared launch params for every L2 hit).
        let ptx = Arc::new(EmittedPtx {
            text: ptx_text,
            launch_geometry: (grid_x_on_disk, block_x_on_disk),
        });
        Ok(Some(CachedKernel::new(
            fingerprint_on_disk,
            ptx,
            CompiledHandle::default(),
        )))
    }

    /// Fingerprint of this cache's *own* (currently-active) HMAC key — the
    /// partition prefix every file this cache writes lands under.
    fn active_key_fingerprint(&self) -> KeyFingerprint {
        KeyFingerprint::of_key(&self.hmac_key)
    }

    /// Parse the key-fingerprint segment out of a cache file name.
    ///
    /// Two on-disk shapes carry the fingerprint:
    ///   * sidecars `{fp}-{cache_key}.ptxbin` — fingerprint is the segment
    ///     before the first `-`.
    ///   * artifact blobs `{content_hash}.{fp}.bin` — fingerprint is the
    ///     second-to-last `.`-delimited segment.
    ///
    /// Returns `None` for any name that does not match one of these shapes
    /// (so unrelated files in the directory are never touched by `gc`).
    fn fingerprint_of_filename(name: &str) -> Option<KeyFingerprint> {
        if let Some(rest) = name.strip_suffix(".ptxbin") {
            // `{fp}-{cache_key}` — split on the first '-'.
            let fp = rest.split('-').next()?;
            if fp.is_empty() || fp.len() != 16 {
                return None;
            }
            return Some(KeyFingerprint(fp.to_string()));
        }
        if let Some(rest) = name.strip_suffix(".bin") {
            // `{content_hash}.{fp}` — fingerprint is the trailing segment.
            let fp = rest.rsplit('.').next()?;
            if fp.len() != 16 {
                return None;
            }
            return Some(KeyFingerprint(fp.to_string()));
        }
        None
    }

    /// Enumerate the distinct HMAC-key fingerprints with at least one file
    /// (sidecar or artifact blob) present in the cache directory. A missing
    /// directory yields an empty set (a fresh cache has nothing to sweep).
    fn key_fingerprints_on_disk(&self) -> std::io::Result<Vec<KeyFingerprint>> {
        let mut seen = std::collections::BTreeSet::new();
        let rd = match std::fs::read_dir(&self.dir) {
            Ok(rd) => rd,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                return Ok(Vec::new());
            }
            Err(e) => return Err(e),
        };
        for entry in rd {
            let entry = entry?;
            if let Some(name) = entry.file_name().to_str() {
                if let Some(fp) = Self::fingerprint_of_filename(name) {
                    seen.insert(fp);
                }
            }
        }
        Ok(seen.into_iter().collect())
    }

    /// Garbage-collect stale-fingerprint files: remove every sidecar /
    /// artifact-blob whose key fingerprint is NOT in `retain`, returning
    /// the count of files removed.
    ///
    /// The active key's fingerprint is unconditionally retained even if a
    /// caller forgets to list it — this is the safety guarantee in the
    /// public-API docs: `gc` only sweeps *stale* generations, never the
    /// live one. Files that do not match a known cache-file shape (anything
    /// `fingerprint_of_filename` rejects) are left untouched.
    fn gc(&self, retain: &[KeyFingerprint]) -> std::io::Result<usize> {
        let active = self.active_key_fingerprint();
        let rd = match std::fs::read_dir(&self.dir) {
            Ok(rd) => rd,
            Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                return Ok(0);
            }
            Err(e) => return Err(e),
        };
        let mut removed = 0usize;
        for entry in rd {
            let entry = entry?;
            let name_os = entry.file_name();
            let name = match name_os.to_str() {
                Some(n) => n,
                None => continue,
            };
            let fp = match Self::fingerprint_of_filename(name) {
                Some(fp) => fp,
                None => continue, // not one of ours — leave it alone
            };
            // Never sweep the active key's files; never sweep a retained one.
            if fp == active || retain.contains(&fp) {
                continue;
            }
            match std::fs::remove_file(entry.path()) {
                Ok(()) => removed += 1,
                Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
                    // Raced with another sweeper / writer; treat as already gone.
                }
                Err(e) => return Err(e),
            }
        }
        Ok(removed)
    }
}

impl Default for KernelCache {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ir::{ElemType, TensorWasmKernelBlueprint, TensorWasmOp};
    use crate::ptx_emit::EmittedPtx;
    use std::thread;

    fn dummy_kernel(fp: u64) -> CachedKernel {
        // Route through `CachedKernel::new` so `integrity_hash` matches the
        // PTX text by construction; `KernelCache::put` rejects entries
        // whose hash disagrees with the text (jit S-3).
        CachedKernel::new(
            fp,
            Arc::new(EmittedPtx {
                text: String::new(),
                launch_geometry: (1, 1),
            }),
            CompiledHandle::default(),
        )
    }

    #[test]
    fn put_then_get() {
        let cache = KernelCache::new();
        let key = CacheKey::for_tenant(TenantId(7), 1, 80);
        cache.put(key, dummy_kernel(1));
        assert_eq!(cache.get(&key).unwrap().fingerprint, 1);
    }

    /// Build a kernel whose PTX text is exactly `bytes` long so byte-cap
    /// tests can reason about `total_bytes` precisely.
    fn sized_kernel(fp: u64, bytes: usize) -> CachedKernel {
        CachedKernel::new(
            fp,
            Arc::new(EmittedPtx {
                text: "x".repeat(bytes),
                launch_geometry: (1, 1),
            }),
            CompiledHandle::default(),
        )
    }

    /// Byte-cap eviction: with a generous count cap but a tight byte cap,
    /// inserting oversized blueprints must keep the resident byte total
    /// bounded by evicting LRU entries, and the eviction order must be LRU.
    #[test]
    fn byte_cap_evicts_lru_and_bounds_total_bytes() {
        // Count cap of 100 (won't bind) but a 250-byte total cap. Each
        // entry is 100 bytes, so at most 2 entries fit (200 <= 250; a 3rd
        // would push to 300 > 250).
        let cache = KernelCache::with_config(
            KernelCacheConfig::default()
                .with_capacity(100)
                .with_max_total_bytes(250),
        );
        let k1 = CacheKey::for_tenant(TenantId(0), 1, 80);
        let k2 = CacheKey::for_tenant(TenantId(0), 2, 80);
        let k3 = CacheKey::for_tenant(TenantId(0), 3, 80);

        cache.put(k1, sized_kernel(1, 100));
        cache.put(k2, sized_kernel(2, 100));
        assert_eq!(cache.total_bytes(), 200, "two 100-byte entries fit");
        assert_eq!(cache.len(), 2);

        // Inserting the third 100-byte entry overflows the byte cap (300 >
        // 250). The LRU entry (k1) must be evicted, bringing the total back
        // to 200 and keeping k2 + k3.
        cache.put(k3, sized_kernel(3, 100));
        assert!(
            cache.total_bytes() <= 250,
            "byte total must stay bounded by the cap, got {}",
            cache.total_bytes()
        );
        assert_eq!(cache.total_bytes(), 200);
        assert_eq!(cache.len(), 2);
        assert!(
            cache.get(&k1).is_none(),
            "k1 is the LRU and must be evicted"
        );
        assert!(cache.get(&k2).is_some(), "k2 must survive");
        assert!(cache.get(&k3).is_some(), "k3 was just inserted");
    }

    /// A single entry larger than the byte cap is still admitted (the cache
    /// never wedges dispatch by refusing an insert) but it evicts every
    /// other entry, so the byte total settles at just that one oversized
    /// entry.
    #[test]
    fn byte_cap_admits_single_oversized_entry() {
        let cache = KernelCache::with_config(
            KernelCacheConfig::default()
                .with_capacity(100)
                .with_max_total_bytes(50),
        );
        let small = CacheKey::for_tenant(TenantId(0), 1, 80);
        let big = CacheKey::for_tenant(TenantId(0), 2, 80);
        cache.put(small, sized_kernel(1, 40));
        cache.put(big, sized_kernel(2, 1000));
        // The oversized entry is served; the small one was evicted to make
        // room. Total transiently exceeds the cap by exactly that one entry.
        assert_eq!(cache.len(), 1);
        assert_eq!(cache.total_bytes(), 1000);
        assert!(
            cache.get(&big).is_some(),
            "oversized entry must still serve"
        );
        assert!(cache.get(&small).is_none(), "smaller LRU entry evicted");
    }

    /// `total_bytes` must track replacement (same key re-inserted with a
    /// different-sized payload) as a delta, not an add.
    #[test]
    fn total_bytes_tracks_replacement() {
        let cache = KernelCache::new();
        let key = CacheKey::for_tenant(TenantId(0), 1, 80);
        cache.put(key, sized_kernel(1, 100));
        assert_eq!(cache.total_bytes(), 100);
        // Replace the same key with a larger payload — net delta is +50.
        cache.put(key, sized_kernel(1, 150));
        assert_eq!(cache.len(), 1);
        assert_eq!(cache.total_bytes(), 150);
    }

    /// Default config keeps the byte cap disabled (count-only behaviour),
    /// while `total_bytes` still tracks the live total.
    #[test]
    fn byte_cap_default_is_none() {
        let cache = KernelCache::new();
        assert_eq!(cache.max_total_bytes(), None);
        cache.put(
            CacheKey::for_tenant(TenantId(0), 1, 80),
            sized_kernel(1, 512),
        );
        assert_eq!(cache.total_bytes(), 512);
    }

    #[test]
    fn lru_evicts_oldest() {
        let cache = KernelCache::with_capacity(2);
        let k1 = CacheKey::for_tenant(TenantId(0), 1, 80);
        let k2 = CacheKey::for_tenant(TenantId(0), 2, 80);
        let k3 = CacheKey::for_tenant(TenantId(0), 3, 80);
        cache.put(k1, dummy_kernel(1));
        cache.put(k2, dummy_kernel(2));
        cache.put(k3, dummy_kernel(3));
        assert!(cache.get(&k1).is_none(), "k1 should have been evicted");
        assert!(cache.get(&k2).is_some());
        assert!(cache.get(&k3).is_some());
        assert_eq!(cache.len(), 2);
    }

    #[test]
    fn lookup_by_blueprint() {
        let cache = KernelCache::new();
        let bp = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        });
        let tenant = TenantId(11);
        let key = CacheKey::for_tenant(tenant, bp.fingerprint(), 80);
        cache.put(key, dummy_kernel(bp.fingerprint()));
        assert!(cache.get_for(tenant, &bp, 80).is_some());
        assert!(
            cache.get_for(tenant, &bp, 89).is_none(),
            "different sm_version is a miss"
        );
        assert!(
            cache.get_for(TenantId(12), &bp, 80).is_none(),
            "different tenant is a miss — keys are tenant-scoped"
        );
    }

    /// jit HIGH (finding 2): a kernel cached under an `f32x4.add` blueprint
    /// must NOT be returned for an `i32x4.add` blueprint. Before the
    /// element-type fix both blueprints fingerprinted identically, so the
    /// integer lookup would HIT the float kernel — cross-kernel cache
    /// aliasing serving a miscompiled kernel. The lookup now misses, which
    /// drives a correct (integer) emit.
    #[test]
    fn element_type_is_not_cache_aliased() {
        let cache = KernelCache::new();
        let tenant = TenantId(7);
        let f32_bp = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::F32,
            lanes: 4,
        });
        let i32_bp = TensorWasmKernelBlueprint::new("k").push(TensorWasmOp::VecAdd {
            elem: ElemType::I32,
            lanes: 4,
        });
        // Distinct cache keys (the rewrite-time pre-population keys on this).
        assert_ne!(f32_bp.fingerprint(), i32_bp.fingerprint());

        // Populate ONLY the f32 kernel.
        let key = CacheKey::for_tenant(tenant, f32_bp.fingerprint(), 80);
        cache.put(key, dummy_kernel(f32_bp.fingerprint()));

        // The f32 lookup hits; the i32 lookup must MISS (no aliasing).
        assert!(cache.get_for(tenant, &f32_bp, 80).is_some());
        assert!(
            cache.get_for(tenant, &i32_bp, 80).is_none(),
            "i32x4.add must not alias the f32x4.add cache slot"
        );
    }

    #[test]
    fn capacity_floor_one() {
        let cache = KernelCache::with_capacity(0);
        assert_eq!(cache.capacity(), 1);
    }

    #[test]
    fn empty_when_new() {
        let cache = KernelCache::new();
        assert!(cache.is_empty());
        assert_eq!(cache.len(), 0);
    }

    #[test]
    fn cache_hit_returns_arc_shared_ptx() {
        use std::sync::Arc;
        let cache = KernelCache::new();
        let bp = TensorWasmKernelBlueprint::new("matmul").push(TensorWasmOp::MatMul {
            m: 16,
            n: 16,
            k: 16,
        });
        let key = CacheKey::for_tenant(TenantId(3), bp.fingerprint(), 80);
        let original = Arc::new(EmittedPtx {
            text: "// pre-emitted".into(),
            launch_geometry: (1, 128),
        });
        cache.put(
            key,
            CachedKernel::new(
                bp.fingerprint(),
                original.clone(),
                CompiledHandle::default(),
            ),
        );
        let hit = cache.get(&key).expect("cache hit");
        // The hit returns the same underlying allocation — no re-emit.
        assert!(Arc::ptr_eq(&hit.ptx, &original));
    }

    /// Concurrent get/put across many threads must not corrupt the cache
    /// or drop entries that haven't been evicted. Uses a capacity large
    /// enough to hold every key inserted so we can assert all `get`s
    /// targeting still-present keys succeed.
    #[test]
    fn concurrent_get_put_dashmap_safe() {
        const N_THREADS: usize = 8;
        const KEYS_PER_THREAD: u64 = 32;
        let cache = KernelCache::with_capacity((N_THREADS as u64 * KEYS_PER_THREAD) as usize);
        let mut handles = Vec::new();
        for t in 0..N_THREADS {
            let cache = cache.clone();
            handles.push(thread::spawn(move || {
                for i in 0..KEYS_PER_THREAD {
                    let key =
                        CacheKey::for_tenant(TenantId(0), (t as u64) * KEYS_PER_THREAD + i, 80);
                    cache.put(key, dummy_kernel(key.blueprint));
                    // Interleave reads of own and (possibly absent) others.
                    let _ = cache.get(&key);
                }
            }));
        }
        for h in handles {
            h.join().expect("worker thread panicked");
        }
        // Every key written must still be retrievable.
        for t in 0..N_THREADS {
            for i in 0..KEYS_PER_THREAD {
                let key = CacheKey::for_tenant(TenantId(0), (t as u64) * KEYS_PER_THREAD + i, 80);
                assert!(
                    cache.get(&key).is_some(),
                    "missing key after concurrent inserts: ({t}, {i})"
                );
            }
        }
    }

    /// T20 perf regression: `KernelCache::get` returns a shared
    /// `Arc<CachedKernel>` rather than a cloned wrapper value. Two
    /// successive hits on the same key must yield pointer-equal Arcs
    /// (same allocation, just a refcount bump per hit) — proving the
    /// hot path no longer clones the 32-byte integrity hash, the
    /// fingerprint word, and the inner `Arc<EmittedPtx>` refcount on
    /// every dispatch.
    #[test]
    fn cache_get_returns_arc_no_clone() {
        let cache = KernelCache::new();
        let key = CacheKey::for_tenant(TenantId(1), 0xABCD, 80);
        let original_ptx = Arc::new(EmittedPtx {
            text: ".visible .entry t20_arc(){}".into(),
            launch_geometry: (1, 32),
        });
        cache.put(
            key,
            CachedKernel::new(0xABCD, original_ptx.clone(), CompiledHandle::default()),
        );
        let first = cache.get(&key).expect("first hit");
        let second = cache.get(&key).expect("second hit");
        // The two `Arc<CachedKernel>` handles must point at the same
        // allocation. If `get` had reverted to cloning the wrapper, the
        // two handles would point at distinct allocations (still sharing
        // the inner `Arc<EmittedPtx>`, but that is a different invariant).
        assert!(
            Arc::ptr_eq(&first, &second),
            "cache.get must return refcount-bumped Arc handles, not cloned \
             wrappers — distinct allocations indicate the T20 perf fix \
             regressed (clone-on-hit reintroduced)"
        );
        // Bonus: the inner PTX is still shared with the original (this was
        // already pinned by `cache_hit_returns_arc_shared_ptx` above).
        assert!(Arc::ptr_eq(&first.ptx, &original_ptx));
    }

    /// T20 perf: `get` increments `cache_hits_total` on every Some-returning
    /// call and `cache_misses_total` on every None-returning call. A miss
    /// followed by a hit must produce one of each, with neither counter
    /// double-counting.
    #[test]
    fn cache_hits_misses_counter() {
        let cache = KernelCache::new();
        assert_eq!(cache.cache_hits_total(), 0);
        assert_eq!(cache.cache_misses_total(), 0);

        let key = CacheKey::for_tenant(TenantId(9), 0xC0FFEE, 80);

        // Miss: nothing has been inserted yet.
        assert!(cache.get(&key).is_none(), "fresh cache must miss");
        assert_eq!(
            cache.cache_misses_total(),
            1,
            "first miss must bump the miss counter exactly once"
        );
        assert_eq!(
            cache.cache_hits_total(),
            0,
            "a miss must not bump the hit counter"
        );

        // Hit: install then look up.
        cache.put(key, dummy_kernel(0xC0FFEE));
        assert!(cache.get(&key).is_some(), "post-put get must hit");
        assert_eq!(
            cache.cache_hits_total(),
            1,
            "first hit must bump the hit counter exactly once"
        );
        assert_eq!(
            cache.cache_misses_total(),
            1,
            "a hit must not bump the miss counter"
        );

        // Second hit increments hits again.
        assert!(cache.get(&key).is_some());
        assert_eq!(cache.cache_hits_total(), 2);
        assert_eq!(cache.cache_misses_total(), 1);
    }

    /// jit L (tenant-isolation defence-in-depth): the V2 disk envelope now
    /// binds `tenant_id`, and the L2 reader cross-checks it against the
    /// requesting key. A sidecar+blob planted at another tenant's
    /// `path_for` location (simulated here by writing under tenant A's key,
    /// then reading the *exact same path* with a key that differs only in
    /// `tenant_id`) must be rejected as a miss and bump the
    /// integrity-rejection counter, rather than being served cross-tenant.
    #[test]
    fn disk_envelope_binds_tenant_and_rejects_cross_tenant_blob() {
        let tmp = tempfile::TempDir::new().expect("tempdir");
        let dir = tmp.path().to_path_buf();
        let hmac_key = [0x5Au8; 32];

        let cache = KernelCache::new().with_disk_persistence(DiskCacheConfig {
            dir: dir.clone(),
            hmac_key,
        });
        let disk = cache.disk.as_ref().expect("disk configured");

        // Same blueprint/sm/emit_config for two tenants; the only difference
        // is `tenant_id`. `path_for` already folds tenant_id into the hash,
        // so a real put lands tenant A and tenant B at different paths — to
        // simulate a *planted* blob we deliberately write A's envelope to B's
        // path and confirm the in-envelope tenant check still rejects it.
        let key_a = CacheKey::for_tenant(TenantId(100), 0xAA, 80);
        let key_b = CacheKey::for_tenant(TenantId(200), 0xAA, 80);

        let kernel = CachedKernel::new(
            0xAA,
            Arc::new(EmittedPtx {
                text: ".visible .entry tenant_bound(){}".into(),
                launch_geometry: (4, 8),
            }),
            CompiledHandle::default(),
        );

        // Honest round-trip for tenant A succeeds and round-trips geometry.
        disk.put(&key_a, &kernel).expect("put A");
        let hit_a = disk.get(&key_a).expect("get A ok").expect("A present");
        assert_eq!(hit_a.fingerprint, 0xAA);
        assert_eq!(hit_a.ptx.launch_geometry, (4, 8));

        // Plant A's blob at B's sidecar path: write A's envelope through the
        // artifact store, then stamp a sidecar at B's `path_for` pointing at
        // it. This mimics an attacker who can drop files at B's path.
        let envelope = DiskCache::encode_v2_envelope(&key_a, &kernel);
        let hash = disk.store.put(&envelope).expect("store put");
        let mut sidecar = Vec::with_capacity(SIDECAR_LEN_V1);
        sidecar.extend_from_slice(SIDECAR_MAGIC_V1);
        sidecar.extend_from_slice(hash.as_bytes());
        std::fs::write(disk.path_for(&key_b), &sidecar).expect("plant sidecar");

        let before = cache.integrity_reject_total();
        // Reading as tenant B must reject: the envelope's bound tenant (A)
        // does not match B's key, so the cross-check fires.
        assert!(
            disk.get(&key_b).expect("get B ok").is_none(),
            "a blob bound to tenant A must not be served to tenant B"
        );
        assert_eq!(
            cache.integrity_reject_total(),
            before + 1,
            "cross-tenant blob must count as an integrity rejection"
        );
    }

    /// jit S-3 T13 regression: `DiskCacheConfig`'s `Debug` impl MUST NOT
    /// dump the raw `hmac_key` bytes. A derived `Debug` would have
    /// formatted the array as `[222, 222, 222, …]` (or `[de, de, …]`
    /// under hex), leaking the server-side secret into any log line,
    /// panic message, or `tracing` field expansion that happens to
    /// include a `DiskCacheConfig`.
    ///
    /// We use the sentinel byte `0xDE` repeated 32 times because the
    /// derived `Debug` would have produced either `222` (decimal —
    /// stdlib default for `u8` arrays) or `de` (hex) at every position;
    /// asserting that neither pattern appears anywhere in the formatted
    /// output catches both cases. We also positively assert the
    /// "redacted" substring is present so the test fails informatively
    /// if someone replaces the manual impl with something else that
    /// happens to omit the bytes but also omits the redaction marker.
    #[test]
    fn disk_cache_config_debug_redacts_hmac_key() {
        let cfg = DiskCacheConfig {
            dir: PathBuf::from("/tmp/tensor-wasm-jit-debug-test"),
            hmac_key: [0xDEu8; 32],
        };
        let dbg = format!("{cfg:?}");
        let lowered = dbg.to_ascii_lowercase();
        // Negative: neither decimal nor hex representation of the key
        // bytes should appear in the formatted output. Derived `Debug`
        // for `[u8; 32]` would have produced `222` thirty-two times
        // (decimal) or `de` thirty-two times (hex/upper-hex
        // alternatives); a single occurrence of either is suspicious,
        // but to keep the assertion robust against incidental
        // substrings in `dir` we count occurrences instead.
        let decimal_hits = lowered.matches("222").count();
        let hex_hits = lowered.matches("de").count();
        assert!(
            decimal_hits < 32,
            "DiskCacheConfig Debug output appears to contain the raw key in \
             decimal form ({decimal_hits} occurrences of \"222\"): {dbg}"
        );
        assert!(
            hex_hits < 32,
            "DiskCacheConfig Debug output appears to contain the raw key in \
             hex form ({hex_hits} occurrences of \"de\"): {dbg}"
        );
        // Positive: the redaction marker is present.
        assert!(
            lowered.contains("redacted"),
            "DiskCacheConfig Debug output should mark hmac_key as redacted: {dbg}"
        );
    }
}
