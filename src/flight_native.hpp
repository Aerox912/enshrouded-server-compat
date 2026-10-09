#pragma once
#include "adapter.hpp"
#include "flight_session.hpp"
namespace xhl::flight {
struct Extension {
    void (*poll)() = nullptr;
    bool (*can_fly)(std::uintptr_t world, std::uint32_t owner) = nullptr;
};
std::optional<Identity> authenticated_identity(std::uint64_t steam);
std::optional<Identity> authenticated_owner(std::uintptr_t world, std::uint32_t owner);
bool owner_alive(std::uintptr_t world, std::uint32_t owner);
// Validate liveness and the captured lifecycle under the same session lock.
bool owner_alive(const Identity&);
std::optional<bool> owner_alive_state(const Identity&);
// Development-only runtime. Not included in ordinary dbghelp builds/packages.
bool start_runtime(HMODULE game, const std::wstring& directory, Logger log, Extension extension = {});
}
