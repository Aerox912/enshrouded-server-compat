#include "callback_writer.hpp"

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

namespace xhl::native_time {
namespace {

bool same_float(float left, float right) noexcept {
    return std::bit_cast<std::uint32_t>(left) ==
           std::bit_cast<std::uint32_t>(right);
}

bool same_schedule(const Schedule& left, const Schedule& right) noexcept {
    return left.dawn == right.dawn && left.dusk == right.dusk &&
           left.day_length == right.day_length &&
           left.night_length == right.night_length;
}

bool same_native_clock(const ClockView& planned,
                       const ClockView& current) noexcept {
    return current.valid() && planned.sync_base == current.sync_base &&
           planned.sync_anchor == current.sync_anchor &&
           same_float(planned.scale, current.scale) &&
           planned.version == current.version &&
           planned.scale_writer_epoch == current.scale_writer_epoch &&
           same_schedule(planned.schedule, current.schedule);
}

bool same_world_callback_context(const CallbackSnapshot& planned,
                                const CallbackSnapshot& current) noexcept {
    return planned.valid_world() && current.valid_world() &&
           planned.world == current.world &&
           planned.world_scope == current.world_scope &&
           planned.callback_epoch == current.callback_epoch;
}

bool same_callback_context(const CallbackSnapshot& planned,
                           const CallbackSnapshot& current) noexcept {
    return same_world_callback_context(planned, current) &&
           same_native_clock(planned.clock, current.clock);
}

bool valid_scale(float scale) noexcept {
    return std::isfinite(scale) && scale >= paused_scale &&
           scale <= asleep_scale;
}

OverrideState prior_for_current_context(const OverrideState& prior,
                                        const CallbackSnapshot& current) noexcept {
    if (!prior.active || !current.valid_world() ||
        prior.world_scope != current.world_scope ||
        prior.owner_identity.world != current.world) return {};
    return prior;
}

bool plan_shape_valid(const CallbackSnapshot& snapshot,
                      const std::optional<Command>& command,
                      const Decision& decision) noexcept {
    const auto& plan = decision.plan;
    if (!valid_scale(plan.sync_scale) ||
        plan.sync_anchor != snapshot.clock.now ||
        plan.sync_version != static_cast<std::uint32_t>(snapshot.clock.version + 1u))
        return false;

    if (plan.kind == PlanKind::set_scale) {
        const auto base = snapshot.clock.base_at_now();
        return base && plan.sync_base == *base &&
               snapshot.clock.scale_writer_epoch != std::numeric_limits<std::uint64_t>::max() &&
               (!command || command->action == WorldTimeAction::set_mode);
    }

    const auto materialized = snapshot.clock.base_at_now();
    return plan.kind == PlanKind::set_hour && command &&
           command->action == WorldTimeAction::set_hour && materialized &&
           plan.hour_materialized_base == *materialized &&
           same_float(plan.sync_scale, snapshot.clock.scale);
}

bool plan_result_matches(const CallbackSnapshot& before,
                         const CallbackSnapshot& after,
                         const Decision& decision) noexcept {
    // The writer must change clock fields. Postwrite validation keeps the
    // callback/world association fixed, then checks those fields against plan.
    if (!same_world_callback_context(before, after)) return false;
    const auto& plan = decision.plan;
    if (after.clock.sync_base != plan.sync_base ||
        after.clock.sync_anchor != plan.sync_anchor ||
        after.clock.version != plan.sync_version ||
        !same_float(after.clock.scale, plan.sync_scale) ||
        !same_schedule(after.clock.schedule, before.clock.schedule)) return false;

    if (plan.kind == PlanKind::set_scale) {
        return before.clock.scale_writer_epoch != std::numeric_limits<std::uint64_t>::max() &&
               after.clock.scale_writer_epoch ==
                   before.clock.scale_writer_epoch + 1;
    }
    return after.clock.scale_writer_epoch == before.clock.scale_writer_epoch;
}

bool override_matches_after(const OverrideState& state,
                           const CallbackSnapshot& after) noexcept {
    return !state.active ||
           (state.world_scope == after.world_scope &&
            state.owner_identity.world == after.world &&
            same_float(state.installed_scale, after.clock.scale) &&
            state.installed_version == after.clock.version &&
            state.installed_base == after.clock.sync_base &&
            state.installed_anchor == after.clock.sync_anchor &&
            state.installed_scale_writer_epoch ==
                after.clock.scale_writer_epoch &&
            same_schedule(state.installed_schedule, after.clock.schedule));
}

} // namespace

ApplyResult apply_plan_once(const CallbackSnapshot& planned_snapshot,
    const std::optional<Command>& command,
    const OverrideState& prior_override,
    const Decision& decision,
    CapabilityProvider capability,
    CallbackWriters writers) noexcept {
    ApplyResult result{};
    result.acknowledge_command = decision.acknowledge_command;
    if (!decision.has_plan) {
        result.status = decision.status == DecisionStatus::expired
            ? ApplyStatus::expired : ApplyStatus::no_plan;
        result.next_override = decision.next_override;
        return result;
    }
    if ((decision.status != DecisionStatus::plan_ready &&
         decision.status != DecisionStatus::restore_ready) ||
        !planned_snapshot.valid_world() || !planned_snapshot.clock.valid() ||
        !writers.callback_context || !writers.read_current ||
        !plan_shape_valid(planned_snapshot, command, decision)) {
        result.status = ApplyStatus::invalid_backend;
        result.next_override = {};
        return result;
    }
    if ((decision.status == DecisionStatus::plan_ready) != command.has_value()) {
        result.status = ApplyStatus::invalid_backend;
        result.next_override = {};
        return result;
    }
    if (command && (!decision.requested_command ||
        *decision.requested_command != *command ||
        decision.command_sequence != command->sequence ||
        command->world_scope != planned_snapshot.world_scope ||
        command->identity.world != planned_snapshot.world)) {
        result.status = ApplyStatus::stale_command;
        result.acknowledge_command = true;
        result.next_override = prior_for_current_context(prior_override,
                                                        planned_snapshot);
        return result;
    }
    if (!command && decision.requested_command) {
        result.status = ApplyStatus::invalid_backend;
        result.next_override = {};
        return result;
    }
    if (decision.plan.kind == PlanKind::set_scale &&
        !writers.call_scale_writer_original_once) {
        result.status = ApplyStatus::invalid_backend;
        result.next_override = prior_for_current_context(prior_override,
                                                        planned_snapshot);
        return result;
    }
    if (decision.plan.kind == PlanKind::set_hour &&
        !writers.write_hour_fields_once) {
        result.status = ApplyStatus::invalid_backend;
        result.next_override = prior_for_current_context(prior_override,
                                                        planned_snapshot);
        return result;
    }

    CallbackSnapshot current{};
    if (!writers.read_current(writers.callback_context, current)) {
        result.status = ApplyStatus::expired;
        result.acknowledge_command = false;
        result.next_override = {};
        return result;
    }
    if (!same_callback_context(planned_snapshot, current)) {
        result.status = ApplyStatus::expired;
        result.acknowledge_command = command.has_value();
        result.next_override = {};
        return result;
    }
    if (command &&
        (!current.valid_owner() || current.owner != command->owner ||
         !current.owner_identity ||
         *current.owner_identity != command->identity)) {
        result.status = ApplyStatus::stale_command;
        result.acknowledge_command = true;
        result.next_override = prior_for_current_context(prior_override,
                                                        current);
        return result;
    }

    // Re-run the pure reducer against the freshly reread callback snapshot.
    // This checks the complete requested action, including target hour/rate,
    // restore baseline, ownership transition, and capability, before any write.
    const auto rechecked = plan_callback(current, command, prior_override,
                                         capability);
    if (rechecked != decision) {
        result.status = rechecked.status == DecisionStatus::denied
            ? ApplyStatus::denied
            : rechecked.status == DecisionStatus::stale_command
                ? ApplyStatus::stale_command : ApplyStatus::expired;
        result.acknowledge_command = rechecked.acknowledge_command;
        // No plan produced by a failed recheck was applied. Keep only the
        // prior value state while its original world/scope remains current.
        result.next_override = prior_for_current_context(prior_override,
                                                        current);
        return result;
    }

    bool writer_succeeded = false;
    if (decision.plan.kind == PlanKind::set_scale) {
        result.writer_invoked = true;
        writer_succeeded = writers.call_scale_writer_original_once(
            writers.callback_context, decision.plan.sync_scale,
            decision.plan.sync_anchor);
    } else {
        const HourFieldSequence writes{
            decision.plan.hour_materialized_base,
            decision.plan.sync_anchor,
            decision.plan.sync_version,
            decision.plan.sync_base
        };
        result.writer_invoked = true;
        writer_succeeded = writers.write_hour_fields_once(
            writers.callback_context, writes);
    }
    result.acknowledge_command = command.has_value();

    if (!writer_succeeded) {
        // A native write may have partially progressed. Read once for bounded
        // observation, but never attempt a second write or retain override state.
        CallbackSnapshot observed{};
        const bool observed_after_failure = writers.read_current(
            writers.callback_context, observed);
        result.status = observed_after_failure ? ApplyStatus::writer_failed
                                               : ApplyStatus::expired;
        result.next_override = {};
        return result;
    }

    CallbackSnapshot after{};
    if (!writers.read_current(writers.callback_context, after) ||
        !plan_result_matches(current, after, decision) ||
        !override_matches_after(decision.next_override, after)) {
        result.status = ApplyStatus::expired;
        result.next_override = {};
        return result;
    }

    result.next_override = decision.next_override;
    result.status = decision.status == DecisionStatus::restore_ready
        ? ApplyStatus::restored : ApplyStatus::applied;
    return result;
}

} // namespace xhl::native_time
