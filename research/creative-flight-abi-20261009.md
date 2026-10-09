# Creative flight ABI investigation, 9 October 2026

## G-FLIGHT-3 outcome and limits

A default-off, bounded read-only configured-input observer is implemented at the
existing authenticated Creative inventory callback. No G movement hook/backend is
installed. The distinct G capability/lease is an approved integration seam owned
by the wire/UI package; F6 `can_fly` must never authorize G. Paired client input,
prediction, gravity and landing/fall contracts remain release gates. Source/static
success and observer tests do not establish live flight acceptance.

## Exact inputs and reproduction

- Server SHA256 `001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637`, timestamp `0x69fdecc9`, image base `0x140000000`, size `0x1da7000`.
- Client SHA256 `af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781`, timestamp `0x6a4236c8`, size `0x2da7000`.
- Authorized original Creative DLL SHA256 `636bb4df119eeb999d0348ff31e26038716fb01515dbfbc81a9323ecaf2f9698`, timestamp `0x6ac7a6b7`, image base `0x180000000`, size `0x44f000`. No source/PDB was present. The DLL was inspected offline and never loaded or installed.

`tools/verify_creative_flight_offsets.py --server SERVER_EXE --creative-module ORIGINAL_DLL --client CLIENT_EXE --json REPORT` rejects other hashes and checks instruction/signature/unwind anchors and reflection names/offsets. It uses no external packages. The maintained signatures are short analysis anchors, not embedded game routines. `python -B -m unittest discover -s tests -p test_creative_flight_offsets.py -v` exercises twelve synthetic PE/regression cases.

## Material correction: Actor state is not gameplay input

The numeric arguments `0x27`, `0x2c`, `0x24`, `0x2b`, `0x25`, `0` passed to
server leaf helper `0x2a1e0` are **Actor State enum indices**. The pinned reflection
rows prove Flying=39, UnderWater=44, Attached=36, FloatingOnWater=43,
HangGliding=37 and Grounded=0. The helper reads Actor `+0x1b1`, `+0xbf8`,
`+0xbd0`, `+0xbd8`. The previous G-FLIGHT-2 action-label hypothesis and proposed
sampling of those bits as Jump/Crouch/Sprint are retired.

Actual `PlayerInputType` reflection at server `0xfc37a0` names **Jump=30,
Sprint=50, Sneak=52**. `PlayerInput` descriptor `0xdec3e0` has `fromClient=8`,
input mode `+0x519`, and `ClientPlayerInputData` descriptor `0xff2c10` has
`digitalInput=0x318`. Thus the 8-byte PlayerDigitalInput mask is at PlayerInput
`+0x320`. ActorInput reflection `0xdf4a40` proves desired world input `+0x198`.
These are configured game input fields, but metadata alone does not prove whether
the encoded values are held, toggled, consumed, or how remapped/controller input
reaches server prediction. The observer labels them as encoded named bits.

## Registered state and mover traces

Server `locomotion_state` descriptor `0x1340100` contains a string-view length `0x10` and
callback `0x189bf0`. The callback uses query begin `0x5dc7f0`, step `0x5d7960`,
row size `0x70`, row buffer RSP+0x40. Registration groups name Locomotion, Actor, Movement,
WorldCollider and other components, but are alphabetically sorted name/length
pairs. Their order does not establish runtime pointer order, even for required
components. The removed generic row-layout arithmetic was unsound. The native
state call `0x189db3` directly loads RDX from RSP+0x50; the Actor helper then
reads its reflected state fields, establishing Actor row+0x10 independently.

The state routine at `0x1767c0` uses an eight-argument Windows x64 convention.
Its unwind entry covers only `0x1767c0..0x176858`; the routine continues through
`0x176966`. Its priorities select climb/attachment, native Flying state 3, water
states 9/10, HangGliding state 4, grounded 0/1, or Falling state 2. Its return
updates DynamicLocomotion `+0x3d`. Full semantic ownership of all eight pointer
arguments is still required before a state detour is activated.

Server `locomotion_execution` descriptor `0x133f2d0` invokes `0x187120`, with
query size `0x138` and row RBP-0x60. Actor is row+0x68, DynamicLocomotion
row+0x98, velocity row+0xb8, config row+0x10. Dispatcher `0x187595` uses state
`+0x3d`; state 3 calls flying mover `0x17f5a0`, water state 9 calls `0x180820`,
and dive state 10 calls `0x17e480`. The dive routine reads config acceleration
`+0x13c`, deceleration `+0x140` and writes velocity. These sites and original
mid-function wrappers are not independent hook contracts.

