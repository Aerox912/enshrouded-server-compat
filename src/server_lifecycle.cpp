// SPDX-License-Identifier: MIT
#include "server_lifecycle.hpp"
#include "vein_radius.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace xhl::lifecycle {
namespace {
using PlayerReset = std::uintptr_t (*)(void*, void*);
using Teardown = std::uintptr_t (*)(void*);
PlayerReset original_player_reset = nullptr;
Teardown original_teardown = nullptr;
std::atomic<PlayerRemoved> player_observer{nullptr};
std::atomic<WorldRemoved> world_observer{nullptr};
HMODULE prepared_game = nullptr;
bool cancelled = false;

std::uintptr_t player_reset_hook(void* state, void* player) {
    DWORD handle = 0;
    DWORD owner = 0;
    SIZE_T copied = 0;
    if (player && ReadProcessMemory(GetCurrentProcess(), player, &handle, sizeof(handle), &copied)
        && copied == sizeof(handle) && handle && handle != 0xffffffff) {
        // Same connection Player handle -> reflected playerId mapping used by
        // the authenticated runtime. This is not the actor death/respawn path.
        owner=(handle & 63)+1;
        vein_radius::retire_player(owner,true);
    }
    if (const auto observer = player_observer.load(std::memory_order_acquire)) observer(state, player);
    const auto result=original_player_reset(state, player);
    if (owner) vein_radius::retire_player(owner);
    return result;
}

std::uintptr_t teardown_hook(void* state) {
    vein_radius::retire_world(true);
    if (const auto observer = world_observer.load(std::memory_order_acquire)) observer(state);
    const auto result=original_teardown(state);
    vein_radius::retire_world();
    return result;
}
}

bool prepare(HMODULE game) {
    if (cancelled) return false;
    if (prepared_game) return prepared_game == game;
    if (!game) return false;
    struct Site { DWORD rva; std::array<unsigned char, 16> bytes; void* detour; void** original; };
    const Site sites[] = {
        {0x68a7e0, {0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x81,0xec,0x90,0,0,0,0x0f,0xb6,0x82},
            reinterpret_cast<void*>(player_reset_hook), reinterpret_cast<void**>(&original_player_reset)},
        {0x68a430, {0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57},
            reinterpret_cast<void*>(teardown_hook), reinterpret_cast<void**>(&original_teardown)}
    };
    const auto base = reinterpret_cast<unsigned char*>(game);
    for (const auto& site : sites)
        if (std::memcmp(base + site.rva, site.bytes.data(), site.bytes.size())) return false;
    size_t created = 0;
    bool queued = true;
    for (const auto& site : sites) {
        if (MH_CreateHook(base + site.rva, site.detour, site.original) != MH_OK) break;
        ++created;
        if (MH_QueueEnableHook(base + site.rva) != MH_OK) { queued = false; break; }
    }
    if (!queued || created != std::size(sites)) {
        for (size_t i = 0; i < created; ++i) MH_RemoveHook(base + sites[i].rva);
        return false;
    }
    prepared_game = game;
    return true;
}

void cancel() {
    cancelled=true;
    if (!prepared_game) return;
    const auto base=reinterpret_cast<unsigned char*>(prepared_game);
    for (DWORD rva : {DWORD(0x68a7e0),DWORD(0x68a430)}) {
        MH_QueueDisableHook(base+rva);
        MH_DisableHook(base+rva);
    }
}

void observe(PlayerRemoved player_removed, WorldRemoved world_removed) {
    player_observer.store(player_removed, std::memory_order_release);
    world_observer.store(world_removed, std::memory_order_release);
}
}
