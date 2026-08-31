// SPDX-License-Identifier: Apache-2.0
//! Panic-contained C ABI for the MVVM downstream integration.

use std::ffi::{c_char, CStr, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::ptr;

use tensor_wasm_core::types::TenantId;
use wasmparser::{ExternalKind, Parser, Payload, Validator};

use crate::cache::{CacheKey, KernelCache};
use crate::rewrite::{rewrite_wasm, OffloadedFunction, RewriteOptions};

const MAX_WASM_BYTES: usize = 256 * 1024 * 1024;

/// An owned translation result. Release it with
/// [`mvvm_tensor_wasm_jit_translation_free`].
#[repr(C)]
pub struct MVVMTensorWasmJITTranslation {
    /// NUL-terminated error string, or null after success.
    pub error: *mut c_char,
    /// Rewritten WebAssembly containing TensorWasm dispatch trampolines.
    pub rewritten_wasm: *mut u8,
    /// Byte length of `rewritten_wasm`.
    pub rewritten_wasm_len: usize,
    /// NUL-terminated selected kernel/export name.
    pub kernel_name: *mut c_char,
    /// Emitted NVIDIA PTX bytes (not NUL-terminated).
    pub ptx: *mut u8,
    /// Byte length of `ptx`.
    pub ptx_len: usize,
    /// BLAKE3 integrity tag over the PTX.
    pub signature: [u8; 32],
    /// Original WebAssembly function index.
    pub function_index: u32,
    /// TensorWasm blueprint fingerprint.
    pub fingerprint: u64,
    /// Number of source operators replaced by the trampoline.
    pub original_op_count: usize,
    /// Recommended CUDA grid size.
    pub grid_size: u32,
    /// Recommended CUDA block size.
    pub block_size: u32,
}

impl Default for MVVMTensorWasmJITTranslation {
    fn default() -> Self {
        Self {
            error: ptr::null_mut(),
            rewritten_wasm: ptr::null_mut(),
            rewritten_wasm_len: 0,
            kernel_name: ptr::null_mut(),
            ptx: ptr::null_mut(),
            ptx_len: 0,
            signature: [0; 32],
            function_index: 0,
            fingerprint: 0,
            original_op_count: 0,
            grid_size: 0,
            block_size: 0,
        }
    }
}

fn into_raw_bytes(bytes: Vec<u8>) -> (*mut u8, usize) {
    let mut bytes = bytes.into_boxed_slice();
    let result = (bytes.as_mut_ptr(), bytes.len());
    std::mem::forget(bytes);
    result
}

unsafe fn free_raw_bytes(data: *mut u8, len: usize) {
    if !data.is_null() {
        drop(Box::from_raw(ptr::slice_from_raw_parts_mut(data, len)));
    }
}

fn exported_functions(wasm: &[u8]) -> Result<Vec<(u32, String)>, String> {
    let mut exports = Vec::new();
    for payload in Parser::new(0).parse_all(wasm) {
        if let Payload::ExportSection(section) =
            payload.map_err(|error| format!("invalid WebAssembly: {error}"))?
        {
            for export in section {
                let export = export.map_err(|error| format!("invalid export: {error}"))?;
                if export.kind == ExternalKind::Func {
                    exports.push((export.index, export.name.to_owned()));
                }
            }
        }
    }
    Ok(exports)
}

fn choose_function(
    offloaded: &[OffloadedFunction],
    exports: &[(u32, String)],
    requested: Option<&str>,
) -> Result<(OffloadedFunction, String), String> {
    if let Some(name) = requested {
        let index = exports
            .iter()
            .find_map(|(index, export)| (export == name).then_some(*index))
            .ok_or_else(|| format!("function '{name}' is not an exported WebAssembly function"))?;
        let function = offloaded
            .iter()
            .find(|function| function.function_index == index)
            .cloned()
            .ok_or_else(|| {
                format!("function '{name}' is not eligible for tensor-wasm-jit PTX offload")
            })?;
        return Ok((function, name.to_owned()));
    }

    let function = offloaded
        .first()
        .cloned()
        .ok_or_else(|| "module contains no tensor-wasm-jit offload candidate".to_owned())?;
    let name = exports
        .iter()
        .find_map(|(index, name)| (*index == function.function_index).then(|| name.clone()))
        .unwrap_or_else(|| format!("func_{}", function.function_index));
    Ok((function, name))
}

fn translate(
    wasm: &[u8],
    requested: Option<&str>,
    sm_version: u32,
    tenant_id: u64,
    v128_ratio_threshold: f32,
    min_trip_count: u64,
) -> Result<MVVMTensorWasmJITTranslation, String> {
    Validator::new()
        .validate_all(wasm)
        .map_err(|error| format!("WebAssembly validation failed: {error}"))?;
    if !(50..=999).contains(&sm_version) {
        return Err(format!("invalid CUDA compute capability sm_{sm_version}"));
    }
    if !v128_ratio_threshold.is_finite() || !(0.0..=1.0).contains(&v128_ratio_threshold) {
        return Err("v128 ratio threshold must be finite and between 0 and 1".to_owned());
    }
    if min_trip_count == 0 {
        return Err("minimum loop trip count must be non-zero".to_owned());
    }

    let exports = exported_functions(wasm)?;
    let cache = KernelCache::new();
    let mut options = RewriteOptions::default();
    options.sm_version = sm_version;
    options.tenant_id = TenantId(tenant_id);
    options.detector.v128_ratio_threshold = v128_ratio_threshold;
    options.detector.min_trip_count = min_trip_count;
    let outcome = rewrite_wasm(wasm, &options, &cache)
        .map_err(|error| format!("tensor-wasm-jit rewrite failed: {error}"))?;
    let (function, kernel_name) =
        choose_function(&outcome.offloaded_functions, &exports, requested)?;
    let key = CacheKey::for_tenant(TenantId(tenant_id), function.fingerprint, sm_version);
    let cached = cache
        .get(&key)
        .ok_or_else(|| "tensor-wasm-jit did not populate its PTX cache".to_owned())?;
    if cached.ptx.text.is_empty() {
        return Err("tensor-wasm-jit emitted empty PTX".to_owned());
    }

    let kernel_name =
        CString::new(kernel_name).map_err(|_| "kernel name contains an interior NUL".to_owned())?;
    let mut result = MVVMTensorWasmJITTranslation::default();
    (result.rewritten_wasm, result.rewritten_wasm_len) = into_raw_bytes(outcome.rewritten_wasm);
    result.kernel_name = kernel_name.into_raw();
    (result.ptx, result.ptx_len) = into_raw_bytes(cached.ptx.text.as_bytes().to_vec());
    result.signature.copy_from_slice(cached.integrity_hash());
    result.function_index = function.function_index;
    result.fingerprint = function.fingerprint;
    result.original_op_count = function.original_op_count;
    result.grid_size = cached.ptx.launch_geometry.0;
    result.block_size = cached.ptx.launch_geometry.1;
    Ok(result)
}

fn set_error(out: *mut MVVMTensorWasmJITTranslation, message: String) -> i32 {
    let message = message.replace('\0', "\\0");
    let message = CString::new(message).expect("NUL bytes were replaced");
    unsafe {
        (*out).error = message.into_raw();
    }
    -1
}

/// Validate and translate a module or named exported function to NVIDIA PTX.
///
/// A null `function_name` selects the first eligible function. Returns zero
/// on success and `-1` on failure; detailed failure text is owned by `out`.
#[no_mangle]
pub unsafe extern "C" fn mvvm_tensor_wasm_jit_translate(
    wasm: *const u8,
    wasm_len: usize,
    function_name: *const c_char,
    sm_version: u32,
    tenant_id: u64,
    v128_ratio_threshold: f32,
    min_trip_count: u64,
    out: *mut MVVMTensorWasmJITTranslation,
) -> i32 {
    if out.is_null() {
        return -1;
    }
    ptr::write(out, MVVMTensorWasmJITTranslation::default());
    if wasm.is_null() || wasm_len == 0 {
        return set_error(out, "WebAssembly input is empty".to_owned());
    }
    if wasm_len > MAX_WASM_BYTES {
        return set_error(
            out,
            format!("WebAssembly input exceeds the {MAX_WASM_BYTES}-byte safety limit"),
        );
    }
    let wasm = std::slice::from_raw_parts(wasm, wasm_len);
    let requested = if function_name.is_null() {
        None
    } else {
        match CStr::from_ptr(function_name).to_str() {
            Ok("") => None,
            Ok(name) => Some(name),
            Err(_) => return set_error(out, "function name is not UTF-8".to_owned()),
        }
    };
    match catch_unwind(AssertUnwindSafe(|| {
        translate(
            wasm,
            requested,
            sm_version,
            tenant_id,
            v128_ratio_threshold,
            min_trip_count,
        )
    })) {
        Ok(Ok(result)) => {
            ptr::write(out, result);
            0
        }
        Ok(Err(error)) => set_error(out, error),
        Err(_) => set_error(out, "tensor-wasm-jit panicked".to_owned()),
    }
}

/// Re-run deterministic lowering and verify both the PTX bytes and BLAKE3 tag.
#[no_mangle]
pub unsafe extern "C" fn mvvm_tensor_wasm_jit_verify(
    wasm: *const u8,
    wasm_len: usize,
    function_name: *const c_char,
    sm_version: u32,
    tenant_id: u64,
    v128_ratio_threshold: f32,
    min_trip_count: u64,
    fingerprint: u64,
    ptx: *const u8,
    ptx_len: usize,
    signature: *const u8,
) -> i32 {
    if wasm.is_null()
        || wasm_len == 0
        || wasm_len > MAX_WASM_BYTES
        || ptx.is_null()
        || signature.is_null()
    {
        return 0;
    }
    let wasm = std::slice::from_raw_parts(wasm, wasm_len);
    let ptx = std::slice::from_raw_parts(ptx, ptx_len);
    let signature = std::slice::from_raw_parts(signature, 32);
    let requested = if function_name.is_null() {
        None
    } else {
        match CStr::from_ptr(function_name).to_str() {
            Ok("") => None,
            Ok(name) => Some(name),
            Err(_) => return 0,
        }
    };
    catch_unwind(AssertUnwindSafe(|| {
        if blake3::hash(ptx).as_bytes() != signature {
            return 0;
        }
        let mut translated = match translate(
            wasm,
            requested,
            sm_version,
            tenant_id,
            v128_ratio_threshold,
            min_trip_count,
        ) {
            Ok(value) => value,
            Err(_) => return 0,
        };
        let matches = translated.fingerprint == fingerprint
            && translated.ptx_len == ptx_len
            && std::slice::from_raw_parts(translated.ptx, translated.ptx_len) == ptx;
        mvvm_tensor_wasm_jit_translation_free(&mut translated);
        i32::from(matches)
    }))
    .unwrap_or(0)
}

/// Release all Rust-owned allocations in a translation result.
#[no_mangle]
pub unsafe extern "C" fn mvvm_tensor_wasm_jit_translation_free(
    result: *mut MVVMTensorWasmJITTranslation,
) {
    if result.is_null() {
        return;
    }
    let result = &mut *result;
    if !result.error.is_null() {
        drop(CString::from_raw(result.error));
    }
    if !result.kernel_name.is_null() {
        drop(CString::from_raw(result.kernel_name));
    }
    free_raw_bytes(result.rewritten_wasm, result.rewritten_wasm_len);
    free_raw_bytes(result.ptx, result.ptx_len);
    *result = MVVMTensorWasmJITTranslation::default();
}

#[cfg(test)]
mod tests {
    use super::*;

    const V128_HEAVY_WAT: &str = r#"
        (module
          (memory 1)
          (func (export "hot") (result i32)
            (local $v v128)
            (loop $L
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
              (local.set $v (i32x4.add (local.get $v) (local.get $v)))
            )
            (i32.const 0)
          )
        )
    "#;

    #[test]
    fn c_api_translates_verifies_and_rejects_tampering() {
        let wasm = wat::parse_str(V128_HEAVY_WAT).expect("valid fixture");
        let name = CString::new("hot").unwrap();
        let mut result = MVVMTensorWasmJITTranslation::default();
        let status = unsafe {
            mvvm_tensor_wasm_jit_translate(
                wasm.as_ptr(),
                wasm.len(),
                name.as_ptr(),
                80,
                7,
                0.05,
                64,
                &mut result,
            )
        };
        assert_eq!(status, 0, "{}", unsafe {
            result
                .error
                .as_ref()
                .map(|_| CStr::from_ptr(result.error).to_string_lossy())
                .unwrap_or_default()
        });
        assert!(result.ptx_len > 0);
        assert!(result.rewritten_wasm_len > 0);
        let verified = unsafe {
            mvvm_tensor_wasm_jit_verify(
                wasm.as_ptr(),
                wasm.len(),
                name.as_ptr(),
                80,
                7,
                0.05,
                64,
                result.fingerprint,
                result.ptx,
                result.ptx_len,
                result.signature.as_ptr(),
            )
        };
        assert_eq!(verified, 1);
        unsafe {
            *result.ptx ^= 1;
        }
        let tampered = unsafe {
            mvvm_tensor_wasm_jit_verify(
                wasm.as_ptr(),
                wasm.len(),
                name.as_ptr(),
                80,
                7,
                0.05,
                64,
                result.fingerprint,
                result.ptx,
                result.ptx_len,
                result.signature.as_ptr(),
            )
        };
        assert_eq!(tampered, 0);
        unsafe {
            mvvm_tensor_wasm_jit_translation_free(&mut result);
        }
    }
}
