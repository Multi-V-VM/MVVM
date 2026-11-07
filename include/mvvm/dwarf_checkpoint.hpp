#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

#include "wasm_export.h"

namespace mvvm::dwarf {

inline constexpr std::uint32_t checkpoint_magic = 0x4d564432U; // "MVD2"
inline constexpr std::uint32_t checkpoint_version = 1;

struct WasmFrame {
    std::uint32_t module_offset{};
    std::uint32_t function_index{};
    std::uint32_t function_offset{};
    std::string function_name;
};

struct NativeFrame {
    std::uint64_t program_counter{};
    std::uint64_t stack_pointer{};
    std::string module;
    std::string symbol;
    std::string source_file;
    std::uint32_t source_line{};
    std::uint32_t source_column{};
};

struct Checkpoint {
    std::uint32_t magic{checkpoint_magic};
    std::uint32_t version{checkpoint_version};
    std::string module_identity;
    std::vector<WasmFrame> wasm_frames;
    std::vector<NativeFrame> native_frames;
    std::vector<std::byte> linear_memory;
};

struct ResumePoint {
    std::uint32_t function_index{};
    std::uint32_t function_offset{};
};

struct CaptureOptions {
    std::size_t max_wasm_frames{256};
    std::size_t max_native_frames{256};
    std::size_t native_frames_to_skip{1};
};

std::expected<Checkpoint, std::string>
capture(wasm_exec_env_t exec_env, std::span<const std::byte> linear_memory,
        std::string module_identity, CaptureOptions options = {});

std::expected<void, std::string>
restore_linear_memory(const Checkpoint &checkpoint,
                      std::span<std::byte> destination,
                      std::string_view expected_module_identity);

std::expected<ResumePoint, std::string>
resume_point(const Checkpoint &checkpoint,
             std::string_view expected_module_identity);

std::string describe(const Checkpoint &checkpoint);

} // namespace mvvm::dwarf
