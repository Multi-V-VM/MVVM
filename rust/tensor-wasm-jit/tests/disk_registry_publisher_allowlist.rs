// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Publisher-allowlist test for [`DiskRegistry`] (T35, v0.4).
//!
//! Pins the operator-side authorization layer: with a configured
//! allowlist of `{"alice", "bob"}`, a manifest claiming `publisher =
//! "eve"` must be refused even when it carries a valid HMAC signature
//! under the registry's signing key, and manifests claiming `alice` or
//! `bob` must be accepted on their first publish.
//!
//! This complements the kernel-publish HTTP scope gate from T1: the
//! HTTP gate decides *who can invoke POST /kernels at all*, while the
//! allowlist decides *which publisher identity the body can claim*.
//! Together they prevent a single signing-key holder from impersonating
//! a peer publisher.
//!
//! Lives behind `kernel-registry` so the default `cargo test -p
//! tensor-wasm-jit` does not pull in the artifact-store / HMAC
//! dependency chain.

#![cfg(feature = "kernel-registry")]

use std::collections::HashSet;

use tensor_wasm_jit::registry::{
    sign_manifest, DiskRegistry, KernelManifest, KernelRegistry, RegistryError,
};

/// Helper: build a v2-signed manifest matching `ptx_text` under `key`
/// with `publisher` set to `publisher_id`.
fn signed_manifest(
    name: &str,
    version: &str,
    publisher_id: &str,
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
        1_700_000_000_000,
        publisher_id.to_string(),
    );
    m.signature = sign_manifest(&m, key);
    m
}

#[test]
fn allowlist_rejects_eve_accepts_alice() {
    let tmp = tempfile::TempDir::new().expect("tempdir");
    let key = [0xe7u8; 32];
    let allowlist: HashSet<String> = ["alice".to_string(), "bob".to_string()]
        .into_iter()
        .collect();
    let reg = DiskRegistry::open(tmp.path().to_path_buf(), key)
        .expect("open")
        .with_publisher_allowlist(allowlist);

    // Eve attempts to publish — must be refused. The signature itself
    // is valid (under the registry key); the rejection is purely the
    // allowlist's doing.
    let ptx_eve = "// eve's ptx\n";
    let eve_manifest = signed_manifest("matmul.f32", "1.0.0", "eve", ptx_eve, &key);
    match reg.publish(eve_manifest, ptx_eve.to_string()) {
        Err(RegistryError::PublisherNotAllowed(name)) => {
            assert_eq!(name, "matmul.f32");
        }
        other => panic!("expected PublisherNotAllowed for eve, got {other:?}"),
    }

    // Alice publishes — must succeed. Use the same kernel name as Eve
    // so we also confirm Eve's rejected attempt did not leave a stale
    // tombstone in the keymap that would conflict.
    let ptx_alice = "// alice's ptx\n";
    let alice_manifest = signed_manifest("matmul.f32", "1.0.0", "alice", ptx_alice, &key);
    reg.publish(alice_manifest, ptx_alice.to_string())
        .expect("alice's publish must succeed");

    // The accepted publish landed in the registry.
    let listed = reg.list();
    assert_eq!(
        listed.len(),
        1,
        "exactly one manifest after the two attempts"
    );
    assert_eq!(
        listed[0].publisher, "alice",
        "alice is the recorded publisher"
    );
}
