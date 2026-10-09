#include "read_only_server_observer.hpp"

#include <MinHook.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {
unsigned validation_calls = 0;
unsigned initialize_calls = 0;
unsigned create_calls = 0;
unsigned enable_calls = 0;
unsigned disable_calls = 0;
unsigned remove_calls = 0;
std::array<std::uintptr_t, xhl::native_time::observer::site_count> targets{};

void noop(void*) noexcept {}

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL " << message << '\n';
    return condition;
}

bool read_snapshot(void*, const xhl::native_time::runtime::NativeQueryView&,
    xhl::native_time::CallbackSnapshot&) noexcept {
    return false;
}

bool write_hour_fields(void*, const xhl::native_time::runtime::NativeQueryView&,
    const xhl::native_time::HourFieldSequence&) noexcept {
    return false;
}

bool deny_time(void*, const xhl::flight::Identity&) noexcept {
    return false;
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

extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID target, LPVOID,
    LPVOID* original) {
    if (create_calls < targets.size())
        targets[create_calls] = reinterpret_cast<std::uintptr_t>(target);
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
    using namespace xhl::native_time;
    using namespace xhl::native_time::observer;
    bool ok = true;
    const auto server = GetModuleHandleW(nullptr);
    SetEnvironmentVariableW(L"XHL_ENABLE_NATIVE_TIME_OBSERVER", nullptr);
    runtime::Host host{};
    host.read_snapshot = &read_snapshot;
    host.write_hour_fields = &write_hour_fields;
    host.time_capability = {nullptr, &deny_time};
    ok &= check(!install_local_server_time_runtime(server, host),
        "integrated time runtime remains off without the exact observer gate");
    ok &= check(validation_calls == 0 && initialize_calls == 0 &&
        create_calls == 0 && enable_calls == 0,
        "default-off path validates no image and creates no hooks");

    SetEnvironmentVariableW(L"XHL_ENABLE_NATIVE_TIME_OBSERVER", L"1");
    ok &= check(install_local_server_time_runtime(server, host),
        "explicit install starts the composed observe-only runtime");
    auto* adapter = local_server_time_runtime_adapter();
    ok &= check(adapter && adapter->state() == runtime::State::running &&
        adapter->mode() == runtime::Mode::observe_only &&
        adapter->hooks_ready() && !adapter->command_backend_ready(),
        "runtime is ready for bounded observation while writes remain disabled");
    ok &= check(local_read_only_server_observer_ready() &&
        create_calls == site_count && enable_calls == site_count &&
        initialize_calls == 1,
        "one adapter installs one hook for every observer site");

    std::size_t cace0_count = 0;
    for (std::size_t i = 0; i < targets.size(); ++i) {
        for (std::size_t j = i + 1; j < targets.size(); ++j)
            ok &= check(targets[i] != targets[j],
                "the six hook targets are unique");
        if (targets[i] == reinterpret_cast<std::uintptr_t>(server) + 0xcace0)
            ++cace0_count;
    }
    ok &= check(cace0_count == 1,
        "the composed runtime owns exactly one CACE0 hook");

    const auto creates_before_standalone = create_calls;
    ok &= check(!install_local_read_only_server_observer(server) &&
        create_calls == creates_before_standalone,
        "the standalone observer entry point cannot install duplicate hooks");
    ok &= check(stop_local_read_only_server_observer() &&
        disable_calls == site_count && remove_calls == 0,
        "integrated stop disables hooks and retains trampolines");
    ok &= check(adapter->state() == runtime::State::stopped,
        "the process-lifetime adapter object remains available after stop");

    SetEnvironmentVariableW(L"XHL_ENABLE_NATIVE_TIME_OBSERVER", nullptr);
    std::cout << (ok ? "PASS native time server runtime integration\n" :
        "FAIL native time server runtime integration\n");
    return ok ? 0 : 1;
}
