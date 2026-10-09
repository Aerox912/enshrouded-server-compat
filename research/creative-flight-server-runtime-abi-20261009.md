# G-DISPATCH7 frozen default-off server runtime and CALL bridge

This package is implemented and self-checked, **uninstalled and disabled**.
It announces no G capability and changes no shared startup/client/CMake code.
Static and fixture results do not establish gameplay, callback phase order,
gravity/landing behavior or paired client prediction acceptance.

## Corrected native route

The earlier synthetic `flying_mover` hook was retired. Real state3 bypasses all
native movers; state4 calls the glider. The correction and complete paired
tables are in `creative-flight-server-runtime-dispatch-correction-20261009.md`.
The authoritative reproduced evidence is
`creative-flight-dispatch-static-evidence-20261009.json`.

Pinned server SHA256:
`001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637`.
Pinned client SHA256:
`AF2F5A1227911D8AA06B3908D6BD0211838211CAE14EA91099CB57D0DF990781`.

Both verified entries (server187120, client3A5C30) execute:
`push rbp; push rsi; lea rbp,[rsp-198]; sub rsp,298`.
Their parent unwind records independently describe allocation298 plus the two
pushes. At server187595/client3A60A5 native RSP is 16-byte aligned,
RBP=nativeRSP+100. Native row is RBP-60, size138; native second object is RBP+1B8.
The original second object comes from query time helper5D98B0, not an invented
simulation frame counter. Native per-row preparation before the site remains
untouched.

Exactly eight displaced bytes:
`48 8B 45 38 0F B6 48 3D`
(`mov rax,[rbp+38]; movzx ecx,byte[rax+3d]`).
The bridge replays both against original RBP after restoring private row
pointers. Native CMP/JA/table sequence at server18759D/client3A60AD is untouched.
The actual tables18775C/3A626C map state3 to187619/3A6129; state4 to the glider
cases1875E5/3A60F5. Those cases call17F5A0/39E2B0.
The explicit G Dive targets are17E480/39D190.
Post-dispatch187619/3A6129 calls17EA40/39D750 with the same native row/second.

## Native bridge and unwind

`creative_flight_dispatch_bridge.asm` is a real CALL target with MASM FRAME.
Entry RSP mod16=8. Push original RBP, push original RFLAGS, allocate1A8:
C++ route CALL has aligned RSP and its normal32-byte shadow space.
Stable bridge RBP addresses saved GPRs, sixteen XMMs, MXCSR and permanent flags.
The emitted unwind declares all nonvolatile GPR/XMM saves, the allocations,
original RBP push and SET_FPREG. The recognized epilog restores stack from
stable RBP, pops original RBP and RETs to the genuine native CALL return PC.
The two padding NOPs lead into unchanged native CMP.

Normal return preserves all GPRs except the native stolen MOV outputs RAX/ECX,
all sixteen XMMs, MXCSR and original flags. Native CMP subsequently sets its
own flags. Real C++ and SEH exceptions unwind to the native caller rather than
running continuation after a partially failed native mover. Only the runtime
translation unit requires /EHa for native SEH private-row RAII cleanup;
other source files retain /EHsc. The C ABI route explicitly uses
`noexcept(false)`. The runtime bridge test and OS RtlVirtualUnwind test
validate actual emitted unwind metadata, not a hypothetical C++ prototype.

## Installable contract, still no installer

`make_dispatch_call_plan` produces eight bytes:
`FF 15 <signed rel32 to private aligned qword slot> 90 90`.
The slot holds the full64-bit address of the assembled bridge.
RIP displacement is calculated from site+6, accepts both representable signed
boundaries with slot alignment, and refuses out-of-range/overflow/overlap,
wrong server site, unverified image/dispatch, changed/truncated native bytes,
null bridge/slot or misalignment. It never allocates, protects or writes memory.
It does not produce a client patch plan; the paired site identifies the client
prediction counterpart, whose independently approved client provider still
needs its own integration.

A later installer owns an exclusive executable patch transaction and must:
verify the full loaded pinned server identity and reproduced dispatcher proof;
verify exactly the original eight site bytes immediately before writing;
allocate/retain a private reachable aligned slot, populate its bridge pointer,
and publish only the validated plan while native execution is quiescent.
Retain the slot and loaded module for every possible bridge invocation.
On teardown disable the runtime, drain its retained callback scopes, quiesce
**all** paths through the patched native function (including denied/outside
adapter scopes), restore only if current bytes equal the exact plan, flush the
instruction cache, remove/drain any scoped callback/iterator/state hooks and
only then release slot/module allocations. Runtime `stop()` drains its
callbacks; it is not a proof that an unscoped native PC has left the ASM bridge.
An eight-byte source write is not assumed atomic or safe merely because of its
size. No general MinHook JMP entry can replace this real-CALL contract.

## Runtime integration API

`ServerGRuntime::start(provider,options)` installs nothing.
Both movement and bounded observation default false. Full pinned hash is
mandatory; movement additionally requires explicit phase-order acceptance and
the pinned Dive pointer. Callbacks/providers remain valid until external
`stop()` drains all scopes. Reentrant stop disables immediately and returns
false; do not destroy the runtime from inside a callback.

The integrator routes only the proven outer Control A0830, State189BF0,
Mover187120 and Gravity7E7E0 callbacks, forwarding their original once.
The global query iterator5D7960 passes through immediately outside the exact
nested scope/query/size. Each actual row retains its query/context, owner from
5D5130 and world from execution root minusCC4218, with registry==world+928 and
source rereads. Owner is1..16. Approval must synchronously verify distinct G
capability/lease/generation, complete authenticated Identity and current live
Actor; it must never return an F6 lease. Provider read must guard every bounded
memory copy. Provider callbacks run under the runtime mutex; they must not wait
on transport work needing that mutex or recursively destroy the runtime.

