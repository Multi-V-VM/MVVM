// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Craton Software Company
//! Deoptimisation guard.
//!
//! When a GPU-offloaded kernel fails at runtime (CUDA error, numerical
//! divergence beyond an acceptable tolerance), the executor falls back to
//! the original CPU implementation. [`DeoptGuard`] tracks whether a given
//! blueprint is currently deopted so subsequent calls go straight to the
//! CPU path without retrying the GPU.

use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;

use dashmap::DashMap;

/// Reason a particular kernel was deoptimised.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DeoptReason {
    /// CUDA driver returned an error during launch or sync.
    CudaError(String),
    /// Numerical result differed from the CPU reference beyond tolerance.
    NumericalDivergence,
    /// PTX assembly failed to validate.
    AssemblyFailed(String),
}

impl DeoptReason {
    /// Stable, machine-readable name (used in metrics labels).
    pub fn name(&self) -> &'static str {
        match self {
            DeoptReason::CudaError(_) => "cuda_error",
            DeoptReason::NumericalDivergence => "numerical_divergence",
            DeoptReason::AssemblyFailed(_) => "assembly_failed",
        }
    }
}

/// Tracks deopted kernels by their blueprint fingerprint.
pub struct DeoptGuard {
    deopted: DashMap<u64, DeoptReason>,
    success_total: AtomicU64,
    fallback_total: AtomicU64,
    metrics: Option<tensor_wasm_core::metrics::TensorWasmMetrics>,
}

impl DeoptGuard {
    /// Construct an empty guard with no Prometheus metrics wiring.
    pub fn new() -> Self {
        Self {
            deopted: DashMap::new(),
            success_total: AtomicU64::new(0),
            fallback_total: AtomicU64::new(0),
            metrics: None,
        }
    }

    /// Construct an empty guard that also publishes success / fallback events
    /// to the supplied [`tensor_wasm_core::metrics::TensorWasmMetrics`] handle. The handle is
    /// `Arc`-backed and cheap to clone — pass the same handle used by the rest
    /// of the process so the `tensor_wasm_offload_success_total` and
    /// `tensor_wasm_offload_fallback_total` counters reflect the kernels driven by
    /// this guard.
    pub fn with_metrics(metrics: tensor_wasm_core::metrics::TensorWasmMetrics) -> Self {
        Self {
            deopted: DashMap::new(),
            success_total: AtomicU64::new(0),
            fallback_total: AtomicU64::new(0),
            metrics: Some(metrics),
        }
    }

    /// Returns true if the blueprint is currently deopted.
    pub fn is_deopted(&self, fingerprint: u64) -> bool {
        self.deopted.contains_key(&fingerprint)
    }

    /// Record a deopt event. Increments the fallback counter (and, if metrics
    /// are wired, the `tensor_wasm_offload_fallback_total` Prometheus counter).
    pub fn record_deopt(&self, fingerprint: u64, reason: DeoptReason) {
        self.fallback_total.fetch_add(1, Ordering::Relaxed);
        if let Some(m) = &self.metrics {
            m.offload_fallback_total().inc();
        }
        self.deopted.insert(fingerprint, reason);
    }

    /// Record a successful GPU execution. Increments the success counter (and,
    /// if metrics are wired, the `tensor_wasm_offload_success_total` Prometheus
    /// counter).
    pub fn record_success(&self) {
        self.success_total.fetch_add(1, Ordering::Relaxed);
        if let Some(m) = &self.metrics {
            m.offload_success_total().inc();
        }
    }

    /// Reset deopt status for a fingerprint (e.g. after a kernel-cache
    /// invalidation that recompiles the PTX).
    pub fn clear_deopt(&self, fingerprint: u64) -> Option<DeoptReason> {
        self.deopted.remove(&fingerprint).map(|(_, v)| v)
    }

    /// Reason for the most recent deopt of this kernel, if any.
    pub fn reason(&self, fingerprint: u64) -> Option<DeoptReason> {
        self.deopted.get(&fingerprint).map(|r| r.value().clone())
    }

