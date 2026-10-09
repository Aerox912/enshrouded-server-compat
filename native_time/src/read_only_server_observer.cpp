#include "read_only_server_observer.hpp"

#include "adapter.hpp"
#include <MinHook.h>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <new>
#include <span>

namespace xhl::native_time::observer {
namespace {

constexpr wchar_t observer_environment_name[] =
    L"XHL_ENABLE_NATIVE_TIME_OBSERVER";

struct ServerRuntimeContext {
    SRWLOCK api_lock = SRWLOCK_INIT;
    HMODULE server = nullptr;
    ReadOnlyObserver observer{};
    bool install_attempted = false;
    bool installed = false;
    bool stopped = false;
};

ServerRuntimeContext* runtime_context() noexcept {
    // This intentionally remains allocated until process teardown. Native
    // detours and their trampolines can outlive disable's return boundary.
    static ServerRuntimeContext* const context =
        new (std::nothrow) ServerRuntimeContext{};
    return context;
}

class ExclusiveLock {
public:
    explicit ExclusiveLock(SRWLOCK* lock) noexcept : lock_(lock) {
        AcquireSRWLockExclusive(lock_);
    }
    ~ExclusiveLock() { ReleaseSRWLockExclusive(lock_); }
    ExclusiveLock(const ExclusiveLock&) = delete;
    ExclusiveLock& operator=(const ExclusiveLock&) = delete;
private:
    SRWLOCK* lock_;
};

bool environment_enabled() noexcept {
    wchar_t value[2]{};
    const DWORD length = GetEnvironmentVariableW(observer_environment_name,
        value, static_cast<DWORD>(std::size(value)));
    return length == 1 && value[0] == L'1';
}

bool guarded_read_memory(std::uintptr_t address, void* output,
    std::size_t size) noexcept {
    if (!address || !output || !size ||
        size > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    __try {
        std::memcpy(output, reinterpret_cast<const void*>(address), size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::uint32_t current_thread_id(void* context) noexcept {
    (void)context;
    return GetCurrentThreadId();
}

std::uint64_t monotonic_milliseconds(void* context) noexcept {
    (void)context;
    return GetTickCount64();
}

bool validate_pinned_image(void* opaque_context,
    std::uintptr_t image_base) noexcept {
    const auto* context = static_cast<const ServerRuntimeContext*>(opaque_context);
    if (!context || !context->server ||
        reinterpret_cast<std::uintptr_t>(context->server) != image_base ||
        GetModuleHandleW(nullptr) != context->server) return false;
    return xhl::validate_server(context->server);
}

bool create_hook(void*, std::uintptr_t target, void* detour,
    void** original) noexcept {
    return MH_CreateHook(reinterpret_cast<void*>(target), detour, original) == MH_OK;
}

bool enable_hook(void*, std::uintptr_t target) noexcept {
    return MH_EnableHook(reinterpret_cast<void*>(target)) == MH_OK;
}

bool disable_hook(void*, std::uintptr_t target) noexcept {
    const MH_STATUS status = MH_DisableHook(reinterpret_cast<void*>(target));
    return status == MH_OK || status == MH_ERROR_DISABLED;
}

bool remove_hook(void*, std::uintptr_t target) noexcept {
    const MH_STATUS status = MH_RemoveHook(reinterpret_cast<void*>(target));
    return status == MH_OK || status == MH_ERROR_NOT_CREATED;
}

const char* site_name(Site site) noexcept {
    switch (site) {
    case Site::query_begin: return "query_begin";
    case Site::daytime_callback: return "daytime_callback";
    case Site::nighttime_skip_callback: return "nighttime_skip_callback";
    case Site::scale_writer: return "scale_writer";
    case Site::daynight_updater: return "daynight_updater";
    case Site::clock_tick: return "clock_tick";
    case Site::count: return "none";
    }
    return "invalid";
}

const char* event_name(EventKind kind) noexcept {
    switch (kind) {
    case EventKind::callback_enter: return "callback_enter";
    case EventKind::callback_exit: return "callback_exit";
    case EventKind::query_result: return "query_result";
    case EventKind::scale_before: return "scale_before";
    case EventKind::scale_after: return "scale_after";
    case EventKind::updater_before: return "updater_before";
    case EventKind::updater_after: return "updater_after";
    case EventKind::tick_before: return "tick_before";
    case EventKind::tick_after: return "tick_after";
    }
    return "invalid";
}

bool emit_line(JsonLineWriter writer, void* context, const char* format,
    auto... arguments) noexcept {
    char line[1536]{};
    const int count = std::snprintf(line, sizeof(line), format, arguments...);
    return count > 0 && static_cast<std::size_t>(count) < sizeof(line) &&
        writer(context, line, static_cast<std::size_t>(count));
}

bool emit_summary(JsonLineWriter writer, void* context,
    const Statistics& stats) noexcept {
    return emit_line(writer, context,
        "{\"type\":\"summary\",\"observer_ready\":false,"
        "\"sampling\":false,\"sample_expired\":%s,"
        "\"callbacks_entered\":%llu,\"callbacks_overlapped\":%llu,"
        "\"callbacks_nested\":%llu,\"max_concurrent_callbacks\":%llu,"
        "\"queries_observed\":%llu,\"writer_calls\":%llu,"
        "\"scale_writer_calls\":%llu,\"events_recorded\":%llu,"
        "\"events_dropped\":%llu,\"associations_unavailable\":%llu,"
        "\"raw_arguments_redacted\":true}\n",
        stats.sample_expired ? "true" : "false",
        static_cast<unsigned long long>(stats.callbacks_entered),
        static_cast<unsigned long long>(stats.callbacks_overlapped),
        static_cast<unsigned long long>(stats.callbacks_nested),
        static_cast<unsigned long long>(stats.maximum_concurrent_callbacks),
        static_cast<unsigned long long>(stats.queries_observed),
        static_cast<unsigned long long>(stats.writer_calls),
        static_cast<unsigned long long>(stats.scale_writer_calls),
        static_cast<unsigned long long>(stats.events_recorded),
        static_cast<unsigned long long>(stats.events_dropped),
        static_cast<unsigned long long>(stats.associations_unavailable));
}

bool emit_event(JsonLineWriter writer, void* context,
    const Event& event) noexcept {
    const auto& clock = event.clock;
    return emit_line(writer, context,
        "{\"type\":\"event\",\"seq\":%llu,\"elapsed_ms\":%llu,"
        "\"callback_epoch\":%llu,\"writer_epoch\":%llu,"
        "\"thread_id\":%u,\"active_callbacks\":%u,\"nesting\":%u,"
        "\"site\":\"%s\",\"kind\":\"%s\",\"enclosing\":\"%s\","
        "\"labels\":{\"query\":\"%016llx\",\"context\":\"%016llx\","
        "\"world\":\"%016llx\",\"clock\":\"%016llx\"},"
        "\"clock\":{\"complete\":%s,\"version_stable\":%s,"
        "\"version_before\":%u,\"version_after\":%u,"
        "\"sync_anchor\":%lld,\"sync_base\":%lld,"
        "\"sync_scale_bits\":\"%08x\",\"dawn\":%lld,\"dusk\":%lld,"
        "\"day_length\":%lld,\"night_length\":%lld,"
        "\"time_of_day\":%lld},\"scale_argument_bits\":\"%08x\"}\n",
        static_cast<unsigned long long>(event.sequence),
        static_cast<unsigned long long>(event.elapsed_ms),
        static_cast<unsigned long long>(event.callback_epoch),
        static_cast<unsigned long long>(event.writer_epoch),
        static_cast<unsigned>(event.thread_id),
        static_cast<unsigned>(event.active_callbacks),
        static_cast<unsigned>(event.nesting_depth),
        site_name(event.site), event_name(event.kind),
        site_name(event.enclosing_callback),
        static_cast<unsigned long long>(event.query_label),
        static_cast<unsigned long long>(event.context_label),
        static_cast<unsigned long long>(event.world_label),
        static_cast<unsigned long long>(event.clock_label),
        clock.complete ? "true" : "false",
        clock.version_stable ? "true" : "false",
        static_cast<unsigned>(clock.version_before),
        static_cast<unsigned>(clock.version_after),
        static_cast<long long>(clock.sync_anchor),
        static_cast<long long>(clock.sync_base),
        static_cast<unsigned>(std::bit_cast<std::uint32_t>(clock.sync_scale)),
        static_cast<long long>(clock.dawn),
        static_cast<long long>(clock.dusk),
        static_cast<long long>(clock.day_length),
        static_cast<long long>(clock.night_length),
        static_cast<long long>(clock.time_of_day),
        static_cast<unsigned>(std::bit_cast<std::uint32_t>(event.scale_argument)));
}

} // namespace

bool install_local_read_only_server_observer(HMODULE server) noexcept {
    auto* context = runtime_context();
    if (!context || !environment_enabled()) return false;
    ExclusiveLock lock(&context->api_lock);
    if (context->install_attempted) return false;
    context->install_attempted = true;

    // This adapter is for the main dedicated-server image only. The validator
    // checks filename, SHA-256, AMD64 PE metadata, and image size.
    if (!server || GetModuleHandleW(nullptr) != server ||
        !xhl::validate_server(server)) return false;
    const MH_STATUS init_status = MH_Initialize();
    if (init_status != MH_OK && init_status != MH_ERROR_ALREADY_INITIALIZED)
        return false;
    context->server = server;

    const auto image_base = reinterpret_cast<std::uintptr_t>(server);
    const RuntimeReaders readers{context, guarded_read_memory,
        current_thread_id, monotonic_milliseconds, validate_pinned_image};
    const HookBackend hooks{context, create_hook, enable_hook,
        disable_hook, remove_hook};
    const InstallOptions options{image_base, readers, hooks, maximum_sample_ms};
    context->installed = context->observer.install(options);
    return context->installed;
}

bool stop_local_read_only_server_observer() noexcept {
    auto* context = runtime_context();
    if (!context) return false;
    ExclusiveLock lock(&context->api_lock);
    if (!context->installed) return false;
    if (context->stopped) return true;
    context->stopped = context->observer.stop();
    return context->stopped;
}

bool local_read_only_server_observer_ready() noexcept {
    auto* context = runtime_context();
    if (!context) return false;
    return context->observer.ready();
}

Statistics local_read_only_server_observer_statistics() noexcept {
    auto* context = runtime_context();
    return context ? context->observer.statistics() : Statistics{};
}

std::size_t copy_local_read_only_server_observer_events(
    std::span<Event> destination) noexcept {
    auto* context = runtime_context();
    if (!context) return 0;
    ExclusiveLock lock(&context->api_lock);
    if (!context->installed) return 0;
    return context->observer.copy_events(destination);
}

bool dump_local_read_only_server_observer_jsonl(JsonLineWriter writer,
    void* writer_context) noexcept {
    auto* context = runtime_context();
    if (!context || !writer) return false;
    {
        ExclusiveLock lock(&context->api_lock);
        if (!context->installed || !context->stopped) return false;
    }

    auto* events = new (std::nothrow) Event[event_capacity];
    if (!events) return false;
    const std::size_t count = context->observer.copy_events(
        std::span<Event>(events, event_capacity));
    std::sort(events, events + count, [](const Event& left, const Event& right) {
        return left.sequence < right.sequence;
    });

    bool ok = emit_summary(writer, writer_context,
        context->observer.statistics());
    for (std::size_t i = 0; ok && i < count; ++i)
        ok = emit_event(writer, writer_context, events[i]);
    delete[] events;
    return ok;
}

} // namespace xhl::native_time::observer
