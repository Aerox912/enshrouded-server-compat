#include "adapter.hpp"
#include <MinHook.h>
#include "adapter_hook_lifecycle.hpp"
#include <bcrypt.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <vector>

namespace xhl {
namespace {
Logger log_message = nullptr;
bool attempted = false;
struct Hook {
    DWORD game_rva, callback_rva, owner_slot, call_slot;
    size_t length;
    std::array<unsigned char, 19> signature;
};
constexpr Hook hooks[] = {
    {0x1c0be0, 0x25b0, 0x49588, 0x495a8, 16,
     {0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55}},
    {0x87dae0, 0x38d0, 0x49580, 0x495a0, 16,
     {0x48,0x8b,0xc4,0x48,0x89,0x58,0x18,0x4c,0x89,0x48,0x20,0x55,0x56,0x57,0x41,0x54}},
    {0x1d38a0, 0x2190, 0x49578, 0x49590, 19,
     {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x68,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20}},
    {0x1d2310, 0x3300, 0x49530, 0x49538, 13,
     {0x48,0x83,0xec,0x38,0x0f,0x10,0x02,0x4c,0x8b,0xc2,0x4c,0x8b,0xc9}},
    {0x145510, 0x4bd0, 0x49520, 0x49528, 12,
     {0x4c,0x89,0x44,0x24,0x18,0x55,0x53,0x56,0x41,0x54,0x41,0x55}}
};
void log(const char* text) { if (log_message) log_message(text); }
SHORT WINAPI no_local_keyboard(int) { return 0; }
bool replace_pointer(void* slot, void* value) {
    DWORD previous;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &previous)) return false;
    InterlockedExchangePointer(static_cast<PVOID volatile*>(slot), value);
    DWORD ignored;
    return VirtualProtect(slot, sizeof(void*), previous, &ignored) != FALSE;
}
bool valid_image(HMODULE image) {
    auto b = reinterpret_cast<unsigned char*>(image);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(b);
    if (!b || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0 || dos->e_lfanew > 4096) return false;
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(b + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC &&
        nt->FileHeader.TimeDateStamp == 0x69fdecc9 && nt->OptionalHeader.SizeOfImage == 0x1da7000;
}
constexpr size_t adapter_hook_count = sizeof(hooks) / sizeof(hooks[0]);
using AdapterHookSet = xhl::adapter_hooks::HookSet<adapter_hook_count>;

struct AdapterHookContext {
    unsigned char* plugin = nullptr;
    std::array<void*, adapter_hook_count> targets{};
    std::array<void*, adapter_hook_count> originals{};
};

AdapterHookSet adapter_hook_set;
AdapterHookContext adapter_hook_context;

int adapter_hook_index(const AdapterHookContext& context, void* target) noexcept {
    for (size_t i = 0; i < context.targets.size(); ++i)
        if (context.targets[i] == target) return static_cast<int>(i);
    return -1;
}

void publish_pointer(unsigned char* plugin, DWORD slot, void* value) noexcept {
    auto* address = reinterpret_cast<PVOID volatile*>(plugin + slot);
    InterlockedExchangePointer(address, value);
}

void clear_pointer_if_unchanged(unsigned char* plugin, DWORD slot,
    void* expected) noexcept {
    auto* address = reinterpret_cast<PVOID volatile*>(plugin + slot);
    InterlockedCompareExchangePointer(address, nullptr, expected);
}

bool create_adapter_hook(void* opaque, void* target, void* detour,
    void** original) noexcept {
    auto& context = *static_cast<AdapterHookContext*>(opaque);
    const int index = adapter_hook_index(context, target);
    if (index < 0 || !context.plugin || !original) return false;
    // Only MH_OK grants ownership. An existing hook may belong to another module.
    if (MH_CreateHook(target, detour, original) != MH_OK) return false;

    const auto i = static_cast<size_t>(index);
    context.originals[i] = *original;
    if (*original) {
        publish_pointer(context.plugin, hooks[i].owner_slot, *original);
        publish_pointer(context.plugin, hooks[i].call_slot, *original);
    }
    return true;
}

bool remove_adapter_hook(void* opaque, void* target) noexcept {
    auto& context = *static_cast<AdapterHookContext*>(opaque);
    const int index = adapter_hook_index(context, target);
    if (index < 0 || !context.plugin ||
        MH_RemoveHook(target) != MH_OK) return false;

    const auto i = static_cast<size_t>(index);
    const auto original = context.originals[i];
    if (original) {
        clear_pointer_if_unchanged(context.plugin, hooks[i].owner_slot, original);
        clear_pointer_if_unchanged(context.plugin, hooks[i].call_slot, original);
    }
    context.originals[i] = nullptr;
    return true;
}

bool enable_adapter_hook(void*, void* target) noexcept {
    const auto status = MH_EnableHook(target);
    return status == MH_OK || status == MH_ERROR_ENABLED;
}

bool disable_adapter_hook(void*, void* target) noexcept {
    const auto status = MH_DisableHook(target);
    return status == MH_OK || status == MH_ERROR_DISABLED;
}
}

