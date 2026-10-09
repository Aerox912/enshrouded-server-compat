#pragma once

#include "native_consumable_cost.hpp"
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace xhl::native_consumable_result_write {

static_assert(sizeof(void*) == 8);
static_assert(sizeof(LONG64) == 8);
static_assert(sizeof(native_consumable_cost::NativeRemovalResult) == 8);
static_assert(offsetof(native_consumable_cost::NativeRemovalResult, status) == 0);
static_assert(offsetof(native_consumable_cost::NativeRemovalResult, padding) == 1);
static_assert(offsetof(native_consumable_cost::NativeRemovalResult, remainder) == 4);
constexpr std::uintptr_t native_result_alignment = 8;

enum class Outcome : std::uint8_t { unchanged, committed, indeterminate };

inline bool writable_single_span(
    const native_consumable_cost::NativeRemovalResult* result) noexcept {
    if (!result) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(result);
    if ((address & (native_result_alignment - 1)) != 0 ||
        address > UINTPTR_MAX - sizeof(*result)) return false;

    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(result, &info, sizeof(info)) ||
        info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto protection = info.Protect & 0xff;
    if (protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
        protection != PAGE_EXECUTE_READWRITE &&
        protection != PAGE_EXECUTE_WRITECOPY) return false;

    const auto region_begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (info.RegionSize > UINTPTR_MAX - region_begin) return false;
    const auto region_end = region_begin + info.RegionSize;
    return address >= region_begin && address + sizeof(*result) <= region_end;
}

// The native result is status byte 0, padding bytes 1..3, and remainder bytes
// 4..7. The 8-byte aligned CAS preserves the current padding and replaces both
// payment fields in one InterlockedCompareExchange64 operation. An access
// violation from that single-span atomic before completion leaves the word
// unchanged, so native payment can run. Other SEH exceptions continue searching.
inline Outcome replace_with_free_result(
    native_consumable_cost::NativeRemovalResult* result) noexcept {
    if (!writable_single_span(result)) return Outcome::unchanged;

    auto* const word = reinterpret_cast<volatile LONG64*>(result);
    constexpr LONG64 preserve_padding = 0x00000000ffffff00LL;
    __try {
        auto observed = InterlockedCompareExchange64(word, 0, 0);
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            const auto replacement = observed & preserve_padding;
            const auto previous = InterlockedCompareExchange64(word,
                replacement, observed);
            if (previous == observed) return Outcome::committed;
            observed = previous;
        }
        return Outcome::unchanged;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ?
        EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return Outcome::unchanged;
    }
}

} // namespace xhl::native_consumable_result_write
