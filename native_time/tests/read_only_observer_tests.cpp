#include "read_only_observer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <thread>

namespace {
using namespace xhl::native_time::observer;

constexpr std::uintptr_t image_base = 0x140000000ULL;
constexpr std::uintptr_t world_execution_offset = 0xcc4218;

struct MemoryImage {
    alignas(16) std::array<std::byte, 0x40> context{};
    alignas(16) std::array<std::byte, 0x60> clock{};
    alignas(16) std::array<std::byte, 0x40> world{};
    void* active_query_output = nullptr;
};

struct FakeHooks {
    MemoryImage* memory = nullptr;
    std::array<void*, site_count> detours{};
    std::array<bool, site_count> enabled{};
    std::array<std::uint32_t, site_count> create_calls{};
    std::array<std::uint32_t, site_count> enable_calls{};
    std::array<std::uint32_t, site_count> disable_calls{};
    std::uint32_t remove_calls = 0;
    int fail_create_at = -1;
    int fail_enable_at = -1;
    std::atomic<std::uint64_t> now_ms{1000};
    std::atomic<bool> block_callbacks{false};
    std::atomic<bool> release_callbacks{false};
    std::atomic<std::uint32_t> callbacks_waiting{0};
    std::atomic<bool> nest_one_callback{false};
    std::atomic<bool> skip_callback_work{false};
    bool valid_image = true;
    std::atomic<std::uint32_t> daytime_calls{0};
    std::atomic<std::uint32_t> skip_calls{0};
    std::atomic<std::uint32_t> query_calls{0};
    std::atomic<std::uint32_t> scale_calls{0};
    std::atomic<std::uint32_t> updater_calls{0};
    std::atomic<std::uint32_t> tick_calls{0};
};

FakeHooks* active_fake = nullptr;
std::atomic<std::uint32_t> next_test_thread_id{100};

template<class T>
void store(std::byte* base, std::size_t offset, T value) noexcept {
    std::memcpy(base + offset, &value, sizeof(value));
}

template<class T>
T load(const std::byte* base, std::size_t offset) noexcept {
    T value{};
    std::memcpy(&value, base + offset, sizeof(value));
    return value;
}

bool copy_region(std::uintptr_t address, void* output, std::size_t size,
    std::uintptr_t begin, const void* data, std::size_t capacity) noexcept {
    if (address < begin || size > capacity || address - begin > capacity - size)
        return false;
    std::memcpy(output, static_cast<const std::byte*>(data) + (address - begin), size);
    return true;
}

bool read_memory(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!active_fake || !active_fake->memory || !address || !output) return false;
    auto& memory = *active_fake->memory;
    if (copy_region(address, output, size,
            reinterpret_cast<std::uintptr_t>(memory.context.data()),
            memory.context.data(), memory.context.size()) ||
        copy_region(address, output, size,
            reinterpret_cast<std::uintptr_t>(memory.clock.data()),
            memory.clock.data(), memory.clock.size()) ||
        copy_region(address, output, size,
            reinterpret_cast<std::uintptr_t>(memory.world.data()),
            memory.world.data(), memory.world.size())) return true;
    if (memory.active_query_output) {
        const auto query = reinterpret_cast<std::uintptr_t>(memory.active_query_output);
        if (copy_region(address, output, size, query, memory.active_query_output, 0x40))
            return true;
    }
    return false;
}

std::uint32_t read_thread(void* context) noexcept {
    (void)context;
    thread_local const std::uint32_t value =
        next_test_thread_id.fetch_add(1, std::memory_order_relaxed);
    return value;
}

std::uint64_t read_clock(void* context) noexcept {
    return static_cast<FakeHooks*>(context)->now_ms.load(std::memory_order_relaxed);
}

bool validate_image(void* context, std::uintptr_t base) noexcept {
    return base == image_base && static_cast<FakeHooks*>(context)->valid_image;
}

