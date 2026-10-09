#include "native_effects.hpp"
#include "no_cost.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <thread>

using namespace xhl::flight;
using namespace xhl::native_effects;
namespace no_cost = xhl::no_cost;

namespace {
struct Fixture {
    Identity identity{};
    bool operator_resolves = true;
    bool actor_alive = true;
    bool lease_current = true;
    bool operator_mapping_current = true;
    std::uint16_t effects = 0;
    std::uint64_t expiry = 100;
    bool become_stale_during_lease = false;
    bool replace_identity_during_lease = false;
    bool invalidate_actor_during_lease = false;
    unsigned operator_resolve_calls = 0;

    Fixture() {
        identity.backend = 1;
        identity.session = 2;
        identity.world = 3;
        identity.player = 64;
        identity.machine = 1;
        identity.peer = 1;
        identity.steam = 0x0110000100000001ULL;
        identity.authentication = 4;
        identity.lifecycle = 5;
    }

    static Fixture* active;
    static std::optional<Identity> resolve_operator_owner_cb(
        const CraftingEventEvidence& event) noexcept {
        auto& f = *active;
        ++f.operator_resolve_calls;
        // Fixture native entity relationships are deliberately independent:
        // operator entity 0x2a0 resolves to Player 64, while query/station
        // entity 0x455 is different from both.
        if (!f.operator_resolves || !f.operator_mapping_current ||
            event.world != f.identity.world || event.query_context != 0xabc ||
            event.actor_descriptor != 0xdef || event.query_entity != 0x455 ||
            event.crafting_operator_id != 0x2a0 ||
            event.crafting_station_id != event.query_entity) return {};
        return f.identity;
    }
    static bool alive_cb(const Identity& identity) noexcept {
        return active->actor_alive && identity == active->identity;
    }
    static std::optional<std::uint16_t> effects_for(const Identity& identity,
        std::uint64_t now_ms) noexcept {
        auto& f = *active;
        if (!f.lease_current || identity != f.identity || now_ms >= f.expiry) return {};
        if (f.become_stale_during_lease) f.actor_alive = false;
        if (f.replace_identity_during_lease) ++f.identity.lifecycle;
        if (f.invalidate_actor_during_lease) f.operator_mapping_current = false;
        return f.effects;
    }
    Services services() {
        active = this;
        return {resolve_operator_owner_cb, alive_cb, effects_for};
    }
};
Fixture* Fixture::active = nullptr;

void check(bool good, const char* message, unsigned& checks) {
    if (!good) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
    ++checks;
}

struct NativeFixture {
    struct ActionPayload { NativeFixture* owner = nullptr; std::uint32_t marker = 9; };
    std::array<std::uint8_t, no_cost::transaction_prefix_size> transaction{};
    std::uint32_t validation_marker = 7;
    std::uint32_t output_marker = 11;
    ActionPayload action_payload{};
    no_cost::NativeCraftProcessorArgs args{};
    std::uint32_t native_status = 0;
    std::uint32_t observed_status = 0;
    std::uint32_t calls = 0;
    bool saw_bypass = false;
    bool arguments_preserved = false;
    bool action_arguments_preserved = false;
    bool action_authorized = false;
    bool processor_called = false;
    const Authorization* action_authorization = nullptr;
    void* expected_recipe = nullptr;
    std::atomic<unsigned>* rendezvous = nullptr;

    void initialize(void* recipe) {
        expected_recipe = recipe;
        action_payload.owner = this;
        args = {transaction.data(), recipe, &output_marker,
            0x123456789abcdef0ULL, &validation_marker, &action_payload, 0x2a0, 1};
    }

    static std::uint32_t __fastcall process(void* transaction, void* recipe,
        void* output, std::uint64_t recipe_options, void* validation_state,
        void* action_data, std::uint32_t operator_entity_id,
        std::uint32_t invocation_flags) {
        auto& self = *static_cast<ActionPayload*>(action_data)->owner;
        ++self.calls;
        self.saw_bypass = (static_cast<std::uint8_t*>(transaction)[
            no_cost::transaction_flags_offset] & no_cost::crafting_ingredient_bypass) != 0;
        self.arguments_preserved = transaction == self.transaction.data() &&
            recipe == self.expected_recipe && output == &self.output_marker &&
            recipe_options == 0x123456789abcdef0ULL &&
            validation_state == &self.validation_marker &&
            action_data == &self.action_payload && self.action_payload.marker == 9 &&
            operator_entity_id == 0x2a0 && invocation_flags == 1;
        if (self.rendezvous) {
            self.rendezvous->fetch_add(1, std::memory_order_acq_rel);
            while (self.rendezvous->load(std::memory_order_acquire) < 2)
                std::this_thread::yield();
        }
        return self.native_status;
    }

