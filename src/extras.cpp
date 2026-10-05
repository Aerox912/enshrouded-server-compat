#include "adapter.hpp"
#include <MinHook.h>
#include <intrin.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <limits>

extern "C" {
void PickupInitialDelayDetour();
void* PickupInitialDelayOriginal = nullptr;
}
namespace xhl {
namespace {
bool attempted_extras = false;
unsigned char* game_base = nullptr;
float (*original_gift_distance)() = nullptr;
__declspec(noinline) float gift_distance() {
    // Only the server's item-transfer distance check. Other users of the same
    // constant (including future callers) retain the game's ordinary distance.
    if (_ReturnAddress() == game_base + 0x154b1c) return std::numeric_limits<float>::max();
    return original_gift_distance();
}
struct Site { DWORD rva; size_t length; std::array<unsigned char, 19> bytes; };
constexpr Site sites[] = {
    {0x152f20,16,{0x40,0x55,0x56,0x57,0x41,0x54,0x41,0x56,0x48,0x8d,0xac,0x24,0xd0,0x8d,0xff,0xff}},
    {0x16b430,15,{0x40,0x55,0x53,0x56,0x57,0x41,0x56,0x48,0x8d,0xac,0x24,0x00,0xff,0xff,0xff}},
    {0xbf181,18,{0x4c,0x89,0x7c,0x24,0x40,0x48,0x8b,0x5e,0x08,0x48,0x85,0xdb,0x0f,0x85,0xdd,0,0,0}},
    {0x833a30,9,{0xf3,0x0f,0x10,0x05,0x80,0x3a,0x32,0x00,0xc3}},
    {0x154b17,8,{0xe8,0x14,0xef,0x6d,0x00,0x0f,0x2f,0xf0}},
    {0x5d5130,8,{0x8b,0x41,0x08,0x4c,0x8b,0xc2,0x85,0xc0}}
};
}
bool start_extras(HMODULE game, const std::wstring& root, Logger log) {
    if (attempted_extras) return false;
    attempted_extras = true;
    auto base = reinterpret_cast<unsigned char*>(game);
    if (!validate_server(game)) { log("EXTRAS NOT APPLIED: unsupported server executable."); return false; }
    for (const auto& s : sites) {
        if (std::memcmp(base + s.rva, s.bytes.data(), s.length)) {
            log("EXTRAS NOT APPLIED: Auto Loot or gifting code changed or already hooked."); return false;
        }
    }
    const auto loot_path = root + L"\\mods\\XHL-Auto-Loot\\XHL-Auto-Loot.dll";
    const auto gift_path = root + L"\\mods\\XHL-Unlimited-Gifting-Range\\XHL-Unlimited-Gifting-Range.dll";
    if (!verify_file(loot_path, "d5d8762ba58e8eec242c56c10c5c350cf24d29f86f7ecddf15061a15becb6101") ||
        !verify_file(gift_path, "95b0fca887688bfe7bddd2bf6f6d5ce680fbecb50299b78b95cc1648a1c2ecb0")) {
        log("EXTRAS NOT APPLIED: original Auto Loot 1.5.0 or Unlimited Gifting 1.0.0 DLL missing/changed."); return false;
    }
    HMODULE loot = LoadLibraryExW(loot_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!loot) { log("EXTRAS NOT APPLIED: original Auto Loot DLL could not load."); return false; }
    auto p = reinterpret_cast<unsigned char*>(loot);
    // Do not run its client-only initializer. The original callbacks implement
    // a locked 1024-entry, 5-second player/item retry cache. The zero-initialized
    // SRWLOCK, cache and stop flag need no client runtime or input subsystem.
    *reinterpret_cast<void**>(p + 0x90d0) = base + 0x5d5130;
    auto status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        log("EXTRAS NOT APPLIED: hook engine initialization failed."); return false;
    }
    void* callbacks[] = {p + 0x16e0, p + 0x1540, reinterpret_cast<void*>(PickupInitialDelayDetour), reinterpret_cast<void*>(gift_distance)};
    std::array<void*, 4> originals{};
    size_t prepared = 0;
    for (; prepared < originals.size(); ++prepared) {
        if (MH_CreateHook(base + sites[prepared].rva, callbacks[prepared], &originals[prepared]) != MH_OK) break;
    }
    if (prepared != originals.size()) {
        for (size_t i = 0; i < prepared; ++i) MH_RemoveHook(base + sites[i].rva);
        log("EXTRAS NOT APPLIED: could not prepare all four hooks."); return false;
    }
    *reinterpret_cast<void**>(p + 0x90e0) = originals[0];
    *reinterpret_cast<void**>(p + 0x90c8) = originals[0];
    *reinterpret_cast<void**>(p + 0x90d8) = originals[1];
    *reinterpret_cast<void**>(p + 0x90c0) = originals[1];
    PickupInitialDelayOriginal = originals[2];
    original_gift_distance = reinterpret_cast<float (*)()>(originals[3]);
    game_base = base;
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                            reinterpret_cast<LPCWSTR>(loot), &pinned)) {
        for (size_t i = 0; i < originals.size(); ++i) MH_RemoveHook(base + sites[i].rva);
        log("EXTRAS NOT APPLIED: could not pin Auto Loot lifetime."); return false;
    }
    for (size_t i = 0; i < originals.size(); ++i) {
        if (MH_QueueEnableHook(base + sites[i].rva) != MH_OK) {
            for (size_t j = 0; j < originals.size(); ++j) MH_RemoveHook(base + sites[j].rva);
            log("EXTRAS NOT APPLIED: could not queue hooks."); return false;
        }
    }
    if (MH_ApplyQueued() != MH_OK) {
        for (size_t i = 0; i < originals.size(); ++i) MH_DisableHook(base + sites[i].rva);
        log("EXTRAS NOT APPLIED: activation failed; attempted to disable extras."); return false;
    }
    log("EXTRAS HOOKS INSTALLED: Auto Loot initial delay + inventory-full retry guard; server gifting distance.");
    log("Matching client mods/settings required. Resource patches are applied offline; multiplayer behavior needs gameplay verification.");
    return true;
}
}
