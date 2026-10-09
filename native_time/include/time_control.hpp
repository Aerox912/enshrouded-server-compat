#pragma once

#include "flight_session.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>

namespace xhl::native_time {

inline constexpr double units_per_hour = 3'600'000'000'000.0;
inline constexpr std::int64_t cycle_units = 86'400'000'000'000LL;
inline constexpr float normal_scale = 1.0f;
inline constexpr float paused_scale = 0.0f;
inline constexpr float asleep_scale = 60.0f;
inline constexpr std::size_t pending_capacity = 64;
// Host-issued in-process world/session context token, never a native pointer.
// For the dedicated-server one-world-per-process assumption it can be a
// process/session scope and should rotate on observed world/session/owner
// lifecycle changes. It is not claimed to prove allocator non-reuse between
// callbacks; commands also bind the full Identity and freshly resolved world.
using WorldScope = std::uint64_t;

enum class WorldTimeAction : std::uint8_t {
    unspecified = 0, set_hour = 1, set_mode = 2
};
enum class WorldTimeMode : std::uint8_t {
    unspecified = 0, normal, pause, fast
};
enum class SubmitStatus : std::uint8_t {
    queued, denied, invalid_identity, invalid_world, invalid_command, full,
    sequence_exhausted
};
enum class DecisionStatus : std::uint8_t {
    invalid_world, invalid_clock, stale_command, denied, invalid_command,
    plan_ready, restore_ready, held, external_change_preserved, expired,
    no_change
};
enum class PlanKind : std::uint8_t { set_hour, set_scale };

struct CapabilityProvider {
    using Check = bool (*)(void* context, const flight::Identity& identity) noexcept;
    void* context = nullptr;
    Check check = nullptr;

    // Missing provider is default-deny. This is a separate private
    // world-time capability, not the Creative flight permission.
    bool allows(const flight::Identity& identity) const noexcept {
        return check != nullptr && check(context, identity);
    }
};

struct Schedule {
    std::int64_t dawn = 0;
    std::int64_t dusk = 0;
    std::int64_t day_length = 0;
    std::int64_t night_length = 0;
    bool operator==(const Schedule&) const = default;
};

// Values are copied inside a validated callback. No native clock pointer or
// reference crosses this interface. base_at_now must use pinned-server math.
struct ClockView {
    std::int64_t sync_base = 0;
    std::int64_t sync_anchor = 0;
    std::int64_t now = 0;
    float scale = normal_scale;
    std::uint32_t version = 0;
    // Adapter-maintained metadata, not native fields. The writer epoch is
    // scoped to WorldScope and advances on every observed native scale-writer
    // entry, including a same-scale write.
    std::uint64_t scale_writer_epoch = 0;
    Schedule schedule{};
    std::optional<std::int64_t> base_at_now() const noexcept;
    bool valid() const noexcept;
};

std::optional<std::int64_t> advance_base(std::int64_t sync_base,
                                         std::int64_t sync_anchor,
                                         float scale,
                                         std::int64_t now) noexcept;

// owner_identity is the current authenticated_owner(world, owner) result.
// It may be absent for cleanup after revocation/disconnect.
struct CallbackSnapshot {
    std::uintptr_t world = 0;
    // Opaque nonzero host context token. Rotate it on observed world/session
    // or owner lifecycle transitions; it is not a native allocator generation.
    WorldScope world_scope = 0;
    // Monotonic count for completed daytime callbacks in this world scope.
    std::uint64_t callback_epoch = 0;
    bool world_is_current = false;
    std::optional<flight::Identity> owner_identity;
    std::uint32_t owner = 0;
    bool owner_resolution_current = false;
    ClockView clock{};
    bool valid_world() const noexcept;
    bool valid_owner() const noexcept;
};

struct Command {
    flight::Identity identity{};
    std::uint32_t owner = 0;
    WorldScope world_scope = 0;
    WorldTimeAction action = WorldTimeAction::unspecified;
    WorldTimeMode mode = WorldTimeMode::unspecified;
    std::uint8_t hour = 0;
    std::uint64_t sequence = 0;
    bool operator==(const Command&) const = default;
};

class CommandQueue {
    struct Entry {
        bool occupied = false;
        Command command{};
    };
    mutable std::mutex mutex_;
    std::array<Entry, pending_capacity> entries_{};
    std::size_t size_ = 0;
    std::uint64_t next_sequence_ = 1;

public:
    SubmitStatus enqueue_hour(const flight::Identity& identity, std::uint32_t owner,
                              WorldScope world_scope, std::uint8_t hour,
                              CapabilityProvider capability) noexcept;
    SubmitStatus enqueue_mode(const flight::Identity& identity, std::uint32_t owner,
                              WorldScope world_scope, WorldTimeMode mode,
                              CapabilityProvider capability) noexcept;
    std::optional<Command> peek_for_world(std::uintptr_t current_world,
                                          WorldScope world_scope) const noexcept;
    std::size_t retire_world_scope(WorldScope world_scope) noexcept;
    bool acknowledge(std::uint64_t sequence) noexcept;
    std::size_t size() const noexcept;
};

struct OverrideState {
    bool active = false;
    WorldScope world_scope = 0;
    flight::Identity owner_identity{};
    std::uint32_t owner = 0;
    float original_scale = normal_scale;
    float installed_scale = normal_scale;
    std::uint32_t installed_version = 0;
    std::int64_t installed_base = 0;
    std::int64_t installed_anchor = 0;
    std::uint64_t installed_callback_epoch = 0;
    std::uint64_t installed_scale_writer_epoch = 0;
    Schedule installed_schedule{};
    bool operator==(const OverrideState&) const = default;
};

struct WritePlan {
    PlanKind kind = PlanKind::set_scale;
    std::int64_t sync_base = 0;
    // For set_hour, materialize the old rate through plan.sync_anchor before
    // writing sync_version and the final target sync_base, matching the
    // original Creative setter sequence. Unused for set_scale.
    std::int64_t hour_materialized_base = 0;
    std::int64_t sync_anchor = 0;
    float sync_scale = normal_scale;
    std::uint32_t sync_version = 0;
    bool operator==(const WritePlan&) const = default;
};

struct Decision {
    DecisionStatus status = DecisionStatus::no_change;
    bool has_plan = false;
    bool acknowledge_command = false;
    std::uint64_t command_sequence = 0;
    std::optional<Command> requested_command;
    WritePlan plan{};
    // Persist only after applying the plan synchronously in the same callback.
    // A pause/fast plan expects exactly one scoped 0x8507C0 writer-epoch step;
    // discard next_override if application does not produce that observation.
    OverrideState next_override{};
    bool operator==(const Decision&) const = default;
};

bool valid_identity(const flight::Identity& identity) noexcept;
std::optional<double> normalize_hour(double hour) noexcept;

// Pure reducer. The host selects state by WorldScope, applies any plan on the
// owning callback, then persists next_override and acknowledges. On world
// teardown it must retire queued commands and drop that scope's state. The
// module never reads/writes game memory and retains no native object address.
Decision plan_callback(const CallbackSnapshot& snapshot,
                       const std::optional<Command>& command,
                       const OverrideState& current_override,
                       CapabilityProvider capability) noexcept;

} // namespace xhl::native_time
