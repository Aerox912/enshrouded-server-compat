#include "runtime_adapter.hpp"

#include "flight_identity.hpp"

#include <algorithm>
#include <limits>

namespace xhl::native_time::runtime {
namespace {

constexpr std::array<std::uintptr_t, observer::site_count> hook_rvas{
    0x5dc7f0, 0xcace0, 0xc9670, 0x8507c0, 0x861790, 0x8614b0
};
constexpr std::uint8_t world_domain = 7;
constexpr std::uint8_t clock_domain = 8;

template<class T>
bool read_offset(observer::MemoryReader reader, std::uintptr_t base,
                 std::uintptr_t offset, T& output) noexcept {
    if (!reader || !base || offset > UINTPTR_MAX - base) return false;
    return reader(base + offset, &output, sizeof(output));
}

std::uint64_t mix_label(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value ? value : 1;
}

std::size_t index_of(observer::Site site) noexcept {
    return static_cast<std::size_t>(site);
}

using QueryBegin = void (XHL_NATIVE_TIME_CALL *)(void*, void*, std::uint32_t);
using NativeCallback = void (XHL_NATIVE_TIME_CALL *)(void*);
using ScaleWriter = void (XHL_NATIVE_TIME_CALL *)(void*, float, std::int64_t);
using DaynightUpdater = void (XHL_NATIVE_TIME_CALL *)(void*, std::uint64_t,
    std::uint64_t, std::int64_t);
using ClockTick = void (XHL_NATIVE_TIME_CALL *)(void*, std::int64_t);

struct NativeCallbackTrace {
    std::uint64_t epoch = 0;
    std::uint64_t enter = 0;
    std::uint64_t query = 0;
    std::uint64_t updater_before = 0;
    std::uint64_t updater_after = 0;
    std::uint64_t tick_before = 0;
    std::uint64_t tick_after = 0;
    std::uint64_t scale_before = 0;
    std::uint64_t scale_after = 0;
    std::uint64_t exit = 0;
    PhasePair pair{};
    bool pair_set = false;
    observer::Site site = observer::Site::count;
};

struct AdapterCallbackTrace {
    std::uint64_t id = 0;
    std::uint64_t enter = 0;
    std::uint64_t query = 0;
    std::uint64_t updater_before = 0;
    std::uint64_t updater_after = 0;
    std::uint64_t tick_before = 0;
    std::uint64_t tick_after = 0;
    std::uint64_t scale_before = 0;
    std::uint64_t scale_after = 0;
    std::uint64_t exit = 0;
    PhasePair pair{};
    bool pair_set = false;
    observer::Site site = observer::Site::count;
};

NativeCallbackTrace* find_native_trace(
    std::array<NativeCallbackTrace, 256>& traces, std::size_t& count,
    std::uint64_t epoch, observer::Site site, bool create) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (traces[i].epoch == epoch && traces[i].site == site)
            return &traces[i];
    }
    if (!create || !epoch || count == traces.size()) return nullptr;
    auto& trace = traces[count++];
    trace = {};
    trace.epoch = epoch;
    trace.site = site;
    return &trace;
}

AdapterCallbackTrace* find_adapter_trace(
    std::array<AdapterCallbackTrace, 256>& traces, std::size_t& count,
    std::uint64_t id, observer::Site site, bool create) noexcept {
    for (std::size_t i = 0; i < count; ++i) {
        if (traces[i].id == id && traces[i].site == site)
            return &traces[i];
    }
    if (!create || !id || count == traces.size()) return nullptr;
    auto& trace = traces[count++];
    trace = {};
    trace.id = id;
    trace.site = site;
    return &trace;
}

bool stable_snapshot(const observer::Event& event) noexcept {
    return event.clock.complete && event.clock.version_stable;
}

bool same_native_pair(const NativeCallbackTrace& left,
                      const NativeCallbackTrace& right) noexcept {
    return left.pair_set && right.pair_set &&
           left.pair.world_label == right.pair.world_label &&
           left.pair.clock_label == right.pair.clock_label;
}

bool same_adapter_pair(const AdapterCallbackTrace& left,
                       const AdapterCallbackTrace& right) noexcept {
    return left.pair_set && right.pair_set && left.pair == right.pair;
}

bool native_day_complete(const NativeCallbackTrace& trace) noexcept {
    return trace.site == observer::Site::daytime_callback && trace.enter &&
        trace.query && trace.updater_before && trace.updater_after &&
        trace.tick_before && trace.tick_after && trace.exit &&
        trace.enter < trace.query && trace.query < trace.updater_before &&
        trace.updater_before < trace.updater_after &&
        trace.updater_after < trace.tick_before &&
        trace.tick_before < trace.tick_after && trace.tick_after < trace.exit;
}

bool native_night_complete(const NativeCallbackTrace& trace) noexcept {
    return trace.site == observer::Site::nighttime_skip_callback && trace.enter &&
        trace.query && trace.scale_before && trace.scale_after && trace.exit &&
        trace.enter < trace.query && trace.query < trace.scale_before &&
        trace.scale_before < trace.scale_after && trace.scale_after < trace.exit;
}

bool adapter_day_complete(const AdapterCallbackTrace& trace) noexcept {
    return trace.site == observer::Site::daytime_callback && trace.enter &&
        trace.query && trace.updater_before && trace.updater_after &&
        trace.tick_before && trace.tick_after && trace.exit &&
        trace.enter < trace.query && trace.query < trace.updater_before &&
        trace.updater_before < trace.updater_after &&
        trace.updater_after < trace.tick_before &&
        trace.tick_before < trace.tick_after && trace.tick_after < trace.exit;
}

bool adapter_night_complete(const AdapterCallbackTrace& trace) noexcept {
    return trace.site == observer::Site::nighttime_skip_callback && trace.enter &&
        trace.query && trace.scale_before && trace.scale_after && trace.exit &&
        trace.enter < trace.query && trace.query < trace.scale_before &&
        trace.scale_before < trace.scale_after && trace.scale_after < trace.exit;
}

bool native_pair_is_stable(const NativeCallbackTrace& trace,
                           std::span<const observer::Event> events) noexcept {
    for (const auto& event : events) {
        if (event.callback_epoch != trace.epoch) continue;
        if (event.kind == observer::EventKind::query_result ||
            event.kind == observer::EventKind::updater_before ||
            event.kind == observer::EventKind::updater_after ||
            event.kind == observer::EventKind::tick_before ||
            event.kind == observer::EventKind::tick_after ||
            event.kind == observer::EventKind::scale_before ||
            event.kind == observer::EventKind::scale_after) {
            if (!stable_snapshot(event)) return false;
        }
    }
    return true;
}

bool add_pair(PhaseReport& report, const PhasePair& pair) noexcept {
    if (!pair.world_label || !pair.clock_label || !pair.world_scope) return false;
    for (std::size_t i = 0; i < report.accepted_pair_count; ++i) {
        if (report.accepted_pairs[i] == pair) return true;
    }
    if (report.accepted_pair_count == report.accepted_pairs.size()) return false;
    report.accepted_pairs[report.accepted_pair_count++] = pair;
    return true;
}

std::size_t find_target_index(
    const std::array<std::uintptr_t, observer::site_count>& targets,
    std::uintptr_t target) noexcept {
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (targets[i] == target) return i;
    }
    return targets.size();
}

} // namespace

std::atomic<RuntimeAdapter*> RuntimeAdapter::active_{nullptr};
thread_local RuntimeAdapter::CallbackFrame* RuntimeAdapter::current_frame_ = nullptr;
thread_local RuntimeAdapter* RuntimeAdapter::query_reader_ = nullptr;