    /// Cumulative successful GPU executions.
    pub fn success_total(&self) -> u64 {
        self.success_total.load(Ordering::Relaxed)
    }

    /// Cumulative fallback events.
    pub fn fallback_total(&self) -> u64 {
        self.fallback_total.load(Ordering::Relaxed)
    }

    /// Offload success rate as a fraction in [0, 1].
    pub fn success_rate(&self) -> f64 {
        let s = self.success_total() as f64;
        let f = self.fallback_total() as f64;
        let total = s + f;
        if total == 0.0 {
            0.0
        } else {
            s / total
        }
    }
}

impl Default for DeoptGuard {
    fn default() -> Self {
        Self::new()
    }
}

/// Shared handle alias for the executor.
pub type SharedDeoptGuard = Arc<DeoptGuard>;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fresh_guard_is_empty() {
        let g = DeoptGuard::new();
        assert!(!g.is_deopted(42));
        assert_eq!(g.success_total(), 0);
        assert_eq!(g.fallback_total(), 0);
        assert_eq!(g.success_rate(), 0.0);
    }

    #[test]
    fn record_deopt_marks_kernel() {
        let g = DeoptGuard::new();
        g.record_deopt(42, DeoptReason::CudaError("ctx not current".into()));
        assert!(g.is_deopted(42));
        assert_eq!(g.fallback_total(), 1);
        let reason = g.reason(42).unwrap();
        assert!(matches!(reason, DeoptReason::CudaError(_)));
    }

    #[test]
    fn clear_deopt_reverts() {
        let g = DeoptGuard::new();
        g.record_deopt(42, DeoptReason::NumericalDivergence);
        let popped = g.clear_deopt(42).unwrap();
        assert_eq!(popped, DeoptReason::NumericalDivergence);
        assert!(!g.is_deopted(42));
    }

    #[test]
    fn success_rate_accumulates() {
        let g = DeoptGuard::new();
        for _ in 0..7 {
            g.record_success();
        }
        for _ in 0..3 {
            g.record_deopt(0, DeoptReason::NumericalDivergence);
        }
        assert!((g.success_rate() - 0.7).abs() < 1e-9);
    }

    #[test]
    fn deopt_reason_names() {
        assert_eq!(
            DeoptReason::NumericalDivergence.name(),
            "numerical_divergence"
        );
        assert_eq!(DeoptReason::CudaError("x".into()).name(), "cuda_error");
        assert_eq!(
            DeoptReason::AssemblyFailed("x".into()).name(),
            "assembly_failed"
        );
    }

    #[test]
    fn with_metrics_increments_offload_success_total() {
        use tensor_wasm_core::metrics::TensorWasmMetrics;
        let metrics = TensorWasmMetrics::new();
        let g = DeoptGuard::with_metrics(metrics.clone());
        g.record_success();
        g.record_success();
        g.record_success();
        // Local counter
        assert_eq!(g.success_total(), 3);
        // TensorWasm metrics counter
        let text = metrics.encode_text();
        assert!(
            text.contains("tensor_wasm_offload_success_total 3"),
            "got:\n{text}"
        );
    }

    #[test]
    fn with_metrics_increments_offload_fallback_total() {
        use tensor_wasm_core::metrics::TensorWasmMetrics;
        let metrics = TensorWasmMetrics::new();
        let g = DeoptGuard::with_metrics(metrics.clone());
        g.record_deopt(1, DeoptReason::NumericalDivergence);
        g.record_deopt(2, DeoptReason::CudaError("x".into()));
        assert_eq!(g.fallback_total(), 2);
        let text = metrics.encode_text();
        assert!(
            text.contains("tensor_wasm_offload_fallback_total 2"),
            "got:\n{text}"
        );
    }

    #[test]
    fn without_metrics_still_works() {
        // Default constructor — no metrics. The old behaviour is preserved.
        let g = DeoptGuard::new();
        g.record_success();
        g.record_deopt(42, DeoptReason::NumericalDivergence);
        assert_eq!(g.success_total(), 1);
        assert_eq!(g.fallback_total(), 1);
    }
}
