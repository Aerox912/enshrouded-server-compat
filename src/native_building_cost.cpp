#include "native_building_cost.hpp"

namespace xhl::native_building_cost {
namespace {
bool valid_owner_identity(const native_effects::Identity& identity,
    std::uintptr_t world, std::uint32_t compact_owner) noexcept {
    if (compact_owner < 1 || compact_owner > 16 || !world ||
        identity.world != world || !identity.lifecycle) return false;
    return identity.valid(compact_owner - 1);
}

bool is_placement_payment_call(std::uintptr_t call_return_rva) noexcept {
    return call_return_rva == placement_payment_dynamic_return_rva ||
        call_return_rva == placement_payment_fixed_return_rva;
}
} // namespace

std::optional<Authorization> authorize(
    const PlacementPaymentEvidence& evidence, std::uint8_t pay_costs,
    std::uint64_t now_ms, const Services& services) noexcept {
    if (!evidence.registered_placement_callback || !evidence.world ||
        evidence.query_owner < 1 || evidence.query_owner > 16 ||
        evidence.transaction_owner != evidence.query_owner ||
        !is_placement_payment_call(evidence.call_return_rva) ||
        evidence.context_mode != normal_payment_mode || !pay_costs ||
        !services.resolve_owner || !services.is_alive ||
        !services.active_effects) return {};

    const auto first = services.resolve_owner(evidence.world, evidence.query_owner);
    if (!first || !valid_owner_identity(*first, evidence.world,
            evidence.query_owner) || !services.is_alive(*first)) return {};

    const auto effects = services.active_effects(*first, now_ms);
    constexpr auto requested = static_cast<std::uint16_t>(
        native_effects::Effect::free_building);
    if (!effects || (*effects & ~native_effects::valid_effect_mask) ||
        !(*effects & requested)) return {};

    const auto current = services.resolve_owner(evidence.world, evidence.query_owner);
    if (!current || *current != *first ||
        !valid_owner_identity(*current, evidence.world, evidence.query_owner) ||
        !services.is_alive(*first)) return {};

    return Authorization(*first);
}

std::uint8_t invoke_native_building_payment(
    NativeBuildingPayment original, const NativeBuildingPaymentArgs& args,
    const PlacementPaymentEvidence& evidence, std::uint64_t now_ms,
    const Services& services) noexcept {
    if (!original) return 0;
    if (authorize(evidence, args.pay_costs, now_ms, services)) return 1;
    return original(args.context, args.cost_data, args.pay_costs);
}

} // namespace xhl::native_building_cost
