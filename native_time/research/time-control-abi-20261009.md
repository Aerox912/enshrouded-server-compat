# TIME-2 time-control ABI trace (2026-10-09)

Scope: offline inspection only. Inputs were read from the pinned dedicated-server image, the authorized original Creative 1.1 DLL, and its signature JSON. No game/server process, installation, runtime state, or Git state was changed.

## Pinned inputs

- Dedicated server: `E:\Scratch\enshrouded-flight-runtime-20261008\enshrouded_server.exe`, SHA-256 `001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637`, image base `0x140000000`.
- Original DLL: `E:\Scratch\creative-original-1.1-20261008\creative\creative.dll`, SHA-256 `636BB4DF119EEB999D0348FF31E26038716FB01515DBFBC81A9323ECAF2F9698`, image base `0x180000000`.
- Signature map: `E:\Scratch\creative-original-1.1-20261008\creative\sigs\6A4236C8-2DA7000.json`, SHA-256 `00B843D5F343801CA54F021A9499581622EB46014A2E58A56DD2EA15A8926D38`.

## Original hour command path

The UI handler at DLL RVA `0x125F30` invokes a `void(float)` callback from its slider record at `0x126029`. The slider label and description are at RVAs `0x31E9D8` and `0x31E9E8` (“TIME OF DAY” and “Jumps forward to the hour you pick.”). The exact callback object is compatible with `SettingsServices::lambda_4`: RTTI identifies `_Func_impl_no_alloc<lambda_4, void(float)>` at `0x415A00`, and its vtable at `0x30FC50` routes the float adapter through `0x44F10` → `0x5B3C` → setter body `0xC8DE0`.

The setter receives a float hour. It clamps the value to nonnegative, wraps it using the float constant `24.0` at `0x317090`, stores the normalized hour at controller offset `+0x4C`, and stores `GetTickCount64()` at `+0x50`. The paired optional-float getter body at `0xC8D10` requires `+0x48 >= 0` and an update timestamp at `+0x60` no older than 2,000 ms; it returns the queued hour at `+0x4C` while nonnegative, otherwise the current hour at `+0x48`. The request itself has its distinct `+0x50` timestamp.

The time-control callback begins at RVA `0xC9110`. It receives a controller pointer in `RCX` and a native clock pointer in `RDX`. It stores current `GetTickCount64()` at controller `+0x60`, reads the native `timeOfDay` qword using the configured offset in controller `+0x40`, divides by `3.6e12` (double constant at `0x317098`) to convert clock units to hours, and stores the current hour at controller `+0x48`. If a queued hour is present at `+0x4C` and at least 300 ms elapsed since the request timestamp at `+0x50`, it atomically clears the queued field to sentinel `-1.0` and applies the request through the clock synchronization fields.

The controller’s offset table is used in this order:

| Controller field | Native field read/written | Signature JSON offset |
|---|---|---:|
| `+0x00` | `syncAnchor` | `+0x10` |
| `+0x08` | `syncBase` | `+0x18` |
| `+0x10` | `syncScale` | `+0x20` |
| `+0x18` | `syncVersion` | `+0x24` |
| `+0x20` | `dawn` | `+0x28` |
| `+0x28` | `dusk` | `+0x30` |
| `+0x30` | `dayLength` | `+0x38` |
| `+0x38` | `nightLength` | `+0x40` |
| `+0x40` | `timeOfDay` | `+0x48` |

For a request, the callback reads day/night schedule values and current sync fields. It advances `syncBase` to the current anchor using elapsed time and `syncScale`, sets `syncAnchor` to the current tick, increments `syncVersion`, and then writes the phase-adjusted requested time into `syncBase`. The code reads `timeOfDay` to expose the current hour; this command handler does not directly write `timeOfDay`. The native clock tick remains responsible for recomputing `timeOfDay` from the synchronization state.

The helper ABI is explicit in the DLL: `0xC7510` reads a qword at base plus configured offset, `0xC74F0` reads a float, `0xC74E0` reads a dword, `0xC79B0` writes a qword, and `0xC79A0` writes a dword. The callback uses the qword writer for `syncAnchor` and `syncBase`, the dword writer for `syncVersion`, and finally writes the chosen phase to `syncBase`.

## Dedicated-server equivalent

