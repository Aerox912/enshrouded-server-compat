#include "read_only_observer.hpp"

#include "flight_identity.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <thread>

namespace xhl::native_time::observer {
namespace {

constexpr std::array<std::uintptr_t, site_count> hook_rvas{
    0x5dc7f0, 0xcace0, 0xc9670, 0x8507c0, 0x861790, 0x8614b0};
constexpr std::uint8_t context_domain = 1;
constexpr std::uint8_t query_domain = 2;
constexpr std::uint8_t world_domain = 3;
constexpr std::uint8_t clock_domain = 4;

using QueryBegin = void (XHL_NATIVE_TIME_CALL *)(void*, void*, std::uint32_t);
using NativeCallback = void (XHL_NATIVE_TIME_CALL *)(void*);
using ScaleWriter = void (XHL_NATIVE_TIME_CALL *)(void*, float, std::int64_t);
using DaynightUpdater = void (XHL_NATIVE_TIME_CALL *)(void*, std::uint64_t,
    std::uint64_t, std::int64_t);
using ClockTick = void (XHL_NATIVE_TIME_CALL *)(void*, std::int64_t);

std::size_t index_of(Site site) noexcept {
    return static_cast<std::size_t>(site);
}

std::uint64_t mix_label(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebULL;
    value ^= value >> 31;
    return value ? value : 1;
}

std::uint64_t update_max(std::atomic<std::uint64_t>& target,
                         std::uint64_t value) noexcept {
    auto previous = target.load(std::memory_order_relaxed);
    while (previous < value &&
           !target.compare_exchange_weak(previous, value,
               std::memory_order_relaxed, std::memory_order_relaxed)) {}
    return previous < value ? value : previous;
}

template<class T>
bool read_offset(MemoryReader reader, std::uintptr_t base,
                 std::uintptr_t offset, T& value) noexcept {
    if (!reader || !base || offset > UINTPTR_MAX - base) return false;
    return reader(base + offset, &value, sizeof(value));
}

} // namespace

std::atomic<ReadOnlyObserver*> ReadOnlyObserver::active_{nullptr};
std::array<std::atomic<void*>, site_count> ReadOnlyObserver::trampolines_{};
std::mutex ReadOnlyObserver::global_lifecycle_mutex_;
thread_local ReadOnlyObserver::CallbackFrame* ReadOnlyObserver::current_frame_ = nullptr;

bool ReadOnlyObserver::install(const InstallOptions& options) noexcept {
    std::lock_guard global_lock(global_lifecycle_mutex_);
    bool expected_attempted = false;
    if (!install_attempted_.compare_exchange_strong(expected_attempted, true,
        std::memory_order_acq_rel, std::memory_order_acquire)) return false;

    if (!options.image_base || !options.readers.read_memory ||
        !options.readers.thread_id || !options.readers.monotonic_ms ||
        !options.readers.validate_image || !options.hooks.create ||
        !options.hooks.enable || !options.hooks.disable || !options.hooks.remove ||
        options.sample_duration_ms == 0 ||
        options.sample_duration_ms > maximum_sample_ms ||
        !options.readers.validate_image(options.readers.context, options.image_base)) {
        return false;
    }
    if (active_.load(std::memory_order_acquire) != nullptr) return false;

    std::array<std::uintptr_t, site_count> targets{};
    std::array<void*, site_count> originals{};
    const std::array<void*, site_count> detours{
        reinterpret_cast<void*>(&ReadOnlyObserver::query_begin_detour),
        reinterpret_cast<void*>(&ReadOnlyObserver::daytime_detour),
        reinterpret_cast<void*>(&ReadOnlyObserver::nighttime_skip_detour),
        reinterpret_cast<void*>(&ReadOnlyObserver::scale_writer_detour),
        reinterpret_cast<void*>(&ReadOnlyObserver::daynight_updater_detour),
        reinterpret_cast<void*>(&ReadOnlyObserver::clock_tick_detour)
    };
    for (std::size_t i = 0; i < site_count; ++i) {
        if (hook_rvas[i] > UINTPTR_MAX - options.image_base) return false;
        targets[i] = options.image_base + hook_rvas[i];
    }

    std::size_t created = 0;
    for (; created < site_count; ++created) {
        if (!options.hooks.create(options.hooks.context, targets[created],
                detours[created], &originals[created]) ||
            !originals[created]) {
            if (originals[created])
                (void)options.hooks.remove(options.hooks.context, targets[created]);
            break;
        }
    }
    if (created != site_count) {
        while (created) {
            --created;
            (void)options.hooks.remove(options.hooks.context, targets[created]);
        }
        return false;
    }

    readers_ = options.readers;
    hooks_ = options.hooks;
    image_base_ = options.image_base;
    targets_ = targets;
    originals_ = originals;
    sample_duration_ms_ = options.sample_duration_ms;

    for (std::size_t i = 0; i < site_count; ++i)
        trampolines_[i].store(originals_[i], std::memory_order_release);

    bool enabled_all = true;
    for (std::size_t i = 0; i < site_count; ++i) {
        if (!hooks_.enable(hooks_.context, targets_[i])) {
            enabled_all = false;
            break;
        }
        enabled_[i] = true;
    }
    if (!enabled_all) {
        for (std::size_t i = site_count; i-- > 0;) {
            if (enabled_[i] && hooks_.disable(hooks_.context, targets_[i]))
                enabled_[i] = false;
        }
        // Keep the trampolines process-resident. Any detour that entered while
        // the partial enable was being rolled back sees no active observer and
        // calls its original exactly once.
        return false;
    }

    ReadOnlyObserver* no_observer = nullptr;
    if (!active_.compare_exchange_strong(no_observer, this,
        std::memory_order_acq_rel, std::memory_order_acquire)) {
        for (std::size_t i = site_count; i-- > 0;) {
            if (enabled_[i] && hooks_.disable(hooks_.context, targets_[i]))
                enabled_[i] = false;
        }
        return false;
    }

    sample_start_ms_ = readers_.monotonic_ms(readers_.context);
    label_salt_ = mix_label(static_cast<std::uint64_t>(image_base_) ^
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this)) ^
        sample_start_ms_ ^ 0xa76f15d4c39b8201ULL);
    sampling_.store(true, std::memory_order_release);
    hooks_ready_.store(true, std::memory_order_release);
    return true;
}

