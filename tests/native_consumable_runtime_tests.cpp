#include "../src/native_consumable_runtime_adapter.hpp"
#include "../src/native_consumable_result_write.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>

namespace adapter = xhl::native_consumable_runtime_adapter;
namespace cost = xhl::native_consumable_cost;

namespace {
int failures = 0;
constexpr std::uintptr_t server_base = 0x140000000;
constexpr std::uintptr_t used_pc = server_base + cost::used_removal_return_rva;
constexpr std::uintptr_t fired_pc = server_base + cost::fired_removal_return_rva;
void* query_context = reinterpret_cast<void*>(0xabc);

void check(bool condition, const char* expression) {
    if (condition) return;
    std::fprintf(stderr, "FAIL %s\n", expression);
    ++failures;
}
#define CHECK(expression) check((expression), #expression)

struct Fixture {
    xhl::flight::Identity identity{};
    cost::QueryOwner query{0x1234, 3};
    cost::NativeRemovalResult output{7, {0xa1, 0xb2, 0xc3}, 77};
    cost::NativeRemovalArgs args{};
    adapter::Mode mode = adapter::Mode::free_consumables;
    bool query_readable = true;
    bool actor_alive = true;
    bool lease_current = true;
    bool mode_current = true;
    bool writer_allowed = true;
    bool writer_fault_after_status = false;
    bool exercise_removal = true;
    std::uintptr_t return_address = used_pc;
    void* native_result = reinterpret_cast<void*>(0xbeef);
    void* adapter_result = nullptr;
    adapter::RemovalDisposition removal_disposition =
        adapter::RemovalDisposition::no_original;
    void* seen_result_argument = nullptr;
    void* seen_query = nullptr;
    void* seen_data = nullptr;
    void* seen_transaction = nullptr;
    std::uint32_t seen_owner = 0;
    std::uint32_t seen_item = 0;
    std::uint64_t seen_slot = 0;
    std::uint32_t seen_quantity = 0;
    unsigned used_calls = 0;
    unsigned fired_calls = 0;
    unsigned remove_calls = 0;
    unsigned query_reads = 0;
    unsigned resolve_calls = 0;
    unsigned alive_calls = 0;
    unsigned lease_calls = 0;
    bool used_scope_seen = false;
    bool fired_scope_seen = false;
    static Fixture* active;

    Fixture() {
        identity.backend = 1;
        identity.session = 2;
        identity.world = query.world;
        identity.player = 66;
        identity.machine = 1;
        identity.peer = 1;
        identity.steam = 0x0110000100000001ULL;
        identity.authentication = 4;
        identity.lifecycle = 5;
        args = {&output, reinterpret_cast<void*>(0xdef), 3, 42,
            0x123456789abcdef0ULL, 2};
    }

