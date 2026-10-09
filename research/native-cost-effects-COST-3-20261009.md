# COST-3 native cost effects evidence

## Pinned image and native craft path

All addresses below refer to `enshrouded_server.exe` with SHA-256
`001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637`.

Registration is present in the same pinned image: RVA `0x1D662F` calls the
descriptor getter `0xA73010`, which returns `.rdata` RVA `0x131F5D0`; that
descriptor's callback pointer at `+0x10` is RVA `0x6C1A0`. The constructor
passes the descriptor to `0x5F7AF0`, which deduplicates and stores it in the
per-world system list at `world+0x6D1010`, with entries of size `0x1B30`.
This proves system registration, but not a scheduler-wide serialized callback
guarantee. The action wrapper and transaction processor calls are synchronous
down one native stack, so the adapter's thread-local scope remains active for
the processor; it does not assume callbacks are globally serialized.

At RVA `0x6C1A0`, the recipe-event callback initializes and advances a query
with row stride `0xE8`. It iterates event rows with stride `0x38`. Reflection
and original signatures name `row+0x08` `craftingOperatorId` (`EntityId`),
`row+0x0C` `craftingStationId` (`EntityId`), `row+0x10` `recipeId`,
`row+0x14` `recipeAmount`, and `row+0x18` `inputCategorySelections`. The
callback calls `0x5D5130` to get the current query entity and compares that
result to `craftingStationId` at `0x6C264`. This is native station/recipe
eligibility. It does not identify the player or inventory owner.

The callback treats `craftingOperatorId` separately. For selected recipe types
it calls `0x119D70` with the ID and descriptor at callback-local `+0xE8`. That
helper bounds the key to `1..0x3FF`, computes `(key-1)*0x60`, verifies the
record's key, and reads its byte at `+0x10`. A later call to `0x169C70` resolves
the same operator ID through the Actor component descriptor using `0x5C8F30`
and checks Actor data at `+0x51E` bit 9. This validates the native operator's
Actor component, but the inspected path does not reveal the operator-to-player
or inventory-owner relation. Numeric equality with the station, query entity,
or authenticated player slot is not evidence of ownership.

The recipe callback calls `0x1ABA90` at `0x6C5E5` and in the amount loop at
`0x6C658`. Its native ABI is `(event_context in RCX, recipe in RDX) -> EAX`.
The wrapper synchronously calls the transaction processor `0x150990` once at
`0x1ABE6E`. The complete Microsoft x64 processor ABI, reconstructed from that
call site and the callee prologue, is:

| Position | Argument |
| --- | --- |
| RCX | transaction at wrapper local `rbp+0x2B0` |
| RDX | recipe pointer in `r14` |
| R8 | output/result local at `rsp+0x60` |
| R9 | recipe option mask in `r12` |
| stack 1 | validation state at `rsp+0x40` |
| stack 2 | action data at `event_context+8` |
| stack 3 | 32-bit `craftingOperatorId` from `event_context[0]` |
| stack 4 | constant `1` |

The processor tests transaction byte `+0xB0` bit `0x08` at `0x150A1A`; when
set it skips the ingredient branch and reaches the ordinary output branch at
`0x150B1E`. The adapter scopes only bit `0x08`, preserving all other bits and
the native output loop. `0x1ABA90` commits with `0x162900` at `0x1AC013` and
rolls back with `0x16AF40` at `0x1ABEB9` based on the native processor and
validation result. The adapter returns native status unchanged, so native
commit/rollback selection remains in control.

The additional transaction wrapper at RVA `0x157150` was recovered through its
resource-removal path: it initializes a native transaction with `0x16BE30`,
gets a query entity with `0x5D5130`, invokes generic resource removal at
`0x163E70` (`0x1572DB`), then commits at `0x157386` or rolls back at
`0x15735B`. Its emitted event contains hash `0x103E50EA`. The wrapper is
entity-scoped and complete through transaction outcome, but the action's
user-facing semantic is not established; this is not sufficient evidence to
apply free-building or free-consumable effects there.

## Authorization and integration seam

`native_effects::authorize` now treats query entity and `craftingStationId` as
separate station eligibility evidence and rejects a mismatch. It then requires
`ResolveOperatorOwner` to resolve `craftingOperatorId` through the current
native Actor descriptor and exact Actor-component pointer match into one
authenticated full `Identity`. It checks that identity's current life and
Creative lease, then resolves the operator again and requires the same full
identity. It never derives a player from the station/query entity or compares
the raw operator ID to the compact owner slot. This craft-event API can mint
only `free_crafting` authority. An absent resolver, unknown operator,
stale/dead identity, missing lease, or changed mapping fails closed.

`no_cost` exposes the exact two-argument `0x1ABA90` action ABI and complete
eight-argument `0x150990` processor ABI. The outer action scope is thread-local;
an unauthorized nested action masks any enclosing permission and restores it
after return. The processor scope sets the native ingredient bit only during
the processor call and preserves its exact result. No removal hook or global
cost toggle is added.

## COST-4 operator-to-owner resolver

`src/native_effects_runtime.cpp` now provides a callable, hook-free
`resolve_crafting_operator_owner` implementation. It binds only after
`initialize_operator_resolver(HMODULE)` succeeds through the existing
`validate_server` check for the pinned dedicated executable. This initialization
does not install hooks or alter the server.

The mapping keeps two native identifier domains separate. The full 32-bit
`craftingOperatorId` is passed unchanged to the Actor component resolver at
`0x5C8F30`; the native validator at `0x119D70` accepts keys `1..0x3FF`, checks
the keyed record, and does not turn that key into a compact owner slot. The
query context's inner context is passed to `0x5D52D0` for the ECS pointer. The
same Actor descriptor then resolves candidate compact owners `1..16`, matching
the query/entity path used by the existing flight Actor observation and
Creative inventory Frame. Only an exact Actor component pointer match to one
current authenticated owner is accepted. Zero matches, multiple matches,
unsupported operators, and stale/dead owners fail closed. The compact owner
slot alone is passed to `authenticated_owner`; raw operator IDs are never
passed to it or masked.

