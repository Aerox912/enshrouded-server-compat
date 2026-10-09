# Current handoff

Deployment: enshrouded-respawn-loot-20261009. User selected Heavy. Main owns architecture, scope, integration, Git delivery, acceptance and the three state documents. Workers own bounded implementation, verification, runtime operations and external documentation.

## Goal and boundaries

Complete all requested native Creative features, repair Always Flying and corpse loot, publish matching Admin/Regular managers, and update all three hosted servers when empty. The active goal replaces the deleted automation. Full scope is recorded in Creative research/native-parity-acceptance.md; do not substitute a smaller candidate.

F6 remembers enabled intent only within the authenticated connection. Execution suspends while dead/spawning/unapproved, then resumes after fresh authorization. Disconnect/revoke clears intent. G is separate and resets on life/actor/session/revocation. Private approved IDs belong only in server configuration.

Public manager remains 4.4.6. package_ready=false. Enshrouded1650d1ae, IKEAc70c80c3 and Soulrendea6b2409 have not received the candidate. Preserve worlds, all current mods, Workshop20x, loaders and Wine overrides. Poll occupancy only after readiness; recheck immediately before each graceful stop.

## Current code and build

- Creative feature worktree: E:\Worktrees\enshrouded-creative-parity-20261009, branch feature/creative-parity-20261009, base ae1f5f8 plus coordinated uncommitted work.
- Server feature worktree: E:\Worktrees\enshrouded-server-parity-20261009, same branch, fast-forwarded base b96aa3f plus coordinated uncommitted work. Build the pair together.
- Server canonical release/native-gameplay-20261009 is pushed at b96aa3f. e0337f3 contains shared forge manifest/rollback helpers. Creative canonical release/native-review-20261009 is pushed at ddc8700e (RC5). Its packaging/version/pin delta over ae1f5f8 still needs reconciliation with feature packaging before the next candidate.
- CHECKPOINT-BUILD8 built Release successfully; all26 registered CTests passed. Manifest: E:\Build\enshrouded-checkpoint-cmake7-20261009\paired-local-release-manifest.txt. Creative server DLL SHA5a1aacd9dccee4a65ce2798dedc19b301abb751f0396b17baf2dca2b425c6079; client1fbd1d5b3a0970c0ec01ece43b4ad6abc5fc099ba409d56eb8d9e5725261f890; loader07ea4fc0252ba9d8931905bf79ed3c57b3a5927057ff6c526b51fa29f5273c10. This is an immutable local test checkpoint, not a complete release.
- CLIENT-LIFE4 is frozen: full EXE hash before reader, request-local life/epoch tickets, fresh actor checks, delayed cost-hook installation, Journey/G/cost retirement, retained F6 intent.39life/104state checks passed with normal fixture write access. VERIFY30 independently accepted integration and persistence checks. Connection hints are polled every8ms; effect checks use fresh native life plus lock-free lease publication. No new synchronous host hook is required solely to eliminate that bounded hint delay.
- VERIFY26/28 accepted the bounded client-life reader and direct pinned-client enum metadata. CLIENT-ENUM8 persisted the descriptor/table assertions; all eight verifier regressions pass.
- Journey J5 reader and unknown-level repair independently pass100checks plus6reprocases; analyzer9tests. Expected1,936recipes covered among1,954rows. J5 manifest E:\Build\enshrouded-journey-export-20261009\JOURNEY5-UNKNOWN\evidence-manifest.json SHA56bef487d3db6c23d9db768a0f3c9018d64793c4748339049398cb849e6b35b4.
- INVENTORY5 fixed dispatch recursion and restored normal grants;15independent regressions pass. Physical slot refs use owned entity/local index, not unproven hotbar ordinals. INVENTORY-METADATA6 corrected the native factory gate name without behavior change.
- TIME7 fixed the real identity/world status mismatch and strengthened callback writer preflight/result handling. Full26-test checkpoint now passes, superseding the prior14/15 result.

## Active packages

- creative_protocol_v2 owns core/protocol/models/native_runtime/shared CMake. Client/provider ownership is creative_flight_native. Do not overlap their files.
- respawn_implementation is the sole Senior, implementing default-off G server movement runtime. Helpers passed independent movement/ABI checks; actual G gameplay remains unverified.
- inventory_native_operations owns GEAR-PAYLOAD7 new-create perk/Level mutation. Attribute indices must resolve from the current registry. Parent chooses explicit base-level UI semantics with finallevel<=50; strongest Take1 reaches the maximum legal finallevel. Original algorithm increments level once per unlock, also maxlevel when equal. Static immediate-caller provenance remains incomplete; gameplay must verify the result.
- native_cost_effects owns COST9 hook-lifetime repair after failed MH_RemoveHook cleanup findings. Server crafting hooks are already user-approved and in the test checkpoint. Building and consumable runtime modules are not active. Fired registration remains unproven; no capability claim.
- world_time_native owns TIME8 integration inside native_time, default-off until callback ordering observations. Resolve world each callback, never retain the clock pointer; separate private permission.
- native_menu_parity completed MENU9 default-off20second HUD observer,97capture/97ABI checks; VERIFY31 accepted both suites and the partial/full unwind probe. It does not identify Rested semantically or add a Flight row yet.
- native_drag_runtime owns DRAG-OBSERVE6 new default-off stock drop observer. No grants/deletes or acceptance mutation. Original Drop+4 is not a proven slot ordinal.
- cloud_candidate owns CRAFT-ENDPOINT6 staging from the frozen checkpoint and matching data. No install/launch until independent client/data acceptance. No hosted changes.

