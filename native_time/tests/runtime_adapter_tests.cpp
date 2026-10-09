#include "runtime_adapter.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>

using namespace xhl;
using namespace xhl::native_time;
using namespace xhl::native_time::runtime;

namespace {
constexpr std::uintptr_t image_base = 0x140000000ULL;
constexpr std::uintptr_t query_address = 0x10000000ULL;
constexpr std::uintptr_t query_context = 0x20000000ULL;
constexpr std::uintptr_t world_address = 0x30000000ULL;
constexpr std::uintptr_t clock_address = 0x40000000ULL;
constexpr std::uintptr_t callback_context = 0x50000000ULL;
constexpr WorldScope test_scope = 0xA001;
constexpr std::array<std::uintptr_t, observer::site_count> hook_rvas{
    0x5dc7f0, 0xcace0, 0xc9670, 0x8507c0, 0x861790, 0x8614b0
};

int failures = 0;
struct Fixture;
Fixture* current_fixture = nullptr;

void check(bool condition, std::string_view message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << message << '\n';
    }
}

struct Fixture {
    native_time::ClockView clock{};
    flight::Identity identity{};
    WorldScope scope = test_scope;
    std::uint64_t monotonic = 100;
    bool time_allowed = true;
    std::size_t host_reads = 0;
    std::int64_t last_host_now = 0;
    bool host_saw_native_now = false;
    bool suppress_tick = false;
    std::size_t hour_writes = 0;
    std::array<std::size_t, observer::site_count> original_calls{};
};

template<class T>
bool copy_if(std::uintptr_t address, std::uintptr_t expected,
    const T& value, void* output, std::size_t size) noexcept {
    if (address != expected || size != sizeof(T) || !output) return false;
    std::memcpy(output, &value, sizeof(T));
    return true;
}

bool read_memory(std::uintptr_t address, void* output, std::size_t size) noexcept {
    if (!current_fixture || !output) return false;
    if (copy_if(address, query_address, query_context, output, size) ||
        copy_if(address, query_context, world_address + 0xcc4218, output, size) ||
        copy_if(address, query_address + 0x10, clock_address, output, size)) return true;

    const auto& clock = current_fixture->clock;
    if (copy_if(address, clock_address + 0x10, clock.sync_anchor, output, size) ||
        copy_if(address, clock_address + 0x18, clock.sync_base, output, size) ||
        copy_if(address, clock_address + 0x20, clock.scale, output, size) ||
        copy_if(address, clock_address + 0x24, clock.version, output, size) ||
        copy_if(address, clock_address + 0x28, clock.schedule.dawn, output, size) ||
        copy_if(address, clock_address + 0x30, clock.schedule.dusk, output, size) ||
        copy_if(address, clock_address + 0x38, clock.schedule.day_length, output, size) ||
        copy_if(address, clock_address + 0x40, clock.schedule.night_length, output, size)) return true;
    const std::int64_t time_of_day = clock.base_at_now().value_or(0);
    return copy_if(address, clock_address + 0x48, time_of_day, output, size);
}

std::uint32_t thread_id(void*) noexcept { return 42; }
std::uint64_t monotonic_ms(void* context) noexcept {
    return static_cast<Fixture*>(context)->monotonic;
}
bool validate_image(void*, std::uintptr_t base) noexcept { return base == image_base; }

bool read_snapshot(void* context, const NativeQueryView& query,
    CallbackSnapshot& output) noexcept {
    auto& fixture = *static_cast<Fixture*>(context);
    ++fixture.host_reads;
    if (query.world != world_address || query.clock != clock_address) return false;
    output = {};
    output.world = world_address;
    output.world_scope = fixture.scope;
    output.callback_epoch = 1;
    output.world_is_current = true;
    output.owner_identity = fixture.identity;
    output.owner = 42;
    output.owner_resolution_current = true;
    output.clock = fixture.clock;
    if (query.native_now_known) {
        fixture.last_host_now = query.native_now;
        fixture.host_saw_native_now = true;
        output.clock.now = query.native_now;
    }
    return true;
}