The server tick candidate resolves semantically: wrapper callsite `0xCAD51` calls `0x8614B0`, while the ambiguous `0x606C6D` callsite targets helper `0x4EF180`. The scale lead at `0xC9791` is an intra-function jump destination and reaches the scale writer `0x8507C0`; it is not a proven standalone hook ABI.

The server tick at `0x8614B0` updates `syncVersion +0x24`, `syncBase +0x18`, `syncAnchor +0x10`, and `timeOfDay +0x48`. The separate server scale writer at `0x8507C0` updates `syncVersion +0x24`, `syncScale +0x20`, `syncAnchor +0x10`, and `syncBase +0x18`. These match the signature JSON field layout and the original DLL’s synchronization strategy.

The wrapper at `0xCACE0` gets an object through helpers `0x5DC7F0` and `0x5D98D0`, keeps it in local `+0x30`, and passes the same pointer in `RCX` to day/night updater `0x861790` and native tick `0x8614B0`. Static code still does not map that object to an authenticated world identity or prove its allocation/destruction boundary. It also does not prove safe serialization of concurrent admin commands with the native tick. No world-scoped backend or code module is therefore safe to emit yet.

## Verification

Run the read-only verifier from any directory:

`C:\Users\herks\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe E:\Worktrees\enshrouded-server-parity-20261009\native_time\tools\verify_time_probe.py`

It checks pinned hashes, signatures, original callback/vtable routing, important native reads/writes, both server tick candidates, and the server writer bytes. A pass does not prove world identity, lifecycle, concurrency, authenticated command permissions, or live-server behavior.

## TIME-3 callback ownership trace (2026-10-09)

This continuation remains offline and uses only the pinned images listed above.

### TIME-3 callback metadata and manager path (historical; refined below)

The server PE exception table places 0xCACE0 in one function range, 0xCACE0..0xCAD66. Nearby .rdata metadata has a name pointer at 0x136B9C0 to 0x136F730 (server_update_daytime), a qword value 0x15 at 0x136B9C8, and the function pointer at 0x136B9D0 to 0xCACE0. This associates the callback body with that name and ID. It does not reveal the metadata consumer or its dispatch callsite.

At entry, 0xCACE0 saves RCX into RSI, then calls 0x5DC7F0 with that context, an output record at stack +0x20, and 0x18 in R8. The helper overwrites R8 before using it, so the meaning of that third argument is not established. The query helper starts by loading [RCX] as its registry root, builds component views from that root's tables, and copies the original context into the first query-output slot. The wrapper then calls 0x82B4C0 and 0x8344A0 on the query result to obtain two qword inputs that it passes to the day/night updater. It calls 0x5D98D0 with the same context for a manager-derived tick value. At 0xCAD29 and 0xCAD49, it loads the query result at wrapper-local +0x30 into RCX and passes it first to 0x861790 and then to 0x8614B0. The latter performs the native clock tick and updates syncVersion, syncBase, syncAnchor, and timeOfDay at the previously established offsets.

The helper 0x5D98D0 follows [context], reads qword [root+0x50] and dword [root+0x6C], multiplies them with overflow protection when the dword is nonzero, and writes the result through its output pointer. The wrapper invokes it before both time functions. This proves a registry-backed context and per-invocation update order. It does not prove that the root is an authenticated world/session identity or that these callbacks cannot run concurrently on different scheduler threads.

The callback body uses one context argument in RCX. At this stage, the metadata consumer and scheduler invocation were still untraced: a Capstone linear sweep found no direct RIP-relative reference to the metadata record, and no E8 rel32 encoding in .text targets 0xCACE0. Later TIME-4 probes below recover a second registered clock writer and narrow the remaining question to runtime serialization/lifecycle evidence.

### Remaining ownership and serialization edge

No constructor, owner registry, world/session token, generation counter, unregister path, or destructor for the context/root/clock component was found in that probe. Parent follow-up `WORLD-IDENTITY-1` later accepted per-call world resolution through `flight::read_query_world`; no persistent clock pointer is used by the policy. Static evidence still does not establish a non-reused world lifecycle token or callback thread affinity/serialization.

At the end of TIME-3, this evidence was still missing and no native backend was emitted. The updated, narrower activation requirements are listed under TIME-4 below.

