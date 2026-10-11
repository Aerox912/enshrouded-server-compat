#include "native_consumable_cost.hpp"
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>

using xhl::flight::Identity;
using namespace xhl::native_consumable_cost;

namespace {
void check(bool condition, const char* message, unsigned& count) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
    ++count;
}

struct Fixture {
    Identity identity{};
    QueryOwner query{};
    bool query_readable = true;
    bool actor_alive = true;
    bool lease_current = true;
    bool replace_identity_on_lease = false;
    bool kill_actor_on_lease = false;
    bool change_query_on_second_read = false;
    bool change_world_on_second_read = false;
    bool revoke_on_second_lease_check = false;
    std::uint16_t effects = static_cast<std::uint16_t>(
        xhl::native_effects::Effect::free_consumables);
    unsigned query_reads = 0;
    unsigned resolves = 0;
    unsigned alive_checks = 0;
    unsigned lease_checks = 0;

    static Fixture* active;

    Fixture() {
        identity.backend = 1;
        identity.session = 2;
        identity.world = 3;
        identity.player = 66; // compact query owner 3 maps to owner slot 2
        identity.machine = 1;
        identity.peer = 1;
        identity.steam = 0x0110000100000001ULL;
        identity.authentication = 4;
        identity.lifecycle = 5;
        query = {3, 3};
    }

    static bool read_query_owner(std::uintptr_t context,
        QueryOwner& out) noexcept {
        auto& fixture = *active;
        ++fixture.query_reads;
        if (!fixture.query_readable || context != 0xabc) return false;
        if (fixture.change_query_on_second_read && fixture.query_reads == 2)
            fixture.query.owner = 4;
        if (fixture.change_world_on_second_read && fixture.query_reads == 2)
            fixture.query.world = 4;
        out = fixture.query;
        return true;
    }

    static std::optional<Identity> resolve_owner(std::uintptr_t world,
        std::uint32_t owner) noexcept {
        auto& fixture = *active;
        ++fixture.resolves;
        if (world != fixture.identity.world || owner != 3) return {};
        return fixture.identity;
    }

    static bool is_alive(const Identity& identity) noexcept {
        auto& fixture = *active;
        ++fixture.alive_checks;
        return fixture.actor_alive && identity == fixture.identity;
    }

    static std::optional<std::uint16_t> active_effects(
        const Identity& identity, std::uint64_t now_ms) noexcept {
        auto& fixture = *active;
        ++fixture.lease_checks;
        if (fixture.replace_identity_on_lease) ++fixture.identity.lifecycle;
        if (fixture.kill_actor_on_lease) fixture.actor_alive = false;
        if (!fixture.lease_current || identity != fixture.identity || now_ms >= 100 ||
            (fixture.revoke_on_second_lease_check && fixture.lease_checks == 2))
            return {};
        return fixture.effects;
    }

    Services services() {
        active = this;
        return {read_query_owner, resolve_owner, is_alive, active_effects};
    }

    ActionFrame frame(ActionKind kind = ActionKind::used) const {
        return {kind, 0x140000000, 0xabc};
    }
};
Fixture* Fixture::active = nullptr;

struct RemovalFixture {
    NativeRemovalResult output{7, {0xa1, 0xb2, 0xc3}, 77};
    NativeRemovalResult* expected_output = &output;
    void* expected_transaction = reinterpret_cast<void*>(0xdef);
    std::uint32_t expected_owner = 3;
    std::uint32_t expected_item = 42;
    std::uint64_t expected_slot = 0x123456789abcdef0ULL;
    std::uint32_t expected_quantity = 2;
    unsigned calls = 0;
    bool args_match = false;
    void* native_return = reinterpret_cast<void*>(0xfeed);
    static RemovalFixture* active;

    static void* __fastcall original(void* result, void* transaction,
        std::uint32_t owner, std::uint32_t item, std::uint64_t packed_slot_ref,
        std::uint32_t quantity) {
        auto& fixture = *active;
        ++fixture.calls;
        fixture.args_match = result == fixture.expected_output &&
            transaction == fixture.expected_transaction &&
            owner == fixture.expected_owner && item == fixture.expected_item &&
            packed_slot_ref == fixture.expected_slot &&
            quantity == fixture.expected_quantity;
        return fixture.native_return;
    }

    NativeRemovalArgs args() const {
        return {expected_output, expected_transaction, expected_owner,
            expected_item, expected_slot, expected_quantity};
    }
};
RemovalFixture* RemovalFixture::active = nullptr;
}