bool write_hour(void* context, const NativeQueryView& query,
    const HourFieldSequence& writes) noexcept {
    auto& fixture = *static_cast<Fixture*>(context);
    if (query.world != world_address || query.clock != clock_address) return false;
    ++fixture.hour_writes;
    fixture.clock.sync_base = writes.target_sync_base;
    fixture.clock.sync_anchor = writes.sync_anchor;
    fixture.clock.version = writes.sync_version;
    return true;
}

bool time_capability(void* context, const flight::Identity& identity) noexcept {
    const auto& fixture = *static_cast<Fixture*>(context);
    return fixture.time_allowed && identity == fixture.identity;
}

using QueryCall = void (XHL_NATIVE_TIME_CALL *)(void*, void*, std::uint32_t);
using CallbackCall = void (XHL_NATIVE_TIME_CALL *)(void*);
using ScaleCall = void (XHL_NATIVE_TIME_CALL *)(void*, float, std::int64_t);
using UpdaterCall = void (XHL_NATIVE_TIME_CALL *)(void*, std::uint64_t,
    std::uint64_t, std::int64_t);
using TickCall = void (XHL_NATIVE_TIME_CALL *)(void*, std::int64_t);

struct Hooks {
    std::array<void*, observer::site_count> detours{};
    std::array<bool, observer::site_count> created{};
    std::array<bool, observer::site_count> enabled{};
    std::size_t create_calls = 0;
    std::size_t enable_calls = 0;
    std::size_t disable_calls = 0;
    std::size_t remove_calls = 0;
    std::size_t fail_create_at = observer::site_count;
};

Hooks* current_hooks = nullptr;

void XHL_NATIVE_TIME_CALL original_query(void*, void*, std::uint32_t) {
    if (current_fixture)
        ++current_fixture->original_calls[static_cast<std::size_t>(observer::Site::query_begin)];
}
void XHL_NATIVE_TIME_CALL original_day(void* context) {
    if (!current_fixture || !current_hooks) return;
    ++current_fixture->original_calls[static_cast<std::size_t>(observer::Site::daytime_callback)];
    const auto query = reinterpret_cast<QueryCall>(current_hooks->detours[0]);
    query(context, reinterpret_cast<void*>(query_address), 0x30);
    const auto updater = reinterpret_cast<UpdaterCall>(current_hooks->detours[4]);
    updater(reinterpret_cast<void*>(clock_address), 1, 2, 1000);
    if (!current_fixture->suppress_tick) {
        const auto tick = reinterpret_cast<TickCall>(current_hooks->detours[5]);
        tick(reinterpret_cast<void*>(clock_address), 1000);
    }
}
void XHL_NATIVE_TIME_CALL original_night(void* context) {
    if (!current_fixture || !current_hooks) return;
    ++current_fixture->original_calls[static_cast<std::size_t>(observer::Site::nighttime_skip_callback)];
    const auto query = reinterpret_cast<QueryCall>(current_hooks->detours[0]);
    query(context, reinterpret_cast<void*>(query_address), 0x30);
    const auto scale = reinterpret_cast<ScaleCall>(current_hooks->detours[3]);
    scale(reinterpret_cast<void*>(clock_address), 1.0f, 1000);
}
void XHL_NATIVE_TIME_CALL original_scale(void*, float scale, std::int64_t now) {
    if (!current_fixture) return;
    ++current_fixture->original_calls[static_cast<std::size_t>(observer::Site::scale_writer)];
    const auto base = current_fixture->clock.base_at_now().value_or(
        current_fixture->clock.sync_base);
    current_fixture->clock.sync_base = base;
    current_fixture->clock.sync_anchor = now;
    current_fixture->clock.scale = scale;
    ++current_fixture->clock.version;
}
void XHL_NATIVE_TIME_CALL original_updater(void*, std::uint64_t,
    std::uint64_t, std::int64_t) {
    if (current_fixture)
        ++current_fixture->original_calls[static_cast<std::size_t>(observer::Site::daynight_updater)];
}
void XHL_NATIVE_TIME_CALL original_tick(void*, std::int64_t) {
    if (current_fixture)
        ++current_fixture->original_calls[static_cast<std::size_t>(observer::Site::clock_tick)];
}

