#include "time_control.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>
#include <vector>

using namespace xhl;
using namespace xhl::native_time;

namespace {
int failures = 0;
constexpr WorldScope test_world_scope = 0xA001;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << message << '\n';
    }
}
bool gate(void* context, const flight::Identity& identity) noexcept {
    const auto* enabled = static_cast<const bool*>(context);
    return enabled != nullptr && *enabled && identity.authentication != 0;
}
flight::Identity make_identity(std::uintptr_t world, std::uint64_t authentication,
                               std::uint64_t lifecycle, std::uint64_t steam = 0x0110000100000001ULL) {
    flight::Identity identity{};
    identity.backend = 0x1000;
    identity.session = 0x2000;
    identity.world = world;
    identity.player = 64;
    identity.machine = 1;
    identity.peer = 1;
    identity.steam = steam;
    identity.authentication = authentication;
    identity.lifecycle = lifecycle;
    return identity;
}
CallbackSnapshot snapshot_for(const flight::Identity& identity, std::uint32_t owner = 42,
                              WorldScope scope = test_world_scope,
                              std::uint64_t callback_epoch = 1) {
    CallbackSnapshot snapshot{};
    snapshot.world = identity.world;
    snapshot.world_scope = scope;
    snapshot.callback_epoch = callback_epoch;
    snapshot.world_is_current = true;
    snapshot.owner_identity = identity;
    snapshot.owner = owner;
    snapshot.owner_resolution_current = true;
    snapshot.clock.now = 1000;
    snapshot.clock.sync_base = 80 * cycle_units / 24;
    snapshot.clock.sync_anchor = 1000;
    snapshot.clock.scale = normal_scale;
    snapshot.clock.version = 4;
    snapshot.clock.schedule.dawn = 6 * cycle_units / 24;
    snapshot.clock.schedule.dusk = 18 * cycle_units / 24;
    snapshot.clock.schedule.day_length = 48 * cycle_units / 24;
    snapshot.clock.schedule.night_length = 24 * cycle_units / 24;
    return snapshot;
}
Command make_hour_command(const flight::Identity& identity, std::uint32_t owner,
                          std::uint8_t hour, std::uint64_t sequence = 1) {
    return Command{identity, owner, test_world_scope, WorldTimeAction::set_hour,
                   WorldTimeMode::unspecified, hour, sequence};
}
Command make_mode_command(const flight::Identity& identity, std::uint32_t owner,
                          WorldTimeMode mode, std::uint64_t sequence = 1) {
    return Command{identity, owner, test_world_scope,
                   WorldTimeAction::set_mode, mode, 0, sequence};
}
}

