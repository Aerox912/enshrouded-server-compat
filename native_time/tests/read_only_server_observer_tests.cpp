#include "read_only_server_observer.hpp"

#include "adapter.hpp"
#include <MinHook.h>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>

namespace {
unsigned validation_calls = 0;
unsigned initialize_calls = 0;
unsigned create_calls = 0;
unsigned enable_calls = 0;
unsigned disable_calls = 0;
unsigned remove_calls = 0;

void noop() noexcept {}

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL " << message << '\n';
    return condition;
}

bool collect_line(void* context, const char* bytes, std::size_t size) noexcept {
    try {
        static_cast<std::string*>(context)->append(bytes, size);
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace

namespace xhl {
bool validate_server(HMODULE) {
    ++validation_calls;
    return true;
}
} // namespace xhl

extern "C" MH_STATUS WINAPI MH_Initialize() {
    ++initialize_calls;
    return MH_OK;
}

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID* original) {
    ++create_calls;
    if (!original) return MH_UNKNOWN;
    *original = reinterpret_cast<LPVOID>(&noop);
    return MH_OK;
}

extern "C" MH_STATUS WINAPI MH_EnableHook(LPVOID) {
    ++enable_calls;
    return MH_OK;
}

extern "C" MH_STATUS WINAPI MH_DisableHook(LPVOID) {
    ++disable_calls;
    return MH_OK;
}

extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID) {
    ++remove_calls;
    return MH_OK;
}

int main() {
    using namespace xhl::native_time::observer;
    bool ok = true;
    SetEnvironmentVariableW(L"XHL_ENABLE_NATIVE_TIME_OBSERVER", nullptr);
    ok &= check(!install_local_read_only_server_observer(GetModuleHandleW(nullptr)),
        "observer install is denied when its environment gate is absent");
    ok &= check(validation_calls == 0 && initialize_calls == 0 && create_calls == 0,
        "default-off install performs no image validation or hook-engine work");

    SetEnvironmentVariableW(L"XHL_ENABLE_NATIVE_TIME_OBSERVER", L"true");
    ok &= check(!install_local_read_only_server_observer(GetModuleHandleW(nullptr)),
        "observer gate accepts only the exact single-character value 1");
    ok &= check(validation_calls == 0 && initialize_calls == 0,
        "a non-exact gate value performs no image validation or hook-engine work");

    SetEnvironmentVariableW(L"XHL_ENABLE_NATIVE_TIME_OBSERVER", L"1");
    ok &= check(install_local_read_only_server_observer(GetModuleHandleW(nullptr)),
        "explicit exact gate validates and installs the six-hook observer");
    ok &= check(validation_calls >= 2 && initialize_calls == 1 &&
        create_calls == site_count && enable_calls == site_count &&
        local_read_only_server_observer_ready(),
        "pinned validator, MinHook initialization, and every hook are wired");
    ok &= check(!dump_local_read_only_server_observer_jsonl(collect_line, nullptr),
        "evidence export is refused before successful stop");

    ok &= check(stop_local_read_only_server_observer(),
        "explicit stop disables the observer hooks");
    ok &= check(disable_calls == site_count && !local_read_only_server_observer_ready(),
        "stop disables all hooks and clears observer readiness");
    std::string evidence;
    ok &= check(dump_local_read_only_server_observer_jsonl(collect_line, &evidence),
        "stopped observer exports JSONL through the caller sink");
    ok &= check(evidence.find("\"type\":\"summary\"") != std::string::npos &&
        evidence.find("\"raw_arguments_redacted\":true") != std::string::npos,
        "JSONL dump includes observer summary and marks native arguments redacted");
    ok &= check(remove_calls == 0,
        "successful stop retains hook trampolines and removes none");

    std::cout << (ok ? "PASS read-only server observer adapter tests\n" :
        "FAIL read-only server observer adapter tests\n");
    return ok ? 0 : 1;
}
