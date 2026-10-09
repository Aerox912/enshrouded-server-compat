# Native release review, 9 October 2026

## Current checkpoint

CHECKPOINT-BUILD8 builds the paired protocol-v2 Release sources and passes all
26 registered CTest targets. CLIENT-LIFE4 separately passes 39 life-binding and
104 client state/configuration checks; independent provider review also passed.
The client life reader and its Dead/Spawning enum values are independently
verified against the pinned client executable. Journey's bounded reader and
unknown-level repair, inventory dispatch repair and the native-time policy
repair have passed their focused checks and are included in the full checkpoint.

VERIFY29 independently accepted the matching selected-mod data: baseline mod
files preserved, intended AutoLoot bone repair present, Workshop 20x retained,
and exactly three Gem Forge files added per profile. Client and server runs
apply six additional patches and pass all six offline forge proofs. The manifest
is E:\Scratch\enshrouded-forge-composition-FORGEDATA5-20261009T122756Z\evidence\selected-mod-composition-manifest.json,
SHA-256 c4fab671aacb4c3f0ee3931aecfc5823eedeb384b0eaa02eea4ac2b9f44a7b14.

No checkpoint installation or gameplay acceptance is claimed. G flight, gear
upgrades, accepted drag/drop, persistent Flight HUD, remaining cost hooks and
native time control still need completion and live evidence. Manager 074f858
passed both cloud builds in run 37924463567 with publication skipped, but still
contains RC5 inputs without the forge module. Package readiness remains false.
The sections below preserve prior review and candidate evidence; this current
checkpoint supersedes their pending-source-review status where stated above.

The requested GPT-6 Pro review ran through ChatGPT Surface Control in the
Enshrouded project. Its initial recommendation was HOLD. The reviewed package
was Creative fd926de with server source 6f789d3. The package remains unready.

## Repairs after review

- Failed authentication for an absent peer no longer interprets handle zero as
  removal of every player. Bulk reset has a separate explicit operation.
- Shared identities carry a lifecycle serial issued by Sessions. Removing an
  owner and immediately observing the same native handles issues a new serial.
  Creative's queued requests, frames and actors compare the complete identity.
- Steam callbacks have synchronized state independent of the deletable mod
  object. Shutdown drains active invocations before clearing callback state.
  The small registered callback allocation is deliberately retained for the
  pinned DLL lifetime so a delayed dispatch cannot access freed storage.
- Successful client hook installation is reused across mod reloads. Creative
  menu hook installation is also idempotent for the same game image.
- Controller output packet numbers now describe the filtered gamepad state,
  including synthetic release when the menu opens while input remains held.

The complete native build passed on Windows. All ten registered CTest targets
passed, including concurrency, same-handle lifecycle and two-peer revocation
regressions. The legacy menu harness also passed all twelve fixture checks.
This is source and harness evidence; loader reload and held-input gameplay on
the exact new package still require acceptance.

Before these repairs, the GitHub native test artifact from run 37853907787
passed 585 checks in Wine 10.0. That includes the legacy menu fixture and a
mapped hook test against the supported server executable. It is not an actual
Wine game-server multiplayer session, and does not validate later binaries.

## Remaining release evidence

1. Verify the native transaction ABI/completion semantics and observe partial-fit
   rollback, customized gear, and save/reconnect persistence.
2. Exercise actual death/respawn and queued grants on rapid same-owner reuse.
   The serial fixes reset invalidation, but does not prove native alive/dead
   observation or simulation-thread ordering at commit.
3. Exercise approved and unapproved clients together, including revocation.
4. Run the exact package in the intended Wine server runtime through loading,
   authenticated transport, hooks, gameplay, shutdown and restart.
5. Verify matching client delivery, fresh empty-server evidence, full rollback
   backups, installed hashes and healthy startup before each hosted deployment.

No new native package has been deployed to Enshrouded, IKEA or Soulrend. Their
existing Workshop 20x baseline remains in place. Public manager publication is
also held. G free flight and the other unimplemented Creative features have
not been promoted to completed functionality.

## rc.2 gameplay and rc.3 follow-up

GitHub run 37857424773 built Creative 9eeb243 with server source c12d723.
The downloaded archive SHA-256 is
45effc1f64c869c8d9e16aeb9b4b71794bb43659e8b92ad533db9f31c19ef3fd.
All 602 offline checks in that artifact passed in Wine 10. The matching local
Windows test server reached Host Online. The user confirmed a lower weapon
level and changed quality both matched the received item, and that the item
persisted after leaving and rejoining. The adapter recorded a native item
commit at 01:11:49 local time, with disconnect and reconnect observed afterward.
This is one approved-player result, not mixed-client or inventory rollback proof.