int main() {
    const auto id = make_identity(0x5000, 1, 1);
    bool allowed = true;
    const CapabilityProvider allow{&allowed, gate};

    const auto midnight = normalize_hour(24.0);
    const auto fractional = normalize_hour(5.25);
    check(midnight && *midnight == 0.0, "hour 24 normalizes to midnight");
    check(fractional && *fractional == 5.25, "fractional hour is preserved");
    check(!normalize_hour(-0.1), "negative hour is rejected");
    check(!normalize_hour(24.01), "hour above the supported range is rejected");
    check(!normalize_hour(std::numeric_limits<double>::infinity()), "infinite hour is rejected");
    check(!normalize_hour(std::numeric_limits<double>::quiet_NaN()), "NaN hour is rejected");

    CommandQueue queue;
    check(queue.enqueue_hour(id, 42, test_world_scope, 24, {}) == SubmitStatus::denied,
          "missing time capability denies by default");
    check(queue.size() == 0, "default-denied command is not queued");
    check(queue.enqueue_hour(id, 42, test_world_scope, 24, allow) == SubmitStatus::queued,
          "separate time capability admits a valid command");
    const auto queued = queue.peek_for_world(id.world, test_world_scope);
    check(queued && queued->action == WorldTimeAction::set_hour && queued->hour == 0,
          "queue stores the explicit action and normalized hour");
    check(queue.acknowledge(queued ? queued->sequence : 0), "command can be acknowledged");
    check(queue.size() == 0, "acknowledgement removes the queued command");
    check(queue.enqueue_hour(id, 42, test_world_scope, 25, allow) == SubmitStatus::invalid_command,
          "out-of-range hour is rejected before queueing");
    check(queue.enqueue_mode(id, 42, test_world_scope,
                             WorldTimeMode::unspecified, allow) ==
              SubmitStatus::invalid_command,
          "unspecified mode is rejected before queueing");
    check(queue.enqueue_mode(id, 42, 0, WorldTimeMode::pause, allow) ==
              SubmitStatus::invalid_world,
          "missing host world/session context token is rejected");
    auto missing_lifecycle = id;
    missing_lifecycle.lifecycle = 0;
    check(queue.enqueue_mode(missing_lifecycle, 42, test_world_scope,
                             WorldTimeMode::pause, allow) ==
              SubmitStatus::invalid_identity,
          "identity without a lifecycle serial is rejected");
    check(queue.enqueue_hour(id, 42, test_world_scope, 9, allow) == SubmitStatus::queued,
          "lifecycle cleanup test command is queued");
    check(queue.retire_world_scope(test_world_scope) == 1 && queue.size() == 0,
          "world retirement drains commands tagged with that host context scope");

    const auto advanced_1000 = advance_base(10000, 1000, normal_scale, 2000);
    const auto advanced_60000 = advance_base(10000, 1000, asleep_scale, 2000);
    check(advanced_1000 && *advanced_1000 == 10999,
          "native float conversion and truncation advance at normal scale");
    check(advanced_60000 && *advanced_60000 == 69999,
          "native float conversion and truncation apply the 60x asleep scale");
    const auto exact_normal = advance_base(10000, 0, normal_scale, 1'000'000'000);
    const auto exact_fast = advance_base(10000, 0, asleep_scale, 1'000'000'000);
    check(exact_normal && *exact_normal == 1'000'010'000,
          "one exact second follows the native normal-scale conversion");
    check(exact_fast && *exact_fast == 60'000'010'000,
          "one exact second follows the native full-asleep conversion");
    check(!advance_base(std::numeric_limits<std::int64_t>::max(), 0,
                        normal_scale, 1000),
          "syncBase addition overflow fails closed");
    check(!advance_base(0, std::numeric_limits<std::int64_t>::min(),
                        normal_scale, std::numeric_limits<std::int64_t>::max()),
          "elapsed-tick subtraction overflow fails closed");

    const auto snapshot = snapshot_for(id);
    Command unspecified{};
    unspecified.identity = id;
    unspecified.owner = 42;
    unspecified.world_scope = test_world_scope;
    unspecified.sequence = 2;
    const auto rejected_unspecified = plan_callback(snapshot, unspecified, {}, allow);
    check(rejected_unspecified.status == DecisionStatus::invalid_command &&
          rejected_unspecified.acknowledge_command && !rejected_unspecified.has_plan,
          "unspecified action cannot produce a native write plan");
    const auto set_noon = plan_callback(snapshot,
        make_hour_command(id, 42, 12), {}, allow);
    check(set_noon.status == DecisionStatus::plan_ready && set_noon.has_plan,
          "hour request produces a callback-local write plan");
    check(set_noon.plan.kind == PlanKind::set_hour &&
          set_noon.plan.sync_scale == normal_scale &&
          set_noon.plan.sync_anchor == snapshot.clock.now,
          "hour plan preserves scale and advances anchor");
    check(set_noon.plan.sync_base == 9 * cycle_units / 2,
          "day/night mapping advances to the next noon phase");
    check(set_noon.plan.hour_materialized_base ==
              *snapshot.clock.base_at_now(),
          "hour plan carries the old-rate materialization value for native write order");
    check(set_noon.plan.sync_version == snapshot.clock.version + 1,
          "hour plan increments the native sync version");

    auto negative_base = snapshot;
    negative_base.clock.sync_base = -1;
    const auto set_midnight = plan_callback(negative_base,
        make_hour_command(id, 42, 0), {}, allow);
    check(set_midnight.has_plan && set_midnight.plan.sync_base == 0,
          "negative base uses positive modulo without signed overflow");

    auto bad_schedule = snapshot;
    bad_schedule.clock.schedule.day_length = std::numeric_limits<std::int64_t>::max();
    bad_schedule.clock.schedule.night_length = 1;
    const auto schedule_overflow = plan_callback(bad_schedule,
        make_hour_command(id, 42, 12), {}, allow);
    check(schedule_overflow.status == DecisionStatus::invalid_clock &&
          !schedule_overflow.has_plan && !schedule_overflow.acknowledge_command,
          "schedule length overflow fails closed without consuming the command");

    auto overflowing_base = snapshot;
    overflowing_base.clock.sync_base = std::numeric_limits<std::int64_t>::max() - 1;
    const auto base_overflow = plan_callback(overflowing_base,
        make_hour_command(id, 42, 12), {}, allow);
    check(base_overflow.status == DecisionStatus::invalid_clock && !base_overflow.has_plan,
          "time jump that overflows syncBase is rejected");

    const auto paused = plan_callback(snapshot,
        make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    check(paused.has_plan && paused.plan.sync_scale == paused_scale,
          "pause uses zero multiplier");
    check(paused.next_override.active &&
          paused.next_override.original_scale == normal_scale &&
          paused.next_override.installed_scale == paused_scale &&
          paused.next_override.installed_version == paused.plan.sync_version,
          "pause records baseline scale and installed version");
    check(paused.plan.sync_base == *snapshot.clock.base_at_now(),
          "scale change preserves the clock position at callback time");

    auto paused_view = snapshot;
    paused_view.clock.scale = paused_scale;
    paused_view.clock.version = paused.plan.sync_version;
    paused_view.clock.sync_base = paused.plan.sync_base;
    paused_view.clock.sync_anchor = paused.plan.sync_anchor;
    paused_view.clock.scale_writer_epoch =
        paused.next_override.installed_scale_writer_epoch;
    paused_view.callback_epoch = snapshot.callback_epoch + 1;
    const auto next_identity = make_identity(id.world, 7, 7, 0x0110000100000007ULL);
    auto next_owner_view = snapshot_for(next_identity, 47);
    next_owner_view.clock.scale = paused_scale;
    next_owner_view.clock.version = paused.plan.sync_version;
    next_owner_view.clock.sync_base = paused.plan.sync_base;
    next_owner_view.clock.sync_anchor = paused.plan.sync_anchor;
    next_owner_view.clock.scale_writer_epoch =
        paused.next_override.installed_scale_writer_epoch;
    next_owner_view.callback_epoch = snapshot.callback_epoch + 1;
    const auto hour_while_paused = plan_callback(next_owner_view,
        make_hour_command(next_identity, 47, 12),
        paused.next_override, allow);
    check(hour_while_paused.has_plan &&
          hour_while_paused.plan.kind == PlanKind::set_hour &&
          hour_while_paused.plan.sync_scale == paused_scale &&
          hour_while_paused.next_override.owner_identity == id &&
          hour_while_paused.next_override.owner == 42 &&
          hour_while_paused.next_override.installed_version ==
              hour_while_paused.plan.sync_version &&
          hour_while_paused.next_override.installed_base ==
              hour_while_paused.plan.sync_base &&
          hour_while_paused.next_override.installed_anchor ==
              hour_while_paused.plan.sync_anchor,
          "one-shot hour change preserves the paused rate and its original owner");
    const auto held = plan_callback(paused_view, {}, paused.next_override, allow);
    check(held.status == DecisionStatus::held && !held.has_plan,
          "authorized owner retains a matching temporary pause");

    auto paused_tick = paused_view;
    paused_tick.clock.now = 2000;
    paused_tick.clock.sync_anchor = 2000;
    paused_tick.clock.version = paused.plan.sync_version + 1;
    paused_tick.clock.sync_base = paused.plan.sync_base;
    paused_tick.callback_epoch = snapshot.callback_epoch + 2;
    const auto held_after_pause_tick = plan_callback(
        paused_tick, {}, paused.next_override, allow);
    check(held_after_pause_tick.status == DecisionStatus::held &&
          held_after_pause_tick.next_override.installed_version ==
              paused_tick.clock.version &&
          held_after_pause_tick.next_override.installed_anchor == 2000,
          "one native paused-tick version increment preserves the owned override");
    auto revoke_after_pause_tick = paused_tick;
    revoke_after_pause_tick.owner_identity.reset();
    revoke_after_pause_tick.owner = 0;
    revoke_after_pause_tick.owner_resolution_current = false;
    const auto restore_after_pause_tick = plan_callback(
        revoke_after_pause_tick, {}, held_after_pause_tick.next_override, allow);
    check(restore_after_pause_tick.status == DecisionStatus::restore_ready &&
          restore_after_pause_tick.plan.sync_scale == normal_scale &&
          restore_after_pause_tick.plan.sync_base == paused_tick.clock.sync_base,
          "revocation after a paused tick restores only the rate and preserves current time");

    bool revoked = false;
    const CapabilityProvider denied{&revoked, gate};
    const auto restored = plan_callback(paused_view, {}, paused.next_override, denied);
    check(restored.status == DecisionStatus::restore_ready && restored.has_plan &&
          restored.plan.sync_scale == normal_scale && !restored.next_override.active,
          "revocation restores the captured scale on the next valid callback");

    auto disconnected = paused_view;
    disconnected.owner_identity.reset();
    disconnected.owner = 0;
    disconnected.owner_resolution_current = false;
    const auto restored_after_disconnect = plan_callback(
        disconnected, {}, paused.next_override, denied);
    check(restored_after_disconnect.status == DecisionStatus::restore_ready &&
          restored_after_disconnect.plan.sync_scale == normal_scale,
          "disconnect cleanup restores only through a current world callback");

    auto paused_hour_view = next_owner_view;
    paused_hour_view.clock.sync_base = hour_while_paused.plan.sync_base;
    paused_hour_view.clock.sync_anchor = hour_while_paused.plan.sync_anchor;
    paused_hour_view.clock.scale = hour_while_paused.plan.sync_scale;
    paused_hour_view.clock.version = hour_while_paused.plan.sync_version;
    const auto restore_after_hour_jump = plan_callback(
        paused_hour_view, {}, hour_while_paused.next_override, denied);
    check(restore_after_hour_jump.status == DecisionStatus::restore_ready &&
          restore_after_hour_jump.plan.sync_scale == normal_scale &&
          restore_after_hour_jump.plan.sync_base == hour_while_paused.plan.sync_base,
          "hour jump refreshes only the installed version and later restore preserves its new hour");

    const auto fast = plan_callback(snapshot,
        make_mode_command(id, 42, WorldTimeMode::fast), {}, allow);
    check(fast.has_plan && fast.plan.sync_scale == asleep_scale,
          "fast mode uses the native full asleep-speed scale");
    auto fast_view = snapshot;
    fast_view.clock.scale = asleep_scale;
    fast_view.clock.version = fast.plan.sync_version;
    fast_view.clock.sync_base = fast.plan.sync_base;
    fast_view.clock.sync_anchor = fast.plan.sync_anchor;
    fast_view.clock.scale_writer_epoch =
        fast.next_override.installed_scale_writer_epoch;
    fast_view.callback_epoch = snapshot.callback_epoch + 1;
    const auto normal = plan_callback(fast_view,
        make_mode_command(id, 42, WorldTimeMode::normal), fast.next_override, allow);
    check(normal.has_plan && normal.plan.sync_scale == normal_scale &&
          normal.plan.sync_base == *fast_view.clock.base_at_now() &&
          !normal.next_override.active,
          "normal restores the captured scale and clears temporary ownership");

    auto fast_tick = snapshot;
    fast_tick.clock.scale = asleep_scale;
    fast_tick.clock.scale_writer_epoch = fast.next_override.installed_scale_writer_epoch;
    fast_tick.clock.version = fast.plan.sync_version + 1;
    fast_tick.clock.now = 1'000'001'000;
    fast_tick.clock.sync_anchor = fast_tick.clock.now;
    fast_tick.clock.sync_base = *advance_base(fast.plan.sync_base,
        fast.plan.sync_anchor, asleep_scale, fast_tick.clock.sync_anchor);
    fast_tick.callback_epoch = snapshot.callback_epoch + 1;
    const auto held_after_fast_tick = plan_callback(
        fast_tick, {}, fast.next_override, allow);
    check(held_after_fast_tick.status == DecisionStatus::held &&
          held_after_fast_tick.next_override.installed_version ==
              fast_tick.clock.version &&
          held_after_fast_tick.next_override.installed_base ==
              fast_tick.clock.sync_base &&
          held_after_fast_tick.next_override.installed_anchor ==
              fast_tick.clock.sync_anchor,
          "one native fast-tick version increment is accepted only with exact scale math");
    auto revoked_after_fast_tick = fast_tick;
    revoked_after_fast_tick.owner_resolution_current = false;
    revoked_after_fast_tick.owner_identity.reset();
    revoked_after_fast_tick.owner = 0;
    const auto restore_after_fast_tick = plan_callback(
        revoked_after_fast_tick, {}, held_after_fast_tick.next_override, denied);
    check(restore_after_fast_tick.status == DecisionStatus::restore_ready &&
          restore_after_fast_tick.plan.sync_scale == normal_scale &&
          restore_after_fast_tick.plan.sync_base == fast_tick.clock.sync_base,
          "revocation after native fast progression restores rate without changing the hour");

    auto partial_rate = snapshot;
    partial_rate.clock.scale = 12.5f;
    const auto partial_pause = plan_callback(
        partial_rate, make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    auto partial_paused_view = partial_rate;
    partial_paused_view.clock.scale = paused_scale;
    partial_paused_view.clock.version = partial_pause.plan.sync_version;
    partial_paused_view.clock.sync_base = partial_pause.plan.sync_base;
    partial_paused_view.clock.sync_anchor = partial_pause.plan.sync_anchor;
    partial_paused_view.clock.scale_writer_epoch =
        partial_pause.next_override.installed_scale_writer_epoch;
    partial_paused_view.callback_epoch = partial_rate.callback_epoch + 1;
    const auto partial_normal = plan_callback(
        partial_paused_view, make_mode_command(id, 42, WorldTimeMode::normal),
        partial_pause.next_override, allow);
    check(partial_normal.has_plan && partial_normal.plan.sync_scale == 12.5f &&
          partial_normal.plan.sync_base == *partial_paused_view.clock.base_at_now(),
          "normal mode restores the actual captured native rate and preserves the hour");

    const auto replacement = make_identity(id.world, 2, 2, 0x0110000100000002ULL);
    auto replacement_view = snapshot_for(replacement, 43);
    replacement_view.clock.scale = paused_scale;
    replacement_view.clock.version = paused.plan.sync_version;
    replacement_view.clock.sync_base = paused.plan.sync_base;
    replacement_view.clock.sync_anchor = paused.plan.sync_anchor;
    replacement_view.clock.scale_writer_epoch =
        paused.next_override.installed_scale_writer_epoch;
    replacement_view.callback_epoch = snapshot.callback_epoch + 1;
    const auto replaced = plan_callback(replacement_view,
        make_mode_command(replacement, 43, WorldTimeMode::fast), paused.next_override, allow);
    check(replaced.has_plan && replaced.plan.sync_scale == asleep_scale &&
          replaced.next_override.owner_identity == replacement &&
          replaced.next_override.original_scale == normal_scale,
          "later authorized command replaces owner without losing original scale");

    auto version_changed = paused_view;
    ++version_changed.clock.version;
    ++version_changed.clock.sync_base;
    const auto preserved = plan_callback(version_changed, {}, paused.next_override, denied);
    check(preserved.status == DecisionStatus::expired &&
          !preserved.has_plan && !preserved.next_override.active,
          "version increment with unexpected native base expires without automatic restore");
    auto scale_writer_changed = paused_view;
    ++scale_writer_changed.clock.version;
    ++scale_writer_changed.clock.scale_writer_epoch;
    const auto preserved_writer = plan_callback(
        scale_writer_changed, {}, paused.next_override, denied);
    check(preserved_writer.status == DecisionStatus::expired &&
          !preserved_writer.has_plan,
          "native scale-writer epoch change expires even at equal scale");
    auto schedule_changed = paused_view;
    ++schedule_changed.clock.schedule.day_length;
    ++schedule_changed.clock.version;
    schedule_changed.callback_epoch += 1;
    const auto preserved_schedule = plan_callback(
        schedule_changed, {}, paused.next_override, denied);
    check(preserved_schedule.status == DecisionStatus::expired &&
          !preserved_schedule.has_plan,
          "native day/night schedule change expires without restoration");
    auto scale_changed = paused_view;
    scale_changed.clock.scale = normal_scale;
    const auto preserved_scale = plan_callback(scale_changed, {}, paused.next_override, denied);
    check(preserved_scale.status == DecisionStatus::expired &&
          !preserved_scale.has_plan,
          "native scale change expires without automatic restore");

    auto stale_identity = snapshot_for(id);
    stale_identity.owner_identity = make_identity(id.world, 9, 9);
    const auto stale = plan_callback(stale_identity,
        make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    check(stale.status == DecisionStatus::stale_command &&
          stale.acknowledge_command && !stale.has_plan,
          "full identity mismatch rejects a superseded queued request");
    auto stale_world = snapshot;
    stale_world.world_is_current = false;
    const auto unresolved = plan_callback(stale_world,
        make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    check(unresolved.status == DecisionStatus::invalid_world && !unresolved.has_plan,
          "stale world query cannot produce a plan");
    auto missing_callback_epoch = snapshot;
    missing_callback_epoch.callback_epoch = 0;
    const auto no_callback_epoch = plan_callback(
        missing_callback_epoch, make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    check(no_callback_epoch.status == DecisionStatus::invalid_world &&
          !no_callback_epoch.has_plan,
          "callback without a validated lifecycle epoch cannot mutate the clock");
    auto rotated_scope_snapshot = snapshot_for(id, 42, test_world_scope + 1,
                                             snapshot.callback_epoch + 1);
    const auto stale_scope_command = plan_callback(
        rotated_scope_snapshot, make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    check(stale_scope_command.status == DecisionStatus::stale_command &&
          stale_scope_command.acknowledge_command && !stale_scope_command.has_plan,
          "an observed host context rotation rejects an old queued command");
    const auto stale_scope_override = plan_callback(
        rotated_scope_snapshot, {}, paused.next_override, denied);
    check(stale_scope_override.status == DecisionStatus::expired &&
          !stale_scope_override.next_override.active && !stale_scope_override.has_plan,
          "an override expires on an observed context transition without restoring into it");
    auto missing_world_override = snapshot;
    missing_world_override.world_is_current = false;
    const auto lost_world_override = plan_callback(
        missing_world_override, {}, paused.next_override, denied);
    check(lost_world_override.status == DecisionStatus::expired &&
          !lost_world_override.next_override.active && !lost_world_override.has_plan,
          "unresolvable current world retires an override without a stale-pointer restore");
    auto invalid_clock_override = paused_view;
    invalid_clock_override.clock.sync_anchor = std::numeric_limits<std::int64_t>::max();
    invalid_clock_override.clock.now = std::numeric_limits<std::int64_t>::min();
    const auto lost_clock_override = plan_callback(
        invalid_clock_override, {}, paused.next_override, denied);
    check(lost_clock_override.status == DecisionStatus::expired &&
          !lost_clock_override.next_override.active && !lost_clock_override.has_plan,
          "invalid callback-local clock expires an override without restoring");
    auto identity_world_mismatch = snapshot;
    identity_world_mismatch.world = id.world + 1;
    const auto mismatch = plan_callback(identity_world_mismatch, {}, paused.next_override, denied);
    check(mismatch.status == DecisionStatus::invalid_world && !mismatch.has_plan,
          "snapshot identity and world must match exactly");

    auto wrapped = snapshot;
    wrapped.clock.version = std::numeric_limits<std::uint32_t>::max();
    const auto version_wrap = plan_callback(wrapped,
        make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    check(version_wrap.has_plan && version_wrap.plan.sync_version == 0,
          "sync version increment follows native 32-bit wrap");
    auto exhausted_writer_epoch = snapshot;
    exhausted_writer_epoch.clock.scale_writer_epoch =
        std::numeric_limits<std::uint64_t>::max();
    const auto writer_epoch_wrap = plan_callback(
        exhausted_writer_epoch, make_mode_command(id, 42, WorldTimeMode::pause), {}, allow);
    check(writer_epoch_wrap.status == DecisionStatus::invalid_clock &&
          !writer_epoch_wrap.has_plan && !writer_epoch_wrap.acknowledge_command,
          "scale-writer epoch exhaustion fails closed without consuming the command");

    CommandQueue concurrent;
    std::atomic<unsigned> accepted = 0;
    std::vector<std::thread> producers;
    for (unsigned thread = 0; thread < 8; ++thread) {
        producers.emplace_back([&concurrent, &id, allow, &accepted] {
            for (unsigned i = 0; i < 8; ++i) {
                if (concurrent.enqueue_mode(id, 42, test_world_scope,
                                            WorldTimeMode::pause, allow) ==
                    SubmitStatus::queued) {
                    accepted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& producer : producers) producer.join();
    check(accepted.load(std::memory_order_relaxed) == pending_capacity &&
          concurrent.size() == pending_capacity,
          "concurrent producers fill the bounded queue without races");
    std::uint64_t previous = 0;
    for (std::size_t i = 0; i < pending_capacity; ++i) {
        const auto item = concurrent.peek_for_world(id.world, test_world_scope);
        check(item && item->sequence > previous, "queue has unique FIFO sequence order");
        if (!item) break;
        previous = item->sequence;
        check(concurrent.acknowledge(item->sequence), "callback consumer acknowledges command");
    }
    check(concurrent.size() == 0, "callback queue drains to empty");

    if (failures != 0) {
        std::cerr << failures << " time-control checks failed\n";
        return 1;
    }
    std::cout << "all native-time policy checks passed\n";
    return 0;
}
