#include "time_control.hpp"

#include <bit>
#include <cmath>
#include <limits>

namespace xhl::native_time {
namespace {

bool valid_scale(float value) noexcept {
    return std::isfinite(value) && value >= paused_scale && value <= asleep_scale;
}
bool same_scale(float left, float right) noexcept {
    return std::bit_cast<std::uint32_t>(left) == std::bit_cast<std::uint32_t>(right);
}
bool valid_override(const OverrideState& state) noexcept {
    return state.active && state.world_scope != 0 && state.owner != 0 &&
           valid_identity(state.owner_identity) &&
           state.owner_identity.world != 0 &&
           valid_scale(state.original_scale) && valid_scale(state.installed_scale);
}
bool same_schedule(const Schedule& left, const Schedule& right) noexcept {
    return left.dawn == right.dawn && left.dusk == right.dusk &&
           left.day_length == right.day_length &&
           left.night_length == right.night_length;
}
bool refresh_owned_override(const CallbackSnapshot& snapshot,
                            const OverrideState& state,
                            OverrideState& refreshed) noexcept {
    if (!valid_override(state) || snapshot.world_scope != state.world_scope ||
        snapshot.world != state.owner_identity.world ||
        snapshot.callback_epoch < state.installed_callback_epoch ||
        snapshot.clock.scale_writer_epoch != state.installed_scale_writer_epoch ||
        !same_scale(snapshot.clock.scale, state.installed_scale) ||
        !same_schedule(snapshot.clock.schedule, state.installed_schedule)) {
        return false;
    }

    refreshed = state;
    if (snapshot.clock.version == state.installed_version &&
        snapshot.clock.sync_base == state.installed_base &&
        snapshot.clock.sync_anchor == state.installed_anchor) {
        refreshed.installed_callback_epoch = snapshot.callback_epoch;
        return true;
    }

    if (snapshot.callback_epoch <= state.installed_callback_epoch ||
        snapshot.clock.version != state.installed_version + 1u ||
        snapshot.clock.sync_anchor < state.installed_anchor ||
        snapshot.clock.sync_anchor > snapshot.clock.now) {
        return false;
    }
    const auto expected_base = advance_base(state.installed_base,
                                            state.installed_anchor,
                                            state.installed_scale,
                                            snapshot.clock.sync_anchor);
    if (!expected_base || snapshot.clock.sync_base != *expected_base) return false;

    // Accept only the pinned native tick's one-version update. Requiring a
    // changed callback epoch, exact base math, unchanged schedule, scale and
    // scale-writer epoch prevents treating a different setter as tick drift.
    refreshed.installed_version = snapshot.clock.version;
    refreshed.installed_base = snapshot.clock.sync_base;
    refreshed.installed_anchor = snapshot.clock.sync_anchor;
    refreshed.installed_callback_epoch = snapshot.callback_epoch;
    return true;
}
bool checked_sub(std::int64_t left, std::int64_t right,
                 std::int64_t& result) noexcept {
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left < min + right) || (right < 0 && left > max + right)) return false;
    result = left - right;
    return true;
}
bool checked_add(std::int64_t left, std::int64_t right,
                 std::int64_t& result) noexcept {
    constexpr auto min = std::numeric_limits<std::int64_t>::min();
    constexpr auto max = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left > max - right) || (right < 0 && left < min - right)) return false;
    result = left + right;
    return true;
}
std::int64_t positive_mod(std::int64_t value, std::int64_t modulus) noexcept {
    auto result = value % modulus;
    if (result < 0) result += modulus;
    return result;
}