Follow-up review found request sequence reuse when a pinned module reconstructed
its Client object. Creative 773f500 gives request IDs module lifetime and adds
eight reconstruction/deduplication checks. Server 98b5334 gives each callback
registration a distinct retained gate, permanently disabling the retired gate.
All ten native CTest targets passed after these changes. GitHub rc.3 run
37858564345 succeeded, then was superseded by the respawn work below. No rc.3
gameplay acceptance is claimed.

The third GPT-6 Pro review accepted the described sequence and registration
repairs without another concrete finding. It withdrew two earlier menu findings
because they depended on an uncommitted proposal that was removed before rc.2.
It explicitly kept overall release on HOLD pending runtime evidence. The review
did not execute the binaries or certify the complete rc.3 diff.

The user confirmed that an additional weapon was not added with a full backpack;
the native rejection log corroborated this rc.2 result. Partial-fit rollback,
mixed approved and denied clients, and actual Wine multiplayer remain open.

## Intentional respawn and bone-pickup candidate

The user changed the respawn requirement: remember an enabled F6 preference
within the same authenticated connection. Actual flight must suspend while the
actor is dead, spawning or unavailable, and resume only after fresh approval and
acknowledgement. Disconnect, revocation and four seconds without trusted replies
clear intent. Old Creative item requests are discarded across actor lifecycles.
The user's rc.2 observation of accidental persistence does not accept this new
implementation.

Creative ae1f5f87ac1a639a1f2a489b376289e337d9f9e9 pins server source
f68faf0a9f108317cf9b9c8b694b1a89601e509b and patch source
7dc8953df88e44fa6fe12a41b247e0475dad64b1 for rc.4. An independent Release
build passed all ten CTest targets, including 192 flight-session, 51 identity
and 480 Creative checks. The final log SHA-256 is
e249dba0341a145503714df119dda897c31ab99cacd061211080ab764d9f9883.
This is local build evidence. The subsequent exact-head GitHub push run
37864540683 succeeded. The downloaded rc.4 archive SHA-256 is
30f3082dc987f400cfbde2e4151e39416e44e8f62612b64f80929504b2aa12f3;
all ten manifest members, source pins and supported-game hashes match. All 13
downloaded test executables passed in the network-disabled Wine container,
totaling 889 checks. Required external fixtures were verified separately. The
menu test requires Xvfb; an initial invocation without it failed, and the
prescribed xvfb-run invocation passed. The game server executable was mapped
without executing gameplay. These results do not establish multiplayer behavior.

Focused GPT-6 Pro reviews identified and then closed four source findings:
rate-limited flight commands losing intent, stale UI actions crossing approval,
actor publications crossing identity/order boundaries, and owner-snapshot expiry
bypassing failed-read invalidation. Independent verification also found and
verified the repair of transient unknown actor evidence incorrectly becoming an
access denial. The reviews cover the supplied focused code and integration
context, not a blanket certification of every changed file or runtime behavior.

The Auto Loot repair preserves the two verified skeleton bone pickup templates
alongside the existing critter exclusions. Twenty-three patch tests and paired
10,745-template probes passed; both full staged rebuilds completed. Probe equality
covers recursively serialized Lua-visible data, not binary resource equality.
The current selected-mod regeneration subsequently completed with 4,222 client
patches and 4,216 server patches. Both final probes cover 10,745 templates.
The client's 13 selected mod directories and server's seven were preserved;
only the Auto Loot Lua changed within each mod tree. Evidence is in
E:\Build\enshrouded-native-rc4-moddata-20261009\evidence\paired-current-selected-results.json,
SHA-256 371ef869da6e0cd6fdc440a7c95b15d6a47dd23deccd380ca25d0affe8e86b4a.
Other Hollow corpse containers are a separate path; live pickup and multiplayer
acceptance remain required.

The intended next test is a separate constrained Wine server with the matching
combined Creative client. The RC5 standalone Flight client is built from
server-compat's flight_client.cpp and uses SteamTransport server approval; it
must not be confused with the older client-local Shroudtopia implementation.
Combined Creative already supplies F6, so these client options remain mutually
exclusive. Public publication and all three hosted deployments remain held.

Delivery inspection found a material dependency mismatch. The manager still
consumes patches v1.0.6 (aabe8e8), whose Auto Loot output is the older critter-only
3bc8782 hash. Server f68faf0 packages patches v1.0.3 (16d16df), whose Auto Loot
input is the original a117a4e hash. The native metadata pin to 7dc8953 does not
repair those delivery paths. A shared immutable candidate patch archive and
frozen PatchTool, corrected dependency locks, and a successor native build are
required before release. No completed 4.4.7 manager artifact is claimed.

