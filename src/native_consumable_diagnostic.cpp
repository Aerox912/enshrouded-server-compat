#include "native_consumable_diagnostic.hpp"

#include "adapter.hpp"
#include "flight_identity.hpp"
#include "flight_native.hpp"
#include "native_consumable_cost.hpp"
#include "native_consumable_result_write.hpp"
#include "native_consumable_runtime_adapter.hpp"
#include "native_cost_hook_lifecycle.hpp"

#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <mutex>

namespace xhl::native_consumable_diagnostic {
namespace {
using ActiveEffects = native_effects::Services::ActiveEffects;
using Mode = native_consumable_runtime_adapter::Mode;
using NativeUsedAction = native_consumable_runtime_adapter::UsedAction;
using NativeFiredAction = native_consumable_runtime_adapter::FiredAction;
using NativeRemoveItems = native_consumable_cost::NativeRemoveItems;

constexpr std::array<std::uintptr_t, 3> hook_rvas{0x5c3a0, 0x30a40, 0x163e70};
constexpr std::uintptr_t used_action_rva = hook_rvas[0];
constexpr std::uintptr_t fired_action_rva = hook_rvas[1];
constexpr std::uintptr_t remove_items_rva = hook_rvas[2];
constexpr std::uintptr_t used_remove_return_rva = 0x5c985;
constexpr std::uintptr_t fired_remove_return_rva = 0x30ce9;
constexpr std::uintptr_t candidate_outer_function_begin = 0x33289;
constexpr std::uintptr_t candidate_outer_function_end = 0x34258;

std::atomic<std::uintptr_t> image_base{0};
std::atomic<Mode> active_mode{Mode::inactive};
std::atomic<ActiveEffects> lease_lookup{nullptr};
std::mutex install_guard;
native_cost_hooks::HookSet<hook_rvas.size()> hook_set;
bool module_pinned = false;
void* original_action_raw = nullptr;
void* original_used_action_raw = nullptr;
void* original_remover_raw = nullptr;
NativeUsedAction original_used_action = nullptr;
NativeFiredAction original_action = nullptr;
NativeRemoveItems original_remover = nullptr;
FiredObservationRing observations;

void __fastcall used_action_hook(void* query_context);
std::uint8_t __fastcall fired_action_hook(void* query_context,
    void* action_data);
void* __fastcall remove_items_hook(void* result, void* transaction,
    std::uint32_t owner, std::uint32_t item, std::uint64_t packed_slot_ref,
    std::uint32_t quantity);

bool minhook_create(void* target, void* detour, void** original) noexcept {
    return MH_CreateHook(target, detour, original) == MH_OK;
}
bool minhook_remove(void* target) noexcept {
    return MH_RemoveHook(target) == MH_OK;
}
bool minhook_enable(void* target) noexcept {
    return MH_EnableHook(target) == MH_OK;
}
bool minhook_disable(void* target) noexcept {
    return MH_DisableHook(target) == MH_OK;
}
constexpr native_cost_hooks::Operations hook_operations{
    minhook_create, minhook_remove, minhook_enable, minhook_disable};

std::array<void*, hook_rvas.size()> hook_targets(std::uintptr_t base) noexcept {
    return {reinterpret_cast<void*>(base + used_action_rva),
        reinterpret_cast<void*>(base + fired_action_rva),
        reinterpret_cast<void*>(base + remove_items_rva)};
}

std::array<void*, hook_rvas.size()> hook_detours() noexcept {
    return {reinterpret_cast<void*>(&used_action_hook),
        reinterpret_cast<void*>(&fired_action_hook),
        reinterpret_cast<void*>(&remove_items_hook)};
}

bool copy_memory(std::uintptr_t address, void* out, std::size_t size) noexcept {
    if (!address || size > UINTPTR_MAX - address) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

using QueryEntity = void* (__fastcall *)(void* query_context, void* result);

bool read_native_query_owner(std::uintptr_t query_context,
    native_consumable_cost::QueryOwner& output) noexcept {
    output = {};
    const auto base = image_base.load(std::memory_order_acquire);
    if (!base || !query_context) return false;
    std::uintptr_t world = 0;
    if (!xhl::flight::read_query_world(copy_memory, query_context, world))
        return false;
    std::uint32_t owner = 0;
    const auto query_entity = reinterpret_cast<QueryEntity>(base + 0x5d5130);
    if (query_entity(reinterpret_cast<void*>(query_context), &owner) != &owner ||
        owner < 1 || owner > 16) return false;
    std::uintptr_t current_world = 0;
    if (!xhl::flight::read_query_world(copy_memory, query_context,
        current_world) || current_world != world) return false;
    output = {world, owner};
    return true;
}

std::optional<xhl::flight::Identity> resolve_native_owner(
    std::uintptr_t world, std::uint32_t owner) noexcept {
    return xhl::flight::authenticated_owner(world, owner);
}

bool native_owner_alive(const xhl::flight::Identity& identity) noexcept {
    return xhl::flight::owner_alive(identity);
}

std::optional<std::uint16_t> native_active_effects(
    const xhl::flight::Identity& identity, std::uint64_t now_ms) noexcept {
    if (active_mode.load(std::memory_order_acquire) != Mode::free_consumables)
        return {};
    const auto lease = lease_lookup.load(std::memory_order_acquire);
    if (!lease) return {};
    const auto effects = lease(identity, now_ms);
    if (active_mode.load(std::memory_order_acquire) != Mode::free_consumables)
        return {};
    return effects;
}

bool effect_mode_is_current() noexcept {
    return active_mode.load(std::memory_order_acquire) == Mode::free_consumables;
}

bool bytes_match(std::uintptr_t base, std::uintptr_t rva,
    const std::uint8_t* expected, std::size_t size) noexcept {
    std::array<std::uint8_t, 32> actual{};
    return size <= actual.size() &&
        copy_memory(base + rva, actual.data(), size) &&
        std::memcmp(actual.data(), expected, size) == 0;
}

bool call_target(std::uintptr_t base, std::uintptr_t call_rva,
    std::uintptr_t& target) noexcept {
    std::array<std::uint8_t, 5> bytes{};
    if (!copy_memory(base + call_rva, bytes.data(), bytes.size()) ||
        bytes[0] != 0xe8) return false;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
    const auto return_address = static_cast<std::intptr_t>(base + call_rva + 5);
    target = static_cast<std::uintptr_t>(return_address + displacement);
    return true;
}

bool registered_used_action_callback(std::uintptr_t base) noexcept {
    static constexpr std::array<std::uint8_t, 3> getter_entry{0x48,0x8d,0x05};
    static constexpr std::array<std::uint8_t, 3> registration_move{0x48,0x8b,0xd0};
    std::array<std::uint8_t, 8> getter{};
    std::array<std::uint8_t, sizeof("actor_apply_buff")> name{};
    std::uintptr_t getter_target = 0;
    std::uintptr_t registration_target = 0;
    if (!call_target(base, 0x1d6a1e, getter_target) ||
        getter_target != base + 0xa72cb0 ||
        !copy_memory(getter_target, getter.data(), getter.size()) ||
        !std::equal(getter_entry.begin(), getter_entry.end(), getter.begin()) ||
        getter[7] != 0xc3 ||
        !bytes_match(base, 0x1d6a23, registration_move.data(),
            registration_move.size()) ||
        !call_target(base, 0x1d6a2c, registration_target) ||
        registration_target != base + 0x5f7af0) return false;

    std::int32_t descriptor_displacement = 0;
    std::memcpy(&descriptor_displacement, getter.data() + 3,
        sizeof(descriptor_displacement));
    const auto descriptor_address = static_cast<std::uintptr_t>(
        static_cast<std::intptr_t>(base + 0xa72cb0 + 7) +
        descriptor_displacement);
    if (descriptor_address != base + 0x1319530) return false;

    std::uintptr_t name_address = 0;
    std::uintptr_t callback_address = 0;
    if (!copy_memory(descriptor_address, &name_address, sizeof(name_address)) ||
        !copy_memory(descriptor_address + 0x10, &callback_address,
            sizeof(callback_address)) ||
        !copy_memory(name_address, name.data(), name.size())) return false;
    return std::memcmp(name.data(), "actor_apply_buff", name.size()) == 0 &&
        callback_address == base + used_action_rva;
}

bool signatures_match(std::uintptr_t base) noexcept {
    static constexpr std::array<std::uint8_t, 6> used_query_size{
        0x41,0xb8,0xa0,0x00,0x00,0x00};
    static constexpr std::array<std::uint8_t, 16> fired_action_entry{
        0x40,0x55,0x56,0x41,0x54,0x41,0x56,0x48,
        0x8d,0xac,0x24,0xe8,0x99,0xfe,0xff,0xb8};
    static constexpr std::array<std::uint8_t, 18> remover_entry{
        0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,
        0x20,0x55,0x57,0x41,0x54,0x41,0x55,0x41,0x57};
    static constexpr std::array<std::uint8_t, 3> query_owner_copy{
        0x44,0x8b,0x00};
    static constexpr std::array<std::uint8_t, 5> used_result_status{
        0x40,0x38,0x74,0x24,0x38};
    static constexpr std::array<std::uint8_t, 4> used_result_remainder{
        0x39,0x74,0x24,0x3c};
    static constexpr std::array<std::uint8_t, 4> fired_result_status{
        0x80,0x7d,0x10,0x00};
    static constexpr std::array<std::uint8_t, 4> fired_result_remainder{
        0x83,0x7d,0x14,0x00};
    std::uintptr_t query_helper = 0;
    std::uintptr_t remover_call = 0;
    std::uintptr_t used_query_helper = 0;
    std::uintptr_t used_remover_call = 0;
    return base && registered_used_action_callback(base) &&
        bytes_match(base, 0x5c3bd, used_query_size.data(), used_query_size.size()) &&
        call_target(base, 0x5c3ca, used_query_helper) &&
        used_query_helper == base + 0x5dc7f0 &&
        call_target(base, 0x5c3dc, used_remover_call) &&
        used_remover_call == base + 0x5d7960 &&
        call_target(base, 0x5c95c, query_helper) &&
        query_helper == base + 0x5d5130 &&
        bytes_match(base, 0x5c97d, query_owner_copy.data(),
            query_owner_copy.size()) &&
        call_target(base, 0x5c980, used_remover_call) &&
        used_remover_call == base + remove_items_rva &&
        bytes_match(base, used_remove_return_rva, used_result_status.data(),
            used_result_status.size()) &&
        bytes_match(base, 0x5c98c, used_result_remainder.data(),
            used_result_remainder.size()) &&
        bytes_match(base, fired_action_rva, fired_action_entry.data(),
            fired_action_entry.size()) &&
        bytes_match(base, remove_items_rva, remover_entry.data(),
            remover_entry.size()) &&
        bytes_match(base, 0x30ce1, query_owner_copy.data(),
            query_owner_copy.size()) &&
        call_target(base, 0x30cc1, query_helper) &&
        query_helper == base + 0x5d5130 &&
        call_target(base, 0x30ce4, remover_call) &&
        remover_call == base + remove_items_rva &&
        bytes_match(base, fired_remove_return_rva, fired_result_status.data(),
            fired_result_status.size()) &&
        bytes_match(base, 0x30cf3, fired_result_remainder.data(),
            fired_result_remainder.size());
}

bool pin_current_module() noexcept {
    if (module_pinned) return true;
    static int module_anchor = 0;
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&module_anchor), &module)) return false;
    module_pinned = true;
    return true;
}