bool ReadOnlyObserver::stop() noexcept {
    std::lock_guard global_lock(global_lifecycle_mutex_);
    sampling_.store(false, std::memory_order_release);
    hooks_ready_.store(false, std::memory_order_release);
    bool complete = true;
    for (std::size_t i = site_count; i-- > 0;) {
        if (!enabled_[i]) continue;
        if (hooks_.disable(hooks_.context, targets_[i])) enabled_[i] = false;
        else complete = false;
    }

    // Do not free hooks or clear the process-lifetime observer/trampolines:
    // hook providers do not guarantee that disable drains entered callbacks.
    for (std::size_t attempt = 0; attempt < 10'000 &&
         in_flight_capture_.load(std::memory_order_acquire) != 0; ++attempt)
        std::this_thread::yield();
    if (in_flight_capture_.load(std::memory_order_acquire) != 0) complete = false;
    return complete;
}

bool ReadOnlyObserver::ready() const noexcept {
    return hooks_ready_.load(std::memory_order_acquire);
}

bool ReadOnlyObserver::sampling() const noexcept {
    if (!sampling_.load(std::memory_order_acquire)) return false;
    std::uint64_t now = 0;
    return capture_still_in_bounds(now);
}

Statistics ReadOnlyObserver::statistics() const noexcept {
    return {
        ready(),
        sampling(),
        sample_expired_.load(std::memory_order_relaxed),
        callbacks_entered_.load(std::memory_order_relaxed),
        callbacks_overlapped_.load(std::memory_order_relaxed),
        callbacks_nested_.load(std::memory_order_relaxed),
        maximum_concurrent_callbacks_.load(std::memory_order_relaxed),
        queries_observed_.load(std::memory_order_relaxed),
        writer_calls_.load(std::memory_order_relaxed),
        scale_writer_calls_.load(std::memory_order_relaxed),
        events_recorded_.load(std::memory_order_relaxed),
        events_dropped_.load(std::memory_order_relaxed),
        associations_unavailable_.load(std::memory_order_relaxed)
    };
}

std::size_t ReadOnlyObserver::copy_events(std::span<Event> destination) const noexcept {
    std::size_t copied = 0;
    for (std::size_t i = 0; i < event_capacity && copied < destination.size(); ++i) {
        if (event_locks_[i].test_and_set(std::memory_order_acquire)) continue;
        const auto event = events_[i];
        event_locks_[i].clear(std::memory_order_release);
        if (event.sequence) destination[copied++] = event;
    }
    return copied;
}

