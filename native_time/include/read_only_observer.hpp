#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>

#if defined(_MSC_VER)
#define XHL_NATIVE_TIME_CALL __fastcall
#else
#define XHL_NATIVE_TIME_CALL
#endif

namespace xhl::native_time::observer {

inline constexpr std::uint64_t maximum_sample_ms = 20'000;
inline constexpr std::size_t event_capacity = 4096;

enum class Site : std::uint8_t {
    query_begin,
    daytime_callback,
    nighttime_skip_callback,
    scale_writer,
    daynight_updater,
    clock_tick,
    count
};
inline constexpr std::size_t site_count = static_cast<std::size_t>(Site::count);

enum class EventKind : std::uint8_t {
    callback_enter,
    callback_exit,
    query_result,
    scale_before,
    scale_after,
    updater_before,
    updater_after,
    tick_before,
    tick_after
};

struct ClockSnapshot {
    std::int64_t sync_anchor = 0;
    std::int64_t sync_base = 0;
    float sync_scale = 0.0f;
    std::uint32_t version_before = 0;
    std::uint32_t version_after = 0;
    std::int64_t dawn = 0;
    std::int64_t dusk = 0;
    std::int64_t day_length = 0;
    std::int64_t night_length = 0;
    std::int64_t time_of_day = 0;
    bool complete = false;
    bool version_stable = false;
};

// Labels are per-run opaque values. Events never expose native addresses.
struct Event {
    std::uint64_t sequence = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t callback_epoch = 0;
    std::uint64_t writer_epoch = 0;
    std::uint64_t query_label = 0;
    std::uint64_t context_label = 0;
    std::uint64_t world_label = 0;
    std::uint64_t clock_label = 0;
    std::uint64_t argument0 = 0;
    std::uint64_t argument1 = 0;
    std::uint64_t argument2 = 0;
    std::uint32_t thread_id = 0;
    std::uint32_t active_callbacks = 0;
    std::uint16_t nesting_depth = 0;
    Site site = Site::query_begin;
    Site enclosing_callback = Site::count;
    EventKind kind = EventKind::query_result;
    float scale_argument = 0.0f;
    ClockSnapshot clock{};
};

struct Statistics {
    bool hooks_ready = false;
    bool sampling = false;
    bool sample_expired = false;
    std::uint64_t callbacks_entered = 0;
    std::uint64_t callbacks_overlapped = 0;
    std::uint64_t callbacks_nested = 0;
    std::uint64_t maximum_concurrent_callbacks = 0;
    std::uint64_t queries_observed = 0;
    std::uint64_t writer_calls = 0;
    std::uint64_t scale_writer_calls = 0;
    std::uint64_t events_recorded = 0;
    std::uint64_t events_dropped = 0;
    std::uint64_t associations_unavailable = 0;
};

using MemoryReader = bool (*)(std::uintptr_t address, void* output,
    std::size_t size) noexcept;
using ThreadIdReader = std::uint32_t (*)(void* context) noexcept;
using MonotonicMilliseconds = std::uint64_t (*)(void* context) noexcept;
using ImageValidator = bool (*)(void* context, std::uintptr_t image_base) noexcept;

// The host maps these operations to its hook provider. The observer is not
// wired or enabled by this module, and install rejects an incomplete provider.
struct HookBackend {
    using Create = bool (*)(void* context, std::uintptr_t target,
        void* detour, void** original) noexcept;
    using Enable = bool (*)(void* context, std::uintptr_t target) noexcept;
    using Disable = bool (*)(void* context, std::uintptr_t target) noexcept;
    using Remove = bool (*)(void* context, std::uintptr_t target) noexcept;
    void* context = nullptr;
    Create create = nullptr;
    Enable enable = nullptr;
    Disable disable = nullptr;
    Remove remove = nullptr;
};

struct RuntimeReaders {
    void* context = nullptr;
    MemoryReader read_memory = nullptr;
    ThreadIdReader thread_id = nullptr;
    MonotonicMilliseconds monotonic_ms = nullptr;
    ImageValidator validate_image = nullptr;
};

struct InstallOptions {
    std::uintptr_t image_base = 0;
    RuntimeReaders readers{};
    HookBackend hooks{};
    std::uint64_t sample_duration_ms = maximum_sample_ms;
};

class ReadOnlyObserver {
public:
    ReadOnlyObserver() noexcept = default;
    ReadOnlyObserver(const ReadOnlyObserver&) = delete;
    ReadOnlyObserver& operator=(const ReadOnlyObserver&) = delete;

    // Installs six pass-through observation hooks only when explicitly called.
    // validate_image must validate the pinned executable. A fully enabled hook
    // set is required before ready() becomes true. The observer, RuntimeReaders
    // context, and HookBackend context must remain alive for the server process
    // lifetime. Detours may still be entered after stop() starts, and stop()
    // cannot prove global quiescence: its capture counter excludes pass-through
    // calls after capture ends and a thread may have loaded the active pointer
    // immediately before hooks were disabled. Process-lifetime storage is the
    // supported lifetime contract for all three objects.
    bool install(const InstallOptions& options) noexcept;
    // Stops capture first, then disables every successfully enabled hook.
    // Trampolines and all install contexts are intentionally retained for any
    // in-flight or late-entered detour. A true result means disable succeeded;
    // it does not mean no detour remains on a native stack.
    bool stop() noexcept;

