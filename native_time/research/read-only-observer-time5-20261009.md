# TIME-5 read-only clock observer

## Implementation

`native_time/src/read_only_observer.cpp` adds an isolated observer with an
explicit `ReadOnlyObserver::install` / `stop` API in
`native_time/include/read_only_observer.hpp`. Nothing calls the install API;
the module does not hook, configure, advertise, or write clock state by itself.
The host must provide a pinned-image validator, guarded memory reader, thread
ID and monotonic-clock readers, and a complete hook backend. An absent callback,
partial hook creation, partial activation, invalid image, or sample duration
outside 1..20,000 ms leaves observer readiness false.

The observer covers six entries:

| Entry | Purpose |
| --- | --- |
| `0x5DC7F0` | Observe the query result already built by the native callback. |
| `0xCACE0` | `server_update_daytime` callback entry/exit. |
| `0xC9670` | `server_nighttime_skip` callback entry/exit. |
| `0x8507C0` | Native scale writer. |
| `0x861790` | Native day/night settings updater. |
| `0x8614B0` | Native clock tick. |

The extra query-helper hook captures slot 0 (the callback context) and slot 2
(the clock) after the original `0x5DC7F0` call returns. It resolves the world
with the existing `flight::read_query_world` on the query output. It therefore
observes the exact query used by each callback, issues no second query, and
does not touch query `context+8`. Query/world/clock/context addresses are
converted immediately to salted per-run labels; event records retain labels,
not pointers.

At query results and immediately before/after each writer trampoline, the
observer reads the pinned clock fields: `syncAnchor +0x10`, `syncBase +0x18`,
`syncScale +0x20`, `syncVersion +0x24`, dawn/dusk/day/night lengths at
`+0x28/+0x30/+0x38/+0x40`, and `timeOfDay +0x48`. It brackets the snapshot with
two version reads so consumers can see whether those values changed during
sampling. The snapshot remains best-effort: matching version reads do not prove
an atomic snapshot if a writer is between its version increment and field
stores.

Events carry callback and writer epochs, thread IDs, callback nesting depth,
global active-callback counts, callback overlap counts, enclosing callback,
opaque query/world/clock labels, native writer arguments, and bounded before/
after field snapshots. A fixed 4,096-entry ring drops a record rather than
waiting on a contended slot. Capture expires after at most 20 seconds. Original
trampolines are called once on every enabled path, including after capture
expires. `stop` disables enabled hooks but leaves trampolines resident for
callbacks already on the native stack. Keep the observer object,
`RuntimeReaders::context`, and `HookBackend::context` alive for the server
process lifetime. A successful stop is not a global quiescence barrier.

## Guard assessment

The three native writer hooks bracket the bodies that the pinned direct-call
scan found at `0xC9802`, `0xCAD37`, and `0xCAD51`. `0x8507C0` calls the pinned
clock-math helper `0x82CFE0` and then writes the scale synchronization fields;
`0x861790` can update synchronization fields when balancing settings change;
`0x8614B0` can update the version, base, anchor, and `timeOfDay`. The callbacks
also perform query and callback work outside those writer bodies.

A lock confined to these helper entries would not hold across arbitrary
scheduler work and could serialize an adapter writer only if every adapter
write used the same lock. The current static evidence does not prove that all
clock mutations route through these three entries or establish runtime
interleaving and lock order. In particular, separately locking the updater and
tick does not establish the full callback ordering. This observer adds no
guard and holds no lock across native callbacks. Its runtime events can show
whether the two callback entries overlap, their threads and epochs, and the
order of the three observed writer bodies. That evidence still cannot prove
that an unobserved alternate write path does not exist.

## Integration contract and limits

`native_time/src/read_only_server_observer.cpp` now supplies the concrete
Windows server adapter. Its explicit install API validates that the supplied
module is the process main image and passes `xhl::validate_server` (filename,
pinned SHA-256, AMD64 PE timestamp, and image size), initializes or reuses
MinHook, and supplies a guarded memory reader, `GetCurrentThreadId`,
`GetTickCount64`, and the six-hook backend. It is not wired into shared runtime
or CMake and nothing calls it in this task.

