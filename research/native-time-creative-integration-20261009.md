# Creative native time host integration

Date: 2026-10-09

## Runtime boundary

Creative consumes the !xhl-time: message family from its existing authenticated Steam channel. The command parser handles status, enable, disable, hour=<0..23>, and mode=normal|pause|fast; messages outside that family continue through the existing Creative protocol parser.

The time runtime is not installed at module startup. An authorized current owner must issue !xhl-time:enable, and the dedicated-server process must have XHL_ENABLE_NATIVE_TIME_OBSERVER=1. The first enable installs the single six-site RuntimeAdapter in observe-only mode for the bounded 20-second sample. A later explicit enable can permit writes only when the accepted phase report contains the current opaque world scope and the private time permission still passes. Status can finalize/report the bounded sample but does not enable writes.

The private permission is creative-time-allowlist.txt; it remains separate from Creative flight permission. Each host callback rebuilds the world and query-to-clock association, resolves the authenticated owner for that world, snapshots the pinned clock fields twice with a stable version, and rechecks the private permission. Command submission and each native write recheck current authentication, owner/life, and the private permission. The host stores no native clock pointer.

## Scope and write rules

The host assigns monotonically increasing opaque scopes. A world change rotates the scope immediately. An owner identity transition withdraws advertised time capability; if a same-world scale override is active, the old scope remains only long enough for the adapter's permission-loss cleanup to restore it, then the scope is retired. Scope exhaustion fails closed.

The hour writer validates the callback-local native tick, materialized base, next sync version, current world/owner, and writable native page before applying the sequence at clock offsets +0x18, +0x10, +0x24, and +0x18. It never writes timeOfDay at +0x48. Mode changes use the adapter's single native scale-writer trampoline. The runtime remains observe-only until both the bounded callback-order sample and explicit enable pass.

## Gear-options producer and limitation

The Creative adapter produces gear-option capacity from `creative::native::item_runtime_info` on the current authenticated inventory callback, using the requested item and rarity. It accepts the result only when item, rarity, and generated-capacity context match and the capacity is known; missing or mismatched native metadata does not fall back to static catalogue values. The `gear_upgrade_validation` capability is advertised only after verified-image serializer initialization and current-frame create, capacity, attribute-index, and staged-mutation readiness pass.

The native adapter does not prove serialization against unrelated inventory writers. `inventory_writer_serialization` remains false, and callback-scope checks do not make a stronger cross-writer guarantee. No live game validation is claimed.

## Build integration and evidence boundary

The shared server CMake target links the Creative gear payload/serializer modules and the native time command parser. It registers the adapter-hook lifecycle test, the gear payload and serializer tests, the native runtime-adapter readiness fixture, the native-time runtime-adapter policy tests (including partial-hook-creation failure), and the server-source include directory needed by the Creative client life reader.

No live 20-second sample or game/server run is claimed here. Sample evidence is pointer-free and bounded in the runtime; local status reports only counts and phase status. Static and synthetic tests do not establish callback ordering or gameplay behavior in a running dedicated server.