void* original_for(std::size_t index) noexcept {
    switch (static_cast<observer::Site>(index)) {
    case observer::Site::query_begin: return reinterpret_cast<void*>(&original_query);
    case observer::Site::daytime_callback: return reinterpret_cast<void*>(&original_day);
    case observer::Site::nighttime_skip_callback: return reinterpret_cast<void*>(&original_night);
    case observer::Site::scale_writer: return reinterpret_cast<void*>(&original_scale);
    case observer::Site::daynight_updater: return reinterpret_cast<void*>(&original_updater);
    case observer::Site::clock_tick: return reinterpret_cast<void*>(&original_tick);
    case observer::Site::count: break;
    }
    return nullptr;
}

bool hook_create(void* context, std::uintptr_t target, void* detour,
    void** original) noexcept {
    auto& hooks = *static_cast<Hooks*>(context);
    const auto index = hooks.create_calls++;
    if (index >= observer::site_count || target != image_base + hook_rvas[index] ||
        index == hooks.fail_create_at) return false;
    hooks.detours[index] = detour;
    hooks.created[index] = true;
    *original = original_for(index);
    return *original != nullptr;
}
bool hook_enable(void* context, std::uintptr_t target) noexcept {
    auto& hooks = *static_cast<Hooks*>(context);
    const auto index = hooks.enable_calls++;
    if (index >= observer::site_count || target != image_base + hook_rvas[index] ||
        !hooks.created[index]) return false;
    hooks.enabled[index] = true;
    return true;
}
bool hook_disable(void* context, std::uintptr_t target) noexcept {
    auto& hooks = *static_cast<Hooks*>(context);
    for (std::size_t i = 0; i < observer::site_count; ++i) {
        if (target == image_base + hook_rvas[i]) {
            ++hooks.disable_calls;
            hooks.enabled[i] = false;
            return true;
        }
    }
    return false;
}
bool hook_remove(void* context, std::uintptr_t target) noexcept {
    auto& hooks = *static_cast<Hooks*>(context);
    for (std::size_t i = 0; i < observer::site_count; ++i) {
        if (target == image_base + hook_rvas[i]) {
            ++hooks.remove_calls;
            hooks.created[i] = false;
            return true;
        }
    }
    return false;
}

flight::Identity test_identity() {
    flight::Identity identity{};
    identity.backend = 0x1000;
    identity.session = 0x2000;
    identity.world = world_address;
    identity.player = 64;
    identity.machine = 1;
    identity.peer = 1;
    identity.steam = 0x0110000100000001ULL;
    identity.authentication = 7;
    identity.lifecycle = 3;
    return identity;
}

Fixture make_fixture() {
    Fixture fixture{};
    fixture.identity = test_identity();
    fixture.clock.now = 1000;
    fixture.clock.sync_base = 80 * cycle_units / 24;
    fixture.clock.sync_anchor = 1000;
    fixture.clock.scale = normal_scale;
    fixture.clock.version = 4;
    fixture.clock.schedule = {
        6 * cycle_units / 24, 18 * cycle_units / 24,
        48 * cycle_units / 24, 24 * cycle_units / 24
    };
    return fixture;
}

StartOptions make_options(Fixture& fixture, Hooks& hooks, Mode mode) {
    StartOptions options{};
    options.mode = mode;
    options.explicitly_enabled = true;
    options.observation.image_base = image_base;
    options.observation.readers.context = &fixture;
    options.observation.readers.read_memory = &read_memory;
    options.observation.readers.thread_id = &thread_id;
    options.observation.readers.monotonic_ms = &monotonic_ms;
    options.observation.readers.validate_image = &validate_image;
    options.observation.hooks = observer::HookBackend{
        &hooks, &hook_create, &hook_enable, &hook_disable, &hook_remove
    };
    options.observation.sample_duration_ms = 500;
    options.host.context = &fixture;
    options.host.read_snapshot = &read_snapshot;
    options.host.write_hour_fields = &write_hour;
    options.host.time_capability = CapabilityProvider{&fixture, &time_capability};
    return options;
}

