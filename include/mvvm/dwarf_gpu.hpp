#pragma once
#include "wasm_export.h"
#include <memory>
#include <span>
#include <vector>
namespace mvvm::dwarf {
// Optional GPU participant in the same stopped-thread DWARF checkpoint.
// Wasm sees stable integer handles, never CUDA virtual addresses.
class GPUContinuation {
public:
    GPUContinuation();
    ~GPUContinuation();
    std::vector<NativeSymbol> symbols();
    std::vector<std::byte> seal(std::span<const std::byte> cpu);
    std::vector<char> open(std::span<const char> packet);
    void restore(); // After CPU identity/layout validation, before native rewind.
    double init_ms{}, capture_ms{}, restore_ms{}, attest_ms{};
    unsigned attestations{};
    unsigned long long launches{};
    const char *mode() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace mvvm::dwarf