PhaseReport assess_phase(const observer::Statistics& statistics,
    std::span<const observer::Event> observer_events,
    std::span<const PhaseEvent> adapter_events) noexcept {
    PhaseReport report{};
    report.status = PhaseStatus::rejected;
    report.reason = PhaseReason::sample_incomplete;

    if (!statistics.hooks_ready) {
        report.reason = PhaseReason::hooks_not_ready;
        return report;
    }
    if (statistics.sampling) {
        report.status = PhaseStatus::waiting;
        report.reason = PhaseReason::sample_active;
        return report;
    }
    if (!statistics.sample_expired) return report;
    if (statistics.events_dropped ||
        statistics.events_recorded > observer::event_capacity ||
        observer_events.size() != statistics.events_recorded) {
        report.reason = PhaseReason::event_loss;
        return report;
    }
    if (statistics.callbacks_overlapped || statistics.callbacks_nested ||
        statistics.maximum_concurrent_callbacks > 1) {
        report.reason = PhaseReason::callback_overlap;
        return report;
    }
    if (statistics.associations_unavailable) {
        report.reason = PhaseReason::association_unavailable;
        return report;
    }

    std::array<NativeCallbackTrace, 256> native_traces{};
    std::array<AdapterCallbackTrace, 256> adapter_traces{};
    std::size_t native_count = 0;
    std::size_t adapter_count = 0;
    bool native_unstable = false;
    bool native_association_missing = false;
    bool adapter_association_missing = false;

    for (const auto& event : observer_events) {
        const bool watched = event.site == observer::Site::daytime_callback ||
            event.site == observer::Site::nighttime_skip_callback;
        if (event.kind == observer::EventKind::callback_enter && watched) {
            auto* trace = find_native_trace(native_traces, native_count,
                event.callback_epoch, event.site, true);
            if (!trace || trace->enter || event.active_callbacks != 1 ||
                event.nesting_depth != 1) {
                report.reason = PhaseReason::callback_overlap;
                return report;
            }
            trace->enter = event.sequence;
        }
        if (event.kind == observer::EventKind::callback_exit && watched) {
            auto* trace = find_native_trace(native_traces, native_count,
                event.callback_epoch, event.site, false);
            if (!trace || trace->exit) {
                report.reason = PhaseReason::callback_order;
                return report;
            }
            trace->exit = event.sequence;
        }
        if (event.kind == observer::EventKind::query_result &&
            (event.enclosing_callback == observer::Site::daytime_callback ||
             event.enclosing_callback == observer::Site::nighttime_skip_callback)) {
            auto* trace = find_native_trace(native_traces, native_count,
                event.callback_epoch, event.enclosing_callback, false);
            if (!trace || !event.query_label || !event.world_label ||
                !event.clock_label || !stable_snapshot(event)) {
                native_association_missing = true;
                native_unstable = native_unstable || !stable_snapshot(event);
                continue;
            }
            const PhasePair pair{event.world_label, event.clock_label, 0};
            if (trace->pair_set &&
                (trace->pair.world_label != pair.world_label ||
                 trace->pair.clock_label != pair.clock_label)) {
                native_association_missing = true;
                continue;
            }
            trace->pair = pair;
            trace->pair_set = true;
            trace->query = event.sequence;
        }

        const bool native_helper =
            event.kind == observer::EventKind::updater_before ||
            event.kind == observer::EventKind::updater_after ||
            event.kind == observer::EventKind::tick_before ||
            event.kind == observer::EventKind::tick_after ||
            event.kind == observer::EventKind::scale_before ||
            event.kind == observer::EventKind::scale_after;
        if (!native_helper) continue;
        if (event.enclosing_callback != observer::Site::daytime_callback &&
            event.enclosing_callback != observer::Site::nighttime_skip_callback) {
            native_association_missing = true;
            continue;
        }
        auto* trace = find_native_trace(native_traces, native_count,
            event.callback_epoch, event.enclosing_callback, false);
        if (!trace || !trace->pair_set || !event.world_label ||
            !event.clock_label || event.world_label != trace->pair.world_label ||
            event.clock_label != trace->pair.clock_label) {
            native_association_missing = true;
            continue;
        }
        if (!stable_snapshot(event)) native_unstable = true;
        switch (event.kind) {
        case observer::EventKind::updater_before:
            if (trace->updater_before) report.reason = PhaseReason::callback_order;
            trace->updater_before = event.sequence;
            break;
        case observer::EventKind::updater_after:
            if (trace->updater_after) report.reason = PhaseReason::callback_order;
            trace->updater_after = event.sequence;
            break;
        case observer::EventKind::tick_before:
            if (trace->tick_before) report.reason = PhaseReason::callback_order;
            trace->tick_before = event.sequence;
            break;
        case observer::EventKind::tick_after:
            if (trace->tick_after) report.reason = PhaseReason::callback_order;
            trace->tick_after = event.sequence;
            break;
        case observer::EventKind::scale_before:
            if (trace->scale_before) report.reason = PhaseReason::callback_order;
            trace->scale_before = event.sequence;
            break;
        case observer::EventKind::scale_after:
            if (trace->scale_after) report.reason = PhaseReason::callback_order;
            trace->scale_after = event.sequence;
            break;
        default:
            break;
        }
        if (report.reason == PhaseReason::callback_order) return report;
    }

    for (const auto& event : adapter_events) {
        const bool watched = event.callback_site == observer::Site::daytime_callback ||
            event.callback_site == observer::Site::nighttime_skip_callback;
        if (event.kind == PhaseEventKind::callback_enter && watched) {
            auto* trace = find_adapter_trace(adapter_traces, adapter_count,
                event.callback_id, event.callback_site, true);
            if (!trace || trace->enter || event.active_callbacks != 1 ||
                event.nesting_depth != 1) {
                report.reason = PhaseReason::callback_overlap;
                return report;
            }
            trace->enter = event.sequence;
        }
        if (event.kind == PhaseEventKind::callback_exit && watched) {
            auto* trace = find_adapter_trace(adapter_traces, adapter_count,
                event.callback_id, event.callback_site, false);
            if (!trace || trace->exit) {
                report.reason = PhaseReason::callback_order;
                return report;
            }
            trace->exit = event.sequence;
        }
        if (event.kind == PhaseEventKind::query_ready && watched) {
            auto* trace = find_adapter_trace(adapter_traces, adapter_count,
                event.callback_id, event.callback_site, false);
            if (!trace || !event.world_label || !event.clock_label ||
                !event.world_scope) {
                adapter_association_missing = true;
                continue;
            }
            const PhasePair pair{event.world_label, event.clock_label,
                                 event.world_scope};
            if (trace->pair_set && trace->pair != pair) {
                adapter_association_missing = true;
                continue;
            }
            trace->pair = pair;
            trace->pair_set = true;
            trace->query = event.sequence;
        }
        if (event.kind == PhaseEventKind::callback_enter ||
            event.kind == PhaseEventKind::callback_exit ||
            event.kind == PhaseEventKind::query_ready) continue;

        const bool adapter_helper =
            event.kind == PhaseEventKind::updater_before ||
            event.kind == PhaseEventKind::updater_after ||
            event.kind == PhaseEventKind::tick_before ||
            event.kind == PhaseEventKind::tick_after ||
            event.kind == PhaseEventKind::scale_before ||
            event.kind == PhaseEventKind::scale_after;
        if (!adapter_helper) continue;
        if (!watched) {
            adapter_association_missing = true;
            continue;
        }
        auto* trace = find_adapter_trace(adapter_traces, adapter_count,
            event.callback_id, event.callback_site, false);
        const PhasePair pair{event.world_label, event.clock_label,
                             event.world_scope};
        if (!trace || !trace->pair_set || pair != trace->pair) {
            adapter_association_missing = true;
            continue;
        }
        switch (event.kind) {
        case PhaseEventKind::updater_before:
            if (trace->updater_before) report.reason = PhaseReason::callback_order;
            trace->updater_before = event.sequence;
            break;
        case PhaseEventKind::updater_after:
            if (trace->updater_after) report.reason = PhaseReason::callback_order;
            trace->updater_after = event.sequence;
            break;
        case PhaseEventKind::tick_before:
            if (trace->tick_before) report.reason = PhaseReason::callback_order;
            trace->tick_before = event.sequence;
            break;
        case PhaseEventKind::tick_after:
            if (trace->tick_after) report.reason = PhaseReason::callback_order;
            trace->tick_after = event.sequence;
            break;
        case PhaseEventKind::scale_before:
            if (trace->scale_before) report.reason = PhaseReason::callback_order;
            trace->scale_before = event.sequence;
            break;
        case PhaseEventKind::scale_after:
            if (trace->scale_after) report.reason = PhaseReason::callback_order;
            trace->scale_after = event.sequence;
            break;
        default:
            break;
        }
        if (report.reason == PhaseReason::callback_order) return report;
    }

    if (native_unstable) {
        report.reason = PhaseReason::unstable_clock;
        return report;
    }
    if (native_association_missing || adapter_association_missing) {
        report.reason = PhaseReason::association_unavailable;
        return report;
    }

    std::array<const NativeCallbackTrace*, 256> native_days{};
    std::array<const NativeCallbackTrace*, 256> native_nights{};
    std::size_t native_days_count = 0;
    std::size_t native_nights_count = 0;
    for (std::size_t i = 0; i < native_count; ++i) {
        const auto& trace = native_traces[i];
        if (native_day_complete(trace) && native_pair_is_stable(trace, observer_events))
            native_days[native_days_count++] = &trace;
        if (native_night_complete(trace) && native_pair_is_stable(trace, observer_events))
            native_nights[native_nights_count++] = &trace;
    }

    std::array<const AdapterCallbackTrace*, 256> adapter_days{};
    std::array<const AdapterCallbackTrace*, 256> adapter_nights{};
    std::size_t adapter_days_count = 0;
    std::size_t adapter_nights_count = 0;
    for (std::size_t i = 0; i < adapter_count; ++i) {
        const auto& trace = adapter_traces[i];
        if (adapter_day_complete(trace)) adapter_days[adapter_days_count++] = &trace;
        if (adapter_night_complete(trace)) adapter_nights[adapter_nights_count++] = &trace;
    }
    report.daytime_callbacks = native_days_count;
    report.nighttime_callbacks = native_nights_count;
    report.scale_writes = static_cast<std::size_t>(statistics.scale_writer_calls);

    if (!native_days_count || !adapter_days_count) {
        report.reason = PhaseReason::missing_daytime;
        return report;
    }
    if (!native_nights_count || !adapter_nights_count) {
        report.reason = PhaseReason::missing_nighttime;
        return report;
    }
    if (!statistics.scale_writer_calls || !adapter_nights_count) {
        report.reason = PhaseReason::missing_scale_path;
        return report;
    }
    if (native_days_count != adapter_days_count ||
        native_nights_count != adapter_nights_count ||
        statistics.scale_writer_calls != adapter_nights_count) {
        report.reason = PhaseReason::association_unavailable;
        return report;
    }

    bool shared_native_pair = false;
    for (std::size_t i = 0; i < native_days_count && !shared_native_pair; ++i) {
        for (std::size_t j = 0; j < native_nights_count; ++j) {
            if (same_native_pair(*native_days[i], *native_nights[j])) {
                shared_native_pair = true;
                break;
            }
        }
    }
    if (!shared_native_pair) {
        report.reason = PhaseReason::no_shared_world_clock;
        return report;
    }

    for (std::size_t i = 0; i < adapter_days_count; ++i) {
        for (std::size_t j = 0; j < adapter_nights_count; ++j) {
            if (!same_adapter_pair(*adapter_days[i], *adapter_nights[j])) continue;
            if (!add_pair(report, adapter_days[i]->pair)) {
                report.reason = PhaseReason::scope_unavailable;
                return report;
            }
        }
    }
    if (!report.accepted_pair_count) {
        report.reason = PhaseReason::no_shared_world_clock;
        return report;
    }

    report.status = PhaseStatus::accepted;
    report.reason = PhaseReason::none;
    return report;
}

