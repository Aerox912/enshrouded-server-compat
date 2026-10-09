#include "../src/adapter_hook_lifecycle.hpp"

#include <array>
#include <cstdio>

namespace {
using xhl::adapter_hooks::HookSet;
using xhl::adapter_hooks::Operations;

constexpr std::size_t hook_count = 5;
enum class ActionKind { create, remove, enable, disable };
struct Action {
    ActionKind kind;
    void* target;
};

std::array<int, hook_count> target_storage{};
std::array<int, hook_count> detour_storage{};
std::array<int, hook_count> trampoline_storage{};
int foreign_target_storage = 0;
const std::array<void*, hook_count> targets{
    &target_storage[0], &target_storage[1], &target_storage[2],
    &target_storage[3], &target_storage[4]};
const std::array<void*, hook_count> detours{
    &detour_storage[0], &detour_storage[1], &detour_storage[2],
    &detour_storage[3], &detour_storage[4]};
void* const foreign_target = &foreign_target_storage;

struct FakeBackend {
    static constexpr std::size_t action_capacity = 64;
    std::array<Action, action_capacity> actions{};
    std::size_t action_count = 0;
    std::array<bool, hook_count> created{};
    std::array<bool, hook_count> foreign_owned{};
    std::array<bool, hook_count> enabled{};
    std::array<bool, hook_count> foreign_pending_enable{};
    std::array<bool, hook_count> in_flight{};
    std::array<unsigned, hook_count> create_calls{};
    std::array<unsigned, hook_count> remove_calls{};
    std::array<unsigned, hook_count> enable_calls{};
    std::array<unsigned, hook_count> disable_calls{};
    int fail_create_index = -1;
    unsigned fail_create_count = 0;
    int fail_remove_index = -1;
    unsigned fail_remove_count = 0;
    int fail_enable_index = -1;
    unsigned fail_enable_count = 0;
    int fail_disable_index = -1;
    unsigned fail_disable_count = 0;

    bool foreign_created = true;
    bool foreign_enabled = false;
    bool foreign_target_pending_enable = true;
    unsigned foreign_target_operation_calls = 0;

    void record(ActionKind kind, void* target) noexcept {
        if (action_count < actions.size()) actions[action_count++] = {kind, target};
    }