    static bool read_query(std::uintptr_t context,
        cost::QueryOwner& output) noexcept {
        auto& fixture = *active;
        ++fixture.query_reads;
        if (!fixture.query_readable || context !=
            reinterpret_cast<std::uintptr_t>(query_context)) return false;
        output = fixture.query;
        return true;
    }
    static std::optional<xhl::flight::Identity> resolve(
        std::uintptr_t world, std::uint32_t owner) noexcept {
        auto& fixture = *active;
        ++fixture.resolve_calls;
        if (world != fixture.query.world || owner != fixture.query.owner)
            return {};
        return fixture.identity;
    }
    static bool alive(const xhl::flight::Identity& identity) noexcept {
        auto& fixture = *active;
        ++fixture.alive_calls;
        return fixture.actor_alive && identity == fixture.identity;
    }
    static std::optional<std::uint16_t> effects(
        const xhl::flight::Identity& identity, std::uint64_t now_ms) noexcept {
        auto& fixture = *active;
        ++fixture.lease_calls;
        if (!fixture.lease_current || identity != fixture.identity ||
            now_ms >= 100) return {};
        return static_cast<std::uint16_t>(
            xhl::native_effects::Effect::free_consumables);
    }
    static adapter::WriteOutcome write_zero(
        cost::NativeRemovalResult* result) noexcept {
        if (!result || !active->writer_allowed)
            return adapter::WriteOutcome::unchanged;
        result->status = 0;
        if (active->writer_fault_after_status)
            return adapter::WriteOutcome::indeterminate;
        result->remainder = 0;
        return adapter::WriteOutcome::committed;
    }
    static adapter::WriteOutcome write_atomic(
        cost::NativeRemovalResult* result) noexcept {
        using WriterOutcome = xhl::native_consumable_result_write::Outcome;
        switch (xhl::native_consumable_result_write::replace_with_free_result(result)) {
        case WriterOutcome::unchanged:
            return adapter::WriteOutcome::unchanged;
        case WriterOutcome::committed:
            return adapter::WriteOutcome::committed;
        case WriterOutcome::indeterminate:
            return adapter::WriteOutcome::indeterminate;
        }
        return adapter::WriteOutcome::indeterminate;
    }
    static bool effect_mode_current() noexcept {
        return active->mode_current;
    }
    static void* original_remove(void* result, void* transaction,
        std::uint32_t owner, std::uint32_t item, std::uint64_t packed_slot_ref,
        std::uint32_t quantity) {
        auto& fixture = *active;
        ++fixture.remove_calls;
        fixture.seen_result_argument = result;
        fixture.seen_transaction = transaction;
        fixture.seen_owner = owner;
        fixture.seen_item = item;
        fixture.seen_slot = packed_slot_ref;
        fixture.seen_quantity = quantity;
        fixture.adapter_result = result;
        return fixture.native_result;
    }
    static void original_used(void* query) {
        auto& fixture = *active;
        ++fixture.used_calls;
        fixture.seen_query = query;
        const auto frame = cost::current_action_frame();
        fixture.used_scope_seen = frame && frame->kind == cost::ActionKind::used &&
            frame->server_base == server_base &&
            frame->query_context == reinterpret_cast<std::uintptr_t>(query);
        if (fixture.exercise_removal) {
            const cost::Services services{read_query, resolve, alive, effects};
            const auto outcome = adapter::invoke_removal(original_remove,
                fixture.args, fixture.return_address, 10, fixture.mode, services,
                write_zero, effect_mode_current);
            fixture.adapter_result = outcome.native_return;
            fixture.removal_disposition = outcome.disposition;
        }
    }
    static std::uint8_t original_fired(void* query, void* data) {
        auto& fixture = *active;
        ++fixture.fired_calls;
        fixture.seen_query = query;
        fixture.seen_data = data;
        const auto frame = cost::current_action_frame();
        fixture.fired_scope_seen = frame && frame->kind == cost::ActionKind::fired &&
            frame->server_base == server_base &&
            frame->query_context == reinterpret_cast<std::uintptr_t>(query);
        if (fixture.exercise_removal) {
            const cost::Services services{read_query, resolve, alive, effects};
            const auto outcome = adapter::invoke_removal(original_remove,
                fixture.args, fixture.return_address, 10, fixture.mode, services,
                write_zero, effect_mode_current);
            fixture.adapter_result = outcome.native_return;
            fixture.removal_disposition = outcome.disposition;
        }
        return 0x5a;
    }
    cost::Services services() {
        active = this;
        return {read_query, resolve, alive, effects};
    }
    void reset_result() {
        output = {7, {0xa1, 0xb2, 0xc3}, 77};
        remove_calls = query_reads = resolve_calls = alive_calls = lease_calls = 0;
        seen_query = seen_data = seen_transaction = adapter_result = nullptr;
        removal_disposition = adapter::RemovalDisposition::no_original;
        seen_result_argument = nullptr;
        seen_owner = seen_item = seen_quantity = 0;
        seen_slot = 0;
        used_scope_seen = fired_scope_seen = false;
    }
    bool forwarded_args_match() const {
        return seen_result_argument == args.result &&
            seen_transaction == args.transaction && seen_owner == args.owner &&
            seen_item == args.item && seen_slot == args.packed_slot_ref &&
            seen_quantity == args.quantity;
    }
};
Fixture* Fixture::active = nullptr;

adapter::RemovalOutcome invoke_atomic_scoped_removal(Fixture& fixture) {
    const auto services = fixture.services();
    const cost::ActionFrame frame{cost::ActionKind::used, server_base,
        reinterpret_cast<std::uintptr_t>(query_context)};
    cost::ActionScope scope(frame);
    return adapter::invoke_removal(Fixture::original_remove, fixture.args,
        used_pc, 10, fixture.mode, services, Fixture::write_atomic,
        Fixture::effect_mode_current);
}
void test_used_action_scopes_and_authorized_adapter() {
    Fixture fixture;
    Fixture::active = &fixture;
    fixture.return_address = used_pc;
    adapter::invoke_used_action(Fixture::original_used, query_context,
        server_base, adapter::Mode::free_consumables);
    CHECK(fixture.used_calls == 1 && fixture.seen_query == query_context);
    CHECK(fixture.used_scope_seen);
    CHECK(fixture.remove_calls == 0);
    CHECK(fixture.adapter_result == fixture.args.result);
    CHECK(fixture.removal_disposition == adapter::RemovalDisposition::bypassed);
    CHECK(fixture.output.status == 0 && fixture.output.remainder == 0);
    CHECK(fixture.output.padding[0] == 0xa1 &&
        fixture.output.padding[1] == 0xb2 && fixture.output.padding[2] == 0xc3);
    CHECK(fixture.query_reads == 2 && fixture.resolve_calls == 2 &&
        fixture.alive_calls == 2 && fixture.lease_calls == 2);
    CHECK(!cost::current_action_frame());
}

void test_fired_helper_scopes_and_forwards_abi() {
    Fixture fixture;
    Fixture::active = &fixture;
    fixture.return_address = fired_pc;
    auto* const action_data = reinterpret_cast<void*>(0x7654);
    const auto result = adapter::invoke_fired_action(Fixture::original_fired,
        query_context, action_data, server_base, adapter::Mode::free_consumables);
    CHECK(result == 0x5a && fixture.fired_calls == 1);
    CHECK(fixture.seen_query == query_context && fixture.seen_data == action_data);
    CHECK(fixture.fired_scope_seen);
    CHECK(fixture.remove_calls == 0 && fixture.adapter_result == fixture.args.result);
    CHECK(fixture.removal_disposition == adapter::RemovalDisposition::bypassed);
    CHECK(fixture.output.status == 0 && fixture.output.remainder == 0);
    CHECK(!cost::current_action_frame());
}

void expect_original_once(Fixture& fixture) {
    fixture.services();
    fixture.reset_result();
    fixture.output = {7, {0xa1, 0xb2, 0xc3}, 77};
    adapter::invoke_used_action(Fixture::original_used, query_context,
        server_base, adapter::Mode::free_consumables);
    CHECK(fixture.used_calls == 1);
    CHECK(fixture.remove_calls == 1);
    CHECK(fixture.removal_disposition == adapter::RemovalDisposition::forwarded);
    CHECK(fixture.adapter_result == fixture.native_result);
    CHECK(fixture.forwarded_args_match());
    CHECK(fixture.output.status == 7 && fixture.output.remainder == 77);
    CHECK(fixture.output.padding[0] == 0xa1 &&
        fixture.output.padding[1] == 0xb2 && fixture.output.padding[2] == 0xc3);
}

void test_denied_and_unwritable_paths_forward_exactly_once() {
    Fixture mismatch;
    mismatch.query.owner = 4;
    expect_original_once(mismatch);

    Fixture unknown;
    unknown.query_readable = false;
    expect_original_once(unknown);

    Fixture no_lease;
    no_lease.lease_current = false;
    expect_original_once(no_lease);

    Fixture writer_failure;
    writer_failure.writer_allowed = false;
    expect_original_once(writer_failure);

    Fixture writer_mutation_failure;
    writer_mutation_failure.writer_fault_after_status = true;
    Fixture::active = &writer_mutation_failure;
    adapter::invoke_used_action(Fixture::original_used, query_context,
        server_base, adapter::Mode::free_consumables);
    CHECK(writer_mutation_failure.remove_calls == 0);
    CHECK(writer_mutation_failure.removal_disposition ==
        adapter::RemovalDisposition::indeterminate);
    CHECK(writer_mutation_failure.adapter_result == nullptr);
    CHECK(writer_mutation_failure.output.status == 0);
    CHECK(writer_mutation_failure.output.remainder == 77);

    Fixture deactivated;
    deactivated.mode_current = false;
    expect_original_once(deactivated);

    Fixture wrong_site;
    wrong_site.return_address = used_pc + 1;
    expect_original_once(wrong_site);

    Fixture missing_result;
    missing_result.args.result = nullptr;
    expect_original_once(missing_result);
    CHECK(missing_result.forwarded_args_match());

    Fixture wrong_action;
    Fixture::active = &wrong_action;
    wrong_action.return_address = fired_pc;
    adapter::invoke_used_action(Fixture::original_used, query_context,
        server_base, adapter::Mode::free_consumables);
    CHECK(wrong_action.remove_calls == 1);
    CHECK(wrong_action.removal_disposition == adapter::RemovalDisposition::forwarded);
    CHECK(wrong_action.forwarded_args_match());
    CHECK(wrong_action.output.status == 7 && wrong_action.output.remainder == 77);
}

void test_probe_and_inactive_modes_never_bypass_costs() {
    Fixture probe;
    Fixture::active = &probe;
    probe.mode = adapter::Mode::read_only_probe;
    probe.return_address = fired_pc;
    const auto data = reinterpret_cast<void*>(0x8877);
    CHECK(adapter::invoke_fired_action(Fixture::original_fired, query_context,
        data, server_base, probe.mode) == 0x5a);
    CHECK(probe.fired_scope_seen && probe.remove_calls == 1);
    CHECK(probe.removal_disposition == adapter::RemovalDisposition::forwarded);
    CHECK(probe.forwarded_args_match());
    CHECK(probe.output.status == 7 && probe.output.remainder == 77);

    Fixture probe_used;
    Fixture::active = &probe_used;
    probe_used.mode = adapter::Mode::read_only_probe;
    probe_used.exercise_removal = false;
    adapter::invoke_used_action(Fixture::original_used, query_context,
        server_base, probe_used.mode);
    CHECK(probe_used.used_calls == 1 && !probe_used.used_scope_seen);

    Fixture inactive;
    Fixture::active = &inactive;
    inactive.mode = adapter::Mode::inactive;
    inactive.exercise_removal = false;
    adapter::invoke_used_action(Fixture::original_used, query_context,
        server_base, inactive.mode);
    CHECK(inactive.used_calls == 1 && !inactive.used_scope_seen);
    CHECK(!cost::current_action_frame());
}

void test_atomic_result_writer() {
    alignas(8) cost::NativeRemovalResult result{7, {0xa1, 0xb2, 0xc3}, 77};
    const auto committed = xhl::native_consumable_result_write::
        replace_with_free_result(&result);
    CHECK(committed == xhl::native_consumable_result_write::Outcome::committed);
    CHECK(result.status == 0 && result.remainder == 0);
    CHECK(result.padding[0] == 0xa1 && result.padding[1] == 0xb2 &&
        result.padding[2] == 0xc3);

    alignas(8) cost::NativeRemovalResult adapter_result{
        8, {0xb1, 0xb2, 0xb3}, 91};
    Fixture atomic_success;
    atomic_success.args.result = &adapter_result;
    const auto bypass = invoke_atomic_scoped_removal(atomic_success);
    CHECK(bypass.disposition == adapter::RemovalDisposition::bypassed);
    CHECK(bypass.native_return == &adapter_result);
    CHECK(atomic_success.remove_calls == 0);
    CHECK(adapter_result.status == 0 && adapter_result.remainder == 0);
    CHECK(adapter_result.padding[0] == 0xb1 && adapter_result.padding[1] == 0xb2 &&
        adapter_result.padding[2] == 0xb3);

    alignas(8) std::array<std::uint8_t, 16> unaligned_storage{};
    unaligned_storage.fill(0x5c);
    std::array<std::uint8_t, 16> before{};
    std::memcpy(before.data(), unaligned_storage.data(), before.size());
    auto* const unaligned = reinterpret_cast<cost::NativeRemovalResult*>(
        unaligned_storage.data() + 1);
    const auto rejected = xhl::native_consumable_result_write::
        replace_with_free_result(unaligned);
    CHECK(rejected == xhl::native_consumable_result_write::Outcome::unchanged);
    CHECK(std::memcmp(before.data(), unaligned_storage.data(), before.size()) == 0);
    Fixture unaligned_forward;
    unaligned_forward.args.result = unaligned;
    const auto unaligned_outcome = invoke_atomic_scoped_removal(unaligned_forward);
    CHECK(unaligned_outcome.disposition == adapter::RemovalDisposition::forwarded);
    CHECK(unaligned_outcome.native_return == unaligned_forward.native_result);
    CHECK(unaligned_forward.remove_calls == 1 && unaligned_forward.forwarded_args_match());
    CHECK(std::memcmp(before.data(), unaligned_storage.data(), before.size()) == 0);
    CHECK(xhl::native_consumable_result_write::replace_with_free_result(nullptr) ==
        xhl::native_consumable_result_write::Outcome::unchanged);

    auto* const page = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    CHECK(page != nullptr);
    if (!page) return;
    auto* const protected_result = reinterpret_cast<cost::NativeRemovalResult*>(page);
    const cost::NativeRemovalResult initial{9, {0xd1, 0xd2, 0xd3}, 123};
    std::memcpy(protected_result, &initial, sizeof(initial));
    DWORD prior_protection = 0;
    const BOOL protected_ok = VirtualProtect(page, 0x1000, PAGE_READONLY,
        &prior_protection);
    CHECK(protected_ok != FALSE);
    if (protected_ok) {
        const auto read_only = xhl::native_consumable_result_write::
            replace_with_free_result(protected_result);
        CHECK(read_only ==
            xhl::native_consumable_result_write::Outcome::unchanged);
        Fixture read_only_forward;
        read_only_forward.args.result = protected_result;
        const auto read_only_outcome = invoke_atomic_scoped_removal(read_only_forward);
        CHECK(read_only_outcome.disposition == adapter::RemovalDisposition::forwarded);
        CHECK(read_only_outcome.native_return == read_only_forward.native_result);
        CHECK(read_only_forward.remove_calls == 1 && read_only_forward.forwarded_args_match());
        CHECK(std::memcmp(protected_result, &initial, sizeof(initial)) == 0);
        DWORD ignored_protection = 0;
        CHECK(VirtualProtect(page, 0x1000, prior_protection,
            &ignored_protection) != FALSE);
        CHECK(std::memcmp(protected_result, &initial, sizeof(initial)) == 0);
    }
    CHECK(VirtualFree(page, 0, MEM_RELEASE) != FALSE);
}
} // namespace

int main() {
    test_used_action_scopes_and_authorized_adapter();
    test_fired_helper_scopes_and_forwards_abi();
    test_denied_and_unwritable_paths_forward_exactly_once();
    test_probe_and_inactive_modes_never_bypass_costs();
    test_atomic_result_writer();
    if (failures) {
        std::fprintf(stderr, "%d native consumable runtime checks failed\n", failures);
        return 1;
    }
    std::puts("PASS used/fired runtime scopes, exact forwarding, write validation, and inactive-mode checks");
    return 0;
}