    bool ready() const noexcept;
    bool sampling() const noexcept;
    Statistics statistics() const noexcept;
    // Copies available records in ring-slot order; the caller may sort by
    // Event::sequence. In-memory native argument qwords are not the redacted
    // evidence format; use the server adapter's JSONL dumper for export.
    std::size_t copy_events(std::span<Event> destination) const noexcept;

private:
    struct CallbackFrame {
        ReadOnlyObserver* observer = nullptr;
        CallbackFrame* previous = nullptr;
        std::uintptr_t context = 0;
        std::uint64_t callback_epoch = 0;
        std::uint64_t context_label = 0;
        std::uint64_t query_label = 0;
        std::uint64_t world_label = 0;
        std::uint64_t clock_label = 0;
        Site callback_site = Site::daytime_callback;
        std::uint16_t depth = 0;
    };

    bool begin_capture() noexcept;
    void end_capture() noexcept;
    bool capture_still_in_bounds(std::uint64_t& now) const noexcept;
    std::uint64_t label(std::uint8_t domain, std::uintptr_t address) const noexcept;
    bool snapshot_clock(std::uintptr_t clock, ClockSnapshot& out) const noexcept;
    void record(Event event) noexcept;
    void observe_query(CallbackFrame& frame, void* output,
        std::uint32_t descriptor_size) noexcept;
    void observe_writer(Site site, EventKind phase, std::uintptr_t clock,
        std::uint64_t argument0,
        std::uint64_t argument1, std::uint64_t argument2,
        float scale_argument, std::uint64_t writer_epoch) noexcept;
    void invoke_callback(Site site, void* context,
        void (XHL_NATIVE_TIME_CALL *original)(void*)) noexcept;

    static void XHL_NATIVE_TIME_CALL query_begin_detour(void* context,
        void* output, std::uint32_t descriptor_size) noexcept;
    static void XHL_NATIVE_TIME_CALL daytime_detour(void* context) noexcept;
    static void XHL_NATIVE_TIME_CALL nighttime_skip_detour(void* context) noexcept;
    static void XHL_NATIVE_TIME_CALL scale_writer_detour(void* clock,
        float scale, std::int64_t now) noexcept;
    static void XHL_NATIVE_TIME_CALL daynight_updater_detour(void* clock,
        std::uint64_t day, std::uint64_t night, std::int64_t now) noexcept;
    static void XHL_NATIVE_TIME_CALL clock_tick_detour(void* clock,
        std::int64_t now) noexcept;

    static std::atomic<ReadOnlyObserver*> active_;
    static std::array<std::atomic<void*>, site_count> trampolines_;
    static std::mutex global_lifecycle_mutex_;
    static thread_local CallbackFrame* current_frame_;

    std::atomic<bool> install_attempted_{false};
    std::atomic<bool> hooks_ready_{false};
    mutable std::atomic<bool> sampling_{false};
    mutable std::atomic<bool> sample_expired_{false};
    std::atomic<std::uint32_t> in_flight_capture_{0};
    std::array<void*, site_count> originals_{};
    std::array<std::uintptr_t, site_count> targets_{};
    std::array<bool, site_count> enabled_{};
    RuntimeReaders readers_{};
    HookBackend hooks_{};
    std::uintptr_t image_base_ = 0;
    std::uint64_t sample_start_ms_ = 0;
    std::uint64_t sample_duration_ms_ = 0;
    std::uint64_t label_salt_ = 0;
    mutable std::array<Event, event_capacity> events_{};
    mutable std::array<std::atomic_flag, event_capacity> event_locks_{};
    std::atomic<std::uint64_t> next_event_sequence_{1};
    std::atomic<std::uint64_t> next_callback_epoch_{1};
    std::atomic<std::uint64_t> next_writer_epoch_{1};
    std::atomic<std::uint64_t> active_callbacks_{0};
    std::atomic<std::uint64_t> callbacks_entered_{0};
    std::atomic<std::uint64_t> callbacks_overlapped_{0};
    std::atomic<std::uint64_t> callbacks_nested_{0};
    std::atomic<std::uint64_t> maximum_concurrent_callbacks_{0};
    std::atomic<std::uint64_t> queries_observed_{0};
    std::atomic<std::uint64_t> writer_calls_{0};
    std::atomic<std::uint64_t> scale_writer_calls_{0};
    std::atomic<std::uint64_t> events_recorded_{0};
    std::atomic<std::uint64_t> events_dropped_{0};
    std::atomic<std::uint64_t> associations_unavailable_{0};
};

} // namespace xhl::native_time::observer
