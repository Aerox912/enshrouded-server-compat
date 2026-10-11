#pragma once

#include "native_effects.hpp"
#include <cstdint>
#include <optional>

namespace xhl::native_building_cost {

inline constexpr std::uintptr_t placement_payment_dynamic_return_rva = 0x9bffc;
inline constexpr std::uintptr_t placement_payment_fixed_return_rva = 0x9c059;
inline constexpr std::size_t payment_context_mode_offset = 0x00;
inline constexpr std::size_t payment_context_transaction_offset = 0x08;
inline constexpr std::size_t transaction_owner_offset = 0xe0;
inline constexpr std::uint8_t normal_payment_mode = 0;

struct PlacementPaymentEvidence {
    // Set only while the registered player_building_place_prop callback is
    // synchronously executing. It is not an identity claim by itself.
    bool registered_placement_callback = false;
    std::uintptr_t world = 0;
    std::uint32_t query_owner = 0;
    std::uint32_t transaction_owner = 0;
    // Return address of the native call into building.payCosts.
    std::uintptr_t call_return_rva = 0;
    // Safely copied from the payment context at entry to building.payCosts.
    std::uint8_t context_mode = 0;
};

struct NativeBuildingPaymentArgs {
    void* context = nullptr;   // RCX
    void* cost_data = nullptr; // RDX
    std::uint8_t pay_costs = 0; // R8B
};

using NativeBuildingPayment = std::uint8_t (__fastcall *)(
    void* context, void* cost_data, std::uint8_t pay_costs);

struct Services {
    using ResolveOwner = std::optional<native_effects::Identity> (*)(
        std::uintptr_t world, std::uint32_t compact_owner) noexcept;
    using IsAlive = bool (*)(const native_effects::Identity&) noexcept;

    ResolveOwner resolve_owner = nullptr;
    IsAlive is_alive = nullptr;
    // Must be a nonrecursive read of the current full-identity lease; do not
    // reacquire a core guard already held by the caller.
    native_effects::Services::ActiveEffects active_effects = nullptr;
};

class Authorization {
    native_effects::Identity identity_{};

    explicit Authorization(const native_effects::Identity& identity) noexcept
        : identity_(identity) {}
    friend std::optional<Authorization> authorize(
        const PlacementPaymentEvidence&, std::uint8_t, std::uint64_t,
        const Services&) noexcept;

public:
    Authorization(const Authorization&) noexcept = default;
    Authorization(Authorization&&) noexcept = default;
    Authorization& operator=(const Authorization&) noexcept = default;
    Authorization& operator=(Authorization&&) noexcept = default;

    const native_effects::Identity& identity() const noexcept { return identity_; }
};

// The query entity and transaction owner must be the same compact owner key.
// The key is resolved through the current authenticated flight identity, then
// checked live and against the current owner-bound Creative lease. This
// authorizes only the two verified placement call sites and never changes a
// transaction flag.
std::optional<Authorization> authorize(
    const PlacementPaymentEvidence&, std::uint8_t pay_costs,
    std::uint64_t now_ms, const Services&) noexcept;

// Returns native true for an authorized requested-payment call. Every other
// case invokes the exact original ABI once and returns its status unchanged.
std::uint8_t invoke_native_building_payment(
    NativeBuildingPayment original, const NativeBuildingPaymentArgs&,
    const PlacementPaymentEvidence&, std::uint64_t now_ms,
    const Services&) noexcept;

} // namespace xhl::native_building_cost