int main() {
    unsigned checks = 0;
    Fixture fixture;
    auto services = fixture.services();
    const auto used_pc = fixture.frame().server_base + used_removal_return_rva;
    const auto fired_pc = fixture.frame(ActionKind::fired).server_base +
        fired_removal_return_rva;

    check(authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "registered used-consumable return site authorizes a live leased owner", checks);
    check(fixture.query_reads == 2 && fixture.resolves == 2 &&
        fixture.alive_checks == 2 && fixture.lease_checks == 2,
        "authorization re-reads native query owner, Identity, actor life, and lease", checks);

    check(authorized_removal(fired_pc, fixture.frame(ActionKind::fired), 3,
        10, services), "fired helper return site is distinct and owner-scoped", checks);
    check(!authorized_removal(fired_pc, fixture.frame(ActionKind::used), 3,
        10, services), "fired return PC cannot borrow the used action scope", checks);
    check(!authorized_removal(used_pc, fixture.frame(ActionKind::fired), 3,
        10, services), "used return PC cannot borrow the fired action scope", checks);
    check(!authorized_removal(used_pc + 1, fixture.frame(), 3, 10, services),
        "nearby native return PCs retain normal removal", checks);

    auto invalid_owner = fixture.frame();
    check(!authorized_removal(used_pc, invalid_owner, 0, 10, services),
        "zero inventory owner fails closed", checks);
    fixture.query.owner = 4;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "query owner and native removal owner must match", checks);
    fixture.query.owner = 3;
    fixture.query.world = 0;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "missing query world fails closed", checks);
    fixture.query.world = 3;

    fixture.lease_current = false;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "missing or expired effect lease denies bypass", checks);
    fixture.lease_current = true;
    fixture.effects = static_cast<std::uint16_t>(
        xhl::native_effects::Effect::free_crafting);
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "free-crafting lease does not authorize consumables", checks);
    fixture.effects = static_cast<std::uint16_t>(
        xhl::native_effects::Effect::free_consumables) | 0x8000;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "unknown effect bits deny bypass", checks);
    fixture.effects = static_cast<std::uint16_t>(
        xhl::native_effects::Effect::free_consumables);

    fixture.replace_identity_on_lease = true;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "owner lifecycle replacement during lease lookup denies bypass", checks);
    fixture.replace_identity_on_lease = false;
    fixture.identity.lifecycle = 5;
    fixture.kill_actor_on_lease = true;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "actor death during lease lookup denies bypass", checks);
    fixture.kill_actor_on_lease = false;
    fixture.actor_alive = true;
    fixture.change_query_on_second_read = true;
    fixture.query_reads = 0;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "native query cursor change during authorization denies bypass", checks);
    fixture.change_query_on_second_read = false;
    fixture.query.owner = 3;

    fixture.query_reads = 0;
    fixture.change_world_on_second_read = true;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services),
        "native query world change during authorization denies bypass", checks);
    fixture.change_world_on_second_read = false;
    fixture.query.world = 3;

    fixture.lease_checks = 0;
    fixture.revoke_on_second_lease_check = true;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10, services) &&
        fixture.lease_checks == 2,
        "lease revoked between the two current-effect reads denies bypass", checks);
    fixture.revoke_on_second_lease_check = false;

    auto no_context = fixture.frame();
    no_context.query_context = 0;
    check(!authorized_removal(used_pc, no_context, 3, 10, services),
        "missing action query context denies bypass", checks);
    Services missing_reader = services;
    missing_reader.read_query_owner = nullptr;
    Services missing_identity = services;
    missing_identity.resolve_owner = nullptr;
    Services missing_liveness = services;
    missing_liveness.is_alive = nullptr;
    Services missing_lease = services;
    missing_lease.active_effects = nullptr;
    check(!authorized_removal(used_pc, fixture.frame(), 3, 10,
        missing_reader) &&
        !authorized_removal(used_pc, fixture.frame(), 3, 10,
            missing_identity) &&
        !authorized_removal(used_pc, fixture.frame(), 3, 10,
            missing_liveness) &&
        !authorized_removal(used_pc, fixture.frame(), 3, 10,
            missing_lease),
        "every absent owner, life, or lease service fails closed", checks);

    Fixture deadline;
    auto deadline_services = deadline.services();
    const auto deadline_frame = deadline.frame();
    const auto deadline_pc = deadline_frame.server_base + used_removal_return_rva;
    check(authorized_removal(deadline_pc, deadline_frame, 3, 99,
        deadline_services), "lease remains valid immediately before deadline", checks);
    check(!authorized_removal(deadline_pc, deadline_frame, 3, 100,
        deadline_services), "lease expires exactly at its deadline", checks);
    Fixture::active = &fixture;

    RemovalFixture native;
    RemovalFixture::active = &native;
    void* result = nullptr;
    {
        ActionScope scope(fixture.frame());
        check(current_action_frame().has_value(),
            "native action scope is present during its callback", checks);
        check(invoke_native_removal(RemovalFixture::original, native.args(),
            used_pc, 10, services, result), "authorized hook adapter executes", checks);
        check(native.calls == 0 && result == &native.output &&
            native.output.status == 0 && native.output.remainder == 0 &&
            native.output.padding[0] == 0xa1 && native.output.padding[1] == 0xb2 &&
            native.output.padding[2] == 0xc3,
            "authorized adapter skips only payment and zeros only native result fields", checks);
    }
    check(!current_action_frame(), "action scope is restored after callback", checks);

    native.output = {7, {0xa1, 0xb2, 0xc3}, 77};
    fixture.lease_current = false;
    {
        ActionScope scope(fixture.frame());
        check(invoke_native_removal(RemovalFixture::original, native.args(),
            used_pc, 10, services, result), "denied hook adapter executes", checks);
    }
    check(native.calls == 1 && native.args_match && result == native.native_return &&
        native.output.status == 7 && native.output.remainder == 77,
        "denied path forwards all native arguments and preserves original result/state", checks);

    {
        ActionScope outer(fixture.frame());
        {
            ActionScope inner(fixture.frame(ActionKind::fired));
            check(current_action_frame()->kind == ActionKind::fired,
                "nested native scope replaces action kind", checks);
        }
        check(current_action_frame()->kind == ActionKind::used,
            "nested native scope restores prior action", checks);
    }

    std::cout << "PASS " << checks << " native consumable adapter checks\n";
    return 0;
}