// Matches the original helper's double division, multiplication, and truncation,
// with range checks before converting the result to int64.
bool scale_segment(std::int64_t value, std::int64_t segment_length,
                   std::int64_t target_length, std::int64_t& result) noexcept {
    if (value < 0 || segment_length <= 0 || target_length <= 0) return false;
    const double scaled = (static_cast<double>(value) /
                           static_cast<double>(segment_length)) *
                          static_cast<double>(target_length);
    constexpr double max_exclusive = 9223372036854775808.0;
    if (!std::isfinite(scaled) || scaled < 0.0 || scaled >= max_exclusive) return false;
    result = static_cast<std::int64_t>(scaled);
    return true;
}
bool valid_schedule(const Schedule& schedule, std::int64_t& period) noexcept {
    if (schedule.dawn < 0 || schedule.dawn >= schedule.dusk ||
        schedule.dusk > cycle_units || schedule.day_length <= 0 ||
        schedule.night_length <= 0) return false;
    return checked_add(schedule.day_length, schedule.night_length, period) && period > 0;
}
std::optional<std::int64_t> target_phase(const Schedule& schedule,
                                         double hour) noexcept {
    std::int64_t period = 0;
    if (!valid_schedule(schedule, period)) return {};
    const auto day_span = schedule.dusk - schedule.dawn;
    const auto night_span = cycle_units - day_span;
    if (night_span <= 0) return {};

    const double raw_double = hour * units_per_hour;
    if (!std::isfinite(raw_double) || raw_double < 0.0 ||
        raw_double > static_cast<double>(cycle_units)) return {};
    const auto raw_hour = static_cast<std::int64_t>(raw_double);

    std::int64_t night_start = 0;
    if (!scale_segment(schedule.dawn, night_span, schedule.night_length, night_start)) return {};
    std::int64_t phase = 0;
    if (raw_hour < schedule.dawn) {
        if (!scale_segment(raw_hour, night_span, schedule.night_length, phase)) return {};
    } else if (raw_hour < schedule.dusk) {
        std::int64_t day_offset = 0;
        if (!scale_segment(raw_hour - schedule.dawn, day_span,
                           schedule.day_length, day_offset) ||
            !checked_add(night_start, day_offset, phase)) return {};
    } else {
        std::int64_t night_offset = 0, after_day = 0;
        if (!scale_segment(raw_hour - schedule.dusk, night_span,
                           schedule.night_length, night_offset) ||
            !checked_add(night_start, schedule.day_length, after_day) ||
            !checked_add(after_day, night_offset, phase)) return {};
    }
    if (phase < 0 || phase >= period) return {};
    return phase;
}
std::optional<std::int64_t> base_for_hour(const ClockView& clock,
                                          double hour) noexcept {
    const auto current_base = clock.base_at_now();
    const auto phase = target_phase(clock.schedule, hour);
    if (!current_base || !phase) return {};
    std::int64_t period = 0;
    if (!valid_schedule(clock.schedule, period)) return {};
    const auto current_phase = positive_mod(*current_base, period);
    auto delta = *phase - current_phase;
    if (delta < 0) delta += period;
    std::int64_t base = 0;
    if (!checked_add(*current_base, delta, base)) return {};
    return base;
}
WritePlan make_plan(const ClockView& clock, PlanKind kind, float scale,
                    std::int64_t base) noexcept {
    return WritePlan{kind, base, 0, clock.now, scale,
                     static_cast<std::uint32_t>(clock.version + 1u)};
}
bool owner_still_authorized(const CallbackSnapshot& snapshot,
                            const OverrideState& state,
                            CapabilityProvider capability) noexcept {
    return snapshot.valid_owner() && snapshot.owner == state.owner &&
           snapshot.owner_identity && *snapshot.owner_identity == state.owner_identity &&
           capability.allows(state.owner_identity);
}
bool mode_is_valid(WorldTimeMode mode) noexcept {
    switch (mode) {
    case WorldTimeMode::normal:
    case WorldTimeMode::pause:
    case WorldTimeMode::fast:
        return true;
    case WorldTimeMode::unspecified:
        return false;
    }
    return false;
}
bool command_is_valid(const Command& command) noexcept {
    if (command.world_scope == 0) return false;
    switch (command.action) {
    case WorldTimeAction::set_hour:
        return command.mode == WorldTimeMode::unspecified &&
               normalize_hour(static_cast<double>(command.hour)).has_value();
    case WorldTimeAction::set_mode:
        return command.hour == 0 && mode_is_valid(command.mode);
    case WorldTimeAction::unspecified:
        return false;
    }
    return false;
}

} // namespace