The separate Wine rc.4 candidate reaches Host_Online and Run with the native
dbghelp.dll loaded, but Windows A2S to 127.0.0.1:27637 fails. Guest-loopback
Podman publications alone are insufficient join evidence; an in-container A2S
probe also timed out. Existing Windows and Wine servers were left unchanged.

The scheduled deployment check was deleted at the user's request because the
active thread goal replaces it. Empty-server deployment conditions still apply.

## Corrected inputs and RC5 verification

The exact 7dc8953 patch archive and frozen PatchTool are published under the
separate prerelease tag patch-candidate-20261009-7dc8953. API asset digests match
5d56dd220681d4bb4ffb3d29ca91214f152cd932f8e92a10612daef57ba73389 and
137396b71678a455c9b733ef38e01c77cc1c5c22e6b8504de5d05a27fcd7598a,
respectively. Stable patch latest remains v1.0.6. Server 9315f7a pins those
inputs; workflow 37904871130 passed and an independent download check confirmed
the expected tool and provenance in both normal and Cheeze profile packages.
That check does not establish combined Creative runtime behavior.

Creative ddc8700 packages rc.5 against server 9315f7a. Push run 37905445031 passed.
The downloaded candidate ZIP SHA-256 is
5dd291ed15c4b435b517d44110270746b93d177b0a3e74795342da4c42fdb7d4.
All ten manifest members and exact source/dependency pins match; private defaults
remain empty. All five DLLs and thirteen test executables differ from rc.4, so
the thirteen rc.5 executables were rerun under offline Wine. All 889 checks
passed. Log SHA-256:
23d34cb6e1a379c057f3106be5f35c5b80bc8785a860e0157ba8f7b74022c6ef.
No rc.5 installation or live acceptance is claimed.

The corrected patch catalog exposed a manager preparation ordering issue: 87
original imports were requested, while the pinned originals contain 84 and the
remaining three Workshop files already come from the gameplay overlay. The
repair applies that verified overlay before original preparation. Independent
source review and eight focused manifest tests pass, with public promotion
still rejecting packageReady=false. Fresh full Admin/Regular assembly and
resulting Workshop, vmkeys, import and edition checks subsequently passed in
VERIFY-7. Both editions preserve 84 unique original inputs across 85 import
rows; the extra Rested/Hoarder variant shares its original source. The first
full builds predate the corrected Flight descriptions and must be rebuilt.
The private gameplay input prerelease gameplay-native-candidate-20261009-rc5
contains the verified 436,844-byte archive with SHA-256
a7e8293c4afe722281e31e30da6492b07916e5f21e8f265c48c8e31f361a065f.
It is a private build input, not a published manager release.

Journey verification independently passed 11 CTest targets and 66 focused
reader/session checks, plus exact alias regeneration and static layout checks.
Unsupported Extern query categories remain unavailable; a resource export is
determining their relevance to the catalogue. Initial forge proofs resolve all
six stock templates on client and server and record unchanged regional caps,
essence IDs, actions and cost modifiers. Independent review found missing
original probe inputs, an absent runtime Lua schema guard and an unchecked
client preview-resolution flag. Repairs and a new replay are required. The
existing signatures cover selected fields, not full nested template equality.
Neither result is live acceptance.

The full Creative parity target remains part of completion. The candidate's
unsupported-feature list and NIC-496 backlog did not establish user approval to
omit true G flight, Journey, upgrades, drag/delete, free actions, regional forges
or time controls. Those native packages are being developed separately from the
candidate under test. Final integrated review and gameplay acceptance remain
required before manager publication and hosted deployment.

## Subsequent verification delta

VERIFY-7B accepted rebuilt Admin/Regular manager payloads, corrected companion
descriptions, all pinned files and edition exclusions. It found a latent
Flight-only lock with no nativeGameplay object could pass the publication gate.
REL-9 repairs that case through shared build/release validation; VERIFY-7C
independently accepted both validation paths. The current packageReady=false lock was
blocked throughout. The final notice-only binaries have source-equivalent UI
coverage (608 Admin / 605 Regular checks), not a same-binary test rerun.

Protocol v2 and separate hour/rate commands passed a paired Release build and
11 CTest targets. Native callbacks and authoritative UI state are still being
integrated. VERIFY-10 exposed scalar KnowledgeQueryData values exported as
arrays, invalidating the earlier query histogram. Native consumer tracing
supports RecipeInfo+0x50; repair the export shape before concluding reader
coverage. VERIFY-11 independently replayed paired forge proofs and confirmed
the six corrected Workshop/Production rows. A small optional catalogue-verifier
parser defect remains in repair; live forge behavior is unverified.