The native gravity system full callback starts `0x7e7e0`, continuation
`0x7e817..0x7e989`, with query size `0x30`: Actor row+8, Gravity row+0x10,
WorldCollider row+0x18, Velocity row+0x20. It applies direction * strength * dt and a
terminal velocity clamp. A shared Gravity config must not be overwritten. The
original gravity lead `0x7e8ae` and fall lead `0x6b711` remain mid-function sites.
A safe full owned callback/temporary-row contract is still needed for cleanup.

## Original behaviour and paired client seam

Original normal state detour `0xd8e80..0xd8ff2` forwards all eight arguments,
records Actor and DynamicLocomotion, and substitutes Falling (2) with Flying (3)
when enabled. Other native states end active flight. Original mover wrapper
`0xd9370..0xd95ad` substitutes a local config and target, invokes the dive mover,
and restores the original row pointers. It uses speeds 1.5 / 8.0, climb-minus-sink
vertical input 5.0, and dive acceleration/deceleration 40.0. Original gravity/fall
wrappers change saved register values, not shared ECS configuration.

The original signature JSON for the pinned client names decideState `0x3955a0`,
diveMover `0x39d190`, moverDispatch `0x3a60a5`, localInputWritten `0x28753d`,
gravity strength lead `0x256bfe` and fall lead `0x232101`. Offline client reads
confirm a paired eight-argument state decision, the same state enum indices, a
similar dive mover, and local ActorInput writes around the input lead. The
original local-input wrapper registers an Actor; it does not establish configured
input semantics or server approval. Hardcoded Space/C/Shift in the original raw
HID callback must be replaced by configured Jump/Sneak/Sprint.

The paired client registration is now checked: `locomotion_state` at
`0x1d32280` invokes `0x3a8700`, uses the native client iterator
`0x8da7c0/0x8d5ca0`, and calls the state decision at `0x3a88c3` with Actor
row+0x10 and the same eight-argument caller setup. `locomotion_execution` at
`0x1d31450` invokes `0x3a5c30`. `player_control_locomotion` at `0x1d3d7a0`
invokes full callback `0x2873a0` with query size `0xb0`. The original input lead
`0x28753d` lies in its continuation, immediately after ActorInput local movement
writes. This system's presence does not prove that all its rows are locally owned.

Client configured action test thunk `0x3d2c60` reaches leaf `0x3cc720`; identical
server leaf bytes occur at `0x1a81d0`. It tests inputConsumer mask+8 and rejects
consumed bits in mask+0. Client consume leaf `0x3cc6b0` / server `0x1a8160` sets
the consumed mask. The original consume wrapper pairs Jump=30 with Glider=35.
Thus raw input bits and game-available actions must not be conflated. This static
proof identifies a separate processed-input seam, while toggle/hold construction
and live controller/prediction behavior remain unaccepted.

## Bounded observer and exact next observation

New `src/creative_flight_probe.hpp` is included once by Creative native_runtime,
with a single call after the current Frame is set in its inventory callback. The
native reader proves PlayerInput `+0x10` (row load `0x15b18d` followed by
PlayerInput field `+0x334` at `0x15b1db`). Linked Actor descriptor `+0xb0` is
independently proven: inventory_actions `0x15b8bb` passes the exact row
(RBP-0x40) as RDX to `0x154990`, `0x1549b7` preserves it in R14,
`0x154b34` loads `[R14+0xb0]`, `0x154b3b` calls component resolver `0x2d0a0`,
and `0x154b49..0x154b4e` immediately tests its result with Actor state helper
`0x2a1e0` (Dead bit 7). These byte/call anchors are checked by the verifier.
ActorInput `+0x18` was not independently proven and has been removed from the
observer. The frame API contains only Identity, Actor and PlayerInput pointers.
The caller supplies current authenticated full Identity, live actor and current
Creative approval before/after three bounded reads. No F6 flight flag is used.
Output contains named input/state booleans and mode. Unmeasured movement is
omitted. No SteamID, identity, address, memory dump or secret is logged.

The private process environment variable `ENSHROUDED_CREATIVE_FLIGHT_INPUT_PROBE`
is read once and defaults off. A future separately authorized test process can
set `enabled=1;owner=1;duration_ms=20000;sample_limit=200;interval_ms=100`.
Malformed/duplicate/unknown keys reject activation. Owner is a local 1..16 slot,
not an identity. No configuration was enabled in this task. Denied pre-start
callbacks perform cheap authorization only. First approved full Identity starts
the bounded window; revocation/identity change stops permanently. Failed reads
consume the post-start budget. Duration <=20 seconds, attempts <=200, reads/logs
<=10Hz, clock rollback stops. All pointers are scoped to the existing callback.