bool valid_identity(const flight::Identity& identity) noexcept {
    return identity.authentication != 0 && identity.lifecycle != 0 &&
           identity.valid(static_cast<std::size_t>(identity.player & 63u));
}
std::optional<double> normalize_hour(double hour) noexcept {
    if (!std::isfinite(hour) || hour < 0.0 || hour > 24.0) return {};
    return hour == 24.0 ? 0.0 : hour;
}
std::optional<std::int64_t> advance_base(std::int64_t sync_base,
                                         std::int64_t sync_anchor,
                                         float scale,
                                         std::int64_t now) noexcept {
    if (!valid_scale(scale)) return {};
    std::int64_t delta = 0;
    if (!checked_sub(now, sync_anchor, delta)) return {};

    // Dedicated server 0x82CFE0: delta * 1e-9 in double, round to float,
    // multiply by the float scale, convert back to double, then multiply by 1e9.
    const double normalized_delta = static_cast<double>(delta) * 1.0e-9;
    const float normalized_float = static_cast<float>(normalized_delta);
    const float scaled_float = normalized_float * scale;
    const double scaled_integer = static_cast<double>(scaled_float) * 1.0e9;
    if (!std::isfinite(scaled_integer)) return {};
    const double truncated = std::trunc(scaled_integer);
    constexpr double min_integer = -9223372036854775808.0;
    constexpr double max_exclusive = 9223372036854775808.0;
    if (truncated < min_integer || truncated >= max_exclusive) return {};
    const auto increment = static_cast<std::int64_t>(truncated);
    std::int64_t result = 0;
    if (!checked_add(sync_base, increment, result)) return {};
    return result;
}
std::optional<std::int64_t> ClockView::base_at_now() const noexcept {
    return advance_base(sync_base, sync_anchor, scale, now);
}
bool ClockView::valid() const noexcept {
    return now >= 0 && valid_scale(scale) && base_at_now().has_value();
}
bool CallbackSnapshot::valid_world() const noexcept {
    if (!world_is_current || world == 0 || world_scope == 0 || callback_epoch == 0)
        return false;
    if (owner_resolution_current) {
        return owner != 0 && owner_identity && owner_identity->world == world &&
               valid_identity(*owner_identity);
    }
    return true;
}
bool CallbackSnapshot::valid_owner() const noexcept {
    return valid_world() && owner_resolution_current && owner != 0 &&
           owner_identity && owner_identity->world == world &&
           valid_identity(*owner_identity);
}

SubmitStatus CommandQueue::enqueue_hour(const flight::Identity& identity,
                                       std::uint32_t owner, WorldScope world_scope,
                                       std::uint8_t hour,
                                       CapabilityProvider capability) noexcept {
    if (!valid_identity(identity) || owner == 0) return SubmitStatus::invalid_identity;
    if (world_scope == 0) return SubmitStatus::invalid_world;
    if (!normalize_hour(static_cast<double>(hour))) return SubmitStatus::invalid_command;
    if (!capability.allows(identity)) return SubmitStatus::denied;

    std::lock_guard lock(mutex_);
    if (size_ >= entries_.size()) return SubmitStatus::full;
    if (next_sequence_ == 0 ||
        next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        return SubmitStatus::sequence_exhausted;
    }
    for (auto& entry : entries_) {
        if (!entry.occupied) {
            const auto normalized = *normalize_hour(static_cast<double>(hour));
            entry.command = Command{identity, owner, world_scope,
                                    WorldTimeAction::set_hour,
                                    WorldTimeMode::unspecified,
                                    static_cast<std::uint8_t>(normalized),
                                    next_sequence_++};
            entry.occupied = true;
            ++size_;
            return SubmitStatus::queued;
        }
    }
    return SubmitStatus::full;
}
SubmitStatus CommandQueue::enqueue_mode(const flight::Identity& identity,
                                        std::uint32_t owner, WorldScope world_scope,
                                        WorldTimeMode mode,
                                        CapabilityProvider capability) noexcept {
    if (!valid_identity(identity) || owner == 0) return SubmitStatus::invalid_identity;
    if (world_scope == 0) return SubmitStatus::invalid_world;
    if (!mode_is_valid(mode)) return SubmitStatus::invalid_command;
    if (!capability.allows(identity)) return SubmitStatus::denied;

    std::lock_guard lock(mutex_);
    if (size_ >= entries_.size()) return SubmitStatus::full;
    if (next_sequence_ == 0 ||
        next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        return SubmitStatus::sequence_exhausted;
    }
    for (auto& entry : entries_) {
        if (!entry.occupied) {
            entry.command = Command{identity, owner, world_scope,
                                    WorldTimeAction::set_mode,
                                    mode, 0, next_sequence_++};
            entry.occupied = true;
            ++size_;
            return SubmitStatus::queued;
        }
    }
    return SubmitStatus::full;
}
std::optional<Command> CommandQueue::peek_for_world(
    std::uintptr_t current_world, WorldScope world_scope) const noexcept {
    if (current_world == 0 || world_scope == 0) return {};
    std::lock_guard lock(mutex_);
    const Entry* selected = nullptr;
    for (const auto& entry : entries_) {
        if (!entry.occupied || entry.command.identity.world != current_world ||
            entry.command.world_scope != world_scope) continue;
        if (selected == nullptr || entry.command.sequence < selected->command.sequence)
            selected = &entry;
    }
    if (selected == nullptr) return {};
    return selected->command;
}
std::size_t CommandQueue::retire_world_scope(WorldScope world_scope) noexcept {
    if (world_scope == 0) return 0;
    std::lock_guard lock(mutex_);
    std::size_t retired = 0;
    for (auto& entry : entries_) {
        if (!entry.occupied || entry.command.world_scope != world_scope) continue;
        entry.occupied = false;
        entry.command = {};
        --size_;
        ++retired;
    }
    return retired;
}
bool CommandQueue::acknowledge(std::uint64_t sequence) noexcept {
    if (sequence == 0) return false;
    std::lock_guard lock(mutex_);
    for (auto& entry : entries_) {
        if (entry.occupied && entry.command.sequence == sequence) {
            entry.occupied = false;
            entry.command = {};
            --size_;
            return true;
        }
    }
    return false;
}
std::size_t CommandQueue::size() const noexcept {
    std::lock_guard lock(mutex_);
    return size_;
}

