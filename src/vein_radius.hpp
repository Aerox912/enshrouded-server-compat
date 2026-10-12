// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
namespace vein_radius {
using RadiusProvider = unsigned (*)();
// Called after the pinned mod has populated its original-call pointers, outside
// DllMain. Queues hooks into the caller's existing MinHook transaction.
bool prepare(HMODULE module, RadiusProvider local_radius = nullptr);
// Cancel only this module's queued/active hooks. Keep trampolines alive because
// activation may have partially succeeded before the caller reports failure.
void cancel();
unsigned current_multiplier();
// Called only for authoritative connection removal/world teardown. Expiry,
// death, radius changes and consumed requests must retain replay protection.
// Native removal brackets its original callback with beginning=true/false.
// The default also supports an immediate authoritative retirement.
void retire_player(DWORD owner, bool beginning = false);
void retire_world(bool beginning = false);
}
