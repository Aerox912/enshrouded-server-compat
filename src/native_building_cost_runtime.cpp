#include "native_building_cost_runtime.hpp"

#include "adapter.hpp"
#include "flight_identity.hpp"
#include "flight_native.hpp"
#include "native_building_cost.hpp"
#include "native_cost_hook_lifecycle.hpp"

#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <mutex>

namespace xhl::native_building_cost_runtime {
namespace {
using ActiveEffects = native_effects::Services::ActiveEffects;
using NativePlacementSystem = void (__fastcall *)(void* query_context);
using NativeBuildingPayment = native_building_cost::NativeBuildingPayment;
using QueryEntity = void* (__fastcall *)(void* query_context, void* result);

constexpr std::array<std::uintptr_t, 2> hook_rvas{0x9b810, 0x1c5e80};
constexpr std::uintptr_t placement_callback_rva = hook_rvas[0];
constexpr std::uintptr_t payment_helper_rva = hook_rvas[1];

std::atomic<std::uintptr_t> image_base{0};
std::atomic<bool> hooks_active{false};
std::atomic<ActiveEffects> lease_lookup{nullptr};
std::mutex install_guard;
native_cost_hooks::HookSet<hook_rvas.size()> hook_set;
bool module_pinned = false;
void* original_placement_raw = nullptr;
void* original_payment_raw = nullptr;
NativePlacementSystem original_placement = nullptr;
NativeBuildingPayment original_payment = nullptr;

void __fastcall placement_system_hook(void* query_context);
std::uint8_t __fastcall building_payment_hook(void* context, void* cost_data,
    std::uint8_t pay_costs);

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
    return {reinterpret_cast<void*>(base + placement_callback_rva),
        reinterpret_cast<void*>(base + payment_helper_rva)};
}

std::array<void*, hook_rvas.size()> hook_detours() noexcept {
    return {reinterpret_cast<void*>(&placement_system_hook),
        reinterpret_cast<void*>(&building_payment_hook)};
}

struct PlacementFrame {
    void* query_context = nullptr;
    std::uintptr_t world = 0;
    bool registered_callback = false;
};
thread_local const PlacementFrame* current_placement_frame = nullptr;

bool copy_memory(std::uintptr_t address, void* out, std::size_t size) noexcept {
    if (!address || size > UINTPTR_MAX - address) return false;
    __try {
        std::memcpy(out, reinterpret_cast<const void*>(address), size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

bool registered_placement_callback(std::uintptr_t base) noexcept {
    constexpr char expected_name[] = "player_building_place_prop";
    std::uintptr_t name_address = 0;
    std::uintptr_t callback_address = 0;
    std::array<char, sizeof(expected_name)> name{};
    const auto descriptor = base + 0x1349cf0;
    if (!copy_memory(descriptor, &name_address, sizeof(name_address)) ||
        !copy_memory(descriptor + 0x10, &callback_address,
            sizeof(callback_address)) ||
        !copy_memory(name_address, name.data(), name.size())) return false;
    return std::memcmp(name.data(), expected_name, sizeof(expected_name)) == 0 &&
        callback_address == base + placement_callback_rva;
}

bool signatures_match(std::uintptr_t base) noexcept {
    static constexpr std::array<std::uint8_t, 17> placement_entry{
        0x40,0x55,0x41,0x57,0x48,0x8d,0xac,0x24,0xa8,0x88,0xff,0xff,
        0xb8,0x58,0x78,0x00,0x00};
    static constexpr std::array<std::uint8_t, 16> payment_entry{
        0x40,0x55,0x56,0x48,0x83,0xec,0x78,0x48,
        0x8b,0xf2,0x48,0x8b,0xe9,0x0f,0xb6,0x11};
    static constexpr std::array<std::uint8_t, 8> dynamic_payment_args{
        0x45,0x0f,0xb6,0x80,0xf8,0x01,0x00,0x00};
    static constexpr std::array<std::uint8_t, 3> fixed_payment_arg{
        0x41,0xb0,0x01};

    std::uintptr_t dynamic_target = 0;
    std::uintptr_t fixed_target = 0;
    return base && registered_placement_callback(base) &&
        bytes_match(base, placement_callback_rva, placement_entry.data(),
            placement_entry.size()) &&
        bytes_match(base, payment_helper_rva, payment_entry.data(),
            payment_entry.size()) &&
        bytes_match(base, 0x9bfef, dynamic_payment_args.data(),
            dynamic_payment_args.size()) &&
        bytes_match(base, 0x9c043, fixed_payment_arg.data(),
            fixed_payment_arg.size()) &&
        call_target(base, 0x9bff7, dynamic_target) &&
        dynamic_target == base + payment_helper_rva &&
        call_target(base, 0x9c054, fixed_target) &&
        fixed_target == base + payment_helper_rva;
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

class PlacementFrameScope {
    const PlacementFrame* previous_ = nullptr;
public:
    explicit PlacementFrameScope(const PlacementFrame& frame) noexcept
        : previous_(current_placement_frame) {
        current_placement_frame = &frame;
    }
    ~PlacementFrameScope() { current_placement_frame = previous_; }
    PlacementFrameScope(const PlacementFrameScope&) = delete;
    PlacementFrameScope& operator=(const PlacementFrameScope&) = delete;
};

PlacementFrame capture_placement_frame(void* query_context) noexcept {
    PlacementFrame frame{};
    frame.query_context = query_context;
    frame.registered_callback = true;
    if (query_context) {
        (void)xhl::flight::read_query_world(copy_memory,
            reinterpret_cast<std::uintptr_t>(query_context), frame.world);
    }
    return frame;
}

bool read_query_owner(const PlacementFrame& frame, std::uintptr_t base,
    std::uint32_t& owner) noexcept {
    owner = 0;
    if (!frame.query_context || !frame.world || !base) return false;
    std::uintptr_t current_world = 0;
    if (!xhl::flight::read_query_world(copy_memory,
        reinterpret_cast<std::uintptr_t>(frame.query_context), current_world) ||
        current_world != frame.world) return false;
    std::uint32_t result = 0;
    const auto query_entity = reinterpret_cast<QueryEntity>(base + 0x5d5130);
    if (query_entity(frame.query_context, &result) != &result ||
        result < 1 || result > 16) return false;
    owner = result;
    return true;
}

bool read_transaction_owner(void* context, std::uint32_t& owner) noexcept {
    owner = 0;
    const auto context_address = reinterpret_cast<std::uintptr_t>(context);
    if (!context || context_address > UINTPTR_MAX -
            native_building_cost::payment_context_transaction_offset) return false;
    std::uintptr_t transaction = 0;
    if (!copy_memory(context_address +
            native_building_cost::payment_context_transaction_offset,
            &transaction, sizeof(transaction)) || !transaction ||
        transaction > UINTPTR_MAX - native_building_cost::transaction_owner_offset)
        return false;
    return copy_memory(transaction + native_building_cost::transaction_owner_offset,
        &owner, sizeof(owner));
}

std::optional<xhl::flight::Identity> resolve_owner(
    std::uintptr_t world, std::uint32_t compact_owner) noexcept {
    return xhl::flight::authenticated_owner(world, compact_owner);
}

bool owner_is_alive(const xhl::flight::Identity& identity) noexcept {
    return xhl::flight::owner_alive(identity);
}

std::optional<std::uint16_t> current_effects(
    const xhl::flight::Identity& identity, std::uint64_t now_ms) noexcept {
    if (!hooks_active.load(std::memory_order_acquire)) return {};
    const auto lease = lease_lookup.load(std::memory_order_acquire);
    if (!lease) return {};
    const auto result = lease(identity, now_ms);
    if (!hooks_active.load(std::memory_order_acquire)) return {};
    return result;
}

bool current_building_lease(const native_effects::Identity& identity,
    std::uint64_t now_ms) noexcept {
    const auto lease = lease_lookup.load(std::memory_order_acquire);
    if (!hooks_active.load(std::memory_order_acquire) || !lease) return false;
    const auto effects = lease(identity, now_ms);
    constexpr auto required = static_cast<std::uint16_t>(
        native_effects::Effect::free_building);
    return hooks_active.load(std::memory_order_acquire) && effects &&
        !(*effects & ~native_effects::valid_effect_mask) &&
        ((*effects & required) != 0);
}

void __fastcall placement_system_hook(void* query_context) {
    if (!original_placement) return;
    if (!hooks_active.load(std::memory_order_acquire)) {
        original_placement(query_context);
        return;
    }
    const auto frame = capture_placement_frame(query_context);
    PlacementFrameScope scope(frame);
    original_placement(query_context);
}

std::uint8_t __fastcall building_payment_hook(void* context, void* cost_data,
    std::uint8_t pay_costs) {
    const auto call_return = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (!original_payment) return 0;
    const auto passthrough = [&]() noexcept {
        return original_payment(context, cost_data, pay_costs);
    };
    if (!hooks_active.load(std::memory_order_acquire)) return passthrough();

    const auto* frame = current_placement_frame;
    const auto base = image_base.load(std::memory_order_acquire);
    if (!context || !cost_data || !frame || !frame->registered_callback || !base ||
        call_return < base) return passthrough();

    std::uint32_t query_owner = 0;
    std::uint32_t transaction_owner = 0;
    std::uint8_t context_mode = 0;
    const auto context_address = reinterpret_cast<std::uintptr_t>(context);
    if (!read_query_owner(*frame, base, query_owner) ||
        !copy_memory(context_address, &context_mode, sizeof(context_mode)) ||
        !read_transaction_owner(context, transaction_owner)) return passthrough();

    const native_building_cost::PlacementPaymentEvidence evidence{
        true, frame->world, query_owner, transaction_owner,
        call_return - base, context_mode};
    const native_building_cost::Services services{
        resolve_owner, owner_is_alive, current_effects};
    const auto authorization = native_building_cost::authorize(evidence,
        pay_costs, GetTickCount64(), services);
    if (!authorization || !hooks_active.load(std::memory_order_acquire) ||
        !current_building_lease(authorization->identity(), GetTickCount64()))
        return passthrough();
    const auto current = xhl::flight::authenticated_owner(frame->world,
        query_owner);
    if (!current || *current != authorization->identity() ||
        !xhl::flight::owner_alive(authorization->identity()) ||
        !hooks_active.load(std::memory_order_acquire)) return passthrough();
    return 1;
}

} // namespace

bool install_placement_payment_hooks(HMODULE server,
    native_effects::Services::ActiveEffects current_effects) noexcept {
    if (!server || !current_effects || !xhl::validate_server(server)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(server);
    if (!signatures_match(base)) return false;

    std::lock_guard lock(install_guard);
    const auto existing_base = image_base.load(std::memory_order_acquire);
    if (existing_base && existing_base != base) return false;
    if (!pin_current_module()) return false;

    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED)
        return false;

    image_base.store(base, std::memory_order_release);
    lease_lookup.store(current_effects, std::memory_order_release);
    const auto targets = hook_targets(base);
    const auto detours = hook_detours();
    const bool prepared = hook_set.prepare(targets, detours, hook_operations);
    original_placement_raw = hook_set.original(0);
    original_payment_raw = hook_set.original(1);
    original_placement = reinterpret_cast<NativePlacementSystem>(
        original_placement_raw);
    original_payment = reinterpret_cast<NativeBuildingPayment>(
        original_payment_raw);
    if (!prepared) {
        hooks_active.store(false, std::memory_order_release);
        return false;
    }
    if (hooks_active.load(std::memory_order_acquire)) return true;

    if (!hook_set.enable_all(targets, hook_operations)) {
        hooks_active.store(false, std::memory_order_release);
        return false;
    }

    hooks_active.store(true, std::memory_order_release);
    return true;
}

bool stop_placement_payment_hooks() noexcept {
    std::lock_guard lock(install_guard);
    hooks_active.store(false, std::memory_order_release);
    const auto base = image_base.load(std::memory_order_acquire);
    if (!base) return true;

    // Retain the lease pointer and trampolines for every in-flight detour.
    return hook_set.disable_all(hook_targets(base), hook_operations);
}

bool placement_payment_hooks_active() noexcept {
    return hooks_active.load(std::memory_order_acquire);
}

} // namespace xhl::native_building_cost_runtime
