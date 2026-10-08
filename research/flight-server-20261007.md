# Dedicated-server flight investigation

Status: **development client/server pair builds; local startup verified; live
flight and hosted deployment not ready**. No hosted server has
been updated for flight. The user reports that flight toggles on but normal
descent persists with a controller, including pulling the stick back.

## Binary evidence

`tools/inspect-flight.py` compares the installed client with the staged dedicated
server without changing either file. `flight-sites-20261007.json` records its
output. Both the full 18,213-byte unwind body and a 256-byte local window have a
unique normalized match. Only relocation-dependent operands are masked.

| Property | Client | Dedicated server |
|---|---|---|
| Revision | 1076226 | 1024233 |
| SHA-256 | af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781 | 001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637 |
| actor_rotation function | 0x22a7a0 | 0x63f50 |
| Glide clamp MOVSS | 0x22afe1 | 0x64791 |
| Original lower pitch | 0.0872664600610733 | 0.0872664600610733 |
| Constant RVA | 0x1400330 | 0xb567fc |

The installed client mod changes this value to -1.57 only in the client process.
Its loader explicitly rejects non-client contexts and it has no network protocol.
The server contains the matching clamp. This supports the server-correction
hypothesis, but static matching alone does not prove the cause during play.

At the server clamp, the query context is still at `[rbp+0x13e0]`; its original
store is at function entry. The current owner ID returned by `0x5d5130` remains
at `[rbp+0x3b0]`. The temporary at `[rbp+0x13f0]` is overwritten with a float
before the clamp and MUST NOT be used as an owner ID. The original condition
codes remain live across MOVSS/MOVSD into the JA at 0x6479f.

## Player mapping, static evidence only

An earlier note in the Creative repository incorrectly called the 0x60-stride
permission table `ServerPlayers`. The `admin_player` registration at 0x1346750
shows it is **BasicPlayerInfos**. `0x11c290` checks its developer permission bit.
Do not change that permission function or treat its records as Session players.

The actual `ServerPlayers` registry is registered at 0x68cda5 using server state
plus 0x1c0. It contains 16 records, stride **0x2bb38**. Native access at 0x68c30c
uses the low six bits of the Session player handle, then compares the whole
handle against record+0. Record+4 holds the machine handle (written at 0x687e12,
validated in the update loop at 0x69f616). Record reset is 0x68a7e0; server teardown
0x68a430 walks all 16 records and releases the world at state+0x1b0.

`0x117ec0(record)` returns `(record.handle & 0x3f) + 1` for a live record. ECS code
such as 0xab3e6 performs the reverse indexing using owner entity minus one.
Thus the candidate character-to-Session mapping has a concrete native basis.
The original hypothesis was that query context+0 is the ECS world and matches
server state+0x1b0. This has not been established; see test5/test6 below. An entity
number or array index alone is not authority and can be reused on reconnect.

The outer Session comes from `[ [server state+0x38] + 0 ]`. It contains two
published snapshots and an internal object at **Session+0x4b68**. Player and
machine offsets below are relative to that internal object, while the online
wrapper is still at outer Session+0x10. The existing research identifies the Session player to
machine to Steam peer chain, full-generation comparisons and Steam backend at
vtable RVA 0x1311048. These layouts remain unverified in a running server.

The event dispatcher at 0x903880 receives backend in RCX and event in RDX. Event
type 19 uses SteamID event+8 and result event+0x10; zero is success. Peer context
is backend+0xd88. Successful auth changes state 1 to 2 under the native lock.
Native auth-disabled mode also sets state 2, so **state 2 alone is insufficient**.
Observe and bind actual successful auth callbacks to the current peer generation,
and clear proofs on removal, reset, auth failure and backend replacement.

## Offline hook experiment

