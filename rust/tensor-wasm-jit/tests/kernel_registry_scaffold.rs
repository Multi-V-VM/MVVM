// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company

//! Integration tests for the signed kernel registry scaffold
//! (roadmap feature #3, v0.3.7).
//!
//! These tests pin the v0.3.7 surface contract so the v0.4 follow-up
//! (on-disk store + server `/kernels` route) cannot quietly regress the
//! signing/verify path the design-partner spec depends on. The
//! production code lives in `src/registry.rs`; the tests live behind
//! the same `kernel-registry` feature so a default `cargo test -p
//! tensor-wasm-jit` does not pull in the HMAC/SHA-2 dependency chain.
//!
//! Threat model pinned here:
//! - A manifest whose `signature` does not verify under the registry's
//!   configured HMAC key must be rejected with `BadSignature`.
//! - A manifest whose `digest` does not match BLAKE3(ptx_text) must be
//!   rejected with `DigestMismatch` (catches in-flight PTX corruption
//!   before any HMAC compute cost is paid).
//! - A successfully-published manifest is readable via `get` and
//!   appears in `list`, returning the exact PTX text the publisher
//!   uploaded (no canonicalisation, no re-encoding).

#![cfg(feature = "kernel-registry")]

use tensor_wasm_jit::registry::{
    sign_manifest, InMemoryRegistry, KernelManifest, KernelRegistry, RegistryError,
};

/// Helper: build a manifest for `ptx_text` signed under `key`. Mirrors
/// the v0.4 CLI's `kernel publish` flow: caller stages every field,
/// computes BLAKE3 over the PTX, asks `sign_manifest` for the HMAC,
/// then writes it back into the manifest before publishing.
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
        "tenant-42".to_string(),
    );
    m.signature = sign_manifest(&m, key);
    m
}

#[test]
fn publish_get_roundtrip_returns_exact_ptx() {
    let key = [0xa5u8; 32];
    let reg = InMemoryRegistry::new(key);
    let ptx = "// .version 8.0\n// .target sm_80\n// kernel body\n".to_string();
    let m = signed_manifest("matmul.f32", "1.0.0", 80, &ptx, &key);

    reg.publish(m.clone(), ptx.clone()).unwrap();

    let got = reg.get("matmul.f32", "1.0.0").expect("get hit");
    assert_eq!(got.0.name, "matmul.f32");
    assert_eq!(got.0.version, "1.0.0");
    assert_eq!(got.0.sm_version, 80);
    // No canonicalisation — caller gets the exact bytes back.
    assert_eq!(got.1, ptx);
}

#[test]
fn list_enumerates_every_published_manifest() {
    let key = [0u8; 32];
    let reg = InMemoryRegistry::new(key);
    for (name, ver) in [
        ("matmul.f32", "1.0.0"),
        ("attention.bf16", "0.2.1"),
        ("conv2d.f16", "1.3.0"),
    ] {
        let ptx = format!("// {name}@{ver}\n");
        let m = signed_manifest(name, ver, 80, &ptx, &key);
        reg.publish(m, ptx).unwrap();
    }
    let listed = reg.list();
    assert_eq!(listed.len(), 3);
    let mut keys: Vec<String> = listed
        .iter()
        .map(|m| format!("{}@{}", m.name, m.version))
        .collect();
    keys.sort();
    assert_eq!(
        keys,
        vec![
            "attention.bf16@0.2.1".to_string(),
            "conv2d.f16@1.3.0".to_string(),
            "matmul.f32@1.0.0".to_string(),
        ]
    );
}

#[test]
fn bad_signature_rejected() {
    let key = [0xa5u8; 32];
    let reg = InMemoryRegistry::new(key);
    let ptx = "// fake ptx\n".to_string();
    let mut m = signed_manifest("matmul.f32", "1.0.0", 80, &ptx, &key);
    // Tamper with the signature: flip a bit so HMAC verify must fail.
    m.signature[0] ^= 0x01;

    match reg.publish(m, ptx) {
        Err(RegistryError::BadSignature(name)) => assert_eq!(name, "matmul.f32"),
        other => panic!("expected BadSignature, got {other:?}"),
    }
}

#[test]
fn signature_under_wrong_key_rejected() {
    let publisher_key = [0xa5u8; 32];
    let registry_key = [0xb6u8; 32];
    let reg = InMemoryRegistry::new(registry_key);
    let ptx = "// fake ptx\n".to_string();
    let m = signed_manifest("matmul.f32", "1.0.0", 80, &ptx, &publisher_key);

    // Manifest is correctly signed — just not under the key the
    // registry trusts. Same failure mode as a flipped-bit signature.
    match reg.publish(m, ptx) {
        Err(RegistryError::BadSignature(_)) => (),
        other => panic!("expected BadSignature, got {other:?}"),
    }
}

#[test]
fn digest_mismatch_rejected() {
    let key = [0xa5u8; 32];
    let reg = InMemoryRegistry::new(key);
    let original_ptx = "// original ptx\n".to_string();
    let m = signed_manifest("matmul.f32", "1.0.0", 80, &original_ptx, &key);

    // Publisher signed `original_ptx` but uploads something else. The
    // BLAKE3 hash of the uploaded text must not match `manifest.digest`,
    // so the registry must reject before even attempting HMAC verify.
    let tampered_ptx = "// tampered ptx\n".to_string();
    match reg.publish(m, tampered_ptx) {
        Err(RegistryError::DigestMismatch(name)) => assert_eq!(name, "matmul.f32"),
        other => panic!("expected DigestMismatch, got {other:?}"),
    }
}

#[test]
fn duplicate_publish_rejected() {
    let key = [0xa5u8; 32];
    let reg = InMemoryRegistry::new(key);
    let ptx = "// fake ptx\n".to_string();
    let m = signed_manifest("matmul.f32", "1.0.0", 80, &ptx, &key);
    reg.publish(m.clone(), ptx.clone()).unwrap();

    match reg.publish(m, ptx) {
        Err(RegistryError::AlreadyRegistered(k)) => {
            assert_eq!(k, "matmul.f32@1.0.0")
        }
        other => panic!("expected AlreadyRegistered, got {other:?}"),
    }
}

#[test]
fn get_missing_returns_not_found() {
    let reg = InMemoryRegistry::new([0u8; 32]);
    match reg.get("nope", "0.0.0") {
        Err(RegistryError::NotFound(k)) => assert_eq!(k, "nope@0.0.0"),
        other => panic!("expected NotFound, got {other:?}"),
    }
}

#[test]
fn sm_version_is_part_of_signed_envelope() {
    // sm_version is a covered field. A manifest signed for sm_80 must
    // not verify if the persisted sm_version is mutated to sm_90.
    let key = [0xa5u8; 32];
    let reg = InMemoryRegistry::new(key);
    let ptx = "// fake ptx\n".to_string();
    let mut m = signed_manifest("matmul.f32", "1.0.0", 80, &ptx, &key);
    // Tamper with sm_version after signing.
    m.sm_version = 90;

    match reg.publish(m, ptx) {
        Err(RegistryError::BadSignature(_)) => (),
        other => panic!("expected BadSignature on sm_version tamper, got {other:?}"),
    }
}