## TIME-4 isolated command and clock policy (2026-10-09)

The TIME-3 conclusion above is superseded only for the pure policy layer. New
`native_time/include/time_control.hpp`, `native_time/src/time_control.cpp`, and
`native_time/tests/time_control_tests.cpp` implement a pointer-free policy and
math seam. There is still no native callback adapter, hook, shared runtime/CMake
integration, or live-server write.

### Command and ownership contract

The isolated model uses `WorldTimeAction::{unspecified,set_hour,set_mode}` and
`WorldTimeMode::{unspecified,normal,pause,fast}`. Its queue API takes the full
`flight::Identity`, owner, a host-issued nonzero `WorldScope`, action, and
capability. An absent capability provider denies by default. The time
capability is separate from flight approval. The bounded queue stores value
commands and rechecks scope, owner identity, and capability in the current
callback snapshot. `WorldScope` is a lifecycle token, never a native pointer;
the host must change it when a world can be destroyed or its address reused,
and call `retire_world_scope` on teardown. The module retains no native clock
or world address.

The host supplies `CallbackSnapshot` only from a validated current callback:
world, lifecycle scope, callback epoch, current owner resolution, full
identity, and copied native clock fields.
Parent-provided `WORLD-IDENTITY-1` evidence authorizes reuse of
`flight::read_query_world` on CACE0's query output slot 0. The host must resolve
the current world and current authenticated owner on each callback and perform
the identity/capability recheck there. Neither the scope token nor the two
epochs are native fields: the host must source them from validated
lifecycle/callback/write observations, never synthesize them from a raw address.

`set_hour` is one-shot and preserves the current rate. `set_mode` preserves the
current hour. Pause installs scale `0.0`; fast installs the native full asleep
scale `60.0`; normal restores the rate captured before a temporary override, or
uses native normal scale `1.0` when no owned override can be restored. A
temporary pause/fast override records the full owning identity, lifecycle
scope, sync base/anchor/version, unchanged day/night schedule, installed scale,
callback epoch, and per-world native scale-writer epoch. An hour jump during an
owned override advances its tracked sync base/anchor/version and keeps the
original owner. Tick progression is accepted only when the callback epoch
advanced, syncVersion advanced exactly once, the pinned native float conversion
reproduces syncBase at the new syncAnchor, the schedule and scale are unchanged,
and the native scale-writer epoch is unchanged. That epoch must advance for
every native scale-helper call, including a same-scale write. Any other
version/base/anchor/schedule/writer change clears the override without restoring
over the newer state.

### Native trace recovered from the pinned server

The full registered function range for `0xCACE0` is `0xCACE0..0xCAD66`. Its
body saves the sole incoming context argument from `RCX`, allocates a 0x20-byte
local query area at `RSP+0x20`, and calls `0x5DC7F0` with that context and the
area. It sets `R8D=0x18`, but the query helper overwrites `R8` before use, so
that value is not a proven length or callback argument. CACE0 reads query slot
1 at `RSP+0x28` for the two day/night inputs, reads slot 2 at `RSP+0x30` as the
clock object, and calls `0x861790` then `0x8614B0` on that clock. Slot 3 at
`RSP+0x38` is not read by this wrapper. The return value is unused. The
registration record at `0x136B9C0` contains the name pointer, string length
`0x15`, and callback pointer at `0x136B9D0`; the middle qword is the 21-byte
name length, not a system ID.

The complete helper body at `0x5DC7F0` resolves the registry root from
`[context]`, writes the original context to output slot 0, derives output group
sizes from registry fields, and materializes multiple component groups. Its
layout includes 8-byte-scaled groups and one 16-byte-scaled group from the word
at root `+0x4A2`. Nearby data has one-count descriptors pointing at the
`BalancingRegistry` and `g38_daytime::Daytime` name records. This is stronger
than the earlier ambiguous mid-function leads, but the pinned offline trace
does not prove which generated group descriptor binds Daytime to the
pointer/counter lane, what the adjacent counter means, or whether the
dispatcher serializes its writes against the normal tick. Do not infer those
semantics from registration-name ordering or string lengths.

Clock advance in server helper `0x82CFE0` is:

