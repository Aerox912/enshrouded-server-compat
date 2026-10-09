#pragma once

#include "time_control.hpp"

namespace xhl::native_time {

enum class ApplyStatus : std::uint8_t {
    no_plan, applied, restored, denied, stale_command, expired,
    invalid_backend, writer_failed
};

struct HourFieldSequence {
    std::int64_t materialized_sync_base = 0;
    std::int64_t sync_anchor = 0;
    std::uint32_t sync_version = 0;
    std::int64_t target_sync_base = 0;
};

// All callbacks and callback_context are valid only during one owning native
// callback. read_current must rebuild the full current snapshot from that
// callback's fresh query/world association. The writer context may contain a
// clock pointer locally, but this module neither receives nor retains it.
//
// Server adapter contract for the pinned image:
// * Run planning/application only after the original CACE0 callback has called
//   0x861790 and 0x8614B0, using that invocation's query slot 0 for the world
//   and slot 2 for the callback-local clock.
// * read_current re-resolves the full world/owner and rereads clock/schedule
//   fields inside that same CACE0 invocation.
// * call_scale_writer_original_once invokes the 0x8507C0 original exactly once
//   (clock in RCX, scale in XMM1, tick in R8), and advances the per-world
//   scale-writer epoch exactly once even when the scale is unchanged.
// * write_hour_fields_once writes clock+0x18 materialized syncBase,
//   clock+0x10 syncAnchor, clock+0x24 syncVersion, then clock+0x18 target
//   syncBase. It must not write clock+0x48 timeOfDay; the native tick owns it.
// The pure adapter remains inactive until the separate runtime acceptance gate
// proves safe serialization with the C9670 and CACE0 native writer paths.
struct CallbackWriters {
    void* callback_context = nullptr;
    bool (*read_current)(void* callback_context,
                         CallbackSnapshot& output) noexcept = nullptr;
    // Maps to the 0x8507C0 trampoline and must invoke that original exactly
    // once with the supplied scale/tick when returning true.
    bool (*call_scale_writer_original_once)(void* callback_context,
        float scale, std::int64_t now) noexcept = nullptr;
    // Maps the server hour write sequence to syncBase, syncAnchor,
    // syncVersion, then target syncBase. Must not touch syncScale/timeOfDay.
    bool (*write_hour_fields_once)(void* callback_context,
        const HourFieldSequence& writes) noexcept = nullptr;
};

struct ApplyResult {
    ApplyStatus status = ApplyStatus::no_plan;
    bool acknowledge_command = false;
    bool writer_invoked = false;
    OverrideState next_override{};
};

// Applies at most one plan synchronously. It rereads the current snapshot,
// verifies world/scope/clock preconditions, and rechecks command identity and
// the separate time capability before invoking one writer callback. The host
// must call this while still inside the callback whose query produced the
// planning snapshot. No lock is taken across native scheduler code.
ApplyResult apply_plan_once(const CallbackSnapshot& planned_snapshot,
    const std::optional<Command>& command,
    const OverrideState& prior_override,
    const Decision& decision,
    CapabilityProvider capability,
    CallbackWriters writers) noexcept;

} // namespace xhl::native_time
