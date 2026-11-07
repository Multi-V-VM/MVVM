#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mvvm::dwarf {
struct SlotValue { std::string name; std::vector<std::byte> bytes; };
struct ContinuationFrame { std::uint32_t function; std::vector<SlotValue> slots; };
struct NativeContinuation {
    std::string module_identity;
    std::vector<ContinuationFrame> frames;
    std::vector<std::byte> memory;
    std::vector<std::byte> globals;
    std::vector<std::vector<std::uint32_t>> tables;
};
// Called on the stopped thread. Numeric values are obtained exclusively from
// DW_AT_location in native code DWARF; no compiler shadow-frame offsets.
std::vector<ContinuationFrame> capture_native_continuation();
void arm_native_restore(const std::vector<ContinuationFrame>&);
bool finish_native_rewind();
bool native_suspend_pending();
bool native_rewind_pending();
double native_restore_time_ms();
void request_native_suspend();
void reset_native_continuation();
}
extern "C" std::uint32_t mvvm_dwarf_enter(void*,std::uint32_t,std::uint32_t);
extern "C" void mvvm_dwarf_leave();
extern "C" std::uint32_t mvvm_dwarf_pending();
