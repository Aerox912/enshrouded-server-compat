// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
namespace vein_radius {
using RadiusProvider = unsigned (*)();
// Called after the pinned mod has populated its original-call pointers, outside
// DllMain. Queues hooks into the caller's existing MinHook transaction.
bool prepare(HMODULE module, RadiusProvider local_radius = nullptr);
unsigned current_multiplier();
}
