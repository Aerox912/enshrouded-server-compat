#include "flight_hook.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <immintrin.h>
#include <thread>
#include <vector>

struct Snapshot {
    std::uint64_t gp[7], flags;
    std::uint32_t xmm[6][4], mxcsr;
};
static_assert(offsetof(Snapshot, xmm) == 0x40 && offsetof(Snapshot, mxcsr) == 0xa0);
extern "C" void ProbeFlight(const void*, std::uint32_t, std::uint64_t, Snapshot*, void*);
extern "C" void ProbeFlightTail();
extern "C" void ClobberFlightVolatiles();
namespace {
unsigned checks = 0;
std::atomic<bool> approved{false};
int known_world;
bool authorize(const void* context, std::uint32_t entity) noexcept {
    // Deliberately a fake authority. These tests do not prove Steam auth,
    // session ownership, networking, glider behavior or a complete server mod.
    const bool result = context == &known_world && entity == 2 && approved.load();
    _mm_setcsr(_mm_getcsr() ^ 0x2000); // A callee's rounding state must not leak.
    ClobberFlightVolatiles();
    return result;
}
void check(bool ok, const char* name) {
    if (!ok) { std::printf("FAIL: %s\n", name); std::exit(1); }
    ++checks; std::printf("PASS: %s\n", name);
}
void logger(const char* text) { std::puts(text); }
void write(void* destination, const void* source, size_t size) {
    DWORD previous, ignored;
    check(VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &previous) != FALSE, "private image is writable for the harness");
    std::memcpy(destination, source, size);
    check(VirtualProtect(destination, size, previous, &ignored) != FALSE, "private image protection restored");
    check(FlushInstructionCache(GetCurrentProcess(), destination, size) != FALSE, "private image instruction cache refreshed");
}
bool probe(void* entry, const void* context, std::uint32_t entity, bool enabled, std::uint64_t flags) {
    Snapshot output{};
    const auto mxcsr = _mm_getcsr();
    ProbeFlight(context, entity, flags, &output, entry);
    float pitch;
    std::memcpy(&pitch, &output.xmm[0][0], sizeof(pitch));
    if (pitch != (enabled ? xhl::flight::enabled_pitch : xhl::flight::vanilla_pitch)) return false;
    for (size_t i = 0; i < 7; ++i) if (output.gp[i] != 0x111 * (i + 1)) return false;
    for (size_t j = 1; j < 4; ++j) if (output.xmm[0][j] != 0) return false;
    for (size_t i = 1; i < 6; ++i) for (auto value : output.xmm[i]) if (value != 0xffffffff) return false;
    // Arithmetic condition codes: OF/SF/ZF/AF/PF/CF. Exclude OS-controlled bits.
    return (output.flags & 0x8d5) == (flags & 0x8d5) && output.mxcsr == mxcsr && _mm_getcsr() == mxcsr;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc < 2 || argc > 3) { std::puts("usage: flight_tests server.exe [reject-site|reject-owner|reject-header|reject-constant|reject-authorizer]"); return 2; }
    const auto game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    check(game != nullptr, "map real server executable without running its entry point");
    auto base = reinterpret_cast<unsigned char*>(game);
    const auto entry = base + xhl::flight::site;
    if (argc == 3) {
        const std::wstring mode = argv[2];
        unsigned char changed = 0xcc;
        auto callback = authorize;
        if (mode == L"reject-site") write(entry, &changed, 1);
        else if (mode == L"reject-owner") write(base + 0x640c0, &changed, 1);
        else if (mode == L"reject-header") {
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            write(&nt->FileHeader.TimeDateStamp, &changed, 1);
        } else if (mode == L"reject-constant") write(base + 0xb567fc, &changed, 1);
        else if (mode == L"reject-authorizer") callback = nullptr;
        else return 2;
        std::array<unsigned char, 16> before{};
        std::memcpy(before.data(), entry, before.size());
        check(!xhl::flight::install(game, callback, logger), "incompatible layout or missing authority rejected");
        check(std::memcmp(before.data(), entry, before.size()) == 0, "rejection leaves glide entry untouched");
    } else {
        check(xhl::flight::install(game, authorize, logger), "install real server glide hook behind fake test authority");
        check(!xhl::flight::install(game, authorize, logger), "duplicate install rejected");
        // Redirect only the continuation inside this private mapping to our
        // register recorder. No executable on disk or game process is changed.
        std::array<unsigned char, 14> tail{0xff, 0x25, 0, 0, 0, 0};
        auto destination = reinterpret_cast<void*>(ProbeFlightTail);
        std::memcpy(tail.data() + 6, &destination, sizeof(destination));
        write(entry + 8, tail.data(), tail.size());
        check(probe(entry, &known_world, 2, false, 0x202), "denied owner gets vanilla pitch; registers/flags/MXCSR preserved");
        approved = true;
        for (auto flags : {0x202ull, 0x243ull, 0xad7ull})
            check(probe(entry, &known_world, 2, true, flags), "approved owner gets flight pitch with original condition codes and registers");
        bool isolated = true;
        for (std::uint32_t entity = 1; entity <= 16; ++entity)
            isolated &= probe(entry, &known_world, entity, entity == 2, 0x243);
        check(isolated, "all 16 slots retain per-character decisions");
        check(probe(entry, nullptr, 2, false, 0x202), "unresolved query context denied");
        int other_world;
        check(probe(entry, &other_world, 2, false, 0x243), "same entity number in another context denied");
        for (auto entity : {0u, 17u, 0xffffffffu})
            check(probe(entry, &known_world, entity, false, 0x202), "invalid owner index denied");
        std::atomic<bool> concurrent_ok{true};
        std::vector<std::thread> workers;
        for (std::uint32_t i = 1; i <= 8; ++i) workers.emplace_back([&, i] {
            for (int j = 0; j < 1000; ++j)
                if (!probe(entry, &known_world, i, i == 2, (j & 1) ? 0x243 : 0x202)) concurrent_ok = false;
        });
        for (auto& worker : workers) worker.join();
        check(concurrent_ok, "8000 simultaneous hook calls preserve owner isolation and machine state");
        approved = false;
        check(probe(entry, &known_world, 2, false, 0x243), "next call uses vanilla pitch after authorization revocation");
        check(MH_DisableHook(entry) == MH_OK, "only the flight hook can be disabled");
        check(probe(entry, &known_world, 2, false, 0x243), "restored original server MOVSS agrees with denied path");
    }
    std::printf("%u checks passed. Offline hook only; authenticated multiplayer flight is NOT verified.\n", checks);
}
