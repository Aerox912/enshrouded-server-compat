// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>

namespace xhl::lifecycle {
using PlayerRemoved = void (*)(void* state, void* player);
using WorldRemoved = void (*)(void* state);
// Shared ownership of the two native connection-lifetime sites. Queues into
// the caller's MinHook transaction and is safe to call from both runtimes.
bool prepare(HMODULE game);
// Cancels this shared module after a failed activation and prevents a later
// component from treating the failed installation as ready.
void cancel();
void observe(PlayerRemoved player_removed, WorldRemoved world_removed);
}
