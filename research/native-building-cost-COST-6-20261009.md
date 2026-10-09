# COST-6 server building payment adapter

## Result

Added a hook-free, action-scoped policy adapter for the native building.payCosts
path used by the registered player_building_place_prop callback. It bypasses
only the requested-payment calls at return RVAs 0x9BFFC and 0x9C059 when they
execute inside that registered callback, use payment mode zero, and the current
query owner and transaction owner resolve to the same live authenticated owner
with a current free_building lease. Denied, unknown, stale, ordinary-player,
and unsupported calls invoke the native helper with the original three
arguments and preserve its return value.

The adapter changes no transaction flags. It leaves native placement creation,
item/output handling, and finalization in control. It does not install hooks,
change runtime readiness, or alter the supported-effects mask.

## Paired path evidence

Inputs were pinned server SHA-256
001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637 and client
SHA-256 af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781.
Client native evidence is in research/native-cost-preview-evidence.md.

Server RVA 0x1C5E80 and client RVA 0x3EA870 have the same 0x1B4-byte body
apart from five relocation windows. The verified call targets at body offsets
+0x94, +0xDD, and +0x170 are respectively server 0x1A27F0, 0x150370, and
0x165090, paired with client 0x3C6ED0, 0x36D370, and 0x3834D0. The paired
prologue and body preserve the helper payment-mode and requested-payment
branches.

The server descriptor at 0x1349CF0 names player_building_place_prop and
registers callback RVA 0x9B810. That callback obtains its current query entity
through 0x5D5130 at 0x9BC73. It reads the query result, stores that owner key
in transaction input +0xC8, and calls the native transaction initializer at
0x9BCEA. The initializer copies input +0xC8 to transaction +0xE0. The
placement callback builds the payment context with the transaction pointer at
context +0x08 and preserves the native mode byte at context +0x00.

Only the two placement calls at 0x9BFF7 and 0x9C054 are in scope. The first
uses the component requested-payment byte at +0x1F8; the second sets R8B=1.
Both continue through the native create and finalization calls at 0x9C0C7 and
0x9C0E8. The distinct player_building_blueprints callback at RVA 0x99C20 and
all other callers of the shared payment helper remain unsupported.

At runtime the parent hook integration must capture the registered placement
callback current query context/world and pass its compact owner, the
transaction owner copied from context +0x08 then transaction +0xE0, the helper
mode byte, and the native call return RVA. It must obtain the live full
identity through the existing authenticated owner service and use a
nonrecursive current-lease read. No station or arbitrary client-provided
identity is used.

## Files and validation

Added src/native_building_cost.hpp, src/native_building_cost.cpp,
tests/native_building_cost_tests.cpp,
tools/verify-native-building-cost.py, and this evidence note. The portable
adapter tests cover both allowed call sites, registration/callsite, mode and
requested-payment gates, owner mismatch and bounds, ordinary/missing/unknown
lease states, invalid or stale identity, death and owner changes across the
check, missing callbacks, exact argument forwarding, native failure status,
and unchanged input bytes.

Run the static verifier with explicit pinned inputs:

    python tools/verify-native-building-cost.py --server E:\Scratch\enshrouded-flight-creative-20261008\enshrouded_server.exe --client D:\Games\SteamLibrary\steamapps\common\Enshrouded\enshrouded.exe

This is static PE evidence plus portable policy evidence. No live hook,
server/client process, empty-inventory placement, ordinary-player in-game cost,
or forced rollback gameplay test was run. The path is not advertised as
supported until the parent accepts and integrates its exact callsite wrapper.
Free blueprints, dismantle, undo, arrows, and consumables are not covered.
