#include "adapter.hpp"
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

namespace {
unsigned checks = 0;
unsigned char* plugin;
std::atomic<DWORD> tick = 10000;
thread_local int observed_activation = -1;
thread_local unsigned unpack_calls = 0;
using Input = int (*)(void*, void*);
using Terraform = int (*)(void*, void*, void*);
Input input;
Terraform terraform;
void check(bool value, const char* name) {
    if (!value) { std::printf("FAIL: %s\n", name); std::exit(1); }
    ++checks;
    std::printf("PASS: %s\n", name);
}
void logger(const char* message) { std::printf("native: %s\n", message); }
DWORD WINAPI clock_stub() { return tick.load(); }
int unpack_stub(void*, void*) { ++unpack_calls; return 732; }
DWORD* activation() { return reinterpret_cast<DWORD* (*)(void*)>(plugin + 0x17880)(plugin + 0x37020); }
int terraform_stub(void*, void*, void*) { observed_activation = static_cast<int>(*activation()); return 891; }
template<class T> void put(void* base, size_t offset, T value) { std::memcpy(static_cast<unsigned char*>(base) + offset, &value, sizeof(value)); }
template<class T> T get(void* base, size_t offset) { T value; std::memcpy(&value, static_cast<unsigned char*>(base) + offset, sizeof(value)); return value; }
void protected_write(void* address, const void* value, size_t length) {
    DWORD old, ignored;
    if (!VirtualProtect(address, length, PAGE_READWRITE, &old)) std::exit(2);
    std::memcpy(address, value, length);
    if (!VirtualProtect(address, length, old, &ignored)) std::exit(2);
    FlushInstructionCache(GetCurrentProcess(), address, length);
}
struct Packet {
    std::array<unsigned char, 0x410> data{};
    Packet(DWORD entity, DWORD nonce, DWORD magic = 0x434c4858) {
        put(data.data(), 0, entity);
        put(message(), 0xf0, static_cast<BYTE>(1));
        put(message(), 0x378, static_cast<BYTE>(1));
        put(message(), 0x37c, magic);
        put(message(), 0x380, nonce);
    }
    void* message() { return data.data() + 16; }
    bool stripped() { return !get<BYTE>(message(), 0x378) && !get<uint64_t>(message(), 0x37c); }
    int send() { return input(nullptr, message()); }
};
struct World {
    std::array<unsigned char, 24 * 4> records{};
    std::array<unsigned char, 32> components{};
    std::array<unsigned char, 64> reflection{};
    DWORD entity = 0xabc00002;
    void* entity_pointer = &entity;
    explicit World(DWORD sender) {
        put(records.data(), 2 * 24 + 0xe, static_cast<WORD>(8));
        put(components.data(), 8, sender);
        put(reflection.data(), 0, records.data());
        put(reflection.data(), 0x10, components.data());
    }
    int mine() { observed_activation = -1; return terraform(nullptr, &entity_pointer, reflection.data()); }
};
void reset_cache() { std::memset(plugin + 0x49100, 0, 64 * 16); }
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) { std::puts("usage: adapter_tests server.exe original-plugin.dll [reject-header|reject-hook|reject-plugin]"); return 2; }
    HMODULE game = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    check(game != nullptr, "map dedicated server executable without running it");
    auto base = reinterpret_cast<unsigned char*>(game);
    if (argc == 4) {
        const std::wstring mode = argv[3];
        if (mode == L"reject-header") {
            const auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            DWORD changed = 0;
            protected_write(&nt->FileHeader.TimeDateStamp, &changed, sizeof(changed));
        } else if (mode == L"reject-hook") {
            BYTE changed = 0xcc;
            protected_write(base + 0x1d2310, &changed, 1);
        }
        const std::wstring path = mode == L"reject-plugin" ? std::wstring(argv[2]) + L".missing" : argv[2];
        check(!xhl::start(game, path, logger), "incompatible input rejected before activating hooks");
        check(base[0x1c0be0] == 0x48 && base[0x145510] == 0x4c, "other server entry points remain unchanged after rejection");
        std::printf("%u checks passed\n", checks);
        return 0;
    }
    check(xhl::start(game, argv[2], logger), "initialize adapter against verified server image");
    check(!xhl::start(game, argv[2], logger), "duplicate initialization rejected");
    plugin = reinterpret_cast<unsigned char*>(GetModuleHandleW(L"XHL-Vein-Mining.dll"));
    check(plugin != nullptr, "original DLL loaded");
    check(get<DWORD>(plugin, 0x37040) == 34, "original config parser loaded all 34 configured material IDs");
    check(!get<BYTE>(plugin, 0x37060) && get<BYTE>(plugin, 0x37060 + 93), "material whitelist preserves exclusion and inclusion");
    const auto no_keyboard = get<SHORT (WINAPI*)(int)>(plugin, 0x4cbe8);
    check(no_keyboard(VK_OEM_3) == 0, "dedicated path never reads local keyboard");
    void* fake_clock = reinterpret_cast<void*>(clock_stub);
    protected_write(plugin + 0x4c830, &fake_clock, sizeof(fake_clock));
    put(plugin, 0x49538, reinterpret_cast<void*>(unpack_stub));
    put(plugin, 0x49528, reinterpret_cast<void*>(terraform_stub));
    input = reinterpret_cast<Input>(base + 0x1d2310);
    terraform = reinterpret_cast<Terraform>(base + 0x145510);
    World alice(0x12340002), bob(0x56780003);
    Packet valid(0x12340002, 0xa5000001);
    check(valid.send() == 732 && valid.stripped() && unpack_calls == 1, "network hook strips activation and forwards original input exactly once");
    check(alice.mine() == 891 && observed_activation == 1, "matching remote player activates mining through the hooked server entry");
    check(*activation() == 0, "activation scope restored after mining");
    alice.mine();
    check(observed_activation == 0, "activation is consumed once");
    Packet(0x12340002, 0xa5000002).send();
    bob.mine();
    check(observed_activation == 0, "another player cannot consume sender activation");
    alice.mine();
    check(observed_activation == 1, "sender activation survives another player's ordinary mining");
    Packet(0x12340002, 0xa5000002).send();
    alice.mine();
    check(observed_activation == 0, "duplicate nonce does not reactivate consumed request");
    Packet(0x12340002, 0xa5000003).send();
    tick += 1001;
    alice.mine();
    check(observed_activation == 0, "expired request leaves ordinary mining unchanged");
    Packet(0x12340002, 0xa5000004).send();
    World reused(0x99990002);
    reused.mine();
    check(observed_activation == 0, "full sender ID checked across entity-index reuse");
    alice.mine();
    check(observed_activation == 1, "correct full sender ID still matches");
    reset_cache();
    for (DWORD invalid : {0x00000001u, 0xa5000000u, 0xa4000001u}) {
        Packet packet(0x12340002, invalid);
        packet.send();
        alice.mine();
        check(packet.stripped() && observed_activation == 0, "invalid protocol value removed without enabling mining");
    }
    Packet ordinary(0x12340002, 123, 456);
    const auto before = ordinary.data;
    ordinary.send();
    check(ordinary.data == before, "ordinary emote/input payload preserved byte for byte");
    for (DWORD invalid : {0u, 0xffffffffu}) {
        Packet packet(invalid, 0xa5000001);
        packet.send();
        check(packet.stripped(), "invalid sender stripped safely");
    }
    reset_cache();
    Packet(0x12340002, 0xa5fffffe).send(); alice.mine();
    Packet(0x12340002, 0xa5000001).send(); alice.mine();
    check(observed_activation == 1, "24-bit nonce wrap accepted");
    reset_cache(); tick = 0xffffff00;
    Packet(0x12340002, 0xa5000001).send(); tick = 0x10; alice.mine();
    check(observed_activation == 1, "tick-count rollover preserves valid request");
    reset_cache(); tick = 10000;
    Packet(0x12340002, 0xa5000001).send();
    *activation() = 7; alice.mine();
    check(observed_activation == 1 && *activation() == 7, "preexisting nested thread state restored");
    *activation() = 0;
    terraform(nullptr, nullptr, nullptr);
    check(observed_activation == 0, "unreadable entity context forwards ordinary mining safely");
    reset_cache();
    std::atomic<unsigned> failures = 0;
    std::vector<std::thread> workers;
    for (DWORD worker = 0; worker < 8; ++worker) workers.emplace_back([&, worker] {
        const DWORD sender = 0x22000002 + worker;
        World world(sender);
        for (DWORD nonce = 1; nonce <= 1000; ++nonce) {
            Packet packet(sender, 0xa5000000 | nonce);
            packet.send();
            world.mine();
            if (observed_activation != 1 || *activation() != 0 || !packet.stripped()) ++failures;
        }
    });
    for (auto& worker : workers) worker.join();
    check(failures == 0, "8000 concurrent player requests keep activation isolated per player and thread");
    std::printf("%u checks passed; no live world modified\n", checks);
    return 0;
}
