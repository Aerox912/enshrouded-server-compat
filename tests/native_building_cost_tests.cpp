#include "../src/native_building_cost.hpp"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

using namespace xhl;
using namespace xhl::native_building_cost;

namespace {
int failures = 0;

struct Fixture {
    std::uintptr_t world = 0x1000;
    std::uint32_t owner = 1;
    native_effects::Identity first{};
    native_effects::Identity second{};
    std::optional<std::uint16_t> effects =
        static_cast<std::uint16_t>(native_effects::Effect::free_building);
    unsigned resolve_calls = 0;
    unsigned alive_calls = 0;
    unsigned effect_calls = 0;
    bool resolve_missing = false;
    bool changed_identity = false;
    unsigned dead_at_call = 0;
    std::uint64_t lease_expires_at = UINT64_MAX;
};

Fixture fixture;

native_effects::Identity make_identity(std::uint32_t owner,
    std::uintptr_t world, std::uint64_t lifecycle = 7) {
    native_effects::Identity identity{};
    identity.backend = 0x10;
    identity.session = 0x20;
    identity.world = world;
    identity.player = 64 + owner - 1;
    identity.machine = 1;
    identity.peer = 1;
    identity.steam = (std::uint64_t{0x01100001} << 32) | 1;
    identity.authentication = 1;
    identity.lifecycle = lifecycle;
    return identity;
}

void reset_fixture() {
    fixture = Fixture{};
    fixture.first = make_identity(fixture.owner, fixture.world);
    fixture.second = fixture.first;
    fixture.resolve_calls = 0;
    fixture.alive_calls = 0;
    fixture.effect_calls = 0;
    fixture.resolve_missing = false;
    fixture.changed_identity = false;
    fixture.dead_at_call = 0;
}

std::optional<native_effects::Identity> resolve_owner(
    std::uintptr_t world, std::uint32_t owner) noexcept {
    ++fixture.resolve_calls;
    if (fixture.resolve_missing || world != fixture.world ||
        owner != fixture.owner) return {};
    if (fixture.changed_identity && fixture.resolve_calls > 1)
        return fixture.second;
    return fixture.first;
}

bool is_alive(const native_effects::Identity& identity) noexcept {
    ++fixture.alive_calls;
    if (fixture.dead_at_call && fixture.alive_calls >= fixture.dead_at_call)
        return false;
    return identity == fixture.first || identity == fixture.second;
}

std::optional<std::uint16_t> active_effects(
    const native_effects::Identity& identity, std::uint64_t now_ms) noexcept {
    ++fixture.effect_calls;
    if (identity != fixture.first || now_ms >= fixture.lease_expires_at) return {};
    return fixture.effects;
}

Services services() noexcept {
    return {resolve_owner, is_alive, active_effects};
}

PlacementPaymentEvidence evidence(std::uintptr_t return_rva =
    placement_payment_dynamic_return_rva) noexcept {
    return {true, fixture.world, fixture.owner, fixture.owner, return_rva,
        normal_payment_mode};
}

std::array<std::uint8_t, 32> context_bytes{};
std::array<std::uint8_t, 16> cost_bytes{};
std::array<std::uint8_t, 256> transaction_bytes{};
void* seen_context = nullptr;
void* seen_cost = nullptr;
std::uint8_t seen_pay_costs = 0;
std::uint8_t original_result = 0xa5;
unsigned original_calls = 0;

std::uint8_t __fastcall original_payment(void* context, void* cost_data,
    std::uint8_t pay_costs) {
    ++original_calls;
    seen_context = context;
    seen_cost = cost_data;
    seen_pay_costs = pay_costs;
    return original_result;
}

void reset_spy() {
    context_bytes.fill(0);
    cost_bytes.fill(0);
    transaction_bytes.fill(0);
    transaction_bytes[0xb0] = 0xa8;
    const auto transaction = reinterpret_cast<std::uintptr_t>(transaction_bytes.data());
    std::memcpy(context_bytes.data() + payment_context_transaction_offset,
        &transaction, sizeof(transaction));
    seen_context = nullptr;
    seen_cost = nullptr;
    seen_pay_costs = 0;
    original_result = 0xa5;
    original_calls = 0;
}

void check(bool condition, const char* expression) {
    if (condition) return;
    std::fprintf(stderr, "FAIL %s\n", expression);
    ++failures;
}
#define CHECK(expression) check((expression), #expression)

NativeBuildingPaymentArgs args(std::uint8_t pay_costs = 1) noexcept {
    return {context_bytes.data(), cost_bytes.data(), pay_costs};
}

void test_authorized_sites() {
    for (const auto site : {placement_payment_dynamic_return_rva,
             placement_payment_fixed_return_rva}) {
        reset_fixture();
        reset_spy();
        const auto result = invoke_native_building_payment(original_payment,
            args(), evidence(site), 100, services());
        CHECK(result == 1);
        CHECK(original_calls == 0);
        CHECK(fixture.resolve_calls == 2);
        CHECK(fixture.alive_calls == 2);
        CHECK(fixture.effect_calls == 1);
    }
}

void expect_original(PlacementPaymentEvidence ev,
    std::uint8_t pay_costs = 1) {
    reset_fixture();
    reset_spy();
    const auto call_args = args(pay_costs);
    const auto result = invoke_native_building_payment(original_payment,
        call_args, ev, 100, services());
    CHECK(result == original_result);
    CHECK(original_calls == 1);
    CHECK(seen_context == call_args.context);
    CHECK(seen_cost == call_args.cost_data);
    CHECK(seen_pay_costs == call_args.pay_costs);
}

void test_fail_closed_scope_and_identifiers() {
    auto ev = evidence();
    ev.registered_placement_callback = false;
    expect_original(ev);

    ev = evidence(placement_payment_dynamic_return_rva - 1);
    expect_original(ev);

    ev = evidence();
    ev.context_mode = 1;
    expect_original(ev);

    ev = evidence();
    ev.query_owner = 2;
    expect_original(ev);

    ev = evidence();
    ev.transaction_owner = 2;
    expect_original(ev);

    ev = evidence();
    ev.query_owner = 0;
    ev.transaction_owner = 0;
    expect_original(ev);

    ev = evidence();
    ev.query_owner = 17;
    ev.transaction_owner = 17;
    expect_original(ev);

    expect_original(evidence(), 0);
}

void test_fail_closed_owner_and_lease() {
    auto ev = evidence();

    reset_fixture();
    reset_spy();
    fixture.effects = static_cast<std::uint16_t>(
        native_effects::Effect::free_crafting);
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    fixture.effects = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(native_effects::Effect::free_building) |
        0x8000u);
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    fixture.effects.reset();
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    fixture.first.lifecycle = 0;
    fixture.second = fixture.first;
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    fixture.dead_at_call = 1;
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    fixture.changed_identity = true;
    fixture.second = make_identity(fixture.owner, fixture.world, 8);
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    fixture.dead_at_call = 2;
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    fixture.resolve_missing = true;
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, services()) == original_result);
    CHECK(original_calls == 1);

    reset_fixture();
    reset_spy();
    const Services missing{};
    CHECK(invoke_native_building_payment(original_payment, args(), ev,
        100, missing) == original_result);
    CHECK(original_calls == 1);
}