The adapter requires both an explicit call to
`install_local_read_only_server_observer(HMODULE)` and the exact environment
value `XHL_ENABLE_NATIVE_TIME_OBSERVER=1`; missing, longer, or different values
deny installation. The gate only enables observation. `ready()` means all six
observation hooks enabled; it is not native clock-backend readiness, a time
permission check, or authorization to write. The separately configured private
time capability remains a distinct default-denied runtime decision.

The adapter's context is intentionally retained for the process lifetime and
owns the observer, immutable server module handle, and the readers/hook callback
contexts. `ReadOnlyObserver` also requires its object, `RuntimeReaders::context`,
and `HookBackend::context` to remain alive for the server process lifetime.
`stop()` sets capture off and disables hooks, but even a successful return is
not a global quiescence barrier: a detour can have already loaded the active
observer pointer, and the capture counter excludes post-stop pass-through calls.
The adapter therefore retains all contexts and original trampolines through
process exit. An already-dispatched callback or writer detour after stop calls
its original exactly once and records no further events.

Evidence export uses an explicit caller-supplied `JsonLineWriter`; this module
does not create or write files. Dumping is available only after successful
stop, emits sequence-sorted JSONL, omits native argument qwords that could
contain addresses, encodes floats as bit patterns, and exposes only per-run
opaque object labels. The sink must synchronously consume each temporary line.

The opaque world label represents the per-call world pointer resolved by the
validated query chain. This observer does not reconstruct the full authenticated
identity, detect world destruction/address reuse, or create a persistent world
scope. Correlate its labels only within the bounded sample and alongside the
runtime's validated identity evidence. No live server run, hook installation,
or gameplay write was performed for this task.

## TIME-6 world-lifetime and writable-backend boundary

The existing `flight::Identity` is a full owner identity containing backend,
session, world, player, machine, peer, Steam ID, authentication serial, and
lifecycle serial. Its lifecycle is maintained by the session/actor logic; it is
not evidence of a native world allocation generation. The query path establishes
the callback-local native world as `query -> context -> execution`, with the
execution state at `world + 0xCC4218`. Neither those facts nor a presumed
one-world-per-process dedicated-server model establish that the native world
pointer cannot be destroyed and reused.

A writable adapter can avoid retaining a clock pointer: queue the complete
authorized `Identity` and command, then resolve the world and clock anew inside
the owning callback, compare the current complete identity and separate time
capability, and perform any read/write only before that callback returns. A
temporary override should be keyed to the current resolved world scope and full
owner identity, and record the values/version it installed. On revoke or
disconnect, restore only through a later callback that resolves the same
validated world and the clock still matches the override's expected progression.
Native tick increments `syncVersion` and advances `syncBase`/`syncAnchor` and
`timeOfDay`, so exact version equality is too strict. A future restoration rule
must accept only progression proven to be native tick/updater activity and
abandon restoration after any unexplained schedule, scale, world, clock, or
owner-generation discontinuity; it must not overwrite arbitrary external
changes.

Observed world-address transitions and owner/session lifecycle changes can
invalidate pending override state without a whole-game allocator proof. They
still cannot distinguish a newly allocated world that reuses the same address
between observations and reproduces the same clock fingerprint. Closing that
specific indistinguishable-reuse case requires either a proven native world
generation/destruction callback or a server-lifetime guarantee; absent either,
the safe fallback is to drop the old override without restoring it. The
read-only observer does not prove writer serialization, native scheduler
ownership, world-generation uniqueness, or restoration semantics and performs
no clock writes.

## Verification

Built the isolated observer tests and server adapter translation unit with MSVC
C++20, `/W4 /WX`; the observer test executable passed. Tests cover invalid image
and duration rejection before hook creation, partial create/enable failures,
exact one-call trampoline behavior, query/world/clock association, unmodified
`context+8`, native field snapshots, nesting/overlap counters, bounded sample
expiry, pass-through after expiry, and already-dispatched callback/writer
detours after successful stop. The adapter was compiled only; its environment
gate and MinHook path were not activated. `native_time/tools/verify_time_probe.py`
also passed all pinned server/DLL hashes, original signature values,
registrations, callback ranges, writer ABIs, direct callsites, field writes,
and conversion checks. No live server run, hook installation, or gameplay write
was performed.