When a reachable native test is separately authorized: obtain fresh Creative
approval, leave input idle, press/release configured Jump, hold/release configured
Sneak, hold/release Sprint, then repeat on controller. Use separate bounded
captures for remapped keys and toggle/hold modes. Compare the named bits, input
mode. Revoke/death during a capture must stop output; another
owner must never appear. Do not treat Flying state 39 as an input action.

A later backend must bind a distinct current G lease to full Identity/lifecycle
on both server and paired client, preserve camera and native collisions, and
prove takeoff/landing/fall cleanup. The observer alone completes no G flight.

## Self-check evidence

MSVC Release `/W4 /WX /EHsc`: the isolated observer test binary passes 42 checks,
and isolated CTest passes 1/1. Build directory is
`E:\Build\enshrouded-gflight-probe-test-20261009`; its scratch CMake source is
`E:\Build\enshrouded-gflight-probe-source-20261009`. The full `creative_native`
static target builds in `E:\Build\enshrouded-gflight-native-20261009`, configured
from the server parity worktree and Creative parity worktree with the existing
Shroudtopia dependency checkout. No shared CMake changes were made for this probe.
No live runtime/gameplay observation or movement mutation was performed.

## Query-layout correction and additional native observations

Registration records use name pointer plus ASCII string length. Neither their
second qwords nor the alphabetical component order denotes native component IDs.
This correction invalidates any per-name slot arithmetic. No movement detour was
implemented from that inference. The unproven ActorInput vector was removed; Actor descriptor proof is now
maintained separately from registration names.

State argument R9 is read at `+0x4c`; it cannot be SlopeConfig (reflected size
`0x20`). WorldCollider (size `0x58`) embeds optional WorldCollisionResults at
`+0x1c`; that nested result has groundVoxelTypeId `+0x30`, totaling `+0x4c`.
This semantic field match supports WorldCollider, but remaining argument types
require caller/native reader proof.

Locomotion size is `0x1c0`; its waterConfig is `+0x124`, nested diveAcceleration
`+0x18` and diveDeceleration `+0x1c`, totaling the dive mover's `+0x13c/+0x140`.
Native fall detection callback `0x6b640` (query `0x50`) computes falling from
DynamicLocomotion state 2 or a special state 1 case. State 3 clears fall tracking
in the ordinary path. This identifies a potential native cleanup path, without
proving callback ordering or toggle/revocation behavior.

Maintained observer self-check: `tools/test_creative_flight_probe.py --build-dir
BUILD_DIR --cmake CMAKE_EXE` creates an isolated marked build directory, builds
Release with `/W4 /WX /EHsc`, and runs CTest plus the 42-check test executable.
It passed at `E:\Build\enshrouded-gflight-probe-maintained-20261009`.

## Paired configured-toggle and client ownership decoder

Native client `server_player_input_begin_frame` callback `0x28f240` copies
PlayerInput.fromClientToggleConfig `+0x510` into consumer bundle `+0x18` at
`0x28f2d4`. Consumer update `0x3d2c70` passes that per-action toggle flag to
leaf `0xca4060`: hold mode returns raw-down and not-consumed; toggle mode
rejects consumed input, inverts previous active only on a raw rising edge,
and otherwise preserves previous active. Sprint and Sneak have additional
state/controller branches. This closes the generic hold/toggle meaning; it
does not establish the final G climb/sink/boost behavior under those branches.

A new uninstalled read-only decoder, `src/creative_flight_local.hpp`, uses
current ECS world's named global registry, rather than a baked player or an
original JSON symbol guess. Native constructor `0x3260f9..0x326135` registers
`LocalPlayerData` through `0x8f5210`, with data at ClientState `+0x5c0`. That
registration routine proves the FNV-1a name hash, bitmap/key/16-bit index
lookup, record stride `0xc0`, record name/length, hash `+0xa0` and data pointers
`+0xa8/+0xb0`. The global registry root is ECS world `+0x6d0f80`. Its LocalPlayerData
hash is `0x9117771d`. Native `local_player_hide` gets its current entity with
`0x8d3490` then calls predicate `0x336d50`; the latter compares that entity
with LocalPlayerData `+4`. The maintained PE verifier checks these anchors.

The decoder returns only a copied actor ID, reads no borrowed pointer later,
checks exact name as well as hash, bounds records <=512, table capacity <=4096
and collision probes <=64, and rejects changed table/record/actor evidence.
It requires matching readable/writable LocalPlayerData pointers; unsupported
alternate storage returns unavailable. It is not a permission source. Future
callers must first verify the pinned client hash, establish the current query's
ECS world, and recheck current server G approval and host generation around
the read. The exact client query-to-world binding still needs native proof.
No production hook or shared runtime/CMake registration invokes this decoder.

