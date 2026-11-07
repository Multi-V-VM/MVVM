#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include "mvvm/dwarf_checkpoint.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <elfutils/libdwfl.h>
#include <format>
#include <memory>
#include <unistd.h>

namespace mvvm::dwarf {
namespace {

struct DwflCloser {
    void operator()(Dwfl *dwfl) const {
        if (dwfl != nullptr)
            dwfl_end(dwfl);
    }
};

using DwflPtr = std::unique_ptr<Dwfl, DwflCloser>;

DwflPtr open_current_process_dwarf() {
    static char *debug_info_path = nullptr;
    static const Dwfl_Callbacks callbacks{
        .find_elf = dwfl_linux_proc_find_elf,
        .find_debuginfo = dwfl_standard_find_debuginfo,
        .section_address = dwfl_offline_section_address,
        .debuginfo_path = &debug_info_path,
    };

    DwflPtr dwfl{dwfl_begin(&callbacks)};
    if (!dwfl)
        return {};
    if (dwfl_linux_proc_report(dwfl.get(), getpid()) != 0)
        return {};
    if (dwfl_report_end(dwfl.get(), nullptr, nullptr) != 0)
        return {};
    return dwfl;
}

void symbolize(Dwfl *dwfl, NativeFrame &frame) {
    if (dwfl == nullptr)
        return;
    auto *module = dwfl_addrmodule(dwfl, frame.program_counter);
    if (module == nullptr)
        return;

    const char *main_file = nullptr;
    const char *debug_file = nullptr;
    const char *module_name = dwfl_module_info(module, nullptr, nullptr, nullptr,
                                                nullptr, nullptr, &main_file,
                                                &debug_file);
    if (main_file != nullptr)
        frame.module = main_file;
    else if (module_name != nullptr)
        frame.module = module_name;

    if (auto *line = dwfl_module_getsrc(module, frame.program_counter)) {
        int line_number = 0;
        int column = 0;
        if (const char *source = dwfl_lineinfo(line, nullptr, &line_number,
                                               &column, nullptr, nullptr)) {
            frame.source_file = source;
            frame.source_line = line_number > 0
                                    ? static_cast<std::uint32_t>(line_number)
                                    : 0;
            frame.source_column = column > 0
                                      ? static_cast<std::uint32_t>(column)
                                      : 0;
        }
    }
}

std::expected<std::vector<NativeFrame>, std::string>
capture_native_frames(std::size_t max_frames, std::size_t skip) {
    unw_context_t context{};
    unw_cursor_t cursor{};
    if (unw_getcontext(&context) < 0)
        return std::unexpected("libunwind could not capture the native context");
    if (unw_init_local(&cursor, &context) < 0)
        return std::unexpected("libunwind could not initialize a local DWARF cursor");

    auto dwfl = open_current_process_dwarf();
    std::vector<NativeFrame> frames;
    frames.reserve(max_frames);
    std::size_t visited = 0;
    while (frames.size() < max_frames) {
        unw_word_t pc = 0;
        unw_word_t sp = 0;
        if (unw_get_reg(&cursor, UNW_REG_IP, &pc) < 0 ||
            unw_get_reg(&cursor, UNW_REG_SP, &sp) < 0)
            break;

        if (visited++ >= skip) {
            NativeFrame frame{
                .program_counter = static_cast<std::uint64_t>(pc),
                .stack_pointer = static_cast<std::uint64_t>(sp),
            };
            std::array<char, 512> symbol{};
            unw_word_t offset = 0;
            if (unw_get_proc_name(&cursor, symbol.data(), symbol.size(),
                                  &offset) == 0)
                frame.symbol = symbol.data();
            symbolize(dwfl.get(), frame);
            frames.push_back(std::move(frame));
        }

        const int step = unw_step(&cursor);
        if (step <= 0)
            break;
    }
    return frames;
}

std::expected<std::vector<WasmFrame>, std::string>
capture_wasm_frames(wasm_exec_env_t exec_env, std::size_t max_frames) {
    if (exec_env == nullptr)
        return std::vector<WasmFrame>{};
    if (max_frames == 0 || max_frames > UINT32_MAX)
        return std::unexpected("invalid maximum WAMR frame count");

    std::vector<WASMCApiFrame> raw(max_frames);
    std::array<char, 256> error{};
    const auto count = wasm_copy_callstack(
        exec_env, raw.data(), static_cast<std::uint32_t>(raw.size()), 0,
        error.data(), static_cast<std::uint32_t>(error.size()));
    if (count == 0 && error[0] != '\0')
        return std::unexpected(std::format("WAMR call-stack capture failed: {}",
                                           error.data()));

    std::vector<WasmFrame> frames;
    frames.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto &frame = raw[index];
        frames.push_back(WasmFrame{
            .module_offset = frame.module_offset,
            .function_index = frame.func_index,
            .function_offset = frame.func_offset,
            .function_name = frame.func_name_wp != nullptr
                                 ? frame.func_name_wp
                                 : std::string{},
        });
    }
    return frames;
}

std::expected<void, std::string>
validate(const Checkpoint &checkpoint, std::string_view module_identity) {
    if (checkpoint.magic != checkpoint_magic)
        return std::unexpected(std::format(
            "invalid checkpoint magic 0x{:08x}", checkpoint.magic));
    if (checkpoint.version != checkpoint_version)
        return std::unexpected(std::format(
            "unsupported checkpoint version {}", checkpoint.version));
    if (checkpoint.module_identity != module_identity)
        return std::unexpected(std::format(
            "module identity mismatch: checkpoint='{}', runtime='{}'",
            checkpoint.module_identity, module_identity));
    return {};
}

} // namespace

