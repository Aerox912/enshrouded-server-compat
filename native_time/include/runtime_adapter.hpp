#pragma once

#include "callback_writer.hpp"
#include "read_only_observer.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

namespace xhl::native_time::runtime {

enum class Mode : std::uint8_t { disabled, observe_only, command_writes };
enum class State : std::uint8_t { stopped, starting, running, stopping, failed };
enum class PhaseStatus : std::uint8_t { not_started, waiting, accepted, rejected, invalidated };
enum class PhaseReason : std::uint8_t {
    none, disabled, hooks_not_ready, sample_active, sample_incomplete,
    event_loss, callback_overlap, association_unavailable, unstable_clock,
    missing_daytime, missing_nighttime, missing_scale_path,
    callback_order, no_shared_world_clock, unassociated_writer,
    host_association_unavailable, scope_unavailable
};

enum class PhaseEventKind : std::uint8_t {
    callback_enter, query_ready, updater_before, updater_after,
    tick_before, tick_after, scale_before, scale_after, callback_exit
};

// This view is valid only inside its owning native callback. Its addresses are
// passed to host callbacks synchronously and are never kept in runtime state.
struct NativeQueryView {
    std::uintptr_t callback_context = 0;
    std::uintptr_t query_output = 0;
    std::uintptr_t world = 0;
    std::uintptr_t clock = 0;
    std::uint32_t descriptor_size = 0;
    // Callback-local value supplied to the native clock tick writer.
    std::int64_t native_now = 0;
    bool native_now_known = false;
};

using SnapshotReader = bool (*)(void* context, const NativeQueryView& query,
    CallbackSnapshot& output) noexcept;
using HourFieldWriter = bool (*)(void* context, const NativeQueryView& query,
    const HourFieldSequence& writes) noexcept;

// read_snapshot re-resolves the current world, full owner identity when one is
// present, host-issued WorldScope and clock values from this callback-local
// query. It must not retain query or clock addresses. Time permission is a
// separate default-deny capability and is not Creative flight permission.
struct Host {
    void* context = nullptr;
    SnapshotReader read_snapshot = nullptr;
    HourFieldWriter write_hour_fields = nullptr;
    CapabilityProvider time_capability{};
};

// Startup is explicit and one-shot. disabled is the default. Observe-only
// startup still requires a fresh host snapshot reader so phase evidence can be
// bound to an opaque scope. command_writes cannot write until a bounded sample
// has been accepted and enable_command_writes (if started observe-only) has
// been explicitly called.
struct StartOptions {
    Mode mode = Mode::disabled;
    bool explicitly_enabled = false;
    observer::InstallOptions observation{};
    Host host{};
};

struct PhaseEvent {
    std::uint64_t sequence = 0;
    std::uint64_t callback_id = 0;
    std::uint64_t world_label = 0;
    std::uint64_t clock_label = 0;
    WorldScope world_scope = 0;
    std::uint32_t thread_id = 0;
    std::uint32_t active_callbacks = 0;
    std::uint16_t nesting_depth = 0;
    observer::Site callback_site = observer::Site::count;
    observer::Site site = observer::Site::count;
    PhaseEventKind kind = PhaseEventKind::callback_enter;
};

struct PhasePair {
    std::uint64_t world_label = 0;
    std::uint64_t clock_label = 0;
    WorldScope world_scope = 0;
    bool operator==(const PhasePair&) const = default;
};

struct PhaseReport {
    PhaseStatus status = PhaseStatus::not_started;
    PhaseReason reason = PhaseReason::disabled;
    std::size_t daytime_callbacks = 0;
    std::size_t nighttime_callbacks = 0;
    std::size_t scale_writes = 0;
    std::array<PhasePair, 16> accepted_pairs{};
    std::size_t accepted_pair_count = 0;
};

// Testable phase validator. Both the observer's stable native snapshots and
// the adapter's callback-scoped query association/order must pass. Addresses
// are absent from these event types.
PhaseReport assess_phase(const observer::Statistics& statistics,
    std::span<const observer::Event> observer_events,
    std::span<const PhaseEvent> adapter_events) noexcept;

class RuntimeAdapter {
public:
    RuntimeAdapter() noexcept = default;
    RuntimeAdapter(const RuntimeAdapter&) = delete;
    RuntimeAdapter& operator=(const RuntimeAdapter&) = delete;