bool RuntimeAdapter::start(observer::ReadOnlyObserver& observer,
                           const StartOptions& options) noexcept {
    if (options.mode == Mode::disabled || !options.explicitly_enabled) return true;
    State expected = State::stopped;
    if (!state_.compare_exchange_strong(expected, State::starting,
            std::memory_order_acq_rel, std::memory_order_acquire)) return false;
    if ((options.mode != Mode::observe_only &&
         options.mode != Mode::command_writes) ||
        !options.observation.image_base || !options.host.read_snapshot ||
        (options.mode == Mode::command_writes &&
         (!options.host.write_hour_fields ||
          !options.host.time_capability.check))) {
        state_.store(State::failed, std::memory_order_release);
        return false;
    }

    RuntimeAdapter* empty = nullptr;
    if (!active_.compare_exchange_strong(empty, this,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
        state_.store(State::failed, std::memory_order_release);
        return false;
    }
    explicitly_enabled_.store(true, std::memory_order_release);
    mode_.store(options.mode, std::memory_order_release);
    host_ = options.host;
    observer_ = &observer;
    observer_readers_ = options.observation.readers;
    image_base_ = options.observation.image_base;
    const auto salt_seed = static_cast<std::uint64_t>(image_base_) ^
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)) ^
        (observer_readers_.monotonic_ms
            ? observer_readers_.monotonic_ms(observer_readers_.context) : 0) ^
        0x2df39b5c76a108e4ULL;
    label_salt_ = mix_label(salt_seed);

    if (!install(observer, options)) {
        mode_.store(Mode::disabled, std::memory_order_release);
        state_.store(State::failed, std::memory_order_release);
        return false;
    }
    hooks_ready_.store(true, std::memory_order_release);
    state_.store(State::running, std::memory_order_release);
    PhaseReport waiting{};
    waiting.status = PhaseStatus::waiting;
    waiting.reason = PhaseReason::sample_active;
    store_phase_report(waiting);
    return true;
}

bool RuntimeAdapter::install(observer::ReadOnlyObserver& observer,
                             const StartOptions& options) noexcept {
    if (!options.observation.hooks.create || !options.observation.hooks.enable ||
        !options.observation.hooks.disable || !options.observation.hooks.remove ||
        !observer_readers_.read_memory || !observer_readers_.thread_id ||
        !observer_readers_.monotonic_ms || !observer_readers_.validate_image) return false;
    underlying_hooks_ = options.observation.hooks;
    for (std::size_t i = 0; i < observer::site_count; ++i) {
        if (hook_rvas[i] > UINTPTR_MAX - image_base_) return false;
        targets_[i] = image_base_ + hook_rvas[i];
    }
    auto wrapped = options.observation;
    wrapped.hooks = observer::HookBackend{
        this, &RuntimeAdapter::create_bridge, &RuntimeAdapter::enable_bridge,
        &RuntimeAdapter::disable_bridge, &RuntimeAdapter::remove_bridge
    };
    return observer.install(wrapped) && observer.ready();
}

bool RuntimeAdapter::stop() noexcept {
    const auto prior_state = state_.exchange(State::stopping,
        std::memory_order_acq_rel);
    if (prior_state == State::stopped) {
        state_.store(State::stopped, std::memory_order_release);
        return true;
    }
    mode_.store(Mode::disabled, std::memory_order_release);
    phase_ready_.store(false, std::memory_order_release);
    hooks_ready_.store(false, std::memory_order_release);
    bool complete = true;
    if (observer_) complete = observer_->stop();
    std::array<WorldScope, max_world_scopes> scopes{};
    std::size_t count = 0;
    {
        std::lock_guard lock(scope_mutex_);
        for (auto& state : scopes_) {
            if (state.occupied && count < scopes.size()) scopes[count++] = state.scope;
            state = {};
        }
    }
    for (std::size_t i = 0; i < count; ++i)
        (void)queue_.retire_world_scope(scopes[i]);
    state_.store(complete ? State::stopped : State::failed,
        std::memory_order_release);
    return complete;
}