    unsigned count(ActionKind kind, void* target) const noexcept {
        unsigned found = 0;
        for (std::size_t i = 0; i < action_count; ++i)
            if (actions[i].kind == kind && actions[i].target == target) ++found;
        return found;
    }
};

unsigned failures = 0;

int target_index(void* target) noexcept {
    for (std::size_t i = 0; i < hook_count; ++i)
        if (targets[i] == target) return static_cast<int>(i);
    return -1;
}

bool create_hook(void* context, void* target, void* detour,
    void** original) noexcept {
    auto& state = *static_cast<FakeBackend*>(context);
    state.record(ActionKind::create, target);
    const int index = target_index(target);
    if (target == foreign_target) ++state.foreign_target_operation_calls;
    if (index < 0 || !detour || !original) return false;
    const auto i = static_cast<std::size_t>(index);
    ++state.create_calls[i];
    if (state.fail_create_index == index && state.fail_create_count) {
        --state.fail_create_count;
        return false;
    }
    if (state.created[i]) return false;
    state.created[i] = true;
    *original = &trampoline_storage[i];
    return true;
}

bool remove_hook(void* context, void* target) noexcept {
    auto& state = *static_cast<FakeBackend*>(context);
    state.record(ActionKind::remove, target);
    const int index = target_index(target);
    if (target == foreign_target) ++state.foreign_target_operation_calls;
    if (index < 0) return false;
    const auto i = static_cast<std::size_t>(index);
    ++state.remove_calls[i];
    if (state.fail_remove_index == index && state.fail_remove_count) {
        --state.fail_remove_count;
        return false;
    }
    if (state.foreign_owned[i]) return false;
    if (state.in_flight[i]) return false;
    state.created[i] = false;
    state.enabled[i] = false;
    state.foreign_pending_enable[i] = false;
    return true;
}

bool enable_hook(void* context, void* target) noexcept {
    auto& state = *static_cast<FakeBackend*>(context);
    state.record(ActionKind::enable, target);
    const int index = target_index(target);
    if (target == foreign_target) ++state.foreign_target_operation_calls;
    if (index < 0) return false;
    const auto i = static_cast<std::size_t>(index);
    ++state.enable_calls[i];
    if (!state.created[i]) return false;
    if (state.foreign_owned[i]) return false;
    if (state.fail_enable_index == index && state.fail_enable_count) {
        --state.fail_enable_count;
        return false;
    }
    state.enabled[i] = true;
    return true;
}

bool disable_hook(void* context, void* target) noexcept {
    auto& state = *static_cast<FakeBackend*>(context);
    state.record(ActionKind::disable, target);
    const int index = target_index(target);
    if (target == foreign_target) ++state.foreign_target_operation_calls;
    if (index < 0) return false;
    const auto i = static_cast<std::size_t>(index);
    ++state.disable_calls[i];
    if (!state.created[i]) return false;
    if (state.foreign_owned[i]) return false;
    if (state.fail_disable_index == index && state.fail_disable_count) {
        --state.fail_disable_count;
        return false;
    }
    state.enabled[i] = false;
    return true;
}

Operations operations(FakeBackend& backend) noexcept {
    return {&backend, create_hook, remove_hook, enable_hook, disable_hook};
}

void check(bool condition, const char* expression) {
    if (condition) return;
    std::fprintf(stderr, "FAIL %s\n", expression);
    ++failures;
}
#define CHECK(expression) check((expression), #expression)

void check_action(const FakeBackend& backend, std::size_t index,
    ActionKind kind, void* target) {
    CHECK(index < backend.action_count);
    if (index >= backend.action_count) return;
    CHECK(backend.actions[index].kind == kind);
    CHECK(backend.actions[index].target == target);
}

void check_foreign_untouched(const FakeBackend& backend) {
    CHECK(backend.foreign_created);
    CHECK(!backend.foreign_enabled);
    CHECK(backend.foreign_target_pending_enable);
    CHECK(backend.foreign_target_operation_calls == 0);
}

void test_already_created_target_is_foreign_and_never_adopted() {
    FakeBackend backend;
    HookSet<hook_count> hooks;
    const auto ops = operations(backend);
    constexpr std::size_t foreign_index = 2;
    backend.created[foreign_index] = true;
    backend.foreign_owned[foreign_index] = true;
    backend.foreign_pending_enable[foreign_index] = true;

    CHECK(!hooks.prepare(targets, detours, ops));
    CHECK(!hooks.prepared(0));
    CHECK(!hooks.prepared(1));
    CHECK(hooks.prepared(foreign_index) == false);
    CHECK(hooks.original(foreign_index) == nullptr);
    CHECK(!hooks.enable_all(targets, ops));

    CHECK(backend.count(ActionKind::create, targets[foreign_index]) == 1);
    CHECK(backend.count(ActionKind::remove, targets[foreign_index]) == 0);
    CHECK(backend.count(ActionKind::enable, targets[foreign_index]) == 0);
    CHECK(backend.count(ActionKind::disable, targets[foreign_index]) == 0);
    CHECK(backend.count(ActionKind::remove, targets[0]) == 1);
    CHECK(backend.count(ActionKind::remove, targets[1]) == 1);
    CHECK(backend.created[foreign_index]);
    CHECK(backend.foreign_owned[foreign_index]);
    CHECK(!backend.enabled[foreign_index]);
    CHECK(backend.foreign_pending_enable[foreign_index]);
    check_action(backend, 0, ActionKind::create, targets[0]);
    check_action(backend, 1, ActionKind::create, targets[1]);
    check_action(backend, 2, ActionKind::create, targets[foreign_index]);
    check_action(backend, 3, ActionKind::remove, targets[1]);
    check_action(backend, 4, ActionKind::remove, targets[0]);
    CHECK(backend.action_count == 5);
    check_foreign_untouched(backend);
}

void test_success_enables_only_owned_hooks_directly() {
    FakeBackend backend;
    HookSet<hook_count> hooks;
    const auto ops = operations(backend);

    CHECK(hooks.prepare(targets, detours, ops));
    CHECK(hooks.all_prepared());
    CHECK(hooks.enable_all(targets, ops));
    CHECK(hooks.all_enabled());
    CHECK(backend.foreign_target_operation_calls == 0);
    CHECK(backend.action_count == hook_count * 2);
    for (std::size_t i = 0; i < hook_count; ++i) {
        CHECK(backend.created[i]);
        CHECK(backend.enabled[i]);
        CHECK(hooks.original(i) == &trampoline_storage[i]);
        CHECK(backend.enable_calls[i] == 1);
        CHECK(backend.count(ActionKind::create, targets[i]) == 1);
        CHECK(backend.count(ActionKind::enable, targets[i]) == 1);
        CHECK(backend.count(ActionKind::remove, targets[i]) == 0);
        CHECK(backend.count(ActionKind::disable, targets[i]) == 0);
        check_action(backend, i, ActionKind::create, targets[i]);
        check_action(backend, hook_count + i, ActionKind::enable, targets[i]);
    }
    check_foreign_untouched(backend);
}

void test_failed_cleanup_retains_owned_hook_and_retry_reuses_it() {
    FakeBackend backend;
    HookSet<hook_count> hooks;
    const auto ops = operations(backend);
    backend.fail_create_index = 3;
    backend.fail_create_count = 1;
    backend.fail_remove_index = 1;
    backend.fail_remove_count = 1;

    CHECK(!hooks.prepare(targets, detours, ops));
    CHECK(!hooks.prepared(0));
    CHECK(hooks.prepared(1));
    CHECK(hooks.original(1) == &trampoline_storage[1]);
    CHECK(!hooks.prepared(2));
    CHECK(!hooks.prepared(3));
    CHECK(!hooks.prepared(4));
    CHECK(backend.remove_calls[0] == 1);
    CHECK(backend.remove_calls[1] == 1);
    CHECK(backend.remove_calls[2] == 1);
    CHECK(backend.remove_calls[3] == 0);
    CHECK(backend.remove_calls[4] == 0);
    CHECK(backend.created[1]);
    check_action(backend, 0, ActionKind::create, targets[0]);
    check_action(backend, 1, ActionKind::create, targets[1]);
    check_action(backend, 2, ActionKind::create, targets[2]);
    check_action(backend, 3, ActionKind::create, targets[3]);
    check_action(backend, 4, ActionKind::remove, targets[2]);
    check_action(backend, 5, ActionKind::remove, targets[1]);
    check_action(backend, 6, ActionKind::remove, targets[0]);
    check_foreign_untouched(backend);

    backend.fail_create_index = -1;
    CHECK(hooks.prepare(targets, detours, ops));
    CHECK(hooks.all_prepared());
    CHECK(backend.create_calls[0] == 2);
    CHECK(backend.create_calls[1] == 1);
    CHECK(backend.create_calls[2] == 2);
    CHECK(backend.create_calls[3] == 2);
    CHECK(backend.create_calls[4] == 1);
    CHECK(backend.remove_calls[1] == 1);
    CHECK(hooks.original(1) == &trampoline_storage[1]);
    check_foreign_untouched(backend);
}

void test_partial_activation_failed_disable_retains_trampolines_and_retries() {
    FakeBackend backend;
    HookSet<hook_count> hooks;
    const auto ops = operations(backend);
    CHECK(hooks.prepare(targets, detours, ops));
    backend.fail_enable_index = 2;
    backend.fail_enable_count = 1;

    CHECK(!hooks.enable_all(targets, ops));
    CHECK(hooks.enabled(0));
    CHECK(hooks.enabled(1));
    CHECK(!hooks.enabled(2));
    CHECK(hooks.ever_enabled(0));
    CHECK(hooks.ever_enabled(1));
    CHECK(!hooks.ever_enabled(2));
    backend.in_flight[0] = true;

    backend.fail_disable_index = 0;
    backend.fail_disable_count = 1;
    CHECK(!hooks.disable_enabled(targets, ops));
    CHECK(hooks.enabled(0));
    CHECK(!hooks.enabled(1));
    CHECK(hooks.prepared(0));
    CHECK(hooks.original(0) == &trampoline_storage[0]);
    CHECK(hooks.ever_enabled(0));
    for (std::size_t i = 0; i < hook_count; ++i)
        CHECK(hooks.original(i) == &trampoline_storage[i]);
    CHECK(backend.remove_calls[0] == 0);
    CHECK(backend.remove_calls[1] == 0);
    check_foreign_untouched(backend);

    CHECK(hooks.disable_enabled(targets, ops));
    CHECK(!hooks.any_enabled());
    CHECK(hooks.ever_enabled(0));
    CHECK(hooks.ever_enabled(1));
    CHECK(backend.remove_calls[0] == 0);
    CHECK(backend.remove_calls[1] == 0);

    backend.fail_enable_index = -1;
    CHECK(hooks.enable_all(targets, ops));
    CHECK(hooks.all_enabled());
    CHECK(hooks.prepare(targets, detours, ops));
    CHECK(backend.remove_calls[0] == 0);
    CHECK(backend.remove_calls[1] == 0);
    check_foreign_untouched(backend);
}
} // namespace

int main() {
    test_success_enables_only_owned_hooks_directly();
    test_already_created_target_is_foreign_and_never_adopted();
    test_failed_cleanup_retains_owned_hook_and_retry_reuses_it();
    test_partial_activation_failed_disable_retains_trampolines_and_retries();

    if (failures) {
        std::fprintf(stderr, "%u adapter hook lifecycle checks failed\n", failures);
        return 1;
    }
    std::puts("PASS adapter hook ownership, per-target activation, cleanup, and retry checks");
    return 0;
}