std::size_t site_for_target(std::uintptr_t target) noexcept {
    constexpr std::array<std::uintptr_t, site_count> rvas{
        0x5dc7f0, 0xcace0, 0xc9670, 0x8507c0, 0x861790, 0x8614b0};
    for (std::size_t i = 0; i < rvas.size(); ++i)
        if (image_base + rvas[i] == target) return i;
    return site_count;
}

void XHL_NATIVE_TIME_CALL original_query(void* context, void* output,
    std::uint32_t) noexcept {
    active_fake->query_calls.fetch_add(1, std::memory_order_relaxed);
    auto* slots = static_cast<std::uintptr_t*>(output);
    slots[0] = reinterpret_cast<std::uintptr_t>(context);
    slots[2] = reinterpret_cast<std::uintptr_t>(active_fake->memory->clock.data());
    active_fake->memory->active_query_output = output;
}

void XHL_NATIVE_TIME_CALL original_daytime(void* context) noexcept {
    active_fake->daytime_calls.fetch_add(1, std::memory_order_relaxed);
    if (active_fake->block_callbacks.load(std::memory_order_acquire)) {
        active_fake->callbacks_waiting.fetch_add(1, std::memory_order_acq_rel);
        while (!active_fake->release_callbacks.load(std::memory_order_acquire))
            std::this_thread::yield();
        return;
    }
    if (active_fake->skip_callback_work.load(std::memory_order_acquire)) return;
    if (active_fake->nest_one_callback.exchange(false, std::memory_order_acq_rel)) {
        using Callback = void (XHL_NATIVE_TIME_CALL *)(void*);
        reinterpret_cast<Callback>(active_fake->detours[
            static_cast<std::size_t>(Site::daytime_callback)])(context);
        return;
    }
    std::array<std::uintptr_t, 4> query{};
    active_fake->memory->active_query_output = query.data();
    using Query = void (XHL_NATIVE_TIME_CALL *)(void*, void*, std::uint32_t);
    auto call_query = reinterpret_cast<Query>(active_fake->detours[
        static_cast<std::size_t>(Site::query_begin)]);
    call_query(context, query.data(), 0x18);
    auto* clock = reinterpret_cast<void*>(query[2]);
    using Updater = void (XHL_NATIVE_TIME_CALL *)(void*, std::uint64_t,
        std::uint64_t, std::int64_t);
    using Tick = void (XHL_NATIVE_TIME_CALL *)(void*, std::int64_t);
    reinterpret_cast<Updater>(active_fake->detours[
        static_cast<std::size_t>(Site::daynight_updater)])(clock, 123, 456, 7000);
    reinterpret_cast<Tick>(active_fake->detours[
        static_cast<std::size_t>(Site::clock_tick)])(clock, 7001);
}

void XHL_NATIVE_TIME_CALL original_skip(void* context) noexcept {
    active_fake->skip_calls.fetch_add(1, std::memory_order_relaxed);
    std::array<std::uintptr_t, 4> query{};
    active_fake->memory->active_query_output = query.data();
    using Query = void (XHL_NATIVE_TIME_CALL *)(void*, void*, std::uint32_t);
    auto call_query = reinterpret_cast<Query>(active_fake->detours[
        static_cast<std::size_t>(Site::query_begin)]);
    call_query(context, query.data(), 0x20);
    using Scale = void (XHL_NATIVE_TIME_CALL *)(void*, float, std::int64_t);
    reinterpret_cast<Scale>(active_fake->detours[
        static_cast<std::size_t>(Site::scale_writer)])(
            reinterpret_cast<void*>(query[2]), 60.0f, 7002);
}

void XHL_NATIVE_TIME_CALL original_scale(void* clock, float scale,
    std::int64_t now) noexcept {
    active_fake->scale_calls.fetch_add(1, std::memory_order_relaxed);
    auto* bytes = static_cast<std::byte*>(clock);
    store(bytes, 0x24, load<std::uint32_t>(bytes, 0x24) + 1);
    store(bytes, 0x20, scale);
    store(bytes, 0x10, now);
    store(bytes, 0x18, load<std::int64_t>(bytes, 0x18) + 100);
}

