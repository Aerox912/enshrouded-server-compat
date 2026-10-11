#include "callback_writer.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string_view>

using namespace xhl;
using namespace xhl::native_time;

namespace {
unsigned failures = 0;
constexpr WorldScope scope = 0x701;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << message << '\n';
    }
}

bool gate(void* context, const flight::Identity& identity) noexcept {
    const auto* allowed = static_cast<const bool*>(context);
    return allowed && *allowed && identity.authentication != 0;
}

flight::Identity identity() {
    flight::Identity value{};
    value.backend = 0x1000;
    value.session = 0x2000;
    value.world = 0x5000;
    value.player = 64;
    value.machine = 1;
    value.peer = 1;
    value.steam = 0x0110000100000001ULL;
    value.authentication = 1;
    value.lifecycle = 1;
    return value;
}

CallbackSnapshot snapshot_for(const flight::Identity& owner) {
    CallbackSnapshot snapshot{};
    snapshot.world = owner.world;
    snapshot.world_scope = scope;
    snapshot.callback_epoch = 10;
    snapshot.world_is_current = true;
    snapshot.owner_identity = owner;
    snapshot.owner = 42;
    snapshot.owner_resolution_current = true;
    snapshot.clock.sync_base = 80 * cycle_units / 24;
    snapshot.clock.sync_anchor = 1000;
    snapshot.clock.now = 1000;
    snapshot.clock.scale = normal_scale;
    snapshot.clock.version = 4;
    snapshot.clock.schedule = {
        6 * cycle_units / 24, 18 * cycle_units / 24,
        48 * cycle_units / 24, 24 * cycle_units / 24
    };
    return snapshot;
}

Command hour_command(const flight::Identity& owner, std::uint8_t hour,
                     std::uint64_t sequence = 1) {
    return Command{owner, 42, scope, WorldTimeAction::set_hour,
                   WorldTimeMode::unspecified, hour, sequence};
}

Command mode_command(const flight::Identity& owner, WorldTimeMode mode,
                     std::uint64_t sequence = 1) {
    return Command{owner, 42, scope, WorldTimeAction::set_mode, mode, 0, sequence};
}

enum class HourWrite : std::uint8_t { base, anchor, version, target_base };

struct FakeCallback {
    CallbackSnapshot current{};
    std::array<HourWrite, 4> write_order{};
    std::size_t writes = 0;
    unsigned reads = 0;
    unsigned scale_calls = 0;
    unsigned hour_calls = 0;
    bool allow_read = true;
    bool allow_scale = true;
    bool allow_hour = true;
};

bool read_current(void* context, CallbackSnapshot& output) noexcept {
    auto& fake = *static_cast<FakeCallback*>(context);
    ++fake.reads;
    if (!fake.allow_read) return false;
    output = fake.current;
    return true;
}

bool call_scale_original(void* context, float scale, std::int64_t now) noexcept {
    auto& fake = *static_cast<FakeCallback*>(context);
    ++fake.scale_calls;
    if (!fake.allow_scale) return false;
    const auto base = advance_base(fake.current.clock.sync_base,
        fake.current.clock.sync_anchor, fake.current.clock.scale, now);
    if (!base || fake.current.clock.scale_writer_epoch == UINT64_MAX) return false;
    fake.current.clock.sync_base = *base;
    fake.current.clock.sync_anchor = now;
    ++fake.current.clock.version;
    fake.current.clock.scale = scale;
    ++fake.current.clock.scale_writer_epoch;
    return true;
}

bool write_hour_fields(void* context, const HourFieldSequence& writes) noexcept {
    auto& fake = *static_cast<FakeCallback*>(context);
    ++fake.hour_calls;
    if (!fake.allow_hour) return false;
    fake.write_order[fake.writes++] = HourWrite::base;
    fake.current.clock.sync_base = writes.materialized_sync_base;
    fake.write_order[fake.writes++] = HourWrite::anchor;
    fake.current.clock.sync_anchor = writes.sync_anchor;
    fake.write_order[fake.writes++] = HourWrite::version;
    fake.current.clock.version = writes.sync_version;
    fake.write_order[fake.writes++] = HourWrite::target_base;
    fake.current.clock.sync_base = writes.target_sync_base;
    return true;
}

CallbackWriters writers_for(FakeCallback& fake) {
    return {&fake, read_current, call_scale_original, write_hour_fields};
}
} // namespace