    static std::uint32_t __fastcall action(void* event, void* recipe) {
        auto& self = *static_cast<NativeFixture*>(event);
        self.action_arguments_preserved = recipe == self.expected_recipe;
        self.action_authorized = no_cost::crafting_authorization_active();
        self.action_authorization = no_cost::current_crafting_authorization();
        self.processor_called = no_cost::invoke_native_crafting_processor(
            process, self.args, self.observed_status);
        return self.observed_status;
    }
};
}

int main() {
    unsigned checks = 0;
    constexpr auto building_bit = static_cast<std::uint16_t>(Effect::free_building);
    constexpr auto crafting_bit = static_cast<std::uint16_t>(Effect::free_crafting);
    constexpr auto consumables_bit = static_cast<std::uint16_t>(Effect::free_consumables);
    check(backend_ready_effects_mask(false, false, false) == 0,
        "no active native cost backend advertises no effects", checks);
    check(backend_ready_effects_mask(true, false, false) == building_bit &&
        backend_ready_effects_mask(false, true, false) == crafting_bit &&
        backend_ready_effects_mask(false, false, true) == consumables_bit,
        "each native cost backend independently advertises only its effect", checks);
    check(backend_ready_effects_mask(true, true, false) == (building_bit | crafting_bit) &&
        backend_ready_effects_mask(true, false, true) == (building_bit | consumables_bit) &&
        backend_ready_effects_mask(false, true, true) == (crafting_bit | consumables_bit) &&
        backend_ready_effects_mask(true, true, true) == valid_effect_mask,
        "partial and complete native cost readiness compose without cross-enabling effects", checks);
    check(lease_effects_for_backends(valid_effect_mask, building_bit | consumables_bit) ==
        (building_bit | consumables_bit) &&
        lease_effects_for_backends(valid_effect_mask, 0) == 0 &&
        lease_effects_for_backends(valid_effect_mask, building_bit | 0x8000) == building_bit,
        "effect leases are intersected with ready backends and unknown bits are excluded", checks);
    Fixture f;
    auto services = f.services();
    CraftingEventEvidence event{f.identity.world, 0xabc, 0xdef,
        0x455, 0x2a0, 0x455};

    std::array<ActorOwnerCandidate, 2> actor_candidates{{
        {1, 0x777, f.identity, true},
        {2, 0x888, Identity{}, true},
    }};
    auto actor_match = match_authenticated_actor(0x2a0, 0x777,
        f.identity.world, actor_candidates);
    check(actor_match && actor_match->compact_owner == 1 &&
        actor_match->identity == f.identity,
        "raw operator EntityId maps by exact Actor component pointer to compact authenticated owner",
        checks);
    check(!match_authenticated_actor(0x2a0, 0x999, f.identity.world,
        actor_candidates),
        "unmatched non-player operator keeps ordinary ingredient costs", checks);
    auto second_owner = f.identity;
    second_owner.player = 65;
    second_owner.steam = 0x0110000100000002ULL;
    actor_candidates[1] = {2, 0x777, second_owner, true};
    check(!match_authenticated_actor(0x2a0, 0x777, f.identity.world,
        actor_candidates),
        "ambiguous Actor component ownership fails closed", checks);
    actor_candidates[1] = {2, 0x888, f.identity, false};
    check(actor_match && !match_authenticated_actor(0x402, 0x777,
        f.identity.world, actor_candidates),
        "EntityId bounds are checked without masking high bits", checks);
    auto stale_actor = f.identity;
    ++stale_actor.lifecycle;
    actor_candidates[0] = {1, 0x777, stale_actor, false};
    check(!match_authenticated_actor(0x2a0, 0x777, f.identity.world,
        actor_candidates),
        "dead or stale owner Actor component cannot authenticate an operator", checks);

    f.effects = static_cast<std::uint16_t>(Effect::free_crafting);
    const auto crafting = authorize(event, Effect::free_crafting, 10, services);
    check(crafting.has_value(), "authenticated live owner with current crafting lease is authorized", checks);
    check(crafting->identity() == f.identity && crafting->effect() == Effect::free_crafting,
        "authorization carries the exact server identity and effect", checks);
    check(event.crafting_operator_id != f.identity.player &&
        event.query_entity == event.crafting_station_id &&
        event.query_entity != event.crafting_operator_id,
        "operator owner and station eligibility are modeled as distinct entities", checks);
    check(f.operator_resolve_calls == 2,
        "operator-to-owner mapping is rechecked after lease lookup", checks);

    f.operator_resolve_calls = 0;
    auto denied = event;
    denied.query_entity = 0;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "zero query entity is rejected", checks);
    denied = event; denied.crafting_station_id = 0;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "zero crafting station is rejected", checks);
    denied = event; denied.query_entity = 0x456;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "query entity must be the event station for native eligibility", checks);
    denied = event; denied.query_context = 0;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "missing native query context is rejected", checks);
    denied = event; denied.actor_descriptor = 0;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "missing Actor component descriptor is rejected", checks);
    denied = event; denied.crafting_operator_id = 0;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "missing operator entity is rejected", checks);
    denied = event; denied.crafting_operator_id = 0x400;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "operator ID outside native component lookup bounds is rejected", checks);
    denied = event; denied.crafting_operator_id = 0x2a1;
    check(!authorize(denied, Effect::free_crafting, 10, services),
        "unknown or forged operator entity has no authenticated owner mapping", checks);
    auto missing_mapping = services;
    missing_mapping.resolve_operator_owner = nullptr;
    check(!authorize(event, Effect::free_crafting, 10, missing_mapping),
        "missing native operator-to-owner resolver keeps ordinary costs", checks);

    f.operator_resolves = false;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "unknown operator owner is denied", checks);
    f.operator_resolves = true;
    const auto valid_identity = f.identity;
    f.identity.player = 80; // Low six bits are outside the 16 authenticated slots.
    check(!authorize(event, Effect::free_crafting, 10, services),
        "operator cannot resolve to an out-of-range player slot", checks);
    f.identity = valid_identity;
    f.actor_alive = false;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "dead or stale actor is denied", checks);
    f.actor_alive = true;
    f.lease_current = false;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "missing or revoked Creative lease is denied", checks);
    f.lease_current = true;
    check(!authorize(event, Effect::free_crafting, f.expiry, services),
        "expired lease is denied", checks);
    f.effects = 0;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "approved owner without the requested effect keeps ordinary costs", checks);
    f.effects = static_cast<std::uint16_t>(Effect::free_building);
    check(!authorize(event, Effect::free_crafting, 10, services),
        "another effect cannot authorize free crafting", checks);
    f.effects = static_cast<std::uint16_t>(Effect::free_crafting) | 0x8000;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "unknown effect bits fail closed", checks);
    f.effects = static_cast<std::uint16_t>(Effect::free_crafting);
    f.become_stale_during_lease = true;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "actor invalidated during lease lookup is denied", checks);
    f.become_stale_during_lease = false;
    f.replace_identity_during_lease = true;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "identity replacement during lease lookup is denied", checks);
    f.replace_identity_during_lease = false;
    f.identity.lifecycle = 5;
    f.actor_alive = true;
    f.operator_mapping_current = true;
    f.invalidate_actor_during_lease = true;
    check(!authorize(event, Effect::free_crafting, 10, services),
        "operator mapping replacement during lease lookup is denied", checks);
    f.invalidate_actor_during_lease = false;
    f.operator_mapping_current = true;

    f.effects = static_cast<std::uint16_t>(Effect::free_crafting);
    auto build = authorize(event, Effect::free_building, 10, services);
    check(!build, "craft event cannot mint building authorization", checks);
    f.effects = static_cast<std::uint16_t>(Effect::free_building);
    build = authorize(event, Effect::free_building, 10, services);
    check(!build, "building lease cannot be authorized from craft event evidence", checks);

    std::array<std::uint8_t, 0xb1> transaction{};
    transaction[no_cost::transaction_flags_offset] = 0x20;
    {
        no_cost::CraftingTransactionScope scope(transaction, &*crafting);
        check(scope.active(), "authorized crafting scope is armed", checks);
        check(transaction[no_cost::transaction_flags_offset] == 0x28,
            "scope sets only the native ingredient-bypass bit", checks);
        transaction[no_cost::transaction_flags_offset] = 0x6f;
    }
    check(transaction[no_cost::transaction_flags_offset] == 0x67,
        "scope clears only its bit and preserves native flag changes", checks);

    transaction[no_cost::transaction_flags_offset] = 0xa8;
    {
        no_cost::CraftingTransactionScope scope(transaction, &*crafting);
        check(scope.active(), "preexisting native bit remains scoped", checks);
    }
    check(transaction[no_cost::transaction_flags_offset] == 0xa8,
        "scope restores a preexisting native bit", checks);

    transaction[no_cost::transaction_flags_offset] = 0x20;
    {
        no_cost::CraftingTransactionScope scope(transaction, nullptr);
        check(!scope.active() && transaction[no_cost::transaction_flags_offset] == 0x20,
            "missing authorization leaves native transaction unchanged", checks);
    }
    {
        no_cost::CraftingTransactionScope scope(std::span<std::uint8_t>(transaction.data(), 0xb0),
            &*crafting);
        check(!scope.active() && transaction[no_cost::transaction_flags_offset] == 0x20,
            "short transaction leaves native memory unchanged", checks);
    }

    int recipe_marker = 17;
    NativeFixture native;
    native.initialize(&recipe_marker);
    native.transaction[no_cost::transaction_flags_offset] = 0x20;
    std::uint32_t native_result = 99;
    check(no_cost::invoke_native_crafting_action(NativeFixture::action, &native,
        &recipe_marker, &*crafting, native_result),
        "verified native action adapter invokes its original", checks);
    check(native_result == 0 && native.processor_called && native.observed_status == 0,
        "action and processor preserve the native success result", checks);
    check(native.action_authorized, "action scope exposes only crafting authority on this thread", checks);
    check(native.action_authorization == &*crafting,
        "native hook can recheck the current full-identity authorization at transaction entry", checks);
    check(native.saw_bypass, "action-local authorization sets the processor flag", checks);
    check(native.arguments_preserved, "all eight processor arguments are preserved", checks);
    check(native.action_arguments_preserved && native.calls == 1,
        "two-argument action ABI is preserved and its processor runs once", checks);
    check(native.transaction[no_cost::transaction_flags_offset] == 0x20 &&
        native.output_marker == 11 && !no_cost::crafting_authorization_active(),
        "processor scope restores flags and action authority after native output", checks);

    native.native_status = 5;
    native.transaction[no_cost::transaction_flags_offset] = 0x60;
    check(no_cost::invoke_native_crafting_action(NativeFixture::action, &native,
        &recipe_marker, &*crafting, native_result) && native_result == 5,
        "native failure status is propagated unchanged", checks);
    check(native.saw_bypass && native.transaction[no_cost::transaction_flags_offset] == 0x60 &&
        !no_cost::crafting_authorization_active(),
        "forced native failure restores only the temporary bypass bit and action authority", checks);

    native.native_status = 0;
    native.transaction[no_cost::transaction_flags_offset] = 0x20;
    {
        no_cost::CraftingActionScope authorized_action(&*crafting);
        {
            no_cost::CraftingActionScope denied_nested_action(nullptr);
            check(no_cost::invoke_native_crafting_processor(NativeFixture::process,
                native.args, native_result) && !native.saw_bypass,
                "unauthorized nested action masks an enclosing crafting authority", checks);
        }
        check(no_cost::invoke_native_crafting_processor(NativeFixture::process,
            native.args, native_result) && native.saw_bypass,
            "outer crafting authority is restored after nested action", checks);
    }
    check(native.transaction[no_cost::transaction_flags_offset] == 0x20,
        "nested processor scopes restore the transaction byte", checks);

    std::atomic<unsigned> entered{0};
    NativeFixture authorized_thread, ordinary_thread;
    authorized_thread.initialize(&recipe_marker);
    ordinary_thread.initialize(&recipe_marker);
    authorized_thread.rendezvous = &entered;
    ordinary_thread.rendezvous = &entered;
    authorized_thread.transaction[no_cost::transaction_flags_offset] = 0x20;
    ordinary_thread.transaction[no_cost::transaction_flags_offset] = 0x20;
    std::uint32_t authorized_thread_result = 99, ordinary_thread_result = 99;
    bool authorized_thread_called = false, ordinary_thread_called = false;
    std::thread a([&] {
        authorized_thread_called = no_cost::invoke_native_crafting_action(
            NativeFixture::action, &authorized_thread, &recipe_marker,
            &*crafting, authorized_thread_result);
    });
    std::thread b([&] {
        ordinary_thread_called = no_cost::invoke_native_crafting_action(
            NativeFixture::action, &ordinary_thread, &recipe_marker,
            nullptr, ordinary_thread_result);
    });
    a.join(); b.join();
    check(authorized_thread_called && ordinary_thread_called &&
        authorized_thread.saw_bypass && !ordinary_thread.saw_bypass,
        "thread-local action authority does not leak across concurrent callbacks", checks);
    check(authorized_thread.transaction[no_cost::transaction_flags_offset] == 0x20 &&
        ordinary_thread.transaction[no_cost::transaction_flags_offset] == 0x20,
        "concurrent transaction scopes restore both native flag bytes", checks);

    native_result = 77;
    check(!no_cost::invoke_native_crafting_action(nullptr, &native, &recipe_marker,
        &*crafting, native_result) &&
        !no_cost::invoke_native_crafting_processor(nullptr, native.args, native_result) &&
        native_result == 77 &&
        native.transaction[no_cost::transaction_flags_offset] == 0x20,
        "missing native originals are reported without dispatch or state mutation", checks);

    Fixture ordinary;
    ordinary.identity.player = 65;
    ordinary.identity.steam = 0x0110000100000002ULL;
    const auto ordinary_services = ordinary.services();
    const CraftingEventEvidence other{ordinary.identity.world, 0xabc, 0xdef,
        0x455, 0x2a0, 0x455};
    check(!authorize(other, Effect::free_crafting, 10, ordinary_services),
        "ordinary second player retains ingredient costs", checks);

    std::cout << checks << " native cost policy checks passed (portable fixtures; no gameplay claim).\n";
}