## Matching data and endpoint

FORGE-DATA5 and VERIFY29 accepted the complete selected-mod stage:
E:\Scratch\enshrouded-forge-composition-FORGEDATA5-20261009T122756Z.
Manifest evidence\selected-mod-composition-manifest.json SHA c4fab671aacb4c3f0ee3931aecfc5823eedeb384b0eaa02eea4ac2b9f44a7b14.
Client108→111/server70→73files: only3GemForge module files added each. Baseline mod bytes preserved; compared with installed source the intended AutoLoot bone patch is the only baseline difference. Workshop20x retained. Pinned EMM0.1.2 produced4,228/4,222patches, six above each baseline. All6regional forge proofs pass offline. Resource proof is not live placement/production/pickup/save evidence.

Pinned client EXE SHAaf2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781; server001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637.
AutoLoot Lua e171920edc367d5fa999740e7128f9461723aa8c8c2f1c83e97fb9730232b303 preserves weakling/hound bones. Other Hollow corpse containers remain separate live checks.

Prepared endpoint E:\Scratch\enshrouded-native-win-protocol2-20261009, loopback query28737, fresh private password/allowlist, empty world. Matching32data files and native checkpoint are being staged. Before launch require>=7GBfreeRAM, fresh hashes and process/listener checks. No firewall/port-forward changes or unrelated process closure.

The installed client remains RC2. Fresh complete rollback and game-closed check precede installation. Combined Creative contains F6; do not install standalone Flight alongside it. Previous active.json was Creativefalse/Flighttrue despite DLL state; recheck drift and change only those two activation flags if still applicable. Preserve all other settings.

CLOSE-OLD-SERVERS-1 stopped exact old RC4 and Flight Wine containers. Pre/post snapshots verified; SIGINT failed, SIGTERM exit143 without OOM/SIGKILL succeeded. Three savefiles changed before final snapshot; do not claim graceful application shutdown. Stop report E:\Build\enshrouded-native-cloud-rc5-20261009\windows-endpoint\close-old-servers-20261009T1115Z\stop-report.json SHA41f1fd9e324097e41f0d8b9de8427f93c3fae54170850cf6f0e672449586ffb9. FreeRAM3107→10597MB then; freshcheckneeded. VM/hosted/unrelatedapps untouched.

Wine11 fixes the exact Steam adapter request failing underWine10. Archived disposable baseline reached SteamInGame and answered in-containerA2S; it is not a current user endpoint or full hosted gameplay proof.

## Delivery and external records

Manager release/native-gameplay-4.4.7 pushed074f8582d3fb3cbb3510266f9a8ba1b149902d26. CI37924463567 passed both editions, publishskipped. Payload still RC5/noGemForge. Earlier92b6399/run37917954833 artifacts independently extracted:113Admin/109Regularfiles exact,84unique originalinputs,Workshop20x/editionexclusions preserved. Artifacts E:\Build\enshrouded-manager-cloud-92b6399-20261009. Downloaded installers not independently executed.

Shared forge packaging validators independently pass, including both real Prepare-Server script fixtures using a stub PatchTool. This does not prove actual archive/EMM/runtime delivery. Final source pins and new artifact/lock remain pending.

RC5 immutable archive E:\Build\enshrouded-native-cloud-rc5-20261009\native-gameplay-candidate\native-gameplay-candidate.zip SHA5dd291ed15c4b435b517d44110270746b93d177b0a3e74795342da4c42fdb7d4; all13offlineWineexecutables889checks. Pins server9315f7a,patches7dc8953,Shroudtopiaee66d296. Not protocol2 and not installed.
Private inputs https://github.com/Aerox912/enshrouded-client-installer/releases/tag/gameplay-native-candidate-20261009-rc5 SHAa7e8293c4afe722281e31e30da6492b07916e5f21e8f265c48c8e31f361a065f.
Patch prerelease patch-candidate-20261009-7dc8953 ZIP5d56dd220681d4bb4ffb3d29ca91214f152cd932f8e92a10612daef57ba73389; stablepatch1.0.6 unchanged.

DOC8 refreshed/readback Notion hub3f36f2829cab81a08305da19ac154e89, inventory3f36f2829cab811eb2b5c0482824a5f4, serveroverview3f36f2829cab81adb4abc0555b7b6ce9, LinearNIC493/496InProgress,NIC494/495held and sourcecompositiondocument746e6139767c.21inventory/21composition/3hostedrows preserved. New checkpoint/client/data proof postdates that update.

Focused prior GPT-6 Pro source findings are resolved; final full-feature review is not done. User approved Enshrouded ChatGPT project. Main exclusively owns visible review, verify GPT-6 and Pro before submission. Existing review thread https://chatgpt.com/g/g-p-6ac81a29422881919957c85c01cc79ed-enshrouded/c/6ac81ca6-2224-83ed-870a-54b54b710060. Draft private Creative PR https://github.com/Aerox912/enshrouded-creative-mode/pull/1.

## Next acceptance

Provider and data reviews are accepted. Install the frozen matching private test bundle with rollback, launch the fresh endpoint, and ask for concise actual crafting/Journey/F6/corpse/forge checks. Continue new G/gear/drag/HUD/cost/time work separately; do not replace tested DLLs silently. Complete full parity, real multiplayer/revocation/partial-fit/persistence, exact packaged-client/server proof, final GPT-6 Pro review, public builds and three empty-server deployments. Closure and Archivist token report remain outstanding.