`src/flight_hook.*` and `src/flight_pitch.asm` implement only the glide-hook
primitive. It requires a supplied authorizer, accepts only character slots 1-16,
checks executable hash/header, original constant, hook bytes and owner-frame
instructions, and makes a separate decision per call. It changes no global game
constant. Failed or revoked authorization retains the original pitch.

The harness is opt-in (`BUILD_FLIGHT_EXPERIMENT=ON`) and separate from `dbghelp`.
Neither the loader nor packaging scripts call or ship the experiment. There is
no deployable flight companion yet, and the fake test authorizer must never be
used as a real authorization implementation.

Built with MSVC 19.44.35228.0 into `E:\Build\enshrouded-flight-server\Release`.
Observed on 2026-10-07:

- 20 primary native-hook checks passed, including 8,000 calls across eight
  threads. These execute the installed hook in a private mapping of the real
  server executable and redirect the continuation to a register recorder.
- 27 rejection checks passed across five separate processes: changed hook,
  changed owner frame, changed PE header, changed constant and absent authorizer.
- Seven existing cloud checks passed.
- RAX, RCX, RDX, R8-R11, XMM1-XMM5, MXCSR and arithmetic flags are preserved.
  XMM0 receives the intended scalar and zero upper lanes, matching MOVSS.
- A different context, other owner, invalid slot and revoked fake approval all
  retain vanilla pitch. Disabling the hook restores the original instruction.

These results do NOT establish live identity resolution, network transport,
controller behavior, mixed-client isolation, Wine compatibility, reconnect/death
handling or synchronized flight. Complete those before preparing a deployment.

## Deployment authorization

The user's latest instruction supersedes the temporary request to keep it local:
deploy the completed fix to Enshrouded, IKEA and Soulrend only while each server
is empty. If occupied, retry every 15 minutes until applied. Do not disconnect
players. The earlier Workshop restart override does not apply to this update.

`flight-deployment.json` currently marks the package unready. Before changing
that status, supply real authorization/transport implementation, matching client
delivery, build hashes, test evidence, file manifest and rollback procedure.
Do not deploy this experiment or call its offline tests full gameplay acceptance.

## Development integration, 8 October

Implemented `flight_session.*`, `flight_identity.*`, `flight_native.*`,
`flight_transport.hpp` and `flight_client.cpp`. These are still opt-in through
`BUILD_FLIGHT_EXPERIMENT`; ordinary `dbghelp` and release packaging exclude them.
`flight_server_dev.dll` is a separate development loader target and
`flight_client_dev.dll` is its Shroudtopia client. Neither is release-ready.

- The server observes actual event-19 Steam success transitions, binds them to
  the current peer handle and SteamID, and resolves generation-bearing player,
  machine and peer records. Failed callbacks, disconnect/reset and character
  removal invalidate state. Native layouts still need live player evidence.
- A strict private `flight-allowlist.cfg` uses `schema=1` followed by one
  `steam_id=<SteamID64>` per line. It is reread every 500 ms. Missing, malformed
  or stale configuration denies access. No real IDs are embedded in source or
  reusable defaults.
- Chat transport was investigated first. Because its reply ABI remains
  unproven, the development pair uses Steam Networking Messages channel 18357.
  Sender identity comes from Steam and must also match a current authenticated
  game owner. Requests carry no SteamID. Steam Messages keeps mod traffic out of
  game chat and shares its underlying peer session across channels. The local
  server logs that the game itself uses NetworkingMessages.
- The 48-byte protocol has a version, build revision, random client nonce,
  random server challenge and monotonic sequence. Duplicates cannot extend
  leases. Approval expires after three seconds without fresh requests. The
  client waits for server approval and anchors expiry to request send time, so
  delayed/replayed replies cannot renew old approval.
- The client discovers a dedicated server only from the game's successful
  native send path and clears discovery on removal/reset or stale traffic.
  Full executable hashes and native entry bytes gate both adapters.