int main() {
    bool allowed = true;
    const CapabilityProvider capability{&allowed, gate};
    const auto owner = identity();

    auto hour_snapshot = snapshot_for(owner);
    hour_snapshot.clock.scale = 12.5f;
    const auto hour = hour_command(owner, 12);
    const auto hour_decision = plan_callback(hour_snapshot, hour, {}, capability);
    FakeCallback hour_fake{};
    hour_fake.current = hour_snapshot;
    const auto hour_result = apply_plan_once(hour_snapshot, hour, {},
        hour_decision, capability, writers_for(hour_fake));
    check(hour_result.status == ApplyStatus::applied &&
          hour_result.writer_invoked && hour_result.acknowledge_command,
          "one-shot hour plan applies synchronously and acknowledges after one write");
    check(hour_fake.hour_calls == 1 && hour_fake.scale_calls == 0 &&
          hour_fake.reads == 2,
          "hour change uses one hour-field writer and never calls the scale setter");
    check(hour_fake.writes == 4 &&
          hour_fake.write_order == std::array<HourWrite, 4>{
              HourWrite::base, HourWrite::anchor, HourWrite::version,
              HourWrite::target_base},
          "hour writes follow materialize-base, anchor, version, target-base order");
    check(hour_fake.current.clock.sync_base == hour_decision.plan.sync_base &&
          hour_fake.current.clock.scale == hour_snapshot.clock.scale &&
          hour_fake.current.clock.version == hour_decision.plan.sync_version,
          "hour plan preserves rate and leaves timeOfDay to the next native tick");

    const auto hour_replay = apply_plan_once(hour_snapshot, hour, {},
        hour_decision, capability, writers_for(hour_fake));
    check(hour_replay.status == ApplyStatus::expired &&
          !hour_replay.writer_invoked && hour_fake.hour_calls == 1,
          "a consumed one-shot hour plan cannot be replayed against changed clock state");
    auto pause_snapshot = snapshot_for(owner);
    pause_snapshot.clock.scale = 12.5f;
    const auto pause = mode_command(owner, WorldTimeMode::pause);
    const auto pause_decision = plan_callback(pause_snapshot, pause, {}, capability);
    FakeCallback pause_fake{};
    pause_fake.current = pause_snapshot;
    const auto pause_result = apply_plan_once(pause_snapshot, pause, {},
        pause_decision, capability, writers_for(pause_fake));
    check(pause_result.status == ApplyStatus::applied &&
          pause_result.next_override.active && pause_fake.scale_calls == 1 &&
          pause_fake.hour_calls == 0,
          "pause invokes the native scale-writer original exactly once");
    check(pause_fake.current.clock.scale == paused_scale &&
          pause_fake.current.clock.sync_base == pause_decision.plan.sync_base &&
          pause_fake.current.clock.version == pause_decision.plan.sync_version,
          "pause readback matches the native setter plan");

    auto paused_tick = pause_fake.current;
    paused_tick.callback_epoch += 1;
    paused_tick.clock.now = 2000;
    paused_tick.clock.sync_anchor = 2000;
    ++paused_tick.clock.version;
    paused_tick.clock.sync_base = *advance_base(
        paused_tick.clock.sync_base, pause_fake.current.clock.sync_anchor,
        paused_scale, paused_tick.clock.sync_anchor);
    paused_tick.owner_identity.reset();
    paused_tick.owner = 0;
    paused_tick.owner_resolution_current = false;
    const auto restore_decision = plan_callback(paused_tick, {},
        pause_result.next_override, CapabilityProvider{});
    check(restore_decision.status == DecisionStatus::restore_ready &&
          restore_decision.plan.sync_scale == 12.5f,
          "revoke plans a compare-checked rate restoration after exact tick progression");
    FakeCallback restore_fake{};
    restore_fake.current = paused_tick;
    const auto restore_result = apply_plan_once(paused_tick, {},
        pause_result.next_override, restore_decision, CapabilityProvider{},
        writers_for(restore_fake));
    check(restore_result.status == ApplyStatus::restored &&
          !restore_result.next_override.active && restore_fake.scale_calls == 1,
          "normal restoration calls the scale original once and retires the override");
    check(restore_fake.current.clock.sync_base == restore_decision.plan.sync_base &&
          restore_fake.current.clock.scale == 12.5f,
          "restore preserves the current hour while restoring the captured non-normal rate");

    auto externally_changed = paused_tick;
    ++externally_changed.clock.scale_writer_epoch;
    FakeCallback changed_restore_fake{};
    changed_restore_fake.current = externally_changed;
    const auto changed_restore = apply_plan_once(paused_tick, {},
        pause_result.next_override, restore_decision, CapabilityProvider{},
        writers_for(changed_restore_fake));
    check(changed_restore.status == ApplyStatus::expired &&
          !changed_restore.writer_invoked && changed_restore_fake.scale_calls == 0 &&
          !changed_restore.next_override.active,
          "a competing native scale writer expires restoration without overwriting it");

    FakeCallback stale_fake{};
    stale_fake.current = pause_snapshot;
    auto stale_command_plan = plan_callback(pause_snapshot, pause, {}, capability);
    ++stale_fake.current.clock.sync_base;
    const auto stale_result = apply_plan_once(pause_snapshot, pause, {},
        stale_command_plan, capability, writers_for(stale_fake));
    check(stale_result.status == ApplyStatus::expired &&
          !stale_result.writer_invoked && stale_fake.scale_calls == 0 &&
          !stale_result.next_override.active,
          "changed prewrite clock fields expire without writing stale state");

    auto forged_fake = FakeCallback{};
    forged_fake.current = pause_snapshot;
    auto forged_decision = plan_callback(pause_snapshot, pause, {}, capability);
    forged_decision.plan.sync_scale = 7.0f;
    const auto forged_result = apply_plan_once(pause_snapshot, pause, {},
        forged_decision, capability, writers_for(forged_fake));
    check(forged_result.status == ApplyStatus::expired &&
          !forged_result.writer_invoked && forged_fake.scale_calls == 0,
          "the writer contract rejects a well-shaped but reducer-inconsistent plan");

    auto rotated_fake = FakeCallback{};
    rotated_fake.current = pause_snapshot;
    ++rotated_fake.current.world_scope;
    const auto rotated_result = apply_plan_once(pause_snapshot, pause, {},
        pause_decision, capability, writers_for(rotated_fake));
    check(rotated_result.status == ApplyStatus::expired &&
          !rotated_result.writer_invoked && rotated_fake.scale_calls == 0,
          "a freshly observed host scope transition blocks stale clock writes");
    FakeCallback denied_fake{};
    denied_fake.current = pause_snapshot;
    auto denied_decision = plan_callback(pause_snapshot, pause, {}, capability);
    allowed = false;
    const auto denied_result = apply_plan_once(pause_snapshot, pause, {},
        denied_decision, capability, writers_for(denied_fake));
    check(denied_result.status == ApplyStatus::denied &&
          denied_result.acknowledge_command && !denied_result.writer_invoked &&
          denied_fake.scale_calls == 0,
          "separate time capability is rechecked immediately before writing");
    allowed = true;

    FakeCallback failed_fake{};
    failed_fake.current = hour_snapshot;
    failed_fake.allow_hour = false;
    const auto failed_hour = apply_plan_once(hour_snapshot, hour, {},
        hour_decision, capability, writers_for(failed_fake));
    check(failed_hour.status == ApplyStatus::writer_failed &&
          failed_hour.writer_invoked && failed_fake.hour_calls == 1 &&
          failed_hour.acknowledge_command && !failed_hour.next_override.active,
          "failed hour write is attempted once, acknowledged, and expires state");

    auto stale_identity_fake = FakeCallback{};
    stale_identity_fake.current = hour_snapshot;
    auto stale_identity_decision = plan_callback(hour_snapshot, hour, {}, capability);
    auto replacement = owner;
    replacement.authentication += 1;
    replacement.lifecycle += 1;
    stale_identity_fake.current.owner_identity = replacement;
    const auto stale_identity_result = apply_plan_once(hour_snapshot, hour, {},
        stale_identity_decision, capability, writers_for(stale_identity_fake));
    check(stale_identity_result.status == ApplyStatus::stale_command &&
          stale_identity_result.acknowledge_command &&
          !stale_identity_result.writer_invoked && stale_identity_fake.hour_calls == 0,
          "fresh identity reread rejects a superseded queued command");

    if (failures) {
        std::cerr << failures << " callback-writer test(s) failed\n";
        return 1;
    }
    std::cout << "PASS callback writer contract tests\n";
    return 0;
}