1. Saturating signed `now - syncAnchor`.
2. Convert the signed delta to double, multiply by `1e-9`, convert to float,
   multiply by float `syncScale`, convert back to double, multiply by `1e9`,
   and truncate toward zero.
3. Saturating addition to `syncBase`, with native sentinel handling.

The pure model matches the conversion sequence and fails closed on arithmetic
overflow rather than imitating native saturation/sentinel values. The scale
writer `0x8507C0` accepts clock in `RCX`, scale in `XMM1`, and current tick in
`R8`; it computes the current base, increments `syncVersion`, then stores
scale, anchor, and base. The native tick has a conditional path that also
increments `syncVersion` and updates base, anchor, and `timeOfDay`; the
day/night updater can also increment the version while changing sync fields.
Therefore version and scale alone cannot identify ownership.

### Native time writers and their registrations

The native scale callsite at `0xC9802` is inside the callback registered as
`server_nighttime_skip`: the metadata at `0x136A360` points to that name, its
length at `0x136A368` is 21, and `0x136A370` points to callback entry `0xC9670`.
The complete callback body is split across the contiguous PDATA fragments
`0xC9670..0xC9684`, `0xC9684..0xC96C3`, `0xC96C3..0xC96CE`,
`0xC96CE..0xC9754`, `0xC9754..0xC9791`, `0xC9791..0xC97E7`, and
`0xC97E7..0xC9816`. Its prologue queries the current registry context through
`0x5DC7F0` using its sole context argument in `RCX`, reads the clock view from
query-local `RSP+0x40`, checks native night/sleep state, and conditionally
reaches the scale call. At `0xC9802` it calls `0x8507C0` with clock in `RCX`,
scale in `XMM1`, and tick in `R8`; the setter returns `void`. A direct E8 scan
finds one callsite to `0x8507C0`, at `0xC9802`.

The second registered writer is `server_update_daytime` at `0xCACE0`. It calls
`0x861790` (day/night settings update) and then `0x8614B0` (clock tick) on the
same query-local clock. The day/night updater can itself increment version and
write `syncBase`/`syncAnchor` if its settings-change flag is set. The tick body
continues across seven adjacent PDATA ranges from `0x8614B0` through
`0x861781`; its flagged path increments version, updates base/anchor, then
stores `timeOfDay`. Direct E8 scans find these two calls only at `0xCAD37` and
`0xCAD51` within `server_update_daytime`.

This recovers the competing native writers and callback metadata. It does not
prove callback thread affinity, execution ordering, or lock order. A shared
recursive guard around the complete `server_nighttime_skip` and
`server_update_daytime` entries could cover the two traced write paths, but
the pinned bodies do not establish that their indirect callees never take
locks or wait on scheduler work. No guard or hook is therefore installed.
The pure reducer instead requires host-observed per-world callback and scale
writer epochs, and accepts one tick increment only when the native conversion,
schedule, rate, and callback progression all agree.

### Remaining native activation gate

The per-call world association is resolved by the parent-provided
`read_query_world` evidence, and the pure command/policy/math layer is ready for
an adapter. Native activation still needs runtime evidence that both registered
callbacks and any command write share a safe serialization boundary, plus a
validated, non-reused per-world lifecycle token for override cleanup. If the
guard requires observing runtime behavior, use a bounded default-off read-only
observer for callback RVA/name, thread ID, context, query slot 0, clock slot 2,
world identity/scope, callback epoch, scale-writer call/epoch, syncBase,
syncAnchor, syncScale, syncVersion, timeOfDay before/after, and world teardown
or address-reuse observations. Do not log credentials or enable clock writes.
Runtime behavior, save behavior, and live server acceptance remain unverified.

### Verification

The policy test is a standalone MSVC C++20 build with `/W4 /WX`; it exercises
hour normalization, action separation, missing capability denial, lifecycle
scope retirement/staleness, native float conversion/truncation, overflow,
day/night phase mapping, pause/fast/normal restoration, one-shot hour changes
during an override, one exact native tick progression at pause and fast scales,
rejection of unrelated scale-writer/schedule changes, and concurrent queue
ordering. The offline binary verifier checks pinned hashes, callback
registrations, PDATA fragments, register inputs, direct callsites, sync writes,
and conversion constants. Neither check proves runtime serialization or live
behavior.