void add_native_callback(std::array<observer::Event, 12>& events,
    std::size_t& count, observer::Site callback_site, std::uint64_t epoch,
    std::uint64_t& sequence, std::uint64_t world_label,
    std::uint64_t clock_label) {
    auto push = [&](observer::Event event) {
        event.sequence = sequence++;
        event.callback_epoch = epoch;
        events[count++] = event;
    };
    observer::Event event{};
    event.site = callback_site;
    event.enclosing_callback = callback_site;
    event.kind = observer::EventKind::callback_enter;
    event.active_callbacks = 1;
    event.nesting_depth = 1;
    push(event);

    event = {};
    event.site = observer::Site::query_begin;
    event.enclosing_callback = callback_site;
    event.kind = observer::EventKind::query_result;
    event.query_label = 1;
    event.world_label = world_label;
    event.clock_label = clock_label;
    event.clock.complete = true;
    event.clock.version_stable = true;
    push(event);

    if (callback_site == observer::Site::daytime_callback) {
        const std::array<std::pair<observer::Site, observer::EventKind>, 4> writers{{
            {observer::Site::daynight_updater, observer::EventKind::updater_before},
            {observer::Site::daynight_updater, observer::EventKind::updater_after},
            {observer::Site::clock_tick, observer::EventKind::tick_before},
            {observer::Site::clock_tick, observer::EventKind::tick_after}
        }};
        for (const auto& [site, kind] : writers) {
            event = {};
            event.site = site;
            event.enclosing_callback = callback_site;
            event.kind = kind;
            event.world_label = world_label;
            event.clock_label = clock_label;
            event.clock.complete = true;
            event.clock.version_stable = true;
            push(event);
        }
    } else {
        const std::array<observer::EventKind, 2> writers{
            observer::EventKind::scale_before, observer::EventKind::scale_after
        };
        for (const auto kind : writers) {
            event = {};
            event.site = observer::Site::scale_writer;
            event.enclosing_callback = callback_site;
            event.kind = kind;
            event.world_label = world_label;
            event.clock_label = clock_label;
            event.clock.complete = true;
            event.clock.version_stable = true;
            push(event);
        }
    }
    event = {};
    event.site = callback_site;
    event.enclosing_callback = callback_site;
    event.kind = observer::EventKind::callback_exit;
    event.active_callbacks = 1;
    event.nesting_depth = 1;
    push(event);
}

void add_adapter_callback(std::array<PhaseEvent, 12>& events,
    std::size_t& count, observer::Site callback_site, std::uint64_t id,
    std::uint64_t& sequence, std::uint64_t world_label,
    std::uint64_t clock_label, WorldScope scope) {
    auto push = [&](PhaseEvent event) {
        event.sequence = sequence++;
        event.callback_id = id;
        event.callback_site = callback_site;
        event.world_scope = scope;
        events[count++] = event;
    };
    PhaseEvent event{};
    event.site = callback_site;
    event.kind = PhaseEventKind::callback_enter;
    event.active_callbacks = 1;
    event.nesting_depth = 1;
    push(event);

    event = {};
    event.site = observer::Site::query_begin;
    event.kind = PhaseEventKind::query_ready;
    event.world_label = world_label;
    event.clock_label = clock_label;
    push(event);

    if (callback_site == observer::Site::daytime_callback) {
        const std::array<std::pair<observer::Site, PhaseEventKind>, 4> writers{{
            {observer::Site::daynight_updater, PhaseEventKind::updater_before},
            {observer::Site::daynight_updater, PhaseEventKind::updater_after},
            {observer::Site::clock_tick, PhaseEventKind::tick_before},
            {observer::Site::clock_tick, PhaseEventKind::tick_after}
        }};
        for (const auto& [site, kind] : writers) {
            event = {};
            event.site = site;
            event.kind = kind;
            event.world_label = world_label;
            event.clock_label = clock_label;
            push(event);
        }
    } else {
        const std::array<std::pair<observer::Site, PhaseEventKind>, 2> writers{{
            {observer::Site::scale_writer, PhaseEventKind::scale_before},
            {observer::Site::scale_writer, PhaseEventKind::scale_after}
        }};
        for (const auto& [site, kind] : writers) {
            event = {};
            event.site = site;
            event.kind = kind;
            event.world_label = world_label;
            event.clock_label = clock_label;
            push(event);
        }
    }
    event = {};
    event.site = callback_site;
    event.kind = PhaseEventKind::callback_exit;
    event.active_callbacks = 1;
    event.nesting_depth = 1;
    event.world_label = world_label;
    event.clock_label = clock_label;
    push(event);
}