std::expected<Checkpoint, std::string>
capture(wasm_exec_env_t exec_env, std::span<const std::byte> linear_memory,
        std::string module_identity, CaptureOptions options) {
    auto wasm_frames = capture_wasm_frames(exec_env, options.max_wasm_frames);
    if (!wasm_frames)
        return std::unexpected(wasm_frames.error());
    auto native_frames = capture_native_frames(options.max_native_frames,
                                                options.native_frames_to_skip);
    if (!native_frames)
        return std::unexpected(native_frames.error());

    return Checkpoint{
        .magic = checkpoint_magic,
        .version = checkpoint_version,
        .module_identity = std::move(module_identity),
        .wasm_frames = std::move(*wasm_frames),
        .native_frames = std::move(*native_frames),
        .linear_memory = {linear_memory.begin(), linear_memory.end()},
    };
}

std::expected<void, std::string>
restore_linear_memory(const Checkpoint &checkpoint,
                      std::span<std::byte> destination,
                      std::string_view expected_module_identity) {
    auto valid = validate(checkpoint, expected_module_identity);
    if (!valid)
        return valid;
    if (destination.size() != checkpoint.linear_memory.size())
        return std::unexpected(std::format(
            "linear-memory size mismatch: checkpoint={} destination={}",
            checkpoint.linear_memory.size(), destination.size()));
    std::ranges::copy(checkpoint.linear_memory, destination.begin());
    return {};
}

std::expected<ResumePoint, std::string>
resume_point(const Checkpoint &checkpoint,
             std::string_view expected_module_identity) {
    auto valid = validate(checkpoint, expected_module_identity);
    if (!valid)
        return std::unexpected(valid.error());
    if (checkpoint.wasm_frames.empty())
        return std::unexpected("checkpoint contains no portable WAMR frame");
    const auto &top = checkpoint.wasm_frames.front();
    return ResumePoint{
        .function_index = top.function_index,
        .function_offset = top.function_offset,
    };
}

std::string describe(const Checkpoint &checkpoint) {
    return std::format(
        "MVVM DWARF checkpoint v{}: module='{}', wasm_frames={}, "
        "native_frames={}, linear_memory={} bytes",
        checkpoint.version, checkpoint.module_identity,
        checkpoint.wasm_frames.size(), checkpoint.native_frames.size(),
        checkpoint.linear_memory.size());
}

} // namespace mvvm::dwarf