Observed checks: 116 session/protocol checks, 34 synthetic native identity
checks, 20 existing mapped-PE glide-hook checks and seven cloud checks passed.
The session/identity checks use simulated memory and transport, not real Steam
authentication. `git diff --check` passed. Both DLLs build with MSVC /W4 /WX.

An independent local server at
`E:\Scratch\enshrouded-flight-runtime-20261008` uses a fresh test world and
loopback query address `127.0.0.1:15637`. Native flight/identity hooks and existing
Vein Mining/Auto Loot/gifting hooks loaded, Steam connected, and Host Online was
observed. A graceful local stop completed all destruction steps before a second
development build was started. This is Windows startup evidence only, not Wine
or player gameplay acceptance.

The user agreed to test. The development client was installed while the game
was closed; the original DLL and configuration were backed up at
`E:\Build\enshrouded-flight-server\client-test-backup-20261008`. Only the flight
DLL was replaced. The saved original SHA-256 is
`4f66cb115f2c67ed9fd603e7cad3b70fce348937d99058b1236d9f39bee44dbd`.
Current development DLL hashes are in `flight-deployment.json`.

Still required: observed successful native Steam proof plus current owner
mapping, server/client activation and controller gliding, deny/revoke/reconnect
tests, character death/respawn behavior, concurrent mixed clients, Wine and
coexistence acceptance, final artifacts/delivery/manifest/rollback. Keep
`package_ready=false` until that evidence exists. Local startup is not permission
to deploy unverified code to the three hosted servers.

Transport reference: https://partner.steamgames.com/doc/api/ISteamNetworkingMessages

## First connected test and mapping correction, 8 October

The client loaded and logged F6 presses at 18:04:57, 18:05:04, 18:05:31 and
18:05:35 UTC. Each was denied because no server challenge had arrived. Native
Steam authentication succeeded on the server, but no character mapping was
resolved. The user observed normal descent and no useful F6 response.

A diagnostic local build identified the first failure as the Session player
record comparison. Read-only inspection while the user was connected showed
server player handle 64 and machine handle 129, but zero at the incorrectly
assumed Session+0x2a60. Constructor disassembly at 0x89473c, 0x894766 and
0x894777 establishes that Session embeds its internal state at +0x4b68.
The adapter now adds this offset to the native player and machine table reads.
It still validates every full handle, the backend, native authentication proof,
and the private allowlist. No authorization check was bypassed.

35 synthetic identity checks and 116 session checks passed after this repair.
The regression check rejects a lookalike player record in the published snapshot
region. The corrected server is running locally as test4; the client DLL is
unchanged. A new connection and flight activation remain necessary. Hosted
servers have not been modified, and package_ready remains false.

## Connected test4 and input cadence repair

At 20:25:09 local time (18:25:09 UTC), native Steam authentication succeeded.
At 20:25:10, the corrected full character/machine/peer mapping resolved. The
server received a mod-channel request at 20:25:19 and approved On at 20:25:42
and 20:25:46. The client recorded server-confirmed On at 18:25:43 and
18:25:47 UTC. Disconnect at 20:25:55 cleared the server owner mapping.
This establishes the observed connection and activation path for this one
approved client. It does not establish mixed-client isolation or gameplay.

The user still reported normal descent and a long F6 delay. The client used
the shared Shroudtopia update loop, whose default interval is 500 ms and whose
input polling can miss short presses. Test5 moves flight input, replies and
notices to a dedicated 8 ms worker. Loader configuration access and logging
remain on the loader thread; mutable protocol and notice state remain on the
worker. Off clears the local approval immediately on a detected input edge.
Reliable protocol packets now request Steam's NoNagle option.

Ten real-worker/simulated-input tests passed, including short presses without
loader updates, held-key suppression, stop/join and cleanup on an injected
worker exception. The 116 session tests and 20 mapped-PE hook checks also passed.
The latter still check register/flag/MXCSR preservation after adding bounded
diagnostic counters for actual glide calls, owner, world match, requested pitch,
vertical velocity and permitted calls. These diagnostics grant no privileges.

