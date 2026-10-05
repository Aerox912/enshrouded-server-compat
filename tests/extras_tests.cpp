#include "adapter.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

extern "C" void ProbePickupDelay(void* settings, void* state, void* entry);
namespace {
unsigned checks = 0;
std::atomic<ULONGLONG> ticks = 10000;
thread_local unsigned calls = 0;
thread_local DWORD item_id = 42;
void check(bool condition, const char* message) {
    if (!condition) { std::printf("FAIL: %s\n", message); std::exit(1); }
    ++checks; std::printf("PASS: %s\n", message);
}
void logger(const char* text) { std::printf("native: %s\n", text); }
ULONGLONG WINAPI clock_stub() { return ticks.load(); }
void result_stub(void*) {}
DWORD* item_stub(void*, DWORD* output) { *output = item_id; return output; }
void* transfer_stub(void* result, void*, void*, void*, void*, void*, DWORD, bool) {
    ++calls; *static_cast<BYTE*>(result) = 9; return result;
}
template<class T> void put(void* base, size_t offset, T value) {
    std::memcpy(static_cast<BYTE*>(base) + offset, &value, sizeof(value));
}
void protect_write(void* target, const void* value, size_t length) {
    DWORD old, ignored;
    if (!VirtualProtect(target, length, PAGE_EXECUTE_READWRITE, &old)) std::exit(2);
    std::memcpy(target, value, length);
    VirtualProtect(target, length, old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), target, length);
}
using Transfer = void* (*)(void*, void*, void*, void*, void*, void*, DWORD, bool);
using Result = void (*)(void*);
Transfer transfer;
Result result;
bool attempt(DWORD player) {
    BYTE output = 0xff; calls = 0;
    auto returned = transfer(&output, &item_id, nullptr, nullptr, nullptr, nullptr, player, true);
    return returned == &output && calls == 1 && output == 9;
}
void full(DWORD player, DWORD item, BYTE flags = 2, BYTE inventory_full = 1) {
    std::array<BYTE, 0x7200> response{};
    put(response.data(), 0xb0, flags);
    put(response.data(), 0xe0, player);
    put(response.data(), 0xe4, item);
    put(response.data(), 0x71a8, inventory_full);
    result(response.data());
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::puts("usage: extras_tests server.exe staged-root [reject-hook|reject-header|reject-plugin]"); return 2; }
    auto game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    check(game != nullptr, "map real dedicated executable without executing the game");
    auto base = reinterpret_cast<BYTE*>(game);
    if (argc == 4) {
        const std::wstring mode = argv[3];
        if (mode == L"reject-hook") { BYTE cc = 0xcc; protect_write(base + 0xbf181, &cc, 1); }
        if (mode == L"reject-header") {
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            DWORD zero = 0; protect_write(&nt->FileHeader.TimeDateStamp, &zero, 4);
        }
        auto path = std::wstring(argv[2]) + (mode == L"reject-plugin" ? L"\\absent" : L"");
        check(!xhl::start_extras(game, path, logger), "changed executable or missing native plugin is rejected");
        check(base[0x833a30] == 0xf3 && base[0x152f20] == 0x40, "rejected setup leaves other entry points unchanged");
        std::printf("%u checks passed\n", checks); return 0;
    }
    check(xhl::start_extras(game, argv[2], logger), "activate all four dedicated hooks");
    check(!xhl::start_extras(game, argv[2], logger), "duplicate initialization refused");
    auto plugin = reinterpret_cast<BYTE*>(GetModuleHandleW(L"XHL-Auto-Loot.dll"));
    check(plugin != nullptr, "original Auto Loot callbacks loaded");
    void* clock = reinterpret_cast<void*>(clock_stub);
    protect_write(plugin + 0xf318, &clock, sizeof(clock));
    put(plugin, 0x90e0, reinterpret_cast<void*>(transfer_stub));
    put(plugin, 0x90d8, reinterpret_cast<void*>(result_stub));
    put(plugin, 0x90d0, reinterpret_cast<void*>(item_stub));
    transfer = reinterpret_cast<Transfer>(base + 0x152f20);
    result = reinterpret_cast<Result>(base + 0x16b430);
    check(attempt(100), "ordinary pickup forwards to the original transfer exactly once");
    full(100,42);
    check(!attempt(100), "InventoryFull suppresses same player's same-item retry");
    check(attempt(200), "full inventory on one player does not suppress another player");
    item_id = 43;
    check(attempt(100), "full inventory result for one item does not suppress another item");
    item_id = 42; ticks += 4999;
    check(!attempt(100), "retry remains suppressed until five-second deadline");
    ticks += 1;
    check(attempt(100), "retry resumes at five-second deadline");
    full(100,42,0);
    check(attempt(100), "unrelated result flags do not suppress pickup");
    full(100,42,2,0);
    check(attempt(100), "successful inventory result does not suppress pickup");
    std::atomic<bool> concurrent_ok = true;
    std::vector<std::thread> workers;
    for (DWORD t = 0; t < 8; ++t) workers.emplace_back([&,t] {
        item_id = 1000 + t;
        for (DWORD i = 0; i < 1000; ++i) {
            full(500 + t, item_id);
            if (attempt(500 + t) || !attempt(900 + t)) concurrent_ok = false;
        }
    });
    for (auto& worker : workers) worker.join();
    check(concurrent_ok, "8000 concurrent inventory-full events preserve player/item isolation");
    // Replace only the continuation in this unexecuted image, so the test runs
    // the real hook and copied spill instruction, then returns to the probe.
    BYTE ret = 0xc3;
    protect_write(base + 0xbf186, &ret, 1);
    std::array<BYTE, 32> settings{}, state{};
    put(settings.data(), 0x10, DWORD{200000123});
    ProbePickupDelay(settings.data(), state.data(), base + 0xbf181);
    check(*reinterpret_cast<ULONGLONG*>(state.data()+8) == 1, "XHL pickup marker skips initial random wait");
    put(state.data(),8,ULONGLONG{987});
    ProbePickupDelay(settings.data(), state.data(), base + 0xbf181);
    check(*reinterpret_cast<ULONGLONG*>(state.data()+8) == 987, "existing pickup timer is preserved");
    put(settings.data(),0x10,DWORD{200000000}); put(state.data(),8,ULONGLONG{0});
    ProbePickupDelay(settings.data(), state.data(), base + 0xbf181);
    check(*reinterpret_cast<ULONGLONG*>(state.data()+8) == 0, "ordinary game pickup zones retain their original delay");
    auto getter = reinterpret_cast<float (*)()>(base+0x833a30);
    check(getter() == 2500.0f, "non-gifting caller retains original squared distance");
    // Execute the actual server call instruction and return immediately after it.
    protect_write(base+0x154b1c,&ret,1);
    auto gifting_site = reinterpret_cast<float (*)()>(base+0x154b17);
    check(gifting_site() == std::numeric_limits<float>::max(), "actual server gifting call receives unlimited squared distance");
    std::printf("%u checks passed\n",checks);
}