PhaseReport RuntimeAdapter::refresh_phase_observation() noexcept {
    const auto finish = [this](const PhaseReport& report, bool accepted) noexcept {
        store_phase_report(report);
        phase_ready_.store(accepted, std::memory_order_release);
        return report;
    };
    if (!observer_ || state() != State::running || !observer_->ready()) {
        PhaseReport report{};
        report.status = PhaseStatus::rejected;
        report.reason = PhaseReason::hooks_not_ready;
        return finish(report, false);
    }
    if (observer_->sampling()) {
        PhaseReport report{};
        report.status = PhaseStatus::waiting;
        report.reason = PhaseReason::sample_active;
        return finish(report, false);
    }
    const auto statistics = observer_->statistics();
    if (statistics.sampling) {
        PhaseReport report{};
        report.status = PhaseStatus::waiting;
        report.reason = PhaseReason::sample_active;
        return finish(report, false);
    }
    if (events_dropped_.load(std::memory_order_acquire) != 0 ||
        events_recorded_.load(std::memory_order_acquire) > runtime_event_capacity) {
        PhaseReport report{};
        report.status = PhaseStatus::rejected;
        report.reason = PhaseReason::event_loss;
        return finish(report, false);
    }
    const auto observer_count = observer_->copy_events(observer_events_);
    const auto runtime_count = copy_phase_events(runtime_events_);
    std::sort(observer_events_.begin(), observer_events_.begin() +
        static_cast<std::ptrdiff_t>(observer_count),
        [](const observer::Event& left, const observer::Event& right) {
            return left.sequence < right.sequence;
        });
    std::sort(runtime_events_.begin(), runtime_events_.begin() +
        static_cast<std::ptrdiff_t>(runtime_count),
        [](const PhaseEvent& left, const PhaseEvent& right) {
            return left.sequence < right.sequence;
        });
    auto report = assess_phase(statistics,
        std::span<const observer::Event>(observer_events_.data(), observer_count),
        std::span<const PhaseEvent>(runtime_events_.data(), runtime_count));
    if (observer_count != statistics.events_recorded ||
        runtime_count != events_recorded_.load(std::memory_order_acquire)) {
        report.status = PhaseStatus::rejected;
        report.reason = PhaseReason::event_loss;
    } else if (phase_fault_.load(std::memory_order_acquire) ||
               unassociated_writer_.load(std::memory_order_acquire)) {
        report.status = PhaseStatus::invalidated;
        report.reason = unassociated_writer_.load(std::memory_order_relaxed)
            ? PhaseReason::unassociated_writer : PhaseReason::callback_overlap;
    }
    const bool accepted = report.status == PhaseStatus::accepted &&
        !phase_fault_.load(std::memory_order_acquire) &&
        !unassociated_writer_.load(std::memory_order_acquire);
    return finish(report, accepted);
}
bool RuntimeAdapter::enable_command_writes() noexcept {
    if (state() != State::running ||
        !explicitly_enabled_.load(std::memory_order_acquire) ||
        !phase_ready_.load(std::memory_order_acquire) ||
        phase_fault_.load(std::memory_order_acquire) ||
        unassociated_writer_.load(std::memory_order_acquire) ||
        !host_.read_snapshot || !host_.write_hour_fields ||
        !host_.time_capability.check || !observer_ || !observer_->ready()) return false;
    mode_.store(Mode::command_writes, std::memory_order_release);
    return true;
}

void RuntimeAdapter::disable_command_writes() noexcept {
    if (state() != State::running) return;
    mode_.store(Mode::observe_only, std::memory_order_release);
    phase_ready_.store(false, std::memory_order_release);
    auto current = retirement_epoch_.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max() &&
        !retirement_epoch_.compare_exchange_weak(current, current + 1,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {}
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        invalidate_phase(PhaseReason::scope_unavailable);
        return;
    }
    std::array<WorldScope, max_world_scopes> scopes{};
    std::size_t count = 0;
    {
        std::lock_guard lock(scope_mutex_);
        for (const auto& state : scopes_) {
            if (state.occupied && count < scopes.size()) scopes[count++] = state.scope;
        }
    }
    for (std::size_t i = 0; i < count; ++i)
        (void)retire_world_scope(scopes[i]);
}
State RuntimeAdapter::state() const noexcept {
    return state_.load(std::memory_order_acquire);
}
Mode RuntimeAdapter::mode() const noexcept {
    return mode_.load(std::memory_order_acquire);
}
bool RuntimeAdapter::hooks_ready() const noexcept {
    return hooks_ready_.load(std::memory_order_acquire);
}
bool RuntimeAdapter::command_backend_ready() const noexcept {
    return state() == State::running && mode() == Mode::command_writes &&
        explicitly_enabled_.load(std::memory_order_acquire) &&
        hooks_ready() && phase_ready_.load(std::memory_order_acquire) &&
        !phase_fault_.load(std::memory_order_acquire) &&
        !unassociated_writer_.load(std::memory_order_acquire) &&
        host_.read_snapshot && host_.write_hour_fields &&
        host_.time_capability.check != nullptr;
}
bool RuntimeAdapter::phase_scope_ready(WorldScope scope) const noexcept {
    return command_backend_ready() && phase_scope_accepted(scope);
}
ApplyStatus RuntimeAdapter::last_apply_status() const noexcept {
    return last_apply_status_.load(std::memory_order_acquire);
}
std::size_t RuntimeAdapter::queued_commands() const noexcept {
    return queue_.size();
}
bool RuntimeAdapter::scope_override_active(WorldScope scope) const noexcept {
    if (!scope) return false;
    std::lock_guard lock(scope_mutex_);
    for (const auto& state : scopes_) {
        if (state.occupied && state.scope == scope)
            return state.override.active;
    }
    return false;
}

std::size_t RuntimeAdapter::copy_phase_events(
    std::span<PhaseEvent> destination) const noexcept {
    std::size_t copied = 0;
    for (std::size_t i = 0; i < runtime_event_capacity && copied < destination.size(); ++i) {
        if (event_locks_[i].test_and_set(std::memory_order_acquire)) continue;
        const auto event = events_[i];
        event_locks_[i].clear(std::memory_order_release);
        if (event.sequence) destination[copied++] = event;
    }
    return copied;
}
std::uint64_t RuntimeAdapter::phase_events_dropped() const noexcept {
    return events_dropped_.load(std::memory_order_acquire);
}

SubmitStatus RuntimeAdapter::set_hour(const flight::Identity& identity,
    std::uint32_t owner, WorldScope scope, std::uint8_t hour) noexcept {
    if (!command_backend_ready() || !phase_scope_accepted(scope))
        return SubmitStatus::denied;
    const auto submitted = queue_.enqueue_hour(identity, owner, scope, hour,
                                                host_.time_capability);
    if (submitted != SubmitStatus::queued) return submitted;
    if (!phase_scope_accepted(scope)) {
        (void)queue_.retire_world_scope(scope);
        return SubmitStatus::denied;
    }
    return SubmitStatus::queued;
}
SubmitStatus RuntimeAdapter::set_mode(const flight::Identity& identity,
    std::uint32_t owner, WorldScope scope, WorldTimeMode requested_mode) noexcept {
    if (!command_backend_ready() || !phase_scope_accepted(scope))
        return SubmitStatus::denied;
    const auto submitted = queue_.enqueue_mode(identity, owner, scope,
        requested_mode, host_.time_capability);
    if (submitted != SubmitStatus::queued) return submitted;
    if (!phase_scope_accepted(scope)) {
        (void)queue_.retire_world_scope(scope);
        return SubmitStatus::denied;
    }
    return SubmitStatus::queued;
}