bool valid_current_identity(const xhl::flight::Identity& identity,
    std::uintptr_t world, std::uint32_t owner) noexcept {
    return world && owner >= 1 && owner <= 16 && identity.world == world &&
        identity.lifecycle && identity.valid(owner - 1);
}

std::uintptr_t capture_candidate_outer_frame(std::uintptr_t base) noexcept {
    std::array<void*, 16> frames{};
    const auto count = CaptureStackBackTrace(0,
        static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    for (USHORT i = 0; i < count; ++i) {
        const auto address = reinterpret_cast<std::uintptr_t>(frames[i]);
        if (address >= base + candidate_outer_function_begin &&
            address < base + candidate_outer_function_end) return address - base;
    }
    return 0;
}

void record_fired_observation(std::uintptr_t return_address,
    std::uint32_t removal_owner,
    const native_consumable_cost::ActionFrame& frame,
    std::uintptr_t base) noexcept {
    if (return_address != base + fired_remove_return_rva ||
        frame.kind != native_consumable_cost::ActionKind::fired ||
        frame.server_base != base || !frame.query_context) return;

    FiredObservation observation{};
    observation.caller_return_rva = return_address - base;
    observation.removal_owner = removal_owner;
    observation.query_owner = removal_owner;
    observation.query_owner_forwarding_proven = true;
    observation.enclosing_frame_rva = capture_candidate_outer_frame(base);
    // COST7's pinned function range proves only a shared enclosing function.
    // Its descriptor/system registration remains unresolved.
    observation.registered_callback_rva = 0;
    observation.callback_registration_proven = false;

    if (!xhl::flight::read_query_world(copy_memory, frame.query_context,
        observation.world)) {
        observations.append(observation);
        return;
    }
    const auto identity = xhl::flight::authenticated_owner(observation.world,
        removal_owner);
    const bool first_valid = identity && valid_current_identity(*identity,
        observation.world, removal_owner);
    const bool first_alive = first_valid && xhl::flight::owner_alive(*identity);
    const auto lease = lease_lookup.load(std::memory_order_acquire);
    observation.lease_known = lease != nullptr;
    std::optional<std::uint16_t> first_effects;
    if (first_alive && lease) {
        first_effects = lease(*identity, GetTickCount64());
    }
    const auto current = first_valid ? xhl::flight::authenticated_owner(
        observation.world, removal_owner) : std::optional<xhl::flight::Identity>{};
    observation.owner_identity_current = current && *current == *identity &&
        valid_current_identity(*current, observation.world, removal_owner);
    observation.actor_alive = observation.owner_identity_current && first_alive &&
        xhl::flight::owner_alive(*current);

    std::optional<std::uint16_t> current_effects;
    if (observation.actor_alive && lease) {
        current_effects = lease(*current, GetTickCount64());
    }
    constexpr auto required = static_cast<std::uint16_t>(
        native_effects::Effect::free_consumables);
    observation.free_consumables_lease_active = observation.lease_known &&
        observation.owner_identity_current && observation.actor_alive &&
        first_effects && current_effects &&
        !(*first_effects & ~native_effects::valid_effect_mask) &&
        !(*current_effects & ~native_effects::valid_effect_mask) &&
        ((*first_effects & required) != 0) &&
        ((*current_effects & required) != 0);
    observations.append(observation);
}

void __fastcall used_action_hook(void* query_context) {
    const auto base = image_base.load(std::memory_order_acquire);
    native_consumable_runtime_adapter::invoke_used_action(original_used_action,
        query_context, base, active_mode.load(std::memory_order_acquire));
}

std::uint8_t __fastcall fired_action_hook(void* query_context,
    void* action_data) {
    const auto base = image_base.load(std::memory_order_acquire);
    return native_consumable_runtime_adapter::invoke_fired_action(original_action,
        query_context, action_data, base,
        active_mode.load(std::memory_order_acquire));
}

native_consumable_runtime_adapter::WriteOutcome write_authorized_result(
    native_consumable_cost::NativeRemovalResult* result) noexcept {
    using WriterOutcome = native_consumable_result_write::Outcome;
    using AdapterOutcome = native_consumable_runtime_adapter::WriteOutcome;
    switch (native_consumable_result_write::replace_with_free_result(result)) {
    case WriterOutcome::unchanged:
        return AdapterOutcome::unchanged;
    case WriterOutcome::committed:
        return AdapterOutcome::committed;
    case WriterOutcome::indeterminate:
        return AdapterOutcome::indeterminate;
    }
    return AdapterOutcome::indeterminate;
}

void* __fastcall remove_items_hook(void* result, void* transaction,
    std::uint32_t owner, std::uint32_t item, std::uint64_t packed_slot_ref,
    std::uint32_t quantity) {
    const auto return_address = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (!original_remover) return nullptr;
    const auto mode = active_mode.load(std::memory_order_acquire);
    const auto base = image_base.load(std::memory_order_acquire);
    if (mode == Mode::read_only_probe) {
        const auto frame = native_consumable_cost::current_action_frame();
        if (base && frame) record_fired_observation(return_address, owner,
            *frame, base);
    }
    const native_consumable_cost::NativeRemovalArgs args{
        static_cast<native_consumable_cost::NativeRemovalResult*>(result),
        transaction, owner, item, packed_slot_ref, quantity};
    const native_consumable_cost::Services services{
        read_native_query_owner, resolve_native_owner, native_owner_alive,
        native_active_effects};
    const auto outcome = native_consumable_runtime_adapter::invoke_removal(original_remover,
        args, return_address, GetTickCount64(), mode, services,
        write_authorized_result,
        effect_mode_is_current);
    // An indeterminate write outcome returns nullptr and never invokes the
    // original remover, so ambiguous result bytes cannot trigger a second payment.
    return outcome.native_return;
}

bool prepare_hooks(HMODULE server, ActiveEffects current_effects) noexcept {
    if (!server || !xhl::validate_server(server)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(server);
    if (!signatures_match(base)) return false;

    std::lock_guard lock(install_guard);
    if (active_mode.load(std::memory_order_acquire) != Mode::inactive)
        return false;
    const auto existing_base = image_base.load(std::memory_order_acquire);
    if (existing_base && existing_base != base) return false;
    if (!pin_current_module()) return false;
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
        return false;

    // Publish the provider before hook creation. Its function and backing state
    // are process-lifetime by API contract if any partial removal fails.
    image_base.store(base, std::memory_order_release);
    if (current_effects)
        lease_lookup.store(current_effects, std::memory_order_release);
    const auto targets = hook_targets(base);
    const auto detours = hook_detours();
    const bool prepared = hook_set.prepare(targets, detours, hook_operations);
    original_used_action_raw = hook_set.original(0);
    original_action_raw = hook_set.original(1);
    original_remover_raw = hook_set.original(2);
    original_used_action = reinterpret_cast<NativeUsedAction>(
        original_used_action_raw);
    original_action = reinterpret_cast<NativeFiredAction>(original_action_raw);
    original_remover = reinterpret_cast<NativeRemoveItems>(original_remover_raw);
    if (!prepared) active_mode.store(Mode::inactive, std::memory_order_release);
    return prepared;
}

bool activate_mode(Mode desired) noexcept {
    std::lock_guard lock(install_guard);
    const auto current = active_mode.load(std::memory_order_acquire);
    if (current == desired) return true;
    if (current != Mode::inactive) return false;
    const auto base = image_base.load(std::memory_order_acquire);
    if (!base || !hook_set.all_prepared() ||
        (desired == Mode::free_consumables &&
            !lease_lookup.load(std::memory_order_acquire))) return false;
    if (!hook_set.enable_all(hook_targets(base), hook_operations)) {
        active_mode.store(Mode::inactive, std::memory_order_release);
        return false;
    }
    active_mode.store(desired, std::memory_order_release);
    return true;
}

bool deactivate_mode(Mode requested) noexcept {
    std::lock_guard lock(install_guard);
    const auto current = active_mode.load(std::memory_order_acquire);
    if (current != Mode::inactive && current != requested) return false;
    active_mode.store(Mode::inactive, std::memory_order_release);
    const auto base = image_base.load(std::memory_order_acquire);
    if (!base) return true;
    return hook_set.disable_all(hook_targets(base), hook_operations);
}

} // namespace

bool prepare_read_only_probe(HMODULE server, ActiveEffects current_effects) noexcept {
    // Preparing a bounded observer never activates it.
    return prepare_hooks(server, current_effects);
}

bool enable_read_only_probe() noexcept {
    return activate_mode(Mode::read_only_probe);
}

bool disable_read_only_probe() noexcept {
    return deactivate_mode(Mode::read_only_probe);
}

bool read_only_probe_enabled() noexcept {
    return active_mode.load(std::memory_order_acquire) == Mode::read_only_probe;
}

bool prepare_free_consumables_hooks(HMODULE server,
    ActiveEffects current_effects) noexcept {
    return current_effects && prepare_hooks(server, current_effects);
}

bool enable_free_consumables() noexcept {
    return activate_mode(Mode::free_consumables);
}

bool disable_free_consumables() noexcept {
    return deactivate_mode(Mode::free_consumables);
}

bool free_consumables_active() noexcept {
    return active_mode.load(std::memory_order_acquire) == Mode::free_consumables;
}

std::size_t read_observations_after(std::uint64_t sequence,
    FiredObservation* output, std::size_t capacity) noexcept {
    return observations.read_after(sequence, output, capacity);
}

std::uint64_t dropped_observations() noexcept {
    return observations.dropped_records();
}

} // namespace xhl::native_consumable_diagnostic