    // The observer and all install/host contexts must be process-lifetime.
    // The supplied hook backend is wrapped so each target has one native
    // detour that forwards through the observer and this adapter. Do not also
    // install the local standalone observer on these targets.
    bool start(observer::ReadOnlyObserver& observer,
        const StartOptions& options) noexcept;
    // Stops new writes first, then asks the observer to disable its six hooks.
    // The observer's trampoline/context lifetime contract still applies.
    bool stop() noexcept;

    PhaseReport refresh_phase_observation() noexcept;
    bool enable_command_writes() noexcept;
    void disable_command_writes() noexcept;
    State state() const noexcept;
    Mode mode() const noexcept;
    bool hooks_ready() const noexcept;
    bool command_backend_ready() const noexcept;
    bool phase_scope_ready(WorldScope scope) const noexcept;
    PhaseReport phase_report() const noexcept;
    ApplyStatus last_apply_status() const noexcept;
    std::size_t queued_commands() const noexcept;
    bool scope_override_active(WorldScope scope) const noexcept;
    std::size_t copy_phase_events(std::span<PhaseEvent> destination) const noexcept;
    std::uint64_t phase_events_dropped() const noexcept;

    SubmitStatus set_hour(const flight::Identity& identity, std::uint32_t owner,
        WorldScope scope, std::uint8_t hour) noexcept;
    SubmitStatus set_mode(const flight::Identity& identity, std::uint32_t owner,
        WorldScope scope, WorldTimeMode mode) noexcept;
    // Call on an observable host/world/session lifecycle transition. This
    // drops value state and queued commands without attempting a stale restore.
    // It performs no native clock write and invalidates any in-flight plan.
    std::size_t retire_world_scope(WorldScope scope) noexcept;

private:
    static constexpr std::size_t max_world_scopes = 32;
    static constexpr std::size_t runtime_event_capacity = observer::event_capacity;

    struct ScopeState {
        bool occupied = false;
        WorldScope scope = 0;
        std::uint64_t callback_epoch = 0;
        std::uint64_t writer_epoch = 0;
        OverrideState override{};
    };
    struct CallbackFrame {
        RuntimeAdapter* adapter = nullptr;
        CallbackFrame* previous = nullptr;
        NativeQueryView query{};
        CallbackSnapshot snapshot{};
        std::uint64_t callback_id = 0;
        std::uint64_t callback_epoch = 0;
        std::uint64_t retirement_epoch = 0;
        std::int64_t native_now = 0;
        WorldScope scope = 0;
        std::uint64_t world_label = 0;
        std::uint64_t clock_label = 0;
        std::uint16_t nesting_depth = 0;
        observer::Site site = observer::Site::count;
        bool query_ready = false;
        bool association_valid = false;
        bool snapshot_valid = false;
        bool native_now_known = false;
    };
    struct WriterFrame {
        RuntimeAdapter* adapter = nullptr;
        CallbackFrame* callback = nullptr;
        bool requires_capability = false;
    };

    static std::atomic<RuntimeAdapter*> active_;
    static thread_local CallbackFrame* current_frame_;
    static thread_local RuntimeAdapter* query_reader_;

    static bool create_bridge(void* context, std::uintptr_t target,
        void* observer_detour, void** original) noexcept;
    static bool enable_bridge(void* context, std::uintptr_t target) noexcept;
    static bool disable_bridge(void* context, std::uintptr_t target) noexcept;
    static bool remove_bridge(void* context, std::uintptr_t target) noexcept;
    static bool read_query_memory(std::uintptr_t address, void* output,
        std::size_t size) noexcept;
    void* bridge_for(std::size_t index) const noexcept;

    static void XHL_NATIVE_TIME_CALL query_bridge(void* context, void* output,
        std::uint32_t descriptor_size) noexcept;
    static void XHL_NATIVE_TIME_CALL daytime_bridge(void* context) noexcept;
    static void XHL_NATIVE_TIME_CALL nighttime_bridge(void* context) noexcept;
    static void XHL_NATIVE_TIME_CALL scale_bridge(void* clock, float scale,
        std::int64_t now) noexcept;
    static void XHL_NATIVE_TIME_CALL updater_bridge(void* clock,
        std::uint64_t day, std::uint64_t night, std::int64_t now) noexcept;
    static void XHL_NATIVE_TIME_CALL tick_bridge(void* clock,
        std::int64_t now) noexcept;