void XHL_NATIVE_TIME_CALL original_updater(void* clock, std::uint64_t,
    std::uint64_t, std::int64_t now) noexcept {
    active_fake->updater_calls.fetch_add(1, std::memory_order_relaxed);
    auto* bytes = static_cast<std::byte*>(clock);
    store(bytes, 0x24, load<std::uint32_t>(bytes, 0x24) + 1);
    store(bytes, 0x10, now);
    store(bytes, 0x18, load<std::int64_t>(bytes, 0x18) + 10);
}

void XHL_NATIVE_TIME_CALL original_tick(void* clock, std::int64_t now) noexcept {
    active_fake->tick_calls.fetch_add(1, std::memory_order_relaxed);
    auto* bytes = static_cast<std::byte*>(clock);
    store(bytes, 0x24, load<std::uint32_t>(bytes, 0x24) + 1);
    store(bytes, 0x10, now);
    store(bytes, 0x18, load<std::int64_t>(bytes, 0x18) + 1);
    store(bytes, 0x48, load<std::int64_t>(bytes, 0x48) + 1);
}

bool create_hook(void* context, std::uintptr_t target, void* detour,
    void** original) noexcept {
    auto& fake = *static_cast<FakeHooks*>(context);
    const auto site = site_for_target(target);
    if (site == site_count) return false;
    ++fake.create_calls[site];
    if (static_cast<int>(site) == fake.fail_create_at) return false;
    fake.detours[site] = detour;
    switch (static_cast<Site>(site)) {
    case Site::query_begin: *original = reinterpret_cast<void*>(&original_query); break;
    case Site::daytime_callback: *original = reinterpret_cast<void*>(&original_daytime); break;
    case Site::nighttime_skip_callback: *original = reinterpret_cast<void*>(&original_skip); break;
    case Site::scale_writer: *original = reinterpret_cast<void*>(&original_scale); break;
    case Site::daynight_updater: *original = reinterpret_cast<void*>(&original_updater); break;
    case Site::clock_tick: *original = reinterpret_cast<void*>(&original_tick); break;
    case Site::count: return false;
    }
    return true;
}

bool enable_hook(void* context, std::uintptr_t target) noexcept {
    auto& fake = *static_cast<FakeHooks*>(context);
    const auto site = site_for_target(target);
    if (site == site_count) return false;
    ++fake.enable_calls[site];
    if (static_cast<int>(site) == fake.fail_enable_at) return false;
    fake.enabled[site] = true;
    return true;
}

bool disable_hook(void* context, std::uintptr_t target) noexcept {
    auto& fake = *static_cast<FakeHooks*>(context);
    const auto site = site_for_target(target);
    if (site == site_count) return false;
    ++fake.disable_calls[site];
    fake.enabled[site] = false;
    return true;
}

bool remove_hook(void* context, std::uintptr_t target) noexcept {
    auto& fake = *static_cast<FakeHooks*>(context);
    if (site_for_target(target) == site_count) return false;
    ++fake.remove_calls;
    return true;
}

InstallOptions options_for(FakeHooks& fake, std::uint64_t duration = maximum_sample_ms) {
    InstallOptions options{};
    options.image_base = image_base;
    options.readers = RuntimeReaders{&fake, read_memory, read_thread,
        read_clock, validate_image};
    options.hooks = HookBackend{&fake, create_hook, enable_hook,
        disable_hook, remove_hook};
    options.sample_duration_ms = duration;
    return options;
}

bool check(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL " << message << '\n';
    return condition;
}

bool has_event(std::span<const Event> events, Site site, EventKind kind) {
    for (const auto& event : events)
        if (event.site == site && event.kind == kind) return true;
    return false;
}

} // namespace