bool ReadOnlyObserver::begin_capture() noexcept {
    if (!sampling_.load(std::memory_order_acquire)) return false;
    in_flight_capture_.fetch_add(1, std::memory_order_acq_rel);
    std::uint64_t now = 0;
    if (!capture_still_in_bounds(now)) {
        in_flight_capture_.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    return true;
}

void ReadOnlyObserver::end_capture() noexcept {
    in_flight_capture_.fetch_sub(1, std::memory_order_acq_rel);
}

bool ReadOnlyObserver::capture_still_in_bounds(std::uint64_t& now) const noexcept {
    now = readers_.monotonic_ms(readers_.context);
    if (!sampling_.load(std::memory_order_acquire)) return false;
    if (now < sample_start_ms_ || now - sample_start_ms_ >= sample_duration_ms_) {
        sample_expired_.store(true, std::memory_order_release);
        sampling_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

std::uint64_t ReadOnlyObserver::label(std::uint8_t domain,
                                     std::uintptr_t address) const noexcept {
    if (!address) return 0;
    const auto value = static_cast<std::uint64_t>(address) ^ label_salt_ ^
        (static_cast<std::uint64_t>(domain) * 0x9e3779b97f4a7c15ULL);
    return mix_label(value);
}

bool ReadOnlyObserver::snapshot_clock(std::uintptr_t clock,
                                      ClockSnapshot& out) const noexcept {
    out = {};
    const auto reader = readers_.read_memory;
    const bool version_start = read_offset(reader, clock, 0x24, out.version_before);
    const bool anchor = read_offset(reader, clock, 0x10, out.sync_anchor);
    const bool base = read_offset(reader, clock, 0x18, out.sync_base);
    const bool scale = read_offset(reader, clock, 0x20, out.sync_scale);
    const bool dawn = read_offset(reader, clock, 0x28, out.dawn);
    const bool dusk = read_offset(reader, clock, 0x30, out.dusk);
    const bool day = read_offset(reader, clock, 0x38, out.day_length);
    const bool night = read_offset(reader, clock, 0x40, out.night_length);
    const bool time = read_offset(reader, clock, 0x48, out.time_of_day);
    const bool version_end = read_offset(reader, clock, 0x24, out.version_after);
    out.complete = version_start && anchor && base && scale && dawn && dusk &&
        day && night && time && version_end;
    out.version_stable = out.complete && out.version_before == out.version_after;
    return out.complete;
}

void ReadOnlyObserver::record(Event event) noexcept {
    std::uint64_t now = 0;
    if (!capture_still_in_bounds(now)) return;
    const auto sequence = next_event_sequence_.fetch_add(1, std::memory_order_relaxed);
    if (!sequence) {
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    event.sequence = sequence;
    event.elapsed_ms = now - sample_start_ms_;
    event.thread_id = readers_.thread_id(readers_.context);
    const auto slot = static_cast<std::size_t>((sequence - 1) % event_capacity);
    if (event_locks_[slot].test_and_set(std::memory_order_acquire)) {
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    events_[slot] = event;
    event_locks_[slot].clear(std::memory_order_release);
    events_recorded_.fetch_add(1, std::memory_order_relaxed);
}

void ReadOnlyObserver::observe_query(CallbackFrame& frame, void* output,
    std::uint32_t descriptor_size) noexcept {
    std::uint64_t now = 0;
    if (!output || !capture_still_in_bounds(now)) return;
    const auto query_address = reinterpret_cast<std::uintptr_t>(output);
    std::uintptr_t world = 0;
    std::uintptr_t clock = 0;
    const bool world_ok = flight::read_query_world(readers_.read_memory,
        query_address, world);
    const bool clock_ok = read_offset(readers_.read_memory, query_address,
        0x10, clock) && clock != 0;

    frame.query_label = label(query_domain, query_address);
    frame.world_label = world_ok ? label(world_domain, world) : 0;
    frame.clock_label = clock_ok ? label(clock_domain, clock) : 0;
    if (!world_ok || !clock_ok) associations_unavailable_.fetch_add(1,
        std::memory_order_relaxed);

    Event event{};
    event.site = Site::query_begin;
    event.enclosing_callback = frame.callback_site;
    event.kind = EventKind::query_result;
    event.callback_epoch = frame.callback_epoch;
    event.query_label = frame.query_label;
    event.context_label = frame.context_label;
    event.world_label = frame.world_label;
    event.clock_label = frame.clock_label;
    event.argument0 = descriptor_size;
    if (clock_ok) (void)snapshot_clock(clock, event.clock);
    record(event);
    queries_observed_.fetch_add(1, std::memory_order_relaxed);
}

void ReadOnlyObserver::observe_writer(Site site, EventKind phase,
    std::uintptr_t clock, std::uint64_t argument0, std::uint64_t argument1,
    std::uint64_t argument2, float scale_argument,
    std::uint64_t writer_epoch) noexcept {
    Event event{};
    event.site = site;
    event.enclosing_callback = current_frame_ && current_frame_->observer == this
        ? current_frame_->callback_site : Site::count;
    event.kind = phase;
    event.writer_epoch = writer_epoch;
    event.clock_label = label(clock_domain, clock);
    event.argument0 = argument0;
    event.argument1 = argument1;
    event.argument2 = argument2;
    event.scale_argument = scale_argument;
    if (current_frame_ && current_frame_->observer == this) {
        const auto& frame = *current_frame_;
        event.callback_epoch = frame.callback_epoch;
        event.context_label = frame.context_label;
        event.query_label = frame.query_label;
        event.world_label = frame.world_label;
        event.nesting_depth = frame.depth;
        event.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
            active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
        if (frame.clock_label && frame.clock_label == event.clock_label)
            event.clock_label = frame.clock_label;
    }
    (void)snapshot_clock(clock, event.clock);
    record(event);
}

void ReadOnlyObserver::invoke_callback(Site site, void* context,
    void (XHL_NATIVE_TIME_CALL *original)(void*)) noexcept {
    if (!original) return;
    if (!begin_capture()) {
        original(context);
        return;
    }

    CallbackFrame frame{};
    frame.observer = this;
    frame.previous = current_frame_;
    frame.context = reinterpret_cast<std::uintptr_t>(context);
    frame.context_label = label(context_domain, frame.context);
    frame.callback_site = site;
    frame.callback_epoch = next_callback_epoch_.fetch_add(1,
        std::memory_order_relaxed);
    frame.depth = frame.previous && frame.previous->observer == this
        ? static_cast<std::uint16_t>(std::min<unsigned>(
            static_cast<unsigned>(frame.previous->depth) + 1u, UINT16_MAX)) : 1;
    const auto active_before = active_callbacks_.fetch_add(1,
        std::memory_order_acq_rel);
    const auto active_now = active_before + 1;
    callbacks_entered_.fetch_add(1, std::memory_order_relaxed);
    if (active_before) callbacks_overlapped_.fetch_add(1, std::memory_order_relaxed);
    if (frame.depth > 1) callbacks_nested_.fetch_add(1, std::memory_order_relaxed);
    update_max(maximum_concurrent_callbacks_, active_now);
    current_frame_ = &frame;

    Event enter{};
    enter.site = site;
    enter.enclosing_callback = site;
    enter.kind = EventKind::callback_enter;
    enter.callback_epoch = frame.callback_epoch;
    enter.context_label = frame.context_label;
    enter.nesting_depth = frame.depth;
    enter.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        active_now, UINT32_MAX));
    record(enter);

    original(context);

    Event leave{};
    leave.site = site;
    leave.enclosing_callback = site;
    leave.kind = EventKind::callback_exit;
    leave.callback_epoch = frame.callback_epoch;
    leave.query_label = frame.query_label;
    leave.context_label = frame.context_label;
    leave.world_label = frame.world_label;
    leave.clock_label = frame.clock_label;
    leave.nesting_depth = frame.depth;
    leave.active_callbacks = static_cast<std::uint32_t>(std::min<std::uint64_t>(
        active_callbacks_.load(std::memory_order_relaxed), UINT32_MAX));
    record(leave);

    current_frame_ = frame.previous;
    active_callbacks_.fetch_sub(1, std::memory_order_acq_rel);
    end_capture();
}

void XHL_NATIVE_TIME_CALL ReadOnlyObserver::query_begin_detour(void* context,
    void* output, std::uint32_t descriptor_size) noexcept {
    auto* observer = active_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<QueryBegin>(
        trampolines_[index_of(Site::query_begin)].load(std::memory_order_acquire));
    if (!original) return;
    original(context, output, descriptor_size);
    if (!observer || !observer->begin_capture()) return;
    if (current_frame_ && current_frame_->observer == observer &&
        current_frame_->context == reinterpret_cast<std::uintptr_t>(context))
        observer->observe_query(*current_frame_, output, descriptor_size);
    observer->end_capture();
}

void XHL_NATIVE_TIME_CALL ReadOnlyObserver::daytime_detour(void* context) noexcept {
    auto* observer = active_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<NativeCallback>(
        trampolines_[index_of(Site::daytime_callback)].load(std::memory_order_acquire));
    if (observer) observer->invoke_callback(Site::daytime_callback, context, original);
    else if (original) original(context);
}

void XHL_NATIVE_TIME_CALL ReadOnlyObserver::nighttime_skip_detour(void* context) noexcept {
    auto* observer = active_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<NativeCallback>(
        trampolines_[index_of(Site::nighttime_skip_callback)].load(std::memory_order_acquire));
    if (observer) observer->invoke_callback(Site::nighttime_skip_callback, context, original);
    else if (original) original(context);
}

void XHL_NATIVE_TIME_CALL ReadOnlyObserver::scale_writer_detour(void* clock,
    float scale, std::int64_t now) noexcept {
    auto* observer = active_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<ScaleWriter>(
        trampolines_[index_of(Site::scale_writer)].load(std::memory_order_acquire));
    if (!original) return;
    if (!observer || !observer->begin_capture()) {
        original(clock, scale, now);
        return;
    }
    const auto epoch = observer->next_writer_epoch_.fetch_add(1,
        std::memory_order_relaxed);
    observer->writer_calls_.fetch_add(1, std::memory_order_relaxed);
    observer->scale_writer_calls_.fetch_add(1, std::memory_order_relaxed);
    const auto address = reinterpret_cast<std::uintptr_t>(clock);
    observer->observe_writer(Site::scale_writer, EventKind::scale_before,
        address, std::bit_cast<std::uint32_t>(scale),
        static_cast<std::uint64_t>(now), 0, scale, epoch);
    original(clock, scale, now);
    observer->observe_writer(Site::scale_writer, EventKind::scale_after,
        address, std::bit_cast<std::uint32_t>(scale),
        static_cast<std::uint64_t>(now), 0, scale, epoch);
    observer->end_capture();
}

void XHL_NATIVE_TIME_CALL ReadOnlyObserver::daynight_updater_detour(void* clock,
    std::uint64_t day, std::uint64_t night, std::int64_t now) noexcept {
    auto* observer = active_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<DaynightUpdater>(
        trampolines_[index_of(Site::daynight_updater)].load(std::memory_order_acquire));
    if (!original) return;
    if (!observer || !observer->begin_capture()) {
        original(clock, day, night, now);
        return;
    }
    const auto epoch = observer->next_writer_epoch_.fetch_add(1,
        std::memory_order_relaxed);
    observer->writer_calls_.fetch_add(1, std::memory_order_relaxed);
    const auto address = reinterpret_cast<std::uintptr_t>(clock);
    observer->observe_writer(Site::daynight_updater, EventKind::updater_before,
        address, day, night, static_cast<std::uint64_t>(now), 0.0f, epoch);
    original(clock, day, night, now);
    observer->observe_writer(Site::daynight_updater, EventKind::updater_after,
        address, day, night, static_cast<std::uint64_t>(now), 0.0f, epoch);
    observer->end_capture();
}

void XHL_NATIVE_TIME_CALL ReadOnlyObserver::clock_tick_detour(void* clock,
    std::int64_t now) noexcept {
    auto* observer = active_.load(std::memory_order_acquire);
    const auto original = reinterpret_cast<ClockTick>(
        trampolines_[index_of(Site::clock_tick)].load(std::memory_order_acquire));
    if (!original) return;
    if (!observer || !observer->begin_capture()) {
        original(clock, now);
        return;
    }
    const auto epoch = observer->next_writer_epoch_.fetch_add(1,
        std::memory_order_relaxed);
    observer->writer_calls_.fetch_add(1, std::memory_order_relaxed);
    const auto address = reinterpret_cast<std::uintptr_t>(clock);
    observer->observe_writer(Site::clock_tick, EventKind::tick_before,
        address, static_cast<std::uint64_t>(now), 0, 0, 0.0f, epoch);
    original(clock, now);
    observer->observe_writer(Site::clock_tick, EventKind::tick_after,
        address, static_cast<std::uint64_t>(now), 0, 0, 0.0f, epoch);
    observer->end_capture();
}

} // namespace xhl::native_time::observer