Isolated Release `/W4 /WX /EHsc` self-check passes 28 checks / CTest 1/1 at
`E:\Build\enshrouded-gflight-local-test-20261009`. Reproduce with
`tools/test_creative_flight_local.py --build-dir BUILD_DIR --cmake CMAKE_EXE`.
This demonstrates parser bounds and stale-read rejection on synthetic native
layouts, not a live local-player or movement observation.

The native local predicate and toggle leaf have no unwind records; the verifier
checks that absence plus exact bounded instruction bytes. It does not invent an
unwind function extent for these leaf routines.

## G-local input and uninstalled native movement helpers

The bounded G input contract uses configured raw Jump (30), Sneak (52), Sprint
(50), and their PlayerInput toggle flags. It deliberately maintains its own
per-G-activation state rather than using vanilla one-shot consumption or the
extra native auto-sprint physical-state branches. Held Jump continues climbing
when vanilla consumes normal jump. Toggle mode changes on a fresh rising edge;
a held toggle at approval or hold/toggle remap is seeded without replaying it.
Landing/attachment/water/gliding clears local flight and toggle state. Dead,
Spawning, revoke, actor replacement or lifecycle change retires that activation;
there is no F6-style preference resume. Controller/remap fidelity remains an
isolated gameplay acceptance requirement.

`src/creative_flight_movement.hpp` implements this value policy. Normal speed is
1.5, fast speed 8, vertical input is `(climb - sink) * 5`, then the entire target
is scaled by speed, matching the original Creative mover. Horizontal input is
copied from native camera-relative desiredWorldMove; no keyboard key or guessed
camera transform is substituted. Every command is tagged with the latest local
sample and can be consumed once, so retained commands cannot replay movement.

The SERVER-only `MovementApproval` contains the current full authenticated
Identity, distinct G activation generation and current Actor pointer. Heartbeats
and unchanged true acknowledgments preserve that generation; new activation or
life retirement changes it. Client prediction must separately bind current host
connection, accepted server G lease/capability epoch and resolved local actor /
world. Server Identity is not added to the wire or fabricated by the client.

`src/creative_flight_native_movement.hpp` contains uninstalled adapter helpers:
current-G approval is checked before/after five bounded input reads. Actor state,
PlayerInput raw mask +0x320, toggle mask +0x510, ActorInput desiredWorldMove +0x198,
and a repeated Actor state are copied while the native frame is valid. Native
state selection rechecks approval and Actor liveness before replacing only the
ordinary Falling result (2) with Flying (3). Other native priorities pass through.

The private Dive helper requires the exact native locomotion_execution row and
its current Actor / Flying state. It copies Locomotion's entire 0x1c0 config,
changes private +0x13c/+0x140 acceleration/deceleration to 40, and substitutes only
row +0x10 config and row +0x98 target for the native call. Native remaining
components/counters and the original second argument are forwarded. Row pointers
restore on normal return and C++ unwind; shared config/target components are not
written. Approval and actual Actor state are rechecked immediately before this
native step, with post-call revocation clearing local state. This does not claim
atomic exclusion of lifecycle changes during an already-entered native step.

Paired control callbacks are server +0xa0830 and client +0x2873a0, query size 0xb0,
row origin RBP-0x50. Direct loads bind PlayerInput row+0x20, Actor row+0x48 and
ActorInput row+0x58. The server continuation explicitly writes ActorInput
+0x198/+0x19c/+0x1a0. The maintained pinned verifier now checks these native
instructions and real unwind fragments. Sampling requires the completed native
control output; registration names/order never establish these slots.

Isolated Release /W4 /WX /EHsc policy tests passed 58 checks / CTest1/1 at
`E:\Build\enshrouded-gflight-movement-test-20261009`; native helper tests passed
49 checks / CTest1/1 at `E:\Build\enshrouded-gflight-native-movement-test-20261009`.
Reproduce with `tools/test_creative_flight_movement.py` or
`tools/test_creative_flight_native_movement.py`, each `--build-dir BUILD_DIR
--cmake CMAKE_EXE`. These synthetic tests prove bounds, approval checks, local
state, command replay rejection, private scratch, and pointer restoration. They
do not execute the native game mover or prove callback ordering.

No movement hook, shared runtime/CMake integration, install or live activation
was performed. Remaining integration gates are the exact paired client query /
world/current-actor binding, callable callback/frame ABI, current client G host
approval provider, and independent helper review. Gravity cancellation and
fall/revocation ordering must also be completed before full G-flight acceptance.
