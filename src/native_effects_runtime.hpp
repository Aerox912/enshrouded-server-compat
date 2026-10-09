#pragma once

#include "native_effects.hpp"
#include <windows.h>

namespace xhl::native_effects {

// Validates and binds only the pinned dedicated-server image. This prepares
// callable lookups; it installs no hooks and changes no native state.
bool initialize_operator_resolver(HMODULE server) noexcept;
bool operator_resolver_ready() noexcept;

// Resolve the full raw craftingOperatorId through the event's Actor component
// descriptor, then match that exact component against the existing compact
// authenticated owner slots. The station/query entity remains independent.
std::optional<Identity> resolve_crafting_operator_owner(
    const CraftingEventEvidence&) noexcept;

// Provides the compiled native owner/liveness callbacks plus the caller's
// current Creative lease lookup. An absent lease or unvalidated image yields
// empty services, so authorization remains disabled.
Services make_native_crafting_services(Services::ActiveEffects lease) noexcept;

// Installs only the registered CraftRecipeEvent callback plus its nested
// transaction action/processor wrappers. Stop disables dispatch but retains
// MinHook trampolines for any callback already on the native stack. The
// reported mask includes only effects with an installed, active native path.
bool install_native_cost_hooks(HMODULE server,
    Services::ActiveEffects lease) noexcept;
bool stop_native_cost_hooks() noexcept;
std::uint16_t supported_effects() noexcept;

}