Completed native Control output supplies configured Jump/Sneak/Sprint and
native camera-relative desired movement. Each copied command is limited to
100ms, full Identity/Actor/G-generation and one consumption. State forwards all
eight native arguments once and only replaces ordinary Falling2 with Flying3.
The dispatch route is a static TLS-scoped no-op outside the exact current
mover row. On approved state3 it invokes Dive with private config/target
pointers and restores them before native dispatch. State4 and other native
states keep their original dispatcher. Grounding, death, spawn, read failure,
approval/Actor/lifecycle/generation change suspend/retire G local state.
Grounded clears only the airborne run and configured-action state: the current
approved G binding remains armed for native walking and a later fresh jump.
Death/spawn/revocation still retire the generation and require fresh activation.
G does not inherit F6's same-session respawn preference.

Gravity copies only the current actor's size14 component, disables private
isActive+10 and substitutes row+10 during that row. It restores before advancing
the cursor and on every normal/exception scope exit. No shared gravity/config
object is changed. The row's Actor+8 is proven by7E85F loading it and calling
2A1E0, which reads Actor current BF8 and predicted state.
The new runtime uses repeated flags1B1 bit0, BF8, BD0 and BD8 reads and
conservatively gates raw OR effective `(BF8|BD0)&~BD8`.
Raw/effective Dead7 and Spawning12 both deny execution; Grounded0 retires the
local flight run. Unknown/inconsistent reads deny. Every helper authorization rechecks the current
raw/effective state; the native Dive entry checks once more after private-pointer
setup. Helpers operate on a candidate policy so their generic deny/revoke path
cannot retire the armed G generation when the specific cause is landing.
Tests cover raw/effective land -> walk -> jump, grounding during state selection
and dispatch, and late death/spawn before native entry. Frozen client-life/F6/shared
movement helper files remain unchanged.

Read-only phase observation starts only at the first approved full Identity,
bounds20s/200 attempts/events and20Hz per phase, and permanently stops on
retirement/revocation. Events contain only phase/local order/elapsed time.
Ordering is an observation counter, not a proven simulation frame.

## Reproduction and scope

Existing Python:
`C:\Users\herks\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe`.
Existing CMake:
`C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`.

Run `tools/test_creative_flight_server_runtime.py --build-dir
E:\Build\enshrouded-gflight-server-runtime-test-20261009 --cmake <cmake>`.
This creates only a marked isolated harness, enables ASM_MASM, applies /EHa
only to the runtime TU, builds MSVC Release /W4 /WX and runs CTest plus both
binaries. Output is retained in that build root's `test-output.log`.
Final results: **775 runtime checks,5161 executable bridge checks,CTest2/2**.
Runtime fixtures include the actual assembled bridge with the retained native
query frame, once-only Dive/continuation, SEH pointer restoration, generation/
death/grounding/prediction mutation, read failure, expiry, drain and observer
bounds. Bridge fixtures exercise all256 uint8 MOV outputs, all preserved
GPR/XMM/MXCSR/flags, exact encoding/refusal bounds, real C++/SEH and OS unwind.

Run `tests/test_creative_flight_dispatch.py`: **12 verifier regressions**.
Run `tools/verify_creative_flight_dispatch.py --server <pinned server>
--client <pinned client> --bridge-pe <isolated bridge test EXE>
--json <evidence JSON> --freeze-package`. The evidence includes SHA256 for the
12 other owned source/test/tool/document files under `package_freeze`; the JSON's
own SHA256 is recorded separately at handoff. Reproduction regenerates those
hashes from the exact current files. No third-party PE dependency is required.
Both images fail closed on SHA256/PE identity. It mechanically checks all256
original states, branch/table/call edges, parent chained unwind/native frame,
server frame/gravity/state anchors and the emitted standalone ASM unwind.

Next isolated acceptance starts with read-only phase capture under authorized
current owner/full Identity, validates actual Control→State→Mover/Gravity
ordering for walk, jump/held jump, sneak/sprint and controller remaps, then
independently tests approved movement, ground landing/gravity restoration,
G revoke/death/respawn/new activation and a denied peer unchanged.
A paired client implementation must receive its current accepted G binding/
generation and local world/Actor/life state, perform equivalent configured
input/state/mover/gravity behavior for prediction and retire on local life or
connection changes. This server package does not complete that client work or
claim full Creative flight acceptance.

## G-SERVER15 command reader lifetime and wired observer

The production G command reader now receives a caller-owned
`std::array<char,128>` from `server_g_process_local_command`. Its
`std::string_view` is consumed while that array remains in scope, through
`parse_g_local_command` and command handling. Empty input and input of 128
bytes or more remain rejected; duplicate-command filtering is unchanged.
This reader lifetime was source-reviewed and compiled, but not exercised
end-to-end through the local file in a live server. VERIFY57 accepted the
bounded repair; see `src/native_runtime.cpp` around lines 670-694.

The observer starts only after a verified, safely settled published server
site, then waits for a local `observe-phase-v1 N` request before sampling.
It does not require `service.ready()` to collect private phase evidence.
Movement and G capability remain closed until matching evidence is explicitly
accepted and the current authenticated binding is rechecked. The accepted
server CMake snapshot for this integration is SHA256
`A32B4EC872C4A622E67B74A8117BDEAE9A20C1295E7858EF7618C57B4F088318`; that
shared file was subsequently released for client wiring. The compact
G-SERVER15 research report and source/artifact manifest record the accepted
snapshot and the focused build/test evidence.
