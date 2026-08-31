// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Regression test for exec S-7: cross-tenant kernel-cache lookup must miss.
//!
//! Tenant A pre-installs a compiled kernel at fingerprint `0x2A` for
//! `sm_80`. Tenant B issues a dispatch with the SAME fingerprint and
//! sm_version. The cache must NOT return tenant A's entry to tenant B —
//! otherwise on the CUDA path tenant B would execute tenant A's kernel
//! against tenant B's memory, the confused-deputy primitive `CacheKey`'s
//! `tenant_id` field exists to prevent.

use std::sync::Arc;

use tensor_wasm_core::types::TenantId;
use tensor_wasm_jit::cache::{CacheKey, CachedKernel, CompiledHandle, KernelCache};
use tensor_wasm_jit::ptx_emit::EmittedPtx;

fn dummy_kernel(fp: u64) -> CachedKernel {
    // Route through `CachedKernel::new` so the BLAKE3 `integrity_hash`
    // matches the PTX text — `KernelCache::put` rejects mismatched
    // entries (jit S-3).
    CachedKernel::new(
        fp,
        Arc::new(EmittedPtx {
            text: format!("// tenant kernel fp={fp}"),
            launch_geometry: (1, 1),
        }),
        CompiledHandle::default(),
    )
}

#[test]
fn cache_is_tenant_scoped() {
    let cache = KernelCache::new();
    let tenant_a = TenantId(1);
    let tenant_b = TenantId(2);
    let blueprint: u64 = 42;
    let sm: u32 = 80;

    // Tenant A installs a kernel.
    cache.put(
        CacheKey::for_tenant(tenant_a, blueprint, sm),
        dummy_kernel(blueprint),
    );

    // Tenant A's own lookup must hit.
    let own_key = CacheKey::for_tenant(tenant_a, blueprint, sm);
    let hit = cache
        .get(&own_key)
        .expect("tenant A must see its own kernel");
    assert_eq!(hit.fingerprint, blueprint);

    // Tenant B issues the SAME fingerprint + sm — this is exactly the
    // attacker shape: guest-supplied fingerprint that happens to collide
    // with another tenant's installed kernel.
    let cross_key = CacheKey::for_tenant(tenant_b, blueprint, sm);
    assert!(
        cache.get(&cross_key).is_none(),
        "tenant B must NOT see tenant A's kernel via fingerprint collision \
         (exec S-7: cross-tenant kernel-cache confused-deputy)"
    );

    // And tenants are independent in both directions: B installing under
    // its own tenant id does not leak back to A.
    cache.put(
        CacheKey::for_tenant(tenant_b, blueprint, sm),
        dummy_kernel(blueprint + 1),
    );
    let b_hit = cache
        .get(&cross_key)
        .expect("tenant B must now see its own kernel");
    assert_eq!(b_hit.fingerprint, blueprint + 1);
    let a_hit_after_b = cache
        .get(&own_key)
        .expect("tenant A's entry must survive tenant B's put");
    assert_eq!(
        a_hit_after_b.fingerprint, blueprint,
        "tenant A's kernel must be unchanged by tenant B's put"
    );
}

#[test]
fn tenant_zero_is_distinct_from_real_tenants() {
    // `TenantId(0)` is used as a placeholder by the rewriter's
    // pre-population path and by bench harnesses. It must NOT alias real
    // tenant ids — otherwise a tenant whose id literally happens to be 0
    // would collide with every pre-populated entry.
    let cache = KernelCache::new();
    let placeholder = TenantId(0);
    let real = TenantId(7);
    let blueprint: u64 = 0xCAFE;
    let sm: u32 = 89;

    cache.put(
        CacheKey::for_tenant(placeholder, blueprint, sm),
        dummy_kernel(blueprint),
    );
    assert!(
        cache
            .get(&CacheKey::for_tenant(real, blueprint, sm))
            .is_none(),
        "real tenant must not pick up the placeholder's entry"
    );
}