int main() {
    bool ok = true;
    MemoryImage memory{};
    const auto context_address = reinterpret_cast<std::uintptr_t>(memory.context.data());
    const auto world_address = reinterpret_cast<std::uintptr_t>(memory.world.data());
    const auto clock_address = reinterpret_cast<std::uintptr_t>(memory.clock.data());
    store(memory.context.data(), 0, world_address + world_execution_offset);
    store(memory.context.data(), 8, std::uint32_t{0xaabbccdd});
    store(memory.clock.data(), 0x10, std::int64_t{100});
    store(memory.clock.data(), 0x18, std::int64_t{500});
    store(memory.clock.data(), 0x20, 1.0f);
    store(memory.clock.data(), 0x24, std::uint32_t{7});
    store(memory.clock.data(), 0x28, std::int64_t{20});
    store(memory.clock.data(), 0x30, std::int64_t{60});
    store(memory.clock.data(), 0x38, std::int64_t{1000});
    store(memory.clock.data(), 0x40, std::int64_t{2000});
    store(memory.clock.data(), 0x48, std::int64_t{23});

    FakeHooks invalid_image{};
    invalid_image.memory = &memory;
    invalid_image.valid_image = false;
    active_fake = &invalid_image;
    static ReadOnlyObserver invalid_image_observer;
    ok &= check(!invalid_image_observer.install(options_for(invalid_image)),
        "an unvalidated server image is rejected before hook creation");
    ok &= check(!invalid_image_observer.ready() &&
        invalid_image.create_calls[0] == 0,
        "image validation failure leaves all hooks untouched");

    FakeHooks invalid_duration{};
    invalid_duration.memory = &memory;
    active_fake = &invalid_duration;
    static ReadOnlyObserver invalid_duration_observer;
    ok &= check(!invalid_duration_observer.install(options_for(invalid_duration, 0)),
        "zero-length sample is rejected");
    ok &= check(!invalid_duration_observer.ready() &&
        invalid_duration.create_calls[0] == 0,
        "invalid sample duration leaves all hooks untouched");

    FakeHooks incomplete{};
    incomplete.memory = &memory;
    incomplete.fail_create_at = static_cast<int>(Site::clock_tick);
    active_fake = &incomplete;
    static ReadOnlyObserver create_failure;
    ok &= check(!create_failure.install(options_for(incomplete)),
        "incomplete create set is rejected");
    ok &= check(!create_failure.ready() && incomplete.remove_calls == site_count - 1,
        "incomplete create set leaves readiness false and removes prepared hooks");

    FakeHooks enable_failure{};
    enable_failure.memory = &memory;
    enable_failure.fail_enable_at = static_cast<int>(Site::daynight_updater);
    active_fake = &enable_failure;
    static ReadOnlyObserver enable_failure_observer;
    ok &= check(!enable_failure_observer.install(options_for(enable_failure)),
        "partial enable set is rejected");
    ok &= check(!enable_failure_observer.ready() &&
        !enable_failure.enabled[static_cast<std::size_t>(Site::query_begin)] &&
        !enable_failure.enabled[static_cast<std::size_t>(Site::daytime_callback)],
        "partial enable rollback leaves readiness false and disables earlier hooks");

    FakeHooks fake{};
    fake.memory = &memory;
    active_fake = &fake;
    static ReadOnlyObserver observer;
    ok &= check(observer.install(options_for(fake)),
        "complete six-hook set installs explicitly");
    ok &= check(observer.ready() && observer.sampling(),
        "readiness becomes true only after all hooks enable");
    for (std::size_t i = 0; i < site_count; ++i)
        ok &= check(fake.enabled[i], "all required hooks enabled");

    using Callback = void (XHL_NATIVE_TIME_CALL *)(void*);
    reinterpret_cast<Callback>(fake.detours[
        static_cast<std::size_t>(Site::daytime_callback)])(memory.context.data());
    reinterpret_cast<Callback>(fake.detours[
        static_cast<std::size_t>(Site::nighttime_skip_callback)])(memory.context.data());

    ok &= check(fake.daytime_calls.load() == 1 && fake.skip_calls.load() == 1 &&
        fake.query_calls.load() == 2 && fake.updater_calls.load() == 1 &&
        fake.tick_calls.load() == 1 && fake.scale_calls.load() == 1,
        "every native callback and writer trampoline runs exactly once");
    ok &= check(load<std::uint32_t>(memory.context.data(), 8) == 0xaabbccdd,
        "observer does not issue a second query or modify context+8");
    auto stats = observer.statistics();
    ok &= check(stats.callbacks_entered == 2 && stats.queries_observed == 2 &&
        stats.writer_calls == 3 && stats.scale_writer_calls == 1 &&
        stats.maximum_concurrent_callbacks == 1,
        "observer records callback epochs and all three writer entries");

    fake.nest_one_callback.store(true, std::memory_order_release);
    reinterpret_cast<Callback>(fake.detours[
        static_cast<std::size_t>(Site::daytime_callback)])(memory.context.data());
    fake.skip_callback_work.store(true, std::memory_order_release);
    fake.block_callbacks.store(true, std::memory_order_release);
    std::thread first([&] {
        reinterpret_cast<Callback>(fake.detours[
            static_cast<std::size_t>(Site::daytime_callback)])(memory.context.data());
    });
    std::thread second([&] {
        reinterpret_cast<Callback>(fake.detours[
            static_cast<std::size_t>(Site::daytime_callback)])(memory.context.data());
    });
    for (std::size_t attempt = 0; attempt < 100'000 &&
         fake.callbacks_waiting.load(std::memory_order_acquire) != 2; ++attempt)
        std::this_thread::yield();
    const auto both_entered = fake.callbacks_waiting.load(std::memory_order_acquire) == 2;
    fake.release_callbacks.store(true, std::memory_order_release);
    first.join();
    second.join();
    fake.block_callbacks.store(false, std::memory_order_release);
    stats = observer.statistics();
    ok &= check(stats.callbacks_nested >= 1,
        "observer counts nested callback invocations");
    ok &= check(both_entered && stats.callbacks_overlapped >= 1 &&
        stats.maximum_concurrent_callbacks >= 2,
        "observer captures concurrent callback overlap across threads");
    ok &= check(fake.daytime_calls.load() == 5 && stats.callbacks_entered == 6,
        "nested and concurrent detours each invoke their original once");

    fake.now_ms.fetch_add(maximum_sample_ms, std::memory_order_relaxed);
    using Scale = void (XHL_NATIVE_TIME_CALL *)(void*, float, std::int64_t);
    reinterpret_cast<Scale>(fake.detours[
        static_cast<std::size_t>(Site::scale_writer)])(memory.clock.data(), 1.0f, 8000);
    stats = observer.statistics();
    ok &= check(stats.sample_expired && !stats.sampling && stats.hooks_ready,
        "sample stops recording at the configured 20 second bound");
    ok &= check(fake.scale_calls.load() == 2,
        "post-window hook remains a pass-through and calls its original once");
    ok &= check(observer.stop(), "stop disables every installed observation hook");
    ok &= check(!observer.ready() && !observer.sampling(),
        "stop clears readiness and capture state");
    const auto scale_calls_before_late_entry = fake.scale_calls.load();
    const auto daytime_calls_before_late_entry = fake.daytime_calls.load();
    const auto records_before_late_entry = observer.statistics().events_recorded;
    reinterpret_cast<Callback>(fake.detours[
        static_cast<std::size_t>(Site::daytime_callback)])(memory.context.data());
    reinterpret_cast<Scale>(fake.detours[
        static_cast<std::size_t>(Site::scale_writer)])(memory.clock.data(), 1.0f, 8001);
    ok &= check(fake.daytime_calls.load() == daytime_calls_before_late_entry + 1 &&
        fake.scale_calls.load() == scale_calls_before_late_entry + 1,
        "detours already dispatched at stop remain pass-through and call originals once");
    ok &= check(observer.statistics().events_recorded == records_before_late_entry,
        "late post-stop detours do not append observer events");

    static std::array<Event, event_capacity> records{};
    const auto count = observer.copy_events(records);
    const std::span<const Event> captured(records.data(), count);
    ok &= check(has_event(captured, Site::query_begin, EventKind::query_result),
        "query output hook records actual world/clock association");
    ok &= check(has_event(captured, Site::daytime_callback, EventKind::callback_enter) &&
        has_event(captured, Site::nighttime_skip_callback, EventKind::callback_exit),
        "both registered callback entry/exit events are present");
    ok &= check(has_event(captured, Site::daynight_updater, EventKind::updater_before) &&
        has_event(captured, Site::daynight_updater, EventKind::updater_after) &&
        has_event(captured, Site::clock_tick, EventKind::tick_before) &&
        has_event(captured, Site::clock_tick, EventKind::tick_after) &&
        has_event(captured, Site::scale_writer, EventKind::scale_before) &&
        has_event(captured, Site::scale_writer, EventKind::scale_after),
        "every writer event brackets a bounded pre/post clock snapshot");

    bool associated_snapshot = false;
    std::uint64_t first_world_label = 0;
    std::uint64_t first_clock_label = 0;
    for (const auto& event : captured) {
        if (event.kind != EventKind::query_result) continue;
        associated_snapshot = event.query_label != 0 && event.context_label != 0 &&
            event.world_label != 0 && event.clock_label != 0 &&
            event.clock.complete && event.clock.version_stable && event.thread_id != 0;
        if (associated_snapshot) {
            first_world_label = event.world_label;
            first_clock_label = event.clock_label;
            break;
        }
    }
    ok &= check(associated_snapshot,
        "captured query snapshots use opaque labels and include all pinned clock fields");
    bool same_clock_writers = first_world_label != 0 && first_clock_label != 0;
    for (const auto& event : captured) {
        if (event.site != Site::clock_tick || event.kind != EventKind::tick_after)
            continue;
        same_clock_writers = same_clock_writers && event.world_label == first_world_label &&
            event.clock_label == first_clock_label && event.callback_epoch != 0 &&
            event.enclosing_callback == Site::daytime_callback;
        break;
    }
    ok &= check(same_clock_writers,
        "writer events correlate to the callback query by opaque world and clock labels");
    bool saw_daytime_query = false;
    bool saw_skip_query = false;
    std::array<std::uint32_t, 8> callback_thread_ids{};
    std::size_t unique_callback_threads = 0;
    for (const auto& event : captured) {
        if (event.kind == EventKind::query_result) {
            if (event.enclosing_callback == Site::daytime_callback &&
                event.argument0 == 0x18) saw_daytime_query = true;
            if (event.enclosing_callback == Site::nighttime_skip_callback &&
                event.argument0 == 0x20) saw_skip_query = true;
        }
        if (event.kind != EventKind::callback_enter || event.thread_id == 0) continue;
        bool known = false;
        for (std::size_t i = 0; i < unique_callback_threads; ++i)
            if (callback_thread_ids[i] == event.thread_id) known = true;
        if (!known && unique_callback_threads < callback_thread_ids.size())
            callback_thread_ids[unique_callback_threads++] = event.thread_id;
    }
    ok &= check(saw_daytime_query && saw_skip_query,
        "query events preserve the callback-specific native descriptor argument");
    ok &= check(unique_callback_threads >= 3,
        "callback events distinguish the main and two overlapping thread IDs");
    ok &= check(context_address != world_address && world_address != clock_address,
        "test fixture keeps the three native object roles independent");

    std::cout << (ok ? "PASS read-only time observer tests\n" :
        "FAIL read-only time observer tests\n");
    return ok ? 0 : 1;
}