std::size_t RuntimeAdapter::retire_world_scope(WorldScope scope) noexcept {
    if (!scope) return 0;
    auto current = retirement_epoch_.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max() &&
        !retirement_epoch_.compare_exchange_weak(current, current + 1,
            std::memory_order_acq_rel, std::memory_order_relaxed)) {}
    if (current == std::numeric_limits<std::uint64_t>::max()) {
        invalidate_phase(PhaseReason::scope_unavailable);
        return 0;
    }
    clear_scope(scope);
    return queue_.retire_world_scope(scope);
}

void* RuntimeAdapter::bridge_for(std::size_t index) const noexcept {
    switch (static_cast<observer::Site>(index)) {
    case observer::Site::query_begin:
        return reinterpret_cast<void*>(&RuntimeAdapter::query_bridge);
    case observer::Site::daytime_callback:
        return reinterpret_cast<void*>(&RuntimeAdapter::daytime_bridge);
    case observer::Site::nighttime_skip_callback:
        return reinterpret_cast<void*>(&RuntimeAdapter::nighttime_bridge);
    case observer::Site::scale_writer:
        return reinterpret_cast<void*>(&RuntimeAdapter::scale_bridge);
    case observer::Site::daynight_updater:
        return reinterpret_cast<void*>(&RuntimeAdapter::updater_bridge);
    case observer::Site::clock_tick:
        return reinterpret_cast<void*>(&RuntimeAdapter::tick_bridge);
    case observer::Site::count:
        break;
    }
    return nullptr;
}

bool RuntimeAdapter::create_bridge(void* context, std::uintptr_t target,
    void* observer_detour, void** original) noexcept {
    auto* self = static_cast<RuntimeAdapter*>(context);
    if (!self || !observer_detour || !original ||
        self->next_create_ >= observer::site_count) return false;
    const auto index = self->next_create_++;
    if (target != self->targets_[index]) return false;
    self->observer_detours_[index] = observer_detour;
    void* native_original = nullptr;
    const bool created = self->underlying_hooks_.create(
        self->underlying_hooks_.context, target, self->bridge_for(index),
        &native_original);
    if (native_original) {
        self->native_originals_[index] = native_original;
        *original = native_original;
        self->created_[index] = created;
    }
    return created && native_original;
}

bool RuntimeAdapter::enable_bridge(void* context, std::uintptr_t target) noexcept {
    auto* self = static_cast<RuntimeAdapter*>(context);
    if (!self) return false;
    const auto index = find_target_index(self->targets_, target);
    if (index == observer::site_count || !self->created_[index]) return false;
    const bool enabled = self->underlying_hooks_.enable(
        self->underlying_hooks_.context, target);
    if (enabled) self->enabled_[index] = true;
    return enabled;
}

bool RuntimeAdapter::disable_bridge(void* context, std::uintptr_t target) noexcept {
    auto* self = static_cast<RuntimeAdapter*>(context);
    if (!self) return false;
    const auto index = find_target_index(self->targets_, target);
    if (index == observer::site_count) return false;
    const bool disabled = self->underlying_hooks_.disable(
        self->underlying_hooks_.context, target);
    if (disabled) self->enabled_[index] = false;
    return disabled;
}

bool RuntimeAdapter::remove_bridge(void* context, std::uintptr_t target) noexcept {
    auto* self = static_cast<RuntimeAdapter*>(context);
    if (!self) return false;
    const auto index = find_target_index(self->targets_, target);
    if (index == observer::site_count) return false;
    const bool removed = self->underlying_hooks_.remove(
        self->underlying_hooks_.context, target);
    if (removed) self->created_[index] = false;
    return removed;
}

bool RuntimeAdapter::read_query_memory(std::uintptr_t address, void* output,
    std::size_t size) noexcept {
    auto* self = query_reader_;
    return self && self->observer_readers_.read_memory && output &&
        self->observer_readers_.read_memory(address, output, size);
}

void XHL_NATIVE_TIME_CALL RuntimeAdapter::query_bridge(void* context, void* output,
    std::uint32_t descriptor_size) noexcept {
    auto* self = active_.load(std::memory_order_acquire);
    if (self) self->forward_query(context, output, descriptor_size);
}
void XHL_NATIVE_TIME_CALL RuntimeAdapter::daytime_bridge(void* context) noexcept {
    auto* self = active_.load(std::memory_order_acquire);
    if (self) self->forward_callback(observer::Site::daytime_callback, context);
}
void XHL_NATIVE_TIME_CALL RuntimeAdapter::nighttime_bridge(void* context) noexcept {
    auto* self = active_.load(std::memory_order_acquire);
    if (self) self->forward_callback(observer::Site::nighttime_skip_callback, context);
}
void XHL_NATIVE_TIME_CALL RuntimeAdapter::scale_bridge(void* clock, float scale,
    std::int64_t now) noexcept {
    auto* self = active_.load(std::memory_order_acquire);
    if (self) self->forward_scale(clock, scale, now);
}
void XHL_NATIVE_TIME_CALL RuntimeAdapter::updater_bridge(void* clock,
    std::uint64_t day, std::uint64_t night, std::int64_t now) noexcept {
    auto* self = active_.load(std::memory_order_acquire);
    if (self) self->forward_updater(clock, day, night, now);
}
void XHL_NATIVE_TIME_CALL RuntimeAdapter::tick_bridge(void* clock,
    std::int64_t now) noexcept {
    auto* self = active_.load(std::memory_order_acquire);
    if (self) self->forward_tick(clock, now);
}

bool RuntimeAdapter::writer_read_current(void* context,
    CallbackSnapshot& output) noexcept {
    auto* writer = static_cast<WriterFrame*>(context);
    if (!writer || !writer->adapter || !writer->callback ||
        !writer->callback->association_valid) return false;
    auto& frame = *writer->callback;
    if (frame.retirement_epoch != writer->adapter->retirement_epoch_.load(
            std::memory_order_acquire)) return false;
    if (!writer->adapter->read_host_snapshot(frame.query, output,
            frame.callback_epoch)) return false;
    return output.world == frame.snapshot.world &&
        output.world_scope == frame.snapshot.world_scope &&
        output.callback_epoch == frame.snapshot.callback_epoch;
}

bool RuntimeAdapter::writer_call_scale_original(void* context, float scale,
    std::int64_t now) noexcept {
    auto* writer = static_cast<WriterFrame*>(context);
    return writer && writer->adapter && writer->callback &&
        writer->adapter->call_native_scale_once(*writer, scale, now);
}

bool RuntimeAdapter::writer_write_hour_once(void* context,
    const HourFieldSequence& writes) noexcept {
    auto* writer = static_cast<WriterFrame*>(context);
    return writer && writer->adapter && writer->callback &&
        writer->adapter->call_native_hour_once(*writer, writes);
}

void RuntimeAdapter::forward_query(void* context, void* output,
    std::uint32_t descriptor_size) noexcept {
    const auto index = index_of(observer::Site::query_begin);
    const auto observer_detour = reinterpret_cast<QueryBegin>(observer_detours_[index]);
    auto* frame = current_frame_;
    const bool scoped = state() == State::running && frame && frame->adapter == this &&
        (frame->site == observer::Site::daytime_callback ||
         frame->site == observer::Site::nighttime_skip_callback) &&
        frame->query.callback_context == reinterpret_cast<std::uintptr_t>(context);
    if (!scoped) {
        const auto original = reinterpret_cast<QueryBegin>(native_originals_[index]);
        if (original) original(context, output, descriptor_size);
        else if (observer_detour) observer_detour(context, output, descriptor_size);
        return;
    }

    if (observer_detour) observer_detour(context, output, descriptor_size);
    else if (const auto original = reinterpret_cast<QueryBegin>(native_originals_[index]))
        original(context, output, descriptor_size);
    capture_query(context, output, descriptor_size);
}