void test_phase_validator() {
    std::array<observer::Event, 12> native_events{};
    std::array<PhaseEvent, 12> adapter_events{};
    std::size_t native_count = 0;
    std::size_t adapter_count = 0;
    std::uint64_t native_sequence = 1;
    std::uint64_t adapter_sequence = 1;
    add_native_callback(native_events, native_count,
        observer::Site::daytime_callback, 1, native_sequence, 11, 22);
    add_native_callback(native_events, native_count,
        observer::Site::nighttime_skip_callback, 2, native_sequence, 11, 22);
    add_adapter_callback(adapter_events, adapter_count,
        observer::Site::daytime_callback, 101, adapter_sequence, 111, 222, 77);
    add_adapter_callback(adapter_events, adapter_count,
        observer::Site::nighttime_skip_callback, 102, adapter_sequence, 111, 222, 77);
    observer::Statistics stats{};
    stats.hooks_ready = true;
    stats.sample_expired = true;
    stats.callbacks_entered = 2;
    stats.maximum_concurrent_callbacks = 1;
    stats.scale_writer_calls = 1;
    stats.events_recorded = native_count;

    const auto accepted = assess_phase(stats, native_events, adapter_events);
    check(accepted.status == PhaseStatus::accepted &&
          accepted.accepted_pair_count == 1,
          "phase validator accepts stable paired day/night writer traces");

    auto overlap = stats;
    overlap.callbacks_overlapped = 1;
    const auto rejected_overlap = assess_phase(overlap, native_events, adapter_events);
    check(rejected_overlap.status == PhaseStatus::rejected &&
          rejected_overlap.reason == PhaseReason::callback_overlap,
          "phase validator rejects callback overlap");

    auto missing = stats;
    auto short_events = std::span<const observer::Event>(native_events.data(),
        native_count - 1);
    const auto rejected_loss = assess_phase(missing, short_events, adapter_events);
    check(rejected_loss.reason == PhaseReason::event_loss,
          "phase validator rejects an incomplete observer ring");

    auto wrong_scope = adapter_events;
    for (auto& event : wrong_scope) {
        if (event.callback_site == observer::Site::nighttime_skip_callback)
            event.world_scope = 78;
    }
    const auto rejected_scope = assess_phase(stats, native_events, wrong_scope);
    check(rejected_scope.reason == PhaseReason::no_shared_world_clock,
          "phase validator rejects a changed host scope between paired callbacks");

    auto unstable = native_events;
    unstable[2].clock.version_stable = false;
    const auto rejected_clock = assess_phase(stats, unstable, adapter_events);
    check(rejected_clock.reason == PhaseReason::unstable_clock,
          "phase validator rejects a writer snapshot with a changing version");
}