void test_owner_alias_and_expiry_boundaries() {
    reset_fixture();
    reset_spy();
    // A resolver that aliases compact owner 1 to the identity authenticated
    // for owner 2 must not authorize, even when the transaction repeats 1.
    fixture.first = make_identity(2, fixture.world);
    fixture.second = fixture.first;
    CHECK(invoke_native_building_payment(original_payment, args(), evidence(),
        99, services()) == original_result);
    CHECK(original_calls == 1);
    CHECK(fixture.effect_calls == 0);

    reset_fixture();
    reset_spy();
    fixture.lease_expires_at = 100;
    CHECK(invoke_native_building_payment(original_payment, args(), evidence(),
        99, services()) == 1);
    CHECK(original_calls == 0);

    reset_fixture();
    reset_spy();
    fixture.lease_expires_at = 100;
    CHECK(invoke_native_building_payment(original_payment, args(), evidence(),
        100, services()) == original_result);
    CHECK(original_calls == 1);
}

void test_preserves_native_context_and_status() {
    reset_fixture();
    reset_spy();
    const auto context_before = context_bytes;
    const auto cost_before = cost_bytes;
    const auto transaction_before = transaction_bytes;
    auto ev = evidence();
    const auto result = invoke_native_building_payment(original_payment,
        args(), ev, 100, services());
    CHECK(result == 1);
    CHECK(context_bytes == context_before);
    CHECK(cost_bytes == cost_before);
    CHECK(transaction_bytes == transaction_before);
    CHECK(transaction_bytes[0xb0] == 0xa8);

    reset_fixture();
    reset_spy();
    original_result = 0x37;
    ev = evidence();
    ev.context_mode = 2;
    const auto call_args = args(1);
    const auto denied_context_before = context_bytes;
    const auto denied_cost_before = cost_bytes;
    const auto denied_transaction_before = transaction_bytes;
    CHECK(invoke_native_building_payment(original_payment, call_args,
        ev, 100, services()) == 0x37);
    CHECK(original_calls == 1);
    CHECK(context_bytes == denied_context_before);
    CHECK(cost_bytes == denied_cost_before);
    CHECK(transaction_bytes == denied_transaction_before);
    CHECK(transaction_bytes[0xb0] == 0xa8);
}
void test_missing_original() {
    reset_fixture();
    reset_spy();
    CHECK(invoke_native_building_payment(nullptr, args(), evidence(), 100,
        services()) == 0);
    CHECK(original_calls == 0);
}
} // namespace

int main() {
    test_authorized_sites();
    test_fail_closed_scope_and_identifiers();
    test_fail_closed_owner_and_lease();
    test_owner_alias_and_expiry_boundaries();
    test_preserves_native_context_and_status();
    test_missing_original();

    if (failures) {
        std::fprintf(stderr, "%d native building-cost checks failed\n", failures);
        return 1;
    }
    std::puts("PASS native building-cost policy and exact forwarding checks");
    return 0;
}
