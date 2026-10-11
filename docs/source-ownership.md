# Source ownership reconciliation, 2026-10-11

This branch preserves the frozen native-input/Journey source candidate and its
server dependencies. The import manifest records pre-reconciliation file hashes.
The import includes prerequisite native inventory, flight, cost and time source
needed to reproduce that candidate. It does not claim those experiments are released.

The combined MSVC Release build passed 49 CTests with the extracted runtime.
Creative's Python tooling passed 26 tests. Runtime-only testing passed 6 CTests.
These are offline/source checks; no installed files or release feeds were changed.
Active Magic Storage development and live recovery files remain in their original
checkouts and are outside this branch.

Shared UI/input/hook and connection ownership now belongs to
[enshrouded-native-runtime](https://github.com/Aerox912/enshrouded-native-runtime).
Consumer copies remain compatibility snapshots for prior builds. Use the pinned
runtime below for the extracted build; installed pairs still need live validation.

Runtime commit: `8e635a1f4a6d519d21a929ed298ef2144bba5194`.

Build the combined candidate with CMake options `BUILD_FLIGHT_EXPERIMENT=ON`,
`BUILD_CREATIVE_EXPERIMENT=ON`, `BUILD_NATIVE_UI_HOST=ON`, `CREATIVE_ROOT`,
`SHROUDTOPIA_ROOT` and `NATIVE_RUNTIME_ROOT` pointing at their pinned checkouts.
Creative behavior and Journey belong to Creative Mode; server adapters, native
policies and transport belong to server-compat. Creative and its Gem Forge client
data belong to the Admin Mod Manager and remain subject to server authorization.
