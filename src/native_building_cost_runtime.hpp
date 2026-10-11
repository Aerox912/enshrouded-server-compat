#pragma once

#include "native_effects.hpp"
#include <windows.h>

namespace xhl::native_building_cost_runtime {

// Installs the registered player_building_place_prop wrapper and its shared
// building.payCosts helper as one fail-closed pair. The lease callback must be
// a nonrecursive snapshot of current effects for the supplied full identity.
// Once preparation begins, the callback function and all state it reads must
// remain valid for the process lifetime: a retained detour or in-flight call
// may still use them after disable or a failed hook removal. This adapter does
// not contribute an advertised-effect bit by itself.
bool install_placement_payment_hooks(HMODULE server,
    native_effects::Services::ActiveEffects current_effects) noexcept;
bool stop_placement_payment_hooks() noexcept;
bool placement_payment_hooks_active() noexcept;

} // namespace xhl::native_building_cost_runtime
