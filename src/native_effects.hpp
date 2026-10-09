#pragma once
#include "flight_session.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace xhl::native_effects {
using xhl::flight::Identity;
enum class Effect : std::uint16_t {
    free_building = 1,
    free_crafting = 2,
    free_consumables = 4,
};

inline constexpr std::uint16_t valid_effect_mask =
    static_cast<std::uint16_t>(Effect::free_building) |
    static_cast<std::uint16_t>(Effect::free_crafting) |
    static_cast<std::uint16_t>(Effect::free_consumables);

namespace recipe_event_layout {
inline constexpr std::size_t stride = 0x38;
inline constexpr std::size_t crafting_operator_id = 0x08;
inline constexpr std::size_t crafting_station_id = 0x0c;
inline constexpr std::size_t recipe = 0x10;
inline constexpr std::size_t amount = 0x14;
}

struct CraftingEventEvidence {
    std::uintptr_t world = 0;
    std::uintptr_t query_context = 0;
    std::uintptr_t actor_descriptor = 0;
    std::uint32_t query_entity = 0;
    std::uint32_t crafting_operator_id = 0;
    std::uint32_t crafting_station_id = 0;
};

// A resolved Actor component reached through the callback's Actor descriptor.
// compact_owner is the 1..16 owner key returned by the existing Actor query;
// entity_id and component are kept separate because craftingOperatorId is a
// native EntityId and is not an authenticated owner index.
struct ActorOwnerCandidate {
    std::uint32_t compact_owner = 0;
    std::uintptr_t component = 0;
    Identity identity{};
    bool alive = false;
};

struct ActorOwnerMatch {
    std::uint32_t compact_owner = 0;
    std::uintptr_t component = 0;
    Identity identity{};
};

// Match by the native Actor component reached using the raw EntityId, then
// require exactly one current live authenticated owner candidate. No bits are
// stripped from entity_id, and it is never passed to authenticated_owner.
std::optional<ActorOwnerMatch> match_authenticated_actor(
    std::uint32_t entity_id, std::uintptr_t operator_component,
    std::uintptr_t world, std::span<const ActorOwnerCandidate> candidates) noexcept;

struct Services {
    // Resolve the full craftingOperatorId to its current Actor component, match
    // that component to one live authenticated owner slot, and return its full
    // identity. Never infer an owner from station/query IDs or client input.
    using ResolveOperatorOwner = std::optional<Identity> (*)(
        const CraftingEventEvidence&) noexcept;
    using IsAlive = bool (*)(const Identity&) noexcept;
    // Validate the full actor identity and return no value unless its matching
    // Creative privilege lease is current.
    using ActiveEffects = std::optional<std::uint16_t> (*)(const Identity&,
        std::uint64_t now_ms) noexcept;
    ResolveOperatorOwner resolve_operator_owner = nullptr;
    IsAlive is_alive = nullptr;
    ActiveEffects active_effects = nullptr;
};

// This evidence can mint only free-crafting authority. The native callback
// compares craftingStationId (row +0x0c) to its query entity.
// craftingOperatorId (row +0x08) is separate and must resolve through the
// native Actor component descriptor to one current authenticated owner slot.
// Runtime wiring must also provide the current Creative session lease.
class Authorization {
    Identity identity_{};
    Effect effect_{};

    Authorization(const Identity& identity, Effect effect) noexcept
        : identity_(identity), effect_(effect) {}
    friend std::optional<Authorization> authorize(const CraftingEventEvidence&,
        Effect, std::uint64_t, const Services&) noexcept;
public:
    Authorization(const Authorization&) noexcept = default;
    Authorization(Authorization&&) noexcept = default;
    Authorization& operator=(const Authorization&) noexcept = default;
    Authorization& operator=(Authorization&&) noexcept = default;

    const Identity& identity() const noexcept { return identity_; }
    Effect effect() const noexcept { return effect_; }
};

// This check does not trust a client identity. It resolves a current server
// owner, checks actor life and the active per-owner effect lease, then repeats
// the identity/life check after the lease lookup.
std::optional<Authorization> authorize(const CraftingEventEvidence&, Effect,
    std::uint64_t now_ms, const Services&) noexcept;
}