Test5 is installed locally and the game was reopened. Original client backup
is unchanged; the restore script and deployment record reference the current
development hash. Actual movement-hook application and responsive gameplay
remain unverified. No hosted server or public release has been changed.

## Test5 result and test6 diagnostics (2026-10-08, 20:46 local)

The user confirmed responsive toggles but unchanged descent. Client event times
show request-on to approval at 62 ms and 63 ms. Off cleared the local effect in
the same polling tick. The server observed authenticated requests and 763 glide
calls, owner 1, with zero allowed calls. Controller input reached desired pitch
near -1.48, so it is not absent backward input.

The first `world_match=0` diagnostic dereferenced the saved stack query from the
receive thread after the movement hook had returned. That diagnostic is not
valid evidence of the pointer chain. Test6 copies the context fields while the
hook is active, alongside the expected authenticated world and whether the
existing session would authorize that expected world. This is observation only;
the authorization comparison has not been relaxed.

Test6 server SHA-256:
`8afec5bd3b675a5bfa715a33e2e4149f5d3d0808ebcca640adcdd82a078281df`.
Client remains test5:
`f0f81b886f62386d555c0ec2704acbad5ac1d2205df8d731035673ee50b3cfd3`.
Only the confirmed-empty isolated local test server was restarted. The hosted
servers remain unchanged, and `package_ready` remains false.

## Test6 root cause and test7 repair (2026-10-08, 20:51 local)

Test6 captured two different temporary system contexts while flight was approved.
In both, context+0 was `0x20598e10228`, exactly authenticated world
`0x2059814c010 + 0xcc4218`. `approved_for_expected=1`, but the previous direct
comparison of query+0 to the world rejected every hook call. This establishes a
specific authorization-adapter bug rather than missing controller input.

The pinned native code at 0x5c92af derives the embedded execution-state address
as world+0xcc4218; 0x5c930f repeats this layout. The repaired read chain is
query -> temporary system context -> embedded execution state, then subtract
0xcc4218 to recover the owning world. The recovered world must still exactly
match the authenticated identity's world inside `Sessions::can_fly`. No owner,
SteamID, allowlist, expiry or lifecycle check was removed.

47 identity tests and 116 session tests passed. Added cases cover different
simulation-thread contexts for one world, another world's same owner number,
missing/unreadable pointers and subtraction underflow. Diagnostics now copy the
resolved world during the hook and never dereference a saved stack pointer.

Test7 server SHA-256:
`15d256f2eec3164136a2de4398e6b52487af90cd90f3b6a2964c39e02acbd71f`.
Installed only in the empty isolated local test server after graceful shutdown.
Client remains test5. Gameplay acceptance is still pending; no hosted deployment.

## Test7 live success, reconnect and revocation

At 20:53 local the user confirmed that controller gliding now levels out or
climbs. The server recorded 1,035 glide calls, all 1,035 authorized, with owner 1
and the corrected world match. F6 request-to-approval was 63 ms. Disconnect
cleared native ownership and the client's active effect.

At 20:54:34 a new authenticated connection and owner mapping appeared. The
client received a fresh challenge, remained Off and required F6 to enable flight
at 20:55:29 (47 ms to approval). This provides live reconnect evidence.

After the user confirmed being safely on the ground, the local allowlist was
backed up and temporarily replaced with an empty valid list for six seconds.
The active client logged `off or approval expired` at 20:56:18. The original
configuration was restored in a finally block and its exact hash verified. At
20:56:22 a fresh challenge arrived; flight remained Off. No hosted configuration
was edited. The private backup remains in the local test root.

This verifies one approved controller client, reconnect, and local approval
revocation. It does not establish mixed-client isolation, unapproved-player
movement, death/respawn, Wine runtime compatibility or hosted acceptance.