    static bool writer_read_current(void* context,
        CallbackSnapshot& output) noexcept;
    static bool writer_call_scale_original(void* context, float scale,
        std::int64_t now) noexcept;
    static bool writer_write_hour_once(void* context,
        const HourFieldSequence& writes) noexcept;

    bool install(observer::ReadOnlyObserver& observer,
        const StartOptions& options) noexcept;
    void forward_query(void* context, void* output,
        std::uint32_t descriptor_size) noexcept;
    void forward_callback(observer::Site site, void* context) noexcept;
    void forward_scale(void* clock, float scale, std::int64_t now) noexcept;
    void forward_updater(void* clock, std::uint64_t day, std::uint64_t night,
        std::int64_t now) noexcept;
    void forward_tick(void* clock, std::int64_t now) noexcept;
    void capture_query(void* context, void* output,
        std::uint32_t descriptor_size) noexcept;
    void run_callback_time_command(CallbackFrame& frame) noexcept;
    bool read_host_snapshot(const NativeQueryView& query,
        CallbackSnapshot& output, std::uint64_t callback_epoch) noexcept;
    bool call_native_scale_once(WriterFrame& frame, float scale,
        std::int64_t now) noexcept;
    bool call_native_hour_once(WriterFrame& frame,
        const HourFieldSequence& writes) noexcept;
    bool can_write_for(const CallbackFrame& frame) const noexcept;
    bool phase_scope_accepted(WorldScope scope) const noexcept;
    bool phase_pair_matches(std::uint64_t world_label,
        std::uint64_t clock_label, WorldScope scope) const noexcept;
    bool bump_writer_epoch(WorldScope scope,
        std::uint64_t& result) noexcept;
    bool scope_state(WorldScope scope, bool create,
        ScopeState& output) noexcept;
    bool set_scope_state(const ScopeState& state) noexcept;
    void clear_scope(WorldScope scope) noexcept;
    void store_phase_report(const PhaseReport& report) noexcept;
    void record(PhaseEvent event) noexcept;
    std::uint64_t opaque_label(std::uint8_t domain,
        std::uintptr_t address) const noexcept;
    void invalidate_phase(PhaseReason reason) noexcept;

    observer::ReadOnlyObserver* observer_ = nullptr;
    observer::RuntimeReaders observer_readers_{};
    observer::HookBackend underlying_hooks_{};
    Host host_{};
    std::atomic<Mode> mode_{Mode::disabled};
    std::atomic<State> state_{State::stopped};
    std::atomic<bool> explicitly_enabled_{false};
    std::atomic<bool> hooks_ready_{false};
    std::atomic<bool> phase_ready_{false};
    std::atomic<bool> phase_fault_{false};
    std::atomic<bool> unassociated_writer_{false};
    std::atomic<ApplyStatus> last_apply_status_{ApplyStatus::no_plan};
    std::array<void*, observer::site_count> observer_detours_{};
    std::array<void*, observer::site_count> native_originals_{};
    std::array<std::uintptr_t, observer::site_count> targets_{};
    std::array<bool, observer::site_count> created_{};
    std::array<bool, observer::site_count> enabled_{};
    std::size_t next_create_ = 0;
    mutable std::mutex scope_mutex_;
    std::array<ScopeState, max_world_scopes> scopes_{};
    std::array<WorldScope, max_world_scopes> retired_scopes_{};
    std::size_t retired_scope_count_ = 0;
    mutable std::mutex phase_mutex_;
    CommandQueue queue_{};
    std::uintptr_t image_base_ = 0;
    std::uint64_t label_salt_ = 0;
    std::atomic<std::uint64_t> next_callback_id_{1};
    std::atomic<std::uint64_t> active_callbacks_{0};
    std::atomic<std::uint64_t> retirement_epoch_{0};
    std::atomic<std::uint64_t> next_event_sequence_{1};
    std::atomic<std::uint64_t> events_recorded_{0};
    std::atomic<std::uint64_t> events_dropped_{0};
    std::array<PhaseEvent, runtime_event_capacity> events_{};
    mutable std::array<std::atomic_flag, runtime_event_capacity> event_locks_{};
    std::array<observer::Event, observer::event_capacity> observer_events_{};
    std::array<PhaseEvent, runtime_event_capacity> runtime_events_{};
    PhaseReport phase_report_{};
};

} // namespace xhl::native_time::runtime