void test_create_failure() {
    auto fixture = make_fixture();
    auto hooks = Hooks{};
    hooks.fail_create_at = 2;
    current_fixture = &fixture;
    current_hooks = &hooks;
    static RuntimeAdapter adapter;
    static observer::ReadOnlyObserver observed;
    const auto options = make_options(fixture, hooks, Mode::observe_only);
    check(!adapter.start(observed, options), "partial hook creation fails startup");
    check(adapter.state() == State::failed && !adapter.hooks_ready(),
          "partial hook creation leaves backend failed closed");
    check(hooks.enable_calls == 0 && hooks.remove_calls == 2,
          "partial hook creation removes only already-created native hooks");
}

void test_runtime_forwarding_and_policy() {
    auto fixture = make_fixture();
    Hooks hooks{};
    current_fixture = &fixture;
    current_hooks = &hooks;

    static RuntimeAdapter adapter;
    static observer::ReadOnlyObserver observed;
    StartOptions defaults{};
    check(adapter.start(observed, defaults), "default adapter start is a no-op");
    check(hooks.create_calls == 0 && !adapter.hooks_ready(),
          "default-off start does not install hooks");

    const auto options = make_options(fixture, hooks, Mode::observe_only);
    check(adapter.start(observed, options), "explicit read-only adapter startup succeeds");
    check(adapter.state() == State::running && adapter.hooks_ready() &&
          hooks.create_calls == observer::site_count &&
          hooks.enable_calls == observer::site_count,
          "one wrapped observer owns all six native hook targets");
    check(!adapter.command_backend_ready() &&
          adapter.set_hour(fixture.identity, 42, fixture.scope, 12) ==
              SubmitStatus::denied,
          "observe-only mode cannot accept a time command");

    const auto day = reinterpret_cast<CallbackCall>(hooks.detours[
        static_cast<std::size_t>(observer::Site::daytime_callback)]);
    const auto night = reinterpret_cast<CallbackCall>(hooks.detours[
        static_cast<std::size_t>(observer::Site::nighttime_skip_callback)]);
    const auto query = reinterpret_cast<QueryCall>(hooks.detours[
        static_cast<std::size_t>(observer::Site::query_begin)]);
    check(day != nullptr && night != nullptr, "day and night bridge detours are installed");
    const auto host_reads_before_unscoped_query = fixture.host_reads;
    const auto observed_queries_before_unscoped_query =
        observed.statistics().queries_observed;
    query(reinterpret_cast<void*>(callback_context),
          reinterpret_cast<void*>(query_address), 0x30);
    check(fixture.original_calls[static_cast<std::size_t>(
              observer::Site::query_begin)] == 1 &&
          fixture.host_reads == host_reads_before_unscoped_query &&
          observed.statistics().queries_observed ==
              observed_queries_before_unscoped_query,
          "shared query calls outside time callbacks only forward the native original");
    day(reinterpret_cast<void*>(callback_context));
    night(reinterpret_cast<void*>(callback_context));
    check(fixture.original_calls[static_cast<std::size_t>(
              observer::Site::daytime_callback)] == 1 &&
          fixture.original_calls[static_cast<std::size_t>(
              observer::Site::nighttime_skip_callback)] == 1 &&
          fixture.original_calls[static_cast<std::size_t>(
              observer::Site::query_begin)] == 3 &&
          fixture.original_calls[static_cast<std::size_t>(
              observer::Site::daynight_updater)] == 1 &&
          fixture.original_calls[static_cast<std::size_t>(
              observer::Site::clock_tick)] == 1 &&
          fixture.original_calls[static_cast<std::size_t>(
              observer::Site::scale_writer)] == 1,
          "wrapped day/night callbacks and all nested originals run exactly once");

    fixture.monotonic = 700;
    const auto phase = adapter.refresh_phase_observation();
    check(phase.status == PhaseStatus::accepted &&
          phase.daytime_callbacks == 1 && phase.nighttime_callbacks == 1 &&
          phase.scale_writes == 1,
          "runtime sample accepts stable shared world/clock writer ordering");
    check(adapter.enable_command_writes() && adapter.command_backend_ready(),
          "writes require both accepted phase evidence and explicit enable");

    check(fixture.host_saw_native_now && fixture.last_host_now == 1000,
          "the owning callback supplies its native tick timestamp to the host");

    const WorldScope unobserved_scope = test_scope + 1;
    check(adapter.set_hour(fixture.identity, 42, unobserved_scope, 12) ==
              SubmitStatus::denied,
          "a world scope without accepted callback phase evidence cannot queue writes");
    fixture.scope = unobserved_scope;
    day(reinterpret_cast<void*>(callback_context));
    check(fixture.hour_writes == 0 && fixture.clock.scale == normal_scale,
          "a changed current scope cannot reuse the prior scope's accepted phase pair");
    fixture.scope = test_scope;

    fixture.time_allowed = false;
    check(adapter.set_hour(fixture.identity, 42, fixture.scope, 12) ==
              SubmitStatus::denied && fixture.hour_writes == 0,
          "default-denied time capability blocks queued commands");
    fixture.time_allowed = true;
    auto stale = fixture.identity;
    ++stale.lifecycle;
    check(adapter.set_hour(stale, 42, fixture.scope, 12) == SubmitStatus::denied,
          "a superseded full identity cannot enqueue a time command");

    check(adapter.set_hour(fixture.identity, 42, fixture.scope, 12) ==
              SubmitStatus::queued,
          "authorized set-hour queues after phase acceptance");
    fixture.suppress_tick = true;
    day(reinterpret_cast<void*>(callback_context));
    check(fixture.hour_writes == 0 && adapter.queued_commands() == 1,
          "a callback without a current native tick cannot apply queued time writes");
    fixture.suppress_tick = false;
    day(reinterpret_cast<void*>(callback_context));
    check(fixture.hour_writes == 1 &&
          adapter.last_apply_status() == ApplyStatus::applied,
          "set-hour writes once from the owning callback after original completion");

    check(adapter.set_mode(fixture.identity, 42, fixture.scope,
              WorldTimeMode::pause) == SubmitStatus::queued,
          "authorized pause command queues");
    day(reinterpret_cast<void*>(callback_context));
    check(fixture.clock.scale == paused_scale &&
          fixture.original_calls[static_cast<std::size_t>(
              observer::Site::scale_writer)] == 2,
          "pause preserves the hour and invokes the native scale original once");

    fixture.time_allowed = false;
    day(reinterpret_cast<void*>(callback_context));
    check(fixture.clock.scale == normal_scale &&
          fixture.original_calls[static_cast<std::size_t>(
              observer::Site::scale_writer)] == 3,
          "permission revocation restores the captured normal rate through a fresh callback");

    fixture.time_allowed = true;
    check(adapter.retire_world_scope(fixture.scope) == 0,
          "scope retirement marks the current world after all commands complete");
    check(adapter.set_hour(fixture.identity, 42, fixture.scope, 12) ==
              SubmitStatus::denied &&
          adapter.set_mode(fixture.identity, 42, fixture.scope,
              WorldTimeMode::pause) == SubmitStatus::denied,
          "a retired world scope cannot accept new hour or mode commands");
    const auto calls_before_stop = fixture.original_calls[
        static_cast<std::size_t>(observer::Site::daytime_callback)];
    check(adapter.stop() && adapter.state() == State::stopped &&
          hooks.disable_calls == observer::site_count,
          "stop disables all six hooks without releasing process-lifetime trampolines");
    day(reinterpret_cast<void*>(callback_context));
    check(fixture.original_calls[static_cast<std::size_t>(
              observer::Site::daytime_callback)] == calls_before_stop + 1,
          "late detour entry after stop still forwards its original exactly once");
    check(fixture.hour_writes == 1,
          "late post-stop callback cannot write the native clock");
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--create-failure") {
        test_create_failure();
    } else {
        test_phase_validator();
        test_runtime_forwarding_and_policy();
    }
    current_fixture = nullptr;
    current_hooks = nullptr;
    if (failures) return 1;
    std::cout << "native_time runtime adapter checks passed (synthetic hooks/memory only).\n";
    return 0;
}