Decision plan_callback(const CallbackSnapshot& snapshot,
                       const std::optional<Command>& command,
                       const OverrideState& current_override,
                       CapabilityProvider capability) noexcept {
    Decision result;
    result.next_override = current_override;
    result.requested_command = command;
    if (!snapshot.valid_world()) {
        result.next_override = {};
        const bool resolved_identity_conflict =
            snapshot.world_is_current && snapshot.owner_resolution_current &&
            (snapshot.world == 0 || snapshot.owner == 0 ||
             !snapshot.owner_identity ||
             snapshot.owner_identity->world != snapshot.world ||
             !valid_identity(*snapshot.owner_identity));
        result.status = resolved_identity_conflict
            ? DecisionStatus::invalid_world
            : current_override.active ? DecisionStatus::expired
                                      : DecisionStatus::invalid_world;
        return result;
    }
    if (!snapshot.clock.valid()) {
        result.next_override = {};
        result.status = current_override.active ? DecisionStatus::expired
                                                : DecisionStatus::invalid_clock;
        return result;
    }

    // A new host-observed context retires old state without attempting to
    // restore through a clock that may belong to a different world lifetime.
    if (current_override.active &&
        current_override.world_scope != snapshot.world_scope) {
        result.next_override = {};
    }

    if (command) {
        result.command_sequence = command->sequence;
        result.acknowledge_command = true;
        if (!snapshot.valid_owner() || command->world_scope != snapshot.world_scope ||
            command->owner != snapshot.owner ||
            !snapshot.owner_identity || command->identity != *snapshot.owner_identity) {
            result.status = DecisionStatus::stale_command;
            return result;
        }
        if (!capability.allows(command->identity)) {
            result.status = DecisionStatus::denied;
            return result;
        }
        if (!command_is_valid(*command)) {
            result.status = DecisionStatus::invalid_command;
            return result;
        }

        OverrideState owned_override{};
        const bool owned = refresh_owned_override(snapshot, current_override,
                                                  owned_override);
        if (command->action == WorldTimeAction::set_hour) {
            const auto base = base_for_hour(snapshot.clock,
                                            static_cast<double>(command->hour));
            if (!base) {
                result.next_override = {};
                result.status = current_override.active ? DecisionStatus::expired
                                                        : DecisionStatus::invalid_clock;
                result.acknowledge_command = false;
                return result;
            }
            result.plan = make_plan(snapshot.clock, PlanKind::set_hour,
                                    snapshot.clock.scale, *base);
            const auto materialized_base = snapshot.clock.base_at_now();
            if (!materialized_base) {
                result.next_override = {};
                result.status = current_override.active ? DecisionStatus::expired
                                                        : DecisionStatus::invalid_clock;
                result.acknowledge_command = false;
                return result;
            }
            result.plan.hour_materialized_base = *materialized_base;
            result.has_plan = true;
            if (owned) {
                // An hour jump is a one-shot action. It changes the native
                // sync version, so keep the active rate override's ownership
                // and advance only the version we installed. The operator
                // issuing this one-shot command does not inherit the override.
                result.next_override = owned_override;
                result.next_override.installed_version = result.plan.sync_version;
                result.next_override.installed_base = result.plan.sync_base;
                result.next_override.installed_anchor = result.plan.sync_anchor;
                result.next_override.installed_callback_epoch = snapshot.callback_epoch;
            } else if (current_override.active &&
                       current_override.world_scope == snapshot.world_scope) {
                result.next_override = {};
            }
            result.status = DecisionStatus::plan_ready;
            return result;
        }

        float desired = normal_scale;
        if (command->mode == WorldTimeMode::pause) desired = paused_scale;
        if (command->mode == WorldTimeMode::fast) desired = asleep_scale;
        if (command->mode == WorldTimeMode::normal)
            desired = owned ? owned_override.original_scale : normal_scale;

        if (snapshot.clock.scale_writer_epoch ==
            std::numeric_limits<std::uint64_t>::max()) {
            result.next_override = {};
            result.status = current_override.active ? DecisionStatus::expired
                                                    : DecisionStatus::invalid_clock;
            result.acknowledge_command = false;
            return result;
        }

        const auto current_base = snapshot.clock.base_at_now();
        if (!current_base) {
            result.next_override = {};
            result.status = current_override.active ? DecisionStatus::expired
                                                    : DecisionStatus::invalid_clock;
            result.acknowledge_command = false;
            return result;
        }
        result.plan = make_plan(snapshot.clock, PlanKind::set_scale,
                                desired, *current_base);
        result.has_plan = true;
        if (command->mode == WorldTimeMode::pause ||
            command->mode == WorldTimeMode::fast) {
            const auto baseline = owned ? owned_override.original_scale
                                        : snapshot.clock.scale;
            result.next_override = {};
            result.next_override.active = true;
            result.next_override.world_scope = snapshot.world_scope;
            result.next_override.owner_identity = command->identity;
            result.next_override.owner = command->owner;
            result.next_override.original_scale = baseline;
            result.next_override.installed_scale = desired;
            result.next_override.installed_version = result.plan.sync_version;
            result.next_override.installed_base = result.plan.sync_base;
            result.next_override.installed_anchor = result.plan.sync_anchor;
            result.next_override.installed_callback_epoch = snapshot.callback_epoch;
            result.next_override.installed_scale_writer_epoch =
                snapshot.clock.scale_writer_epoch + 1;
            result.next_override.installed_schedule = snapshot.clock.schedule;
        } else if (current_override.active &&
                   current_override.world_scope == snapshot.world_scope) {
            result.next_override = {};
        }
        result.status = DecisionStatus::plan_ready;
        return result;
    }

    if (!current_override.active) {
        result.status = DecisionStatus::no_change;
        return result;
    }
    if (current_override.world_scope != snapshot.world_scope ||
        current_override.owner_identity.world != snapshot.world) {
        result.next_override = {};
        result.status = DecisionStatus::expired;
        return result;
    }
    OverrideState owned_override{};
    if (!refresh_owned_override(snapshot, current_override, owned_override)) {
        result.next_override = {};
        result.status = DecisionStatus::expired;
        return result;
    }
    if (owner_still_authorized(snapshot, owned_override, capability)) {
        result.next_override = owned_override;
        result.status = DecisionStatus::held;
        return result;
    }

    const auto current_base = snapshot.clock.base_at_now();
    if (!current_base || snapshot.clock.scale_writer_epoch ==
            std::numeric_limits<std::uint64_t>::max()) {
        result.next_override = {};
        result.status = DecisionStatus::expired;
        return result;
    }
    result.plan = make_plan(snapshot.clock, PlanKind::set_scale,
                            owned_override.original_scale, *current_base);
    result.has_plan = true;
    result.next_override = {};
    result.status = DecisionStatus::restore_ready;
    return result;
}

} // namespace xhl::native_time
