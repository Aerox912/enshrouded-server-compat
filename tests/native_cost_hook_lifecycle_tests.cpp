#include "../src/native_cost_hook_lifecycle.hpp"

#include <array>
#include <cstdio>

using xhl::native_cost_hooks::HookSet;
using xhl::native_cost_hooks::Operations;

namespace {
int failures = 0;
int target_a = 0;
int target_b = 0;
int detour_a = 0;
int detour_b = 0;
int trampoline_a = 0;
int trampoline_b = 0;

const std::array<void*, 2> hook_targets_fixture{&target_a, &target_b};
const std::array<void*, 2> hook_detours_fixture{&detour_a, &detour_b};

struct FakeMinHook {
    std::array<bool, 2> created{};
    std::array<bool, 2> enabled{};
    std::array<unsigned, 2> create_calls{};
    std::array<unsigned, 2> remove_calls{};
    int fail_create_index = -1;
    unsigned fail_create_count = 0;
    int fail_remove_index = -1;
    unsigned fail_remove_count = 0;
    int fail_enable_index = -1;
    unsigned fail_enable_count = 0;
    int fail_disable_index = -1;
    unsigned fail_disable_count = 0;
};

FakeMinHook* fake = nullptr;

int index_of(void* target) noexcept {
    if (target == hook_targets_fixture[0]) return 0;
    if (target == hook_targets_fixture[1]) return 1;
    return -1;
}

bool create_hook(void* target, void* detour, void** original) noexcept {
    const auto index = index_of(target);
    if (!fake || index < 0 || !detour || !original) return false;
    auto& state = *fake;
    ++state.create_calls[static_cast<std::size_t>(index)];
    if (state.fail_create_index == index && state.fail_create_count) {
        --state.fail_create_count;
        return false;
    }
    if (state.created[static_cast<std::size_t>(index)]) return false;
    state.created[static_cast<std::size_t>(index)] = true;
    *original = index == 0 ? static_cast<void*>(&trampoline_a)
                           : static_cast<void*>(&trampoline_b);
    return true;
}

bool remove_hook(void* target) noexcept {
    const auto index = index_of(target);
    if (!fake || index < 0) return false;
    auto& state = *fake;
    ++state.remove_calls[static_cast<std::size_t>(index)];
    if (state.fail_remove_index == index && state.fail_remove_count) {
        --state.fail_remove_count;
        return false;
    }
    state.created[static_cast<std::size_t>(index)] = false;
    state.enabled[static_cast<std::size_t>(index)] = false;
    return true;
}

bool enable_hook(void* target) noexcept {
    const auto index = index_of(target);
    if (!fake || index < 0 || !fake->created[static_cast<std::size_t>(index)])
        return false;
    auto& state = *fake;
    if (state.fail_enable_index == index && state.fail_enable_count) {
        --state.fail_enable_count;
        return false;
    }
    state.enabled[static_cast<std::size_t>(index)] = true;
    return true;
}

bool disable_hook(void* target) noexcept {
    const auto index = index_of(target);
    if (!fake || index < 0) return false;
    auto& state = *fake;
    if (state.fail_disable_index == index && state.fail_disable_count) {
        --state.fail_disable_count;
        return false;
    }
    state.enabled[static_cast<std::size_t>(index)] = false;
    return true;
}

constexpr Operations fake_operations{create_hook, remove_hook, enable_hook,
    disable_hook};

void check(bool condition, const char* expression) {
    if (condition) return;
    std::fprintf(stderr, "FAIL %s\n", expression);
    ++failures;
}
#define CHECK(expression) check((expression), #expression)

int fake_original_a(int value) { return value + 17; }
int fake_original_b(int value) { return value + 29; }

int forward_while_inactive(const HookSet<2>& hooks, std::size_t index,
    int value) {
    if (index == 0 && hooks.original(index) == &trampoline_a)
        return fake_original_a(value);
    if (index == 1 && hooks.original(index) == &trampoline_b)
        return fake_original_b(value);
    return -1;
}

void test_successful_partial_create_rollback_and_retry() {
    FakeMinHook state{};
    fake = &state;
    HookSet<2> hooks;
    state.fail_create_index = 1;
    state.fail_create_count = 1;
    CHECK(!hooks.prepare(hook_targets_fixture, hook_detours_fixture,
        fake_operations));
    CHECK(!hooks.prepared(0));
    CHECK(hooks.original(0) == nullptr);
    CHECK(!state.created[0]);
    CHECK(state.remove_calls[0] == 1);

    state.fail_create_index = -1;
    CHECK(hooks.prepare(hook_targets_fixture, hook_detours_fixture,
        fake_operations));
    CHECK(hooks.all_prepared());
    CHECK(state.create_calls[0] == 2);
    CHECK(state.create_calls[1] == 2);
    CHECK(hooks.enable_all(hook_targets_fixture, fake_operations));
    CHECK(hooks.all_enabled());
    CHECK(hooks.disable_all(hook_targets_fixture, fake_operations));
    CHECK(!hooks.enabled(0) && !hooks.enabled(1));
}

void test_failed_partial_remove_retains_trampoline_and_retries_safely() {
    FakeMinHook state{};
    fake = &state;
    HookSet<2> hooks;
    state.fail_create_index = 1;
    state.fail_create_count = 1;
    state.fail_remove_index = 0;
    state.fail_remove_count = 1;
    CHECK(!hooks.prepare(hook_targets_fixture, hook_detours_fixture,
        fake_operations));
    CHECK(hooks.prepared(0));
    CHECK(hooks.original(0) == &trampoline_a);
    CHECK(state.created[0]);
    CHECK(forward_while_inactive(hooks, 0, 5) == 22);

    state.fail_create_index = -1;
    CHECK(hooks.prepare(hook_targets_fixture, hook_detours_fixture,
        fake_operations));
    CHECK(hooks.all_prepared());
    CHECK(state.create_calls[0] == 1);
    CHECK(state.create_calls[1] == 2);
    CHECK(hooks.original(0) == &trampoline_a);
    CHECK(forward_while_inactive(hooks, 0, 9) == 26);
}

void test_failed_enable_and_disable_preserve_state_for_retry() {
    FakeMinHook state{};
    fake = &state;
    HookSet<2> hooks;
    CHECK(hooks.prepare(hook_targets_fixture, hook_detours_fixture,
        fake_operations));

    bool active = false;
    state.fail_enable_index = 1;
    state.fail_enable_count = 1;
    state.fail_disable_index = 0;
    state.fail_disable_count = 1;
    CHECK(!hooks.enable_all(hook_targets_fixture, fake_operations));
    CHECK(!active);
    CHECK(hooks.enabled(0));
    CHECK(!hooks.enabled(1));
    CHECK(state.enabled[0]);
    CHECK(forward_while_inactive(hooks, 0, 3) == 20);

    state.fail_enable_index = -1;
    active = hooks.enable_all(hook_targets_fixture, fake_operations);
    CHECK(active);
    CHECK(hooks.all_enabled());

    state.fail_disable_index = 1;
    state.fail_disable_count = 1;
    active = false;
    CHECK(!hooks.disable_all(hook_targets_fixture, fake_operations));
    CHECK(!active);
    CHECK(hooks.enabled(1));
    CHECK(!hooks.enabled(0));
    CHECK(hooks.disable_all(hook_targets_fixture, fake_operations));
    CHECK(!hooks.enabled(0) && !hooks.enabled(1));
}

void test_missing_operations_fail_without_state_change() {
    HookSet<2> hooks;
    const Operations no_remove{create_hook, nullptr, enable_hook, disable_hook};
    const Operations no_enable{create_hook, remove_hook, nullptr, disable_hook};
    const Operations no_disable{create_hook, remove_hook, enable_hook, nullptr};
    CHECK(!hooks.prepare(hook_targets_fixture, hook_detours_fixture, no_remove));
    CHECK(!hooks.prepared(0) && !hooks.prepared(1));
    CHECK(!hooks.enable_all(hook_targets_fixture, no_enable));
    CHECK(!hooks.disable_all(hook_targets_fixture, no_disable));
}
} // namespace

int main() {
    test_successful_partial_create_rollback_and_retry();
    test_failed_partial_remove_retains_trampoline_and_retries_safely();
    test_failed_enable_and_disable_preserve_state_for_retry();
    test_missing_operations_fail_without_state_change();

    if (failures) {
        std::fprintf(stderr, "%d native hook-lifecycle checks failed\n", failures);
        return 1;
    }
    std::puts("PASS native hook partial-create, retry, pass-through, and disable checks");
    return 0;
}
