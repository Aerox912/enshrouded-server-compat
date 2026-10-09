#include "native_effects.hpp"

namespace xhl::native_effects {
namespace {
bool valid_effect(Effect effect) noexcept {
    return effect == Effect::free_crafting;
}
}

std::optional<ActorOwnerMatch> match_authenticated_actor(
    std::uint32_t entity_id, std::uintptr_t operator_component,
    std::uintptr_t world, std::span<const ActorOwnerCandidate> candidates) noexcept {
    if (!entity_id || entity_id > 0x3ff || !operator_component || !world) return {};

    std::optional<ActorOwnerMatch> match;
    for (const auto& candidate : candidates) {
        if (!candidate.alive || !candidate.component ||
            candidate.compact_owner < 1 || candidate.compact_owner > 16 ||
            candidate.identity.world != world || !candidate.identity.lifecycle ||
            !candidate.identity.valid(candidate.compact_owner - 1)) continue;
        if (candidate.component != operator_component) continue;
        if (match) return {}; // More than one authenticated owner is ambiguous.
        match = ActorOwnerMatch{candidate.compact_owner, candidate.component,
            candidate.identity};
    }
    return match;
}

std::optional<Authorization> authorize(const CraftingEventEvidence& event, Effect effect,
    std::uint64_t now_ms, const Services& services) noexcept {
    if (!event.world || !event.query_context || !event.actor_descriptor ||
        !event.query_entity || !event.crafting_operator_id ||
        event.crafting_operator_id > 0x3ff || !event.crafting_station_id ||
        event.crafting_station_id != event.query_entity || !valid_effect(effect) ||
        !services.resolve_operator_owner || !services.is_alive ||
        !services.active_effects) return {};

    const auto identity = services.resolve_operator_owner(event);
    if (!identity) return {};
    const auto owner_slot = identity->player & 63;
    if (owner_slot >= 16 || identity->world != event.world ||
        !identity->valid(owner_slot) || !identity->lifecycle ||
        !services.is_alive(*identity)) return {};

    const auto effects = services.active_effects(*identity, now_ms);
    const auto requested = static_cast<std::uint16_t>(effect);
    if (!effects || (*effects & ~valid_effect_mask) ||
        !(*effects & requested)) return {};

    const auto current = services.resolve_operator_owner(event);
    if (!current || *current != *identity || !services.is_alive(*identity) ||
        current->world != event.world || (current->player & 63) >= 16 ||
        !current->valid(current->player & 63) || !current->lifecycle) return {};
    return Authorization(*identity, effect);
}
}