void RuntimeAdapter::forward_callback(observer::Site site, void* context) noexcept {
    const auto index = index_of(site);
    const auto observer_detour = reinterpret_cast<NativeCallback>(observer_detours_[index]);
    const auto native_original = reinterpret_cast<NativeCallback>(native_originals_[index]);
    if (state() != State::running || !observer_detour) {
        if (observer_detour) observer_detour(context);
        else if (native_original) native_original(context);
        return;
    }

    CallbackFrame frame{};
    frame.adapter = this;
    frame.previous = current_frame_;
    frame.query.callback_context = reinterpret_cast<std::uintptr_t>(context);
    frame.callback_id = next_callback_id_.fetch_add(1, std::memory_order_relaxed);
    frame.callback_epoch = frame.callback_id;
    frame.retirement_epoch = retirement_epoch_.load(std::memory_order_acquire);
    frame.site = site;
    frame.nesting_depth = frame.previous && frame.previous->adapter == this
        ? static_cast<std::uint16_t>(std::min<unsigned>(
            static_cast<unsigned>(frame.previous->nesting_depth) + 1u, UINT16_MAX)) : 1;
    const auto active_before = active_callbacks_.fetch_add(1, std::memory_order_acq_rel);
    const auto active_now = active_before + 1;
    if (active_before != 0 || frame.nesting_depth != 1) {
        phase_fault_.store(true, std::memory_order_release);
        phase_ready_.store(false, std::memory_order_release);
    }
    current_frame_ = &frame;
    PhaseEvent enter{};
    enter.callback_id = frame.callback_id;
    enter.callback_site = site;
    enter.site = site;
    enter.thread_id = observer_readers_.thread_id
        ? observer_readers_.thread_id(observer_readers_.context) : 0;
    enter.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        active_now, UINT32_MAX));
    enter.nesting_depth = frame.nesting_depth;
    enter.kind = PhaseEventKind::callback_enter;
    record(enter);

    observer_detour(context);
    if (site == observer::Site::daytime_callback)
        run_callback_time_command(frame);

    PhaseEvent exit{};
    exit.callback_id = frame.callback_id;
    exit.callback_site = site;
    exit.site = site;
    exit.world_label = frame.world_label;
    exit.clock_label = frame.clock_label;
    exit.world_scope = frame.scope;
    exit.thread_id = observer_readers_.thread_id
        ? observer_readers_.thread_id(observer_readers_.context) : 0;
    exit.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
    exit.nesting_depth = frame.nesting_depth;
    exit.kind = PhaseEventKind::callback_exit;
    record(exit);
    current_frame_ = frame.previous;
    active_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
}

void RuntimeAdapter::forward_scale(void* clock, float scale,
    std::int64_t now) noexcept {
    const auto index = index_of(observer::Site::scale_writer);
    const auto observer_detour = reinterpret_cast<ScaleWriter>(observer_detours_[index]);
    const auto native_original = reinterpret_cast<ScaleWriter>(native_originals_[index]);
    auto* frame = current_frame_;
    bool associated = state() == State::running && frame && frame->adapter == this &&
        frame->query_ready && frame->association_valid &&
        frame->query.clock == reinterpret_cast<std::uintptr_t>(clock);
    if (associated && host_.read_snapshot) {
        CallbackSnapshot snapshot{};
        associated = read_host_snapshot(frame->query, snapshot, frame->callback_epoch) &&
            snapshot.world == frame->query.world &&
            (!frame->scope || frame->scope == snapshot.world_scope);
        if (associated) frame->scope = snapshot.world_scope;
    }
    std::uint64_t epoch = 0;
    if (associated && mode() == Mode::command_writes)
        associated = bump_writer_epoch(frame->scope, epoch);
    if (!associated) {
        if (state() == State::running) {
            unassociated_writer_.store(true, std::memory_order_release);
            invalidate_phase(PhaseReason::unassociated_writer);
        }
    } else {
        PhaseEvent before{};
        before.callback_id = frame->callback_id;
        before.callback_site = frame->site;
        before.site = observer::Site::scale_writer;
        before.world_label = frame->world_label;
        before.clock_label = frame->clock_label;
        before.world_scope = frame->scope;
        before.thread_id = observer_readers_.thread_id
            ? observer_readers_.thread_id(observer_readers_.context) : 0;
        before.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
        before.nesting_depth = frame->nesting_depth;
        before.kind = PhaseEventKind::scale_before;
        record(before);
    }
    if (observer_detour) observer_detour(clock, scale, now);
    else if (native_original) native_original(clock, scale, now);
    if (associated) {
        PhaseEvent after{};
        after.callback_id = frame->callback_id;
        after.callback_site = frame->site;
        after.site = observer::Site::scale_writer;
        after.world_label = frame->world_label;
        after.clock_label = frame->clock_label;
        after.world_scope = frame->scope;
        after.thread_id = observer_readers_.thread_id
            ? observer_readers_.thread_id(observer_readers_.context) : 0;
        after.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
        after.nesting_depth = frame->nesting_depth;
        after.kind = PhaseEventKind::scale_after;
        record(after);
    }
    (void)epoch;
}

void RuntimeAdapter::forward_updater(void* clock, std::uint64_t day,
    std::uint64_t night, std::int64_t now) noexcept {
    const auto index = index_of(observer::Site::daynight_updater);
    const auto observer_detour = reinterpret_cast<DaynightUpdater>(observer_detours_[index]);
    const auto native_original = reinterpret_cast<DaynightUpdater>(native_originals_[index]);
    auto* frame = current_frame_;
    const bool associated = state() == State::running && frame && frame->adapter == this &&
        frame->query_ready && frame->association_valid &&
        frame->query.clock == reinterpret_cast<std::uintptr_t>(clock);
    if (!associated) {
        if (state() == State::running) invalidate_phase(PhaseReason::association_unavailable);
    } else {
        PhaseEvent before{};
        before.callback_id = frame->callback_id;
        before.callback_site = frame->site;
        before.site = observer::Site::daynight_updater;
        before.world_label = frame->world_label;
        before.clock_label = frame->clock_label;
        before.world_scope = frame->scope;
        before.thread_id = observer_readers_.thread_id
            ? observer_readers_.thread_id(observer_readers_.context) : 0;
        before.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
        before.nesting_depth = frame->nesting_depth;
        before.kind = PhaseEventKind::updater_before;
        record(before);
    }
    if (observer_detour) observer_detour(clock, day, night, now);
    else if (native_original) native_original(clock, day, night, now);
    if (associated) {
        PhaseEvent after{};
        after.callback_id = frame->callback_id;
        after.callback_site = frame->site;
        after.site = observer::Site::daynight_updater;
        after.world_label = frame->world_label;
        after.clock_label = frame->clock_label;
        after.world_scope = frame->scope;
        after.thread_id = observer_readers_.thread_id
            ? observer_readers_.thread_id(observer_readers_.context) : 0;
        after.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
        after.nesting_depth = frame->nesting_depth;
        after.kind = PhaseEventKind::updater_after;
        record(after);
    }
}