The resolver checks that the query context still resolves to the event world,
that the station remains equal to the callback query entity, and that the raw
operator resolves to a unique owner Actor. It performs a second complete native
lookup, then requires the operator pointer, owner Actor pointer, compact owner,
and full `Identity` including lifecycle to remain equal and live. The policy
layer calls the resolver before and after the current Creative lease check and
checks liveness on both sides. `make_native_crafting_services` supplies these
compiled resolver/liveness callbacks and accepts the existing current-effect
lease lookup; it returns empty services before the exact server image is
validated or when no lease callback is supplied. The lease callback must be a
nonrecursive read of the current full-identity lease.

This proves a static component-to-authenticated-owner mapping route, but no
runtime hook was installed and no gameplay was exercised. Registration is
known; scheduler-wide callback serialization remains unproven. The resolver
therefore uses only callback-lifetime pointers synchronously and rechecks native
ownership rather than relying on shared mutable action state. The crafting
transaction's ingredient flag remains scoped around the original processor
call, with native output and commit/rollback behavior unchanged.

## COST-4 crafting hook adapter

`src/native_effects_runtime.cpp` adds a callable install/stop/report surface:
`install_native_cost_hooks(HMODULE, Services::ActiveEffects)`,
`stop_native_cost_hooks()`, and `supported_effects()`. Initialization first
validates the exact pinned server and all three native entry signatures. It
then installs hooks only at the registered system callback `0x6C1A0`, its
recipe action wrapper `0x1ABA90`, and the nested transaction processor
`0x150990`. The verifier checks the descriptor getter's direct target, the
descriptor callback at `+0x10`, and the action/processor prologues against the
pinned image. MinHook's include path is required to compile this module.

The system detour captures the current query's world and Actor descriptor,
restores the native query cursor, and calls the original callback exactly once.
The action detour preserves event/recipe arguments and native return status;
it authorizes only after validating station/query equality and resolving the
operator's Actor component to a live authenticated owner with a current
`free_crafting` lease. The processor detour preserves all eight native
arguments, rechecks the current full owner identity, life, and lease at the
transaction boundary, and scopes transaction `+0xB0` bit `0x08` only around
the processor.
Native recipe/station checks, output generation, result status, commit, rollback,
and transaction deduplication stay in the original native code. Invalid,
unknown, unsupported, ordinary-player, and stale-lease cases call the original
action and processor with ordinary ingredient costs.

The three hooks enable as a set; a partial enable failure disables the sites
that were enabled and reports no supported effect. Stop clears the active
dispatch bit before disabling sites, then masks any inherited action scope if
a processor detour is still reached. Trampolines are intentionally retained;
the adapter module and lease callback provider must remain loaded for process
lifetime, including after stop, because there is no in-flight callback join or
safe DLL-unload contract. A processor that already passed its lease check can
finish its synchronous native transaction while stop or revocation occurs;
stop prevents later dispatch but does not cancel work already admitted.
`supported_effects()` reports only `free_crafting`
while all three sites are active. The current lease callback must be a
nonrecursive read of the owner-bound Creative effects state; it must not enter
the same nonrecursive core guard held by its caller.

The added detours compile, and the static verifier validates pinned target
bytes, but no hook was installed in a server process. Callback scheduling and
module-unload races remain unproven at runtime. Building and consumable actions
remain unavailable and are not included in the supported-effects mask.

Building and consumable actions remain unavailable. The complete
`0x157150` resource-removal transaction wrapper is entity-scoped and has a
native commit/rollback path, but its action semantics are still not identified
as placement or consumption. The generic remover and broad building-cost
candidate do not identify the authorized actor/action by themselves, so neither
is assigned a bypass.

The existing `read_query_world` comment records query contexts captured on
different simulation threads. This does not prove callback scheduling is
serialized. The adapter therefore relies on thread-local action scope and
transaction-local flags rather than shared per-action state; concurrent
fixture tests exercise that isolation.

## Verification

Built with MSVC 14.44 using `/std:c++20 /W4 /WX /EHsc /MT /O2`. The focused
test executable is `E:\Build\enshrouded-cost-parity-20261009\cost4-tests\native_cost_tests.exe`;
it passed 55 portable fixture checks, including exact-pointer mapping to a
different compact owner key, unmatched and ambiguous operators, invalid raw
EntityId bounds without masking, stale/dead owners, current lease/liveness
rechecks, ordinary-player denial, exact native argument forwarding,
success/failure result propagation, current full-identity action-scope access,
nested authorization masking, thread isolation, and transaction flag
restoration. The Windows resolver and hook adapter compiled to
`E:\Build\enshrouded-cost-parity-20261009\cost4-runtime\native_effects_runtime.obj`
with `/W4 /WX` and the MinHook headers.

`tools/verify-native-cost-operator.py` passed against
`E:\Scratch\enshrouded-flight-creative-20261008\enshrouded_server.exe`. It
checks the pinned image hash, reflected operator/station names and offsets,
raw operator range/table validation, ECS accessor, query entity accessor, the
registered callback pointer at descriptor `+0x10`, hook entry signatures, and
the original Actor-component call into `0x5C8F30`. These checks prove only
static binary layout and callable compilation; they do not establish runtime
dispatch, an installed adapter, empty-inventory crafting, ordinary-player
costs in game, rollback under a live failure, or placement/consumable parity.