LOOT-7 reproduces Wine's failure on Steam's exact adapter-query arguments,
including the 15,000-byte-buffer alternative. The fresh Windows RC5 server
reached Steam InGame and answered loopback A2S, then was stopped for further
socket-role and graceful-stop verification. These runtime observations are
not multiplayer or gameplay acceptance. Public release and hosted state remain
unchanged.

Manager commit 92b63991dda788fc19e004cce4270ebecbaac9a1 is pushed. GitHub run
37917954833 succeeded for Admin and Regular; the publication job was skipped.
Downloaded ZIP hashes are 83ca525546dea4182b59713a0432cf53c5f3a53fe9d0306b77c4231d487f6dd7
(Admin) and dec9a3dc7b2f23e520954dc3ed06c38fc27cedb04e4d8a2e34d1c6604afa2a59
(Regular). REL-10 verified sidecars, source/lock pins and readiness=false;
VERIFY-13 independently accepted all 113 Admin and 109 Regular embedded files,
edition exclusions, exact hashes and the rejecting publication gate. It did not
run the downloaded installers. These cloud artifacts
supersede local package hashes for cloud-delivery claims, not for gameplay.

The user subsequently approved wiring and enabling the crafting hooks in the
isolated local server. The paired integration builds and passes 13 CTest
targets. VERIFY-14 accepted the isolated server crafting adapter and 55 portable
checks; VERIFY-16C accepted the isolated client preview adapter, 19 policy checks
and pinned static targets. Neither independently verifies integrated leases or
live crafting. Those checks remain assigned before the gameplay test.

VERIFY-15 reproduced corrected paired Journey scalar exports and all expected
recipe coverage. It also found malformed condition dictionaries accepted as
complete, a stale code-hash manifest and a compound-query closure gap. Repair
and independent recheck remain necessary. Current native runtime categories
include NPC, altar and player-level conditions; the older coverage conclusion
does not apply. The optional forge catalogue parser repair passed VERIFY-11B.

The low-RAM blocker is resolved. At the user's request, CLOSE-OLD-SERVERS-1
backed up and stopped both old local test servers, recovering free memory from
3.1 to 10.6 GB. Final on-disk snapshots are verified; targeted SIGTERM was
required, so graceful application shutdown is not confirmed. Hosted servers
and the shared VM are unchanged. Fresh headroom must still be checked before
the next test. LOOT-9 confirms that the archived Wine 11 baseline connected to
Steam and answered in-container A2S. That stopped baseline is not a current
user join endpoint or hosted Wine acceptance.

VERIFY-16I accepted the integrated server lease checks. The client flag
restoration repair passed independent review, but the fresh local actor/life
gate remains under integration. No live crafting claim follows from these
tests. JOURNEY-4's bounded page reader and strict export repair pass focused
checks and await independent review. VERIFY-18 accepted optional shared forge
packaging and rollback-plan validators; manager 074f858 and server e0337f3
contain those helpers. Final combined artifacts, gameplay acceptance and the
integrated GPT-6 Pro review remain required before publication or deployment.

The later RC6 GitHub candidate passed exact-head build and archive inspection,
including shared Gem Forge files and empty reusable allowlists. VERIFY50 also
accepted local RC6 manager previews, their locked inputs and edition selection;
public manager 4.4.6 and the three hosted baselines remain unchanged.

Current feature work is designated rc.7 with Creative protocol 3 for an explicit
terminal uncertain-commit result. VERIFY46B accepted that result contract and
same-sequence deduplication. It does not establish live gear mutation. VERIFY51
accepted the server time integration and independently passed 16 selected tests.
No live callback-order sample exists, and native time writes remain gated.

VERIFY49 found a blocking G-flight installer recovery defect: an injected partial
instruction write followed by rollback failure could resume threads with mixed
bytes. G-INSTALL12 is repairing ownership and recovery before independent recheck.
Production Windows 26300 support remains unavailable.

The user requested a fullscreen native-style catalogue, centered gear modal and
server-confirmed Creative/Journey selector, including controller and 3:2 support.
That UI is being prepared in an isolated protocol-2 pair with accepted bounded
approval/life diagnostics. The installed checkpoint previously failed to obtain
Creative approval; its cause remains unknown. No new repair DLL is installed.
The complete paired artifact, gameplay matrix and final GPT-6 Pro review remain
required before any public release or hosted deployment.