void RuntimeAdapter::forward_tick(void* clock, std::int64_t now) noexcept {
    const auto index = index_of(observer::Site::clock_tick);
    const auto observer_detour = reinterpret_cast<ClockTick>(observer_detours_[index]);
    const auto native_original = reinterpret_cast<ClockTick>(native_originals_[index]);
    auto* frame = current_frame_;
    const bool associated = state() == State::running && frame && frame->adapter == this &&
        frame->query_ready && frame->association_valid &&
        frame->query.clock == reinterpret_cast<std::uintptr_t>(clock);
    if (!associated) {
        if (state() == State::running) invalidate_phase(PhaseReason::association_unavailable);
    } else {
        PhaseEvent before{};
        before.callback_id = frame->callback_id;
        before.callback_site = frame->site;
        before.site = observer::Site::clock_tick;
        before.world_label = frame->world_label;
        before.clock_label = frame->clock_label;
        before.world_scope = frame->scope;
        before.thread_id = observer_readers_.thread_id
            ? observer_readers_.thread_id(observer_readers_.context) : 0;
        before.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
        before.nesting_depth = frame->nesting_depth;
        before.kind = PhaseEventKind::tick_before;
        record(before);
    }
    if (observer_detour) observer_detour(clock, now);
    else if (native_original) native_original(clock, now);
    if (associated) {
        frame->native_now = now;
        frame->native_now_known = true;
        frame->query.native_now = now;
        frame->query.native_now_known = true;
    }
    if (associated) {
        PhaseEvent after{};
        after.callback_id = frame->callback_id;
        after.callback_site = frame->site;
        after.site = observer::Site::clock_tick;
        after.world_label = frame->world_label;
        after.clock_label = frame->clock_label;
        after.world_scope = frame->scope;
        after.thread_id = observer_readers_.thread_id
            ? observer_readers_.thread_id(observer_readers_.context) : 0;
        after.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
        after.nesting_depth = frame->nesting_depth;
        after.kind = PhaseEventKind::tick_after;
        record(after);
    }
}

void RuntimeAdapter::capture_query(void* context, void* output,
    std::uint32_t descriptor_size) noexcept {
    auto* frame = current_frame_;
    if (!frame || frame->adapter != this ||
        frame->query.callback_context != reinterpret_cast<std::uintptr_t>(context) ||
        !output) return;
    NativeQueryView query{};
    query.callback_context = reinterpret_cast<std::uintptr_t>(context);
    query.query_output = reinterpret_cast<std::uintptr_t>(output);
    query.descriptor_size = descriptor_size;
    query.native_now = frame->native_now;
    query.native_now_known = frame->native_now_known;
    std::uintptr_t world = 0;
    auto* previous_reader = query_reader_;
    query_reader_ = this;
    const bool world_ok = flight::read_query_world(
        &RuntimeAdapter::read_query_memory, query.query_output, world);
    query_reader_ = previous_reader;
    query.world = world;
    const bool clock_ok = read_offset(observer_readers_.read_memory, query.query_output, 0x10, query.clock) && query.clock;

    const bool changed_query = frame->query_ready &&
        (frame->query.world != query.world || frame->query.clock != query.clock);
    frame->query = query;
    frame->query_ready = world_ok && clock_ok;
    frame->association_valid = frame->query_ready && !changed_query;
    frame->world_label = opaque_label(world_domain, query.world);
    frame->clock_label = opaque_label(clock_domain, query.clock);
    frame->snapshot_valid = false;
    if (changed_query) invalidate_phase(PhaseReason::association_unavailable);
    if (frame->query_ready) {
        CallbackSnapshot snapshot{};
        if (!read_host_snapshot(query, snapshot, frame->callback_id) ||
            (frame->scope && frame->scope != snapshot.world_scope)) {
            frame->association_valid = false;
            invalidate_phase(PhaseReason::host_association_unavailable);
        } else {
            frame->scope = snapshot.world_scope;
        }
    } else {
        invalidate_phase(PhaseReason::association_unavailable);
    }

    PhaseEvent event{};
    event.callback_id = frame->callback_id;
    event.callback_site = frame->site;
    event.site = observer::Site::query_begin;
    event.world_label = frame->world_label;
    event.clock_label = frame->clock_label;
    event.world_scope = frame->scope;
    event.thread_id = observer_readers_.thread_id
        ? observer_readers_.thread_id(observer_readers_.context) : 0;
    event.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
    event.nesting_depth = frame->nesting_depth;
    event.kind = PhaseEventKind::query_ready;
    record(event);
}

void RuntimeAdapter::run_callback_time_command(CallbackFrame& frame) noexcept {
    if (!frame.query_ready || !frame.association_valid || !frame.scope ||
        !frame.query.world || !frame.query.clock || !host_.read_snapshot ||
        frame.retirement_epoch != retirement_epoch_.load(std::memory_order_acquire)) {
        invalidate_phase(PhaseReason::association_unavailable);
        return;
    }
    CallbackSnapshot snapshot{};
    if (!read_host_snapshot(frame.query, snapshot, frame.callback_id) ||
        snapshot.world_scope != frame.scope) {
        invalidate_phase(PhaseReason::host_association_unavailable);
        return;
    }
    ScopeState current{};
    if (!scope_state(snapshot.world_scope, true, current) ||
        current.callback_epoch == std::numeric_limits<std::uint64_t>::max()) {
        invalidate_phase(PhaseReason::scope_unavailable);
        return;
    }
    ++current.callback_epoch;
    snapshot.callback_epoch = current.callback_epoch;
    current.scope = snapshot.world_scope;
    current.occupied = true;
    if (!set_scope_state(current)) {
        invalidate_phase(PhaseReason::scope_unavailable);
        return;
    }
    frame.callback_epoch = snapshot.callback_epoch;
    frame.snapshot = snapshot;
    frame.snapshot_valid = true;
    if (mode() != Mode::command_writes || !can_write_for(frame)) return;

    if (!scope_state(snapshot.world_scope, false, current)) return;
    const auto command = queue_.peek_for_world(snapshot.world, snapshot.world_scope);
    if (!command && !current.override.active) return;
    const auto decision = plan_callback(snapshot, command, current.override,
                                        host_.time_capability);
    WriterFrame writer{this, &frame, command.has_value()};
    const CallbackWriters writers{
        &writer, &RuntimeAdapter::writer_read_current,
        &RuntimeAdapter::writer_call_scale_original,
        &RuntimeAdapter::writer_write_hour_once
    };
    const auto applied = apply_plan_once(snapshot, command, current.override,
                                         decision, host_.time_capability, writers);
    last_apply_status_.store(applied.status, std::memory_order_release);
    if (command && applied.acknowledge_command)
        (void)queue_.acknowledge(command->sequence);
    if (frame.retirement_epoch != retirement_epoch_.load(std::memory_order_acquire)) {
        clear_scope(snapshot.world_scope);
        return;
    }
    // A native scale writer can advance writer_epoch during apply_plan_once.
    // Merge callback-owned state into the latest scope value so this older
    // local snapshot cannot erase that ownership fence.
    ScopeState committed{};
    if (!scope_state(snapshot.world_scope, false, committed)) {
        invalidate_phase(PhaseReason::scope_unavailable);
        return;
    }
    committed.callback_epoch = snapshot.callback_epoch;
    committed.override = applied.next_override;
    if (!set_scope_state(committed))
        invalidate_phase(PhaseReason::scope_unavailable);
}

bool RuntimeAdapter::read_host_snapshot(const NativeQueryView& query,
    CallbackSnapshot& output, std::uint64_t callback_epoch) noexcept {
    output = {};
    if (!host_.read_snapshot || !query.world || !query.clock) return false;
    CallbackSnapshot snapshot{};
    if (!host_.read_snapshot(host_.context, query, snapshot) ||
        !snapshot.valid_world() || snapshot.world != query.world ||
        !snapshot.world_scope || !snapshot.clock.valid()) return false;
    if (snapshot.owner_resolution_current && !snapshot.valid_owner()) return false;
    ScopeState state{};
    if (!scope_state(snapshot.world_scope, true, state)) return false;
    snapshot.callback_epoch = callback_epoch ? callback_epoch : state.callback_epoch;
    snapshot.clock.scale_writer_epoch = state.writer_epoch;
    output = snapshot;
    return true;
}