std::wstring module_path(HMODULE module) {
    std::wstring path(32768, L'\0');
    DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return path;
}

bool verify_file(const std::wstring& path, const char* expected) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> buffer{};
    DWORD length = 0;
    while (ok) {
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &length, nullptr)) { ok = false; break; }
        if (!length) break;
        ok = BCryptHashData(hash, buffer.data(), length, 0) >= 0;
    }
    std::array<unsigned char, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    char hex[65]{};
    constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < digest.size(); ++i) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    return ok && std::strcmp(hex, expected) == 0;
}

bool validate_server(HMODULE game) {
    const auto path = module_path(game);
    return !_wcsicmp(std::filesystem::path(path).filename().c_str(), L"enshrouded_server.exe") &&
        verify_file(path, server_hash) && valid_image(game);
}

bool start(HMODULE game, const std::wstring& plugin_path, Logger logger) {
    if (attempted) return false;
    attempted = true;
    log_message = logger;
    if (!validate_server(game)) {
        log("NOT APPLIED: dedicated-server executable does not match the verified build.");
        return false;
    }
    auto base = reinterpret_cast<unsigned char*>(game);
    for (const auto& h : hooks) {
        if (std::memcmp(base + h.game_rva, h.signature.data(), h.length)) {
            log("NOT APPLIED: a mining/input function is changed or already hooked.");
            return false;
        }
    }
    if (!verify_file(plugin_path, plugin_hash)) {
        log("NOT APPLIED: original XHL-Vein-Mining v1.0.33 DLL is missing or changed.");
        return false;
    }
    HMODULE plugin = LoadLibraryExW(plugin_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!plugin) { log("NOT APPLIED: could not load original XHL native plugin."); return false; }
    auto p = reinterpret_cast<unsigned char*>(plugin);

    if (!replace_pointer(p + 0x4cbe8, reinterpret_cast<void*>(no_local_keyboard))) {
        log("NOT APPLIED: could not disable client keyboard polling."); return false;
    }
    // The original initializer installs client cursor, rendering and outbound
    // input hooks. Do not call it. Retain its config parser and gameplay callbacks.
    *reinterpret_cast<Logger*>(p + 0x495b8) = logger;
    reinterpret_cast<void (*)()>(p + 0x2b00)();
    DWORD tls = TlsAlloc();
    if (tls == TLS_OUT_OF_INDEXES) { log("NOT APPLIED: TLS allocation failed."); return false; }
    *reinterpret_cast<DWORD*>(p + 0x370e0) = tls;
    *reinterpret_cast<void**>(p + 0x49598) = base + 0x881170;
    *reinterpret_cast<void**>(p + 0x49510) = base + 0x8b48f0;
    *reinterpret_cast<void**>(p + 0x49508) = base + 0x8b48b0;
    HMODULE pinned;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(plugin), &pinned)) {
        log("NOT APPLIED: could not pin native plugin lifetime.");
        return false;
    }
    const auto initialization = MH_Initialize();
    // MinHook is process-wide; an already initialized instance remains shared.
    if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED) {
        log("NOT APPLIED: hook engine initialization failed.");
        return false;
    }

    adapter_hook_context.plugin = p;
    for (size_t i = 0; i < adapter_hook_count; ++i)
        adapter_hook_context.targets[i] = base + hooks[i].game_rva;
    std::array<void*, adapter_hook_count> detours{};
    for (size_t i = 0; i < adapter_hook_count; ++i)
        detours[i] = p + hooks[i].callback_rva;

    const xhl::adapter_hooks::Operations hook_operations{
        &adapter_hook_context, create_adapter_hook, remove_adapter_hook,
        enable_adapter_hook, disable_adapter_hook};
    if (!adapter_hook_set.prepare(adapter_hook_context.targets, detours,
        hook_operations)) {
        if (adapter_hook_set.any_prepared()) {
            log("NOT APPLIED: could not prepare all five server hooks; an owned trampoline could not be removed and remains retained with the plugin pinned.");
        } else {
            log("NOT APPLIED: could not prepare all five server hooks; prepared adapter hooks were removed.");
        }
        return false;
    }

    // MinHook's per-target operation freezes threads for this hook only and
    // does not apply other modules' pending queue state. Original-call pointers
    // were published as each trampoline was created, before activation starts.
    if (!adapter_hook_set.enable_all(adapter_hook_context.targets,
        hook_operations)) {
        const bool disabled = adapter_hook_set.disable_enabled(
            adapter_hook_context.targets, hook_operations);
        if (disabled) {
            log("NOT APPLIED: per-target hook activation failed; enabled adapter hooks were disabled and their trampolines remain retained.");
        } else {
            log("PARTIAL ACTIVATION: an owned hook could not be disabled after activation failed; callbacks may still run, so the pinned plugin and all trampolines remain retained.");
        }
        return false;
    }
    log("HOOKS INSTALLED: 5 server gameplay/input hooks; client graphics and keyboard disabled.");
    log("Clients require XHL-Vein-Mining v1.0.33. Multiplayer mining and payout still require gameplay verification.");
    return true;
}
}
