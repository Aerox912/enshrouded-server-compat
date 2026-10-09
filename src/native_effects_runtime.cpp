#include "native_effects_runtime.hpp"
#include "adapter.hpp"
#include "flight_native.hpp"
#include "flight_identity.hpp"
#include "no_cost.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <mutex>

namespace xhl::native_effects {
namespace {
std::atomic<std::uintptr_t> image_base{0};
std::atomic<bool> hooks_active{false};
std::atomic<Services::ActiveEffects> lease_lookup{nullptr};
std::mutex install_guard;
bool hooks_prepared = false;
std::array<bool, 3> site_enabled{};
void* original_system_raw = nullptr;
void* original_action_raw = nullptr;
void* original_processor_raw = nullptr;

using NativeCraftSystem = void (__fastcall *)(void* query_context);
NativeCraftSystem original_system = nullptr;
no_cost::NativeCraftAction original_action = nullptr;
no_cost::NativeCraftProcessor original_processor = nullptr;

struct CraftingQueryFrame {
    std::uintptr_t world = 0;
    std::uintptr_t query_context = 0;
    std::uintptr_t actor_descriptor = 0;
    bool valid = false;
};
thread_local const CraftingQueryFrame* active_query_frame = nullptr;

constexpr std::array<std::uintptr_t, 3> hook_rvas{
    0x6c1a0, 0x1aba90, 0x150990};
constexpr std::uint16_t implemented_effects =
    static_cast<std::uint16_t>(Effect::free_crafting);

bool copy_memory(std::uintptr_t address, void* out, std::size_t size) noexcept {
    if (!address || size > UINTPTR_MAX - address) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool write_memory(std::uintptr_t address, const void* in, std::size_t size) noexcept {
    if (!address || size > UINTPTR_MAX - address) return false;
    __try {
        std::memcpy(reinterpret_cast<void*>(address), in, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

template<class F>
F native_function(std::uintptr_t base, std::uintptr_t rva) noexcept {
    return reinterpret_cast<F>(base + rva);
}

using ContextEcs = void* (__fastcall *)(void* context);
using ResolveComponent = void* (__fastcall *)(void* result, void* ecs,
    void* descriptor, std::uint32_t entity_id);

void* actor_component(std::uintptr_t base, void* ecs, std::uintptr_t descriptor,
    std::uint32_t entity_id) noexcept {
    if (!base || !ecs || !descriptor || !entity_id || entity_id > 0x3ff) return nullptr;
    // The first result word is the component pointer in the Creative inventory
    // Frame resolver. Preserve the full 32-bit EntityId; do not mask it into an
    // owner slot or discard any native key bits.
    std::array<std::uintptr_t, 2> result{};
    native_function<ResolveComponent>(base, 0x5c8f30)(result.data(), ecs,
        reinterpret_cast<void*>(descriptor), entity_id);
    return reinterpret_cast<void*>(result[0]);
}

struct QueryAccess {
    std::uintptr_t world = 0;
    void* ecs = nullptr;
};

bool query_access(const CraftingEventEvidence& event, std::uintptr_t base,
    QueryAccess& access) noexcept {
    access = {};
    if (!event.query_context || !event.world ||
        !xhl::flight::read_query_world(copy_memory, event.query_context, access.world) ||
        access.world != event.world) return false;

    std::uintptr_t inner_context = 0;
    if (!copy_memory(event.query_context, &inner_context, sizeof(inner_context)) ||
        !inner_context) return false;
    access.ecs = native_function<ContextEcs>(base, 0x5d52d0)(
        reinterpret_cast<void*>(inner_context));
    return access.ecs != nullptr;
}

std::optional<ActorOwnerMatch> find_owner(std::uintptr_t base,
    const CraftingEventEvidence& event, const QueryAccess& access,
    std::uintptr_t operator_component) noexcept {
    std::array<ActorOwnerCandidate, 16> candidates{};
    for (std::uint32_t owner = 1; owner <= candidates.size(); ++owner) {
        const auto identity = xhl::flight::authenticated_owner(access.world, owner);
        if (!identity || identity->world != access.world ||
            !identity->valid(owner - 1) || !identity->lifecycle) continue;
        const auto component = actor_component(base, access.ecs,
            event.actor_descriptor, owner);
        candidates[owner - 1] = {owner, reinterpret_cast<std::uintptr_t>(component),
            *identity, xhl::flight::owner_alive(*identity)};
    }
    return match_authenticated_actor(event.crafting_operator_id,
        operator_component, access.world, candidates);
}

std::optional<Identity> resolve_once(const CraftingEventEvidence& event,
    std::uintptr_t base, std::uintptr_t& operator_component,
    ActorOwnerMatch& match) noexcept {
    QueryAccess access;
    if (!event.actor_descriptor || !event.query_entity ||
        !event.crafting_station_id || event.query_entity != event.crafting_station_id ||
        !event.crafting_operator_id || event.crafting_operator_id > 0x3ff ||
        !query_access(event, base, access)) return {};

    operator_component = reinterpret_cast<std::uintptr_t>(actor_component(base,
        access.ecs, event.actor_descriptor, event.crafting_operator_id));
    if (!operator_component) return {};
    const auto found = find_owner(base, event, access, operator_component);
    if (!found) return {};
    match = *found;
    return found->identity;
}

std::optional<Identity> resolve_callback(
    const CraftingEventEvidence& event) noexcept {
    return resolve_crafting_operator_owner(event);
}

bool alive_callback(const Identity& identity) noexcept {
    return xhl::flight::owner_alive(identity);
}

class QueryFrameScope {
    const CraftingQueryFrame* previous_ = nullptr;
public:
    explicit QueryFrameScope(const CraftingQueryFrame& frame) noexcept
        : previous_(active_query_frame) { active_query_frame = &frame; }
    ~QueryFrameScope() { active_query_frame = previous_; }
    QueryFrameScope(const QueryFrameScope&) = delete;
    QueryFrameScope& operator=(const QueryFrameScope&) = delete;
};

class QueryCursorRestore {
    std::uintptr_t address_ = 0;
    std::uint32_t value_ = 0;
public:
    explicit QueryCursorRestore(std::uintptr_t query) noexcept {
        if (query && query <= UINTPTR_MAX - 8 &&
            copy_memory(query + 8, &value_, sizeof(value_))) address_ = query + 8;
    }
    ~QueryCursorRestore() {
        if (address_) (void)write_memory(address_, &value_, sizeof(value_));
    }
    bool valid() const noexcept { return address_ != 0; }
    QueryCursorRestore(const QueryCursorRestore&) = delete;
    QueryCursorRestore& operator=(const QueryCursorRestore&) = delete;
};

using QueryBegin = void (__fastcall *)(void*, void*, std::uint32_t);
using QueryNext = bool (__fastcall *)(void*, void*, std::uint32_t);
using QueryEntity = void* (__fastcall *)(void*, void*);

std::uintptr_t capture_actor_descriptor(std::uintptr_t base,
    std::uintptr_t query_context) noexcept {
    if (!base || !query_context) return 0;
    QueryCursorRestore restore(query_context);
    if (!restore.valid()) return 0;

    alignas(16) std::array<std::uint8_t, 0xe8> row{};
    native_function<QueryBegin>(base, 0x5dc7f0)(
        reinterpret_cast<void*>(query_context), row.data(), 0xe8);
    if (!native_function<QueryNext>(base, 0x5d7960)(
        reinterpret_cast<void*>(query_context), row.data(), 0xe8)) return 0;

    std::uintptr_t descriptor = 0;
    if (!copy_memory(reinterpret_cast<std::uintptr_t>(row.data()) + 0x38,
        &descriptor, sizeof(descriptor))) return 0;
    return descriptor;
}

CraftingQueryFrame capture_query_frame(std::uintptr_t base,
    void* query_context) noexcept {
    CraftingQueryFrame frame{};
    frame.query_context = reinterpret_cast<std::uintptr_t>(query_context);
    if (!query_context || !xhl::flight::read_query_world(copy_memory,
        frame.query_context, frame.world)) return frame;
    frame.actor_descriptor = capture_actor_descriptor(base, frame.query_context);
    frame.valid = frame.world && frame.actor_descriptor;
    return frame;
}

void __fastcall crafting_system_hook(void* query_context) {
    if (!original_system) return;
    if (!hooks_active.load(std::memory_order_acquire)) {
        original_system(query_context);
        return;
    }
    const auto base = image_base.load(std::memory_order_acquire);
    const auto frame = capture_query_frame(base, query_context);
    QueryFrameScope scope(frame);
    original_system(query_context);
}

std::uint32_t __fastcall crafting_action_hook(void* event_context, void* recipe) {
    if (!original_action) return 0;
    std::optional<Authorization> authorization;
    const auto* frame = active_query_frame;
    if (hooks_active.load(std::memory_order_acquire) && frame && frame->valid) {
        const auto event_address = reinterpret_cast<std::uintptr_t>(event_context);
        std::uint32_t operator_id = 0, station_id = 0, query_entity = 0;
        if (event_address && event_address <= UINTPTR_MAX - 8 &&
            copy_memory(event_address, &operator_id, sizeof(operator_id)) &&
            copy_memory(event_address + 4, &station_id, sizeof(station_id))) {
            const auto base = image_base.load(std::memory_order_acquire);
            const auto entity_result = native_function<QueryEntity>(base, 0x5d5130)(
                reinterpret_cast<void*>(frame->query_context), &query_entity);
            if (entity_result == &query_entity && query_entity) {
                const CraftingEventEvidence evidence{frame->world,
                    frame->query_context, frame->actor_descriptor, query_entity,
                    operator_id, station_id};
                const auto services = make_native_crafting_services(
                    lease_lookup.load(std::memory_order_acquire));
                authorization = authorize(evidence, Effect::free_crafting,
                    GetTickCount64(), services);
            }
        }
    }

    std::uint32_t result = 0;
    (void)no_cost::invoke_native_crafting_action(original_action, event_context,
        recipe, authorization ? &*authorization : nullptr, result);
    return result;
}

bool current_crafting_lease(const Authorization* authorization) noexcept {
    if (!authorization || authorization->effect() != Effect::free_crafting ||
        !hooks_active.load(std::memory_order_acquire)) return false;
    const auto& identity = authorization->identity();
    const auto owner_slot = identity.player & 63;
    if (!identity.world || owner_slot >= 16 || !identity.valid(owner_slot) ||
        !identity.lifecycle || !xhl::flight::owner_alive(identity)) return false;
    const auto current = xhl::flight::authenticated_owner(identity.world, owner_slot);
    if (!current || *current != identity) return false;

    const auto lease = lease_lookup.load(std::memory_order_acquire);
    if (!lease) return false;
    const auto effects = lease(identity, GetTickCount64());
    return effects && !(*effects & ~valid_effect_mask) &&
        (*effects & static_cast<std::uint16_t>(Effect::free_crafting)) &&
        hooks_active.load(std::memory_order_acquire);
}

std::uint32_t __fastcall crafting_processor_hook(void* transaction, void* recipe,
    void* output, std::uint64_t recipe_options, void* validation_state,
    void* action_data, std::uint32_t operator_entity_id,
    std::uint32_t invocation_flags) {
    if (!original_processor) return 0;
    const no_cost::NativeCraftProcessorArgs args{transaction, recipe, output,
        recipe_options, validation_state, action_data, operator_entity_id,
        invocation_flags};
    std::uint32_t result = 0;
    if (!current_crafting_lease(no_cost::current_crafting_authorization())) {
        // Recheck the identity, life, and lease at the native transaction
        // boundary. A denied or revoked nested action masks any enclosing
        // thread-local authorization before the processor reads its flags.
        no_cost::CraftingActionScope denied(nullptr);
        (void)no_cost::invoke_native_crafting_processor(original_processor,
            args, result);
    } else {
        (void)no_cost::invoke_native_crafting_processor(original_processor,
            args, result);
    }
    return result;
}

bool signatures_match(std::uintptr_t base) noexcept {
    static constexpr std::array<std::uint8_t, 16> system{
        0x40,0x55,0x41,0x54,0x48,0x8d,0xac,0x24,
        0xb8,0xfd,0xff,0xff,0x48,0x81,0xec,0x48};
    static constexpr std::array<std::uint8_t, 16> action{
        0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,
        0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x55};
    static constexpr std::array<std::uint8_t, 16> processor{
        0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x41,
        0x56,0x48,0x81,0xec,0x80,0x00,0x00,0x00};
    return base &&
        !std::memcmp(reinterpret_cast<const void*>(base + hook_rvas[0]),
            system.data(), system.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(base + hook_rvas[1]),
            action.data(), action.size()) &&
        !std::memcmp(reinterpret_cast<const void*>(base + hook_rvas[2]),
            processor.data(), processor.size());
}
}

bool initialize_operator_resolver(HMODULE server) noexcept {
    if (!server || !xhl::validate_server(server)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(server);
    auto expected = std::uintptr_t{0};
    if (image_base.compare_exchange_strong(expected, base,
        std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    return expected == base;
}

bool operator_resolver_ready() noexcept {
    return image_base.load(std::memory_order_acquire) != 0;
}

bool install_native_cost_hooks(HMODULE server,
    Services::ActiveEffects lease) noexcept {
    if (!server || !lease || !initialize_operator_resolver(server)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(server);
    if (!signatures_match(base)) return false;

    std::lock_guard lock(install_guard);
    lease_lookup.store(lease, std::memory_order_release);
    if (!hooks_prepared) {
        const auto initialized = MH_Initialize();
        if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
            return false;

        const std::array<LPVOID, 3> targets{
            reinterpret_cast<LPVOID>(base + hook_rvas[0]),
            reinterpret_cast<LPVOID>(base + hook_rvas[1]),
            reinterpret_cast<LPVOID>(base + hook_rvas[2])};
        const std::array<LPVOID, 3> detours{
            reinterpret_cast<LPVOID>(&crafting_system_hook),
            reinterpret_cast<LPVOID>(&crafting_action_hook),
            reinterpret_cast<LPVOID>(&crafting_processor_hook)};
        std::array<void**, 3> originals{
            &original_system_raw, &original_action_raw, &original_processor_raw};
        std::size_t created = 0;
        for (; created < targets.size(); ++created) {
            if (MH_CreateHook(targets[created], detours[created],
                originals[created]) != MH_OK) break;
        }
        if (created != targets.size()) {
            while (created) {
                --created;
                (void)MH_RemoveHook(targets[created]);
                *originals[created] = nullptr;
            }
            lease_lookup.store(nullptr, std::memory_order_release);
            return false;
        }
        original_system = reinterpret_cast<NativeCraftSystem>(original_system_raw);
        original_action = reinterpret_cast<no_cost::NativeCraftAction>(original_action_raw);
        original_processor = reinterpret_cast<no_cost::NativeCraftProcessor>(original_processor_raw);
        hooks_prepared = true;
    }

    if (hooks_active.load(std::memory_order_acquire)) return true;
    constexpr std::array<std::uintptr_t, 3> rvas = hook_rvas;
    std::size_t enabled = 0;
    for (; enabled < rvas.size(); ++enabled) {
        if (site_enabled[enabled]) continue;
        if (MH_EnableHook(reinterpret_cast<LPVOID>(base + rvas[enabled])) != MH_OK)
            break;
        site_enabled[enabled] = true;
    }
    if (enabled != rvas.size()) {
        hooks_active.store(false, std::memory_order_release);
        for (std::size_t i = rvas.size(); i-- > 0;) {
            if (!site_enabled[i]) continue;
            if (MH_DisableHook(reinterpret_cast<LPVOID>(base + rvas[i])) == MH_OK)
                site_enabled[i] = false;
        }
        lease_lookup.store(nullptr, std::memory_order_release);
        return false;
    }
    hooks_active.store(true, std::memory_order_release);
    return true;
}

bool stop_native_cost_hooks() noexcept {
    std::lock_guard lock(install_guard);
    hooks_active.store(false, std::memory_order_release);
    const auto base = image_base.load(std::memory_order_acquire);
    if (!base || !hooks_prepared) {
        lease_lookup.store(nullptr, std::memory_order_release);
        return true;
    }

    bool success = true;
    for (std::size_t i = hook_rvas.size(); i-- > 0;) {
        if (!site_enabled[i]) continue;
        if (MH_DisableHook(reinterpret_cast<LPVOID>(base + hook_rvas[i])) == MH_OK)
            site_enabled[i] = false;
        else success = false;
    }
    // Keep the provider pointer and trampolines for process lifetime. Disabled
    // detours are pass-through and will not invoke the lease lookup.
    return success;
}

std::uint16_t supported_effects() noexcept {
    std::lock_guard lock(install_guard);
    if (!hooks_active.load(std::memory_order_acquire)) return 0;
    for (const auto enabled : site_enabled) if (!enabled) return 0;
    return implemented_effects;
}

std::optional<Identity> resolve_crafting_operator_owner(
    const CraftingEventEvidence& event) noexcept {
    const auto base = image_base.load(std::memory_order_acquire);
    if (!base) return {};

    // This entire resolver runs synchronously while the native event callback's
    // query context and descriptor are live. Re-read native ownership and both
    // Actor pointers after the first match; then the policy layer repeats the
    // owner/liveness check after consulting the Creative lease.
    std::uintptr_t operator_before = 0;
    ActorOwnerMatch first{};
    const auto identity_before = resolve_once(event, base, operator_before, first);
    if (!identity_before) return {};

    std::uintptr_t operator_after = 0;
    ActorOwnerMatch second{};
    const auto identity_after = resolve_once(event, base, operator_after, second);
    if (!identity_after || *identity_after != *identity_before ||
        operator_after != operator_before || second.component != first.component ||
        second.compact_owner != first.compact_owner ||
        second.identity != first.identity ||
        !xhl::flight::owner_alive(second.identity)) return {};

    const auto current = xhl::flight::authenticated_owner(event.world,
        second.compact_owner);
    if (!current || *current != second.identity) return {};
    return second.identity;
}

Services make_native_crafting_services(Services::ActiveEffects lease) noexcept {
    if (!operator_resolver_ready() || !lease) return {};
    return {resolve_callback, alive_callback, lease};
}

}
