#include "mvvm/dwarf_checkpoint.hpp"
#include "mvvm/reflection_codec.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(std::string_view message) {
    std::cerr << std::format("FAIL: {}\n", message);
    std::exit(1);
}

} // namespace

int main() {
    std::array<std::byte, 64> memory{};
    for (std::size_t index = 0; index < memory.size(); ++index)
        memory[index] = static_cast<std::byte>(index);

    auto captured = mvvm::dwarf::capture(
        nullptr, memory, "sha256:test-module",
        {.max_wasm_frames = 8, .max_native_frames = 32,
         .native_frames_to_skip = 0});
    if (!captured)
        fail(captured.error());
    if (captured->native_frames.empty())
        fail("DWARF/libunwind returned no native frames");

    captured->wasm_frames.push_back({
        .module_offset = 0,
        .function_index = 7,
        .function_offset = 19,
        .function_name = "checkpoint_target",
    });

    const auto bytes = mvvm::codec::encode(*captured);
    auto decoded = mvvm::codec::decode<mvvm::dwarf::Checkpoint>(bytes);
    if (!decoded)
        fail(decoded.error());

    if (decoded->module_identity != captured->module_identity ||
        decoded->wasm_frames.size() != 1 ||
        decoded->wasm_frames.front().function_index != 7 ||
        decoded->linear_memory != captured->linear_memory)
        fail("C++26 reflection round trip changed checkpoint state");

    std::array<std::byte, 64> restored{};
    auto restored_result = mvvm::dwarf::restore_linear_memory(
        *decoded, restored, "sha256:test-module");
    if (!restored_result)
        fail(restored_result.error());
    if (restored != memory)
        fail("restored linear memory differs from captured memory");

    auto resume = mvvm::dwarf::resume_point(*decoded,
                                             "sha256:test-module");
    if (!resume)
        fail(resume.error());
    if (resume->function_index != 7 || resume->function_offset != 19)
        fail("portable resume point changed during serialization");

    std::cout << mvvm::dwarf::describe(*decoded) << '\n';
    std::cout << std::format(
        "PASS: GCC C++26 reflection schema=0x{:016x}, bytes={}, first_native='{}'\n",
        mvvm::codec::schema_fingerprint<mvvm::dwarf::Checkpoint>(),
        bytes.size(), decoded->native_frames.front().symbol);
}
