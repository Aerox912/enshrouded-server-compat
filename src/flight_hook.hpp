#pragma once
#include "adapter.hpp"
#include <cstdint>

namespace xhl::flight {
inline constexpr DWORD site = 0x64791;
inline constexpr float vanilla_pitch = 0.0872664600610733f;
inline constexpr float enabled_pitch = -1.57f;

// The caller must establish current authenticated ownership, allowlist approval
// and client activation. Entity numbers alone are NOT authenticated identities.
// Called on simulation threads; it must be thread-safe, bounded and noexcept.
using Authorizer = bool (*)(const void* query_context, std::uint32_t entity) noexcept;

struct GlideProbe {
    std::uint64_t calls = 0, allowed = 0;
    std::uintptr_t context = 0;
    std::uint32_t owner = 0;
    float desired_pitch = 0, vertical_velocity = 0;
};
// Bounded development diagnostics. Counters never influence authorization.
GlideProbe glide_probe() noexcept;

// Experimental hook primitive. Nothing in the server loader calls this yet.
// A null authorizer is rejected. Callback/module lifetime is process-long.
bool install(HMODULE game, Authorizer authorizer, Logger logger);
}