bool RuntimeAdapter::call_native_scale_once(WriterFrame& writer, float scale,
    std::int64_t now) noexcept {
    auto& frame = *writer.callback;
    if (!can_write_for(frame) || frame.retirement_epoch !=
        retirement_epoch_.load(std::memory_order_acquire)) return false;
    CallbackSnapshot current{};
    if (!read_host_snapshot(frame.query, current, frame.callback_epoch) ||
        current.world != frame.snapshot.world ||
        current.world_scope != frame.snapshot.world_scope) return false;
    if (writer.requires_capability &&
        (!current.valid_owner() || !frame.snapshot.owner_identity ||
         !current.owner_identity ||
         *current.owner_identity != *frame.snapshot.owner_identity ||
         current.owner != frame.snapshot.owner ||
         !host_.time_capability.allows(*current.owner_identity))) return false;
    std::uint64_t writer_epoch = 0;
    if (!bump_writer_epoch(current.world_scope, writer_epoch) ||
        frame.retirement_epoch != retirement_epoch_.load(std::memory_order_acquire))
        return false;
    const auto original = reinterpret_cast<ScaleWriter>(
        native_originals_[index_of(observer::Site::scale_writer)]);
    if (!original) return false;
    original(reinterpret_cast<void*>(frame.query.clock), scale, now);
    return true;
}

bool RuntimeAdapter::call_native_hour_once(WriterFrame& writer,
    const HourFieldSequence& writes) noexcept {
    auto& frame = *writer.callback;
    if (!writer.requires_capability || !can_write_for(frame) ||
        !host_.write_hour_fields ||
        frame.retirement_epoch != retirement_epoch_.load(std::memory_order_acquire))
        return false;
    CallbackSnapshot current{};
    if (!read_host_snapshot(frame.query, current, frame.callback_epoch) ||
        current.world != frame.snapshot.world ||
        current.world_scope != frame.snapshot.world_scope ||
        !current.valid_owner() || !frame.snapshot.owner_identity ||
        !current.owner_identity ||
        *current.owner_identity != *frame.snapshot.owner_identity ||
        current.owner != frame.snapshot.owner ||
        !host_.time_capability.allows(*current.owner_identity) ||
        frame.retirement_epoch != retirement_epoch_.load(std::memory_order_acquire))
        return false;
    return host_.write_hour_fields(host_.context, frame.query, writes);
}

bool RuntimeAdapter::can_write_for(const CallbackFrame& frame) const noexcept {
    return state() == State::running && mode() == Mode::command_writes &&
        explicitly_enabled_.load(std::memory_order_acquire) && hooks_ready() &&
        phase_ready_.load(std::memory_order_acquire) &&
        !phase_fault_.load(std::memory_order_acquire) &&
        !unassociated_writer_.load(std::memory_order_acquire) &&
        frame.adapter == this && frame.query_ready && frame.association_valid &&
        frame.native_now_known && frame.query.native_now_known &&
        frame.snapshot_valid && frame.scope &&
        frame.retirement_epoch == retirement_epoch_.load(std::memory_order_acquire) &&
        phase_pair_matches(frame.world_label, frame.clock_label, frame.scope);
}

bool RuntimeAdapter::phase_scope_accepted(WorldScope scope) const noexcept {
    if (!scope || !phase_ready_.load(std::memory_order_acquire)) return false;
    {
        std::lock_guard lock(scope_mutex_);
        for (std::size_t i = 0; i < retired_scope_count_; ++i)
            if (retired_scopes_[i] == scope) return false;
    }
    std::lock_guard lock(phase_mutex_);
    for (std::size_t i = 0; i < phase_report_.accepted_pair_count; ++i)
        if (phase_report_.accepted_pairs[i].world_scope == scope) return true;
    return false;
}

bool RuntimeAdapter::phase_pair_matches(std::uint64_t world_label,
    std::uint64_t clock_label, WorldScope scope) const noexcept {
    if (!world_label || !clock_label || !scope ||
        !phase_ready_.load(std::memory_order_acquire)) return false;
    std::lock_guard lock(phase_mutex_);
    for (std::size_t i = 0; i < phase_report_.accepted_pair_count; ++i) {
        const auto& pair = phase_report_.accepted_pairs[i];
        if (pair.world_label == world_label && pair.clock_label == clock_label &&
            pair.world_scope == scope)
            return true;
    }
    return false;
}

bool RuntimeAdapter::bump_writer_epoch(WorldScope scope,
    std::uint64_t& result) noexcept {
    ScopeState state{};
    if (!scope_state(scope, true, state) ||
        state.writer_epoch == std::numeric_limits<std::uint64_t>::max()) return false;
    ++state.writer_epoch;
    if (!set_scope_state(state)) return false;
    result = state.writer_epoch;
    return true;
}

bool RuntimeAdapter::scope_state(WorldScope scope, bool create,
    ScopeState& output) noexcept {
    if (!scope) return false;
    std::lock_guard lock(scope_mutex_);
    for (std::size_t i = 0; i < retired_scope_count_; ++i)
        if (retired_scopes_[i] == scope) return false;
    ScopeState* free_slot = nullptr;
    for (auto& state : scopes_) {
        if (state.occupied && state.scope == scope) {
            output = state;
            return true;
        }
        if (!state.occupied && !free_slot) free_slot = &state;
    }
    if (!create || !free_slot) return false;
    *free_slot = {};
    free_slot->occupied = true;
    free_slot->scope = scope;
    output = *free_slot;
    return true;
}

bool RuntimeAdapter::set_scope_state(const ScopeState& state) noexcept {
    if (!state.occupied || !state.scope) return false;
    std::lock_guard lock(scope_mutex_);
    for (std::size_t i = 0; i < retired_scope_count_; ++i)
        if (retired_scopes_[i] == state.scope) return false;
    ScopeState* free_slot = nullptr;
    for (auto& current : scopes_) {
        if (current.occupied && current.scope == state.scope) {
            current = state;
            return true;
        }
        if (!current.occupied && !free_slot) free_slot = &current;
    }
    if (!free_slot) return false;
    *free_slot = state;
    return true;
}

void RuntimeAdapter::clear_scope(WorldScope scope) noexcept {
    if (!scope) return;
    std::lock_guard lock(scope_mutex_);
    bool already_retired = false;
    for (std::size_t i = 0; i < retired_scope_count_; ++i)
        already_retired = already_retired || retired_scopes_[i] == scope;
    for (auto& state : scopes_) {
        if (state.occupied && state.scope == scope) state = {};
    }
    if (already_retired) return;
    if (retired_scope_count_ == retired_scopes_.size()) {
        phase_fault_.store(true, std::memory_order_release);
        phase_ready_.store(false, std::memory_order_release);
        return;
    }
    retired_scopes_[retired_scope_count_++] = scope;
}
void RuntimeAdapter::record(PhaseEvent event) noexcept {
    if (!observer_ || !observer_->sampling()) return;
    const auto sequence = next_event_sequence_.fetch_add(1, std::memory_order_relaxed);
    if (!sequence) {
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
        phase_fault_.store(true, std::memory_order_release);
        return;
    }
    event.sequence = sequence;
    const auto slot = static_cast<std::size_t>((sequence - 1) % runtime_event_capacity);
    if (event_locks_[slot].test_and_set(std::memory_order_acquire)) {
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
        phase_fault_.store(true, std::memory_order_release);
        return;
    }
    events_[slot] = event;
    event_locks_[slot].clear(std::memory_order_release);
    events_recorded_.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t RuntimeAdapter::opaque_label(std::uint8_t domain,
    std::uintptr_t address) const noexcept {
    if (!address) return 0;
    return mix_label(static_cast<std::uint64_t>(address) ^ label_salt_ ^
        (static_cast<std::uint64_t>(domain) * 0x9e3779b97f4a7c15ULL));
}

void RuntimeAdapter::store_phase_report(const PhaseReport& report) noexcept {
    std::lock_guard lock(phase_mutex_);
    phase_report_ = report;
}

void RuntimeAdapter::invalidate_phase(PhaseReason reason) noexcept {
    phase_fault_.store(true, std::memory_order_release);
    phase_ready_.store(false, std::memory_order_release);
    PhaseReport report{};
    report.status = PhaseStatus::invalidated;
    report.reason = reason;
    store_phase_report(report);
}

} // namespace xhl::native_time::runtime









