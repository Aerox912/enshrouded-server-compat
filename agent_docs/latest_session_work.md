# Current handoff

Deployment: enshrouded-respawn-loot-20261009. User selected Heavy. Main owns architecture, scope, integration acceptance, Git delivery and canonical state. Workers own bounded implementation, verification, runtime operations and external documentation. Completed task names below describe evidence, not current ownership.

## Goal and constraints

Complete full native Creative parity, repair Always Flying and corpse loot, publish matching Admin/Regular managers, and deploy to all three hosted servers when empty. Full acceptance scope: Creative `research/native-parity-acceptance.md`. The active goal replaced the deleted automation; do not recreate it or shrink the feature scope.

Public managers remain 4.4.6; package_ready=false. Hosted Enshrouded (1650d1ae), IKEA (c70c80c3), Soulrend (ea6b2409) are unchanged. Preserve worlds, current mods, Workshop 20x, loaders, Wine overrides and settings. Do not poll occupancy before readiness. Then require fresh authenticated occupancy, an immediate pre-stop recheck, complete verified rollback backups, exact installed hashes and fresh healthy startup/authorization evidence.

Private approved IDs belong only in server configuration. No IDs/passwords in binaries, defaults, docs or output. F6 retains intent only within one authenticated connection, suspends effects during dead/spawn/unapproved state and resumes after fresh authorization; disconnect/revoke clears it. G is separate and resets on life/actor/session/revoke. Landing ends the G flight run but may leave it armed.

## Repositories and immutable checkpoints

- Creative feature: E:\Worktrees\enshrouded-creative-parity-20261009, feature/creative-parity-20261009, pushed HEAD 85fdf3f790469268c6afee7ad95727139b5c2447 plus coordinated dirty/untracked work.
- Server feature: E:\Worktrees\enshrouded-server-parity-20261009, same branch, pushed HEAD 5cbe7fd327744ad5ac481a5de6c549432bae2378 plus coordinated changes. Preserve them; build the pair together.
- Creative canonical: E:\Repos\enshrouded-creative-mode, release/native-review-20261009, ddc8700e, native_runtime.cpp dirty. Server canonical: E:\Repos\enshrouded-server-compat, release/native-gameplay-20261009, docs checkpoint 2234fa9c466c179cf496e8deb93279720c900813 plus newer canonical documentation edits. Preserve tools/__pycache__.
- Manager: E:\Repos\enshrouded-client-installer, release/native-gameplay-4.4.7, pushed 23ed410f95dea332005963715f309f77690c35ca. Untracked MOD-INVENTORY.md, server-originals.lock.json and caches were not committed.
- Unpublished successor rc.7 uses Creative protocol 3 and Flight protocol 1. Immutable RC6 and isolated UI repair retain Creative protocol 2. Do not port the feature core/protocol into the isolated pair.

Pinned EXEs: client af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781; server 001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637.

RC6 GitHub run 37942933979 passed 26 native and three controls tests. E:\Build\enshrouded-native-cloud-rc6-20261009 contains outer candidate SHA7428695e19bf29110defb3df109ab85e84b0a085a10e5cc57b4235beacb34aba, tests SHAefcd10fb0dcc1f6fb8fae67b6e8eb2656bc06bc2b3f3d459e51f3e2c76a4df09, inner ZIP SHA9a5abdecc51f3a1ac5b1b5342a70af39d10be78eeed546470c5949f9ddc836f0. VERIFY43 accepted 16 payload members, 29 test executables, shared forge files, empty allowlists and external EML contract. These artifacts are not the unfinished rc.7 tree.

## Current ownership

- fullscreen_menu: MENU-REFERENCE19, native menu/layout/controller files in both Creative UI trees. Repair actual category ensure-visible calculation and retain mode pending during ordinary in-flight freshness; add the corroborated pinned native frame call.
- approval_repair: GEAR-CLIENT18 in feature native_client.cpp, session.cpp, capacity-cache helper and focused tests. Owns reply.op correlation and bounded authenticated query cache. Preserve APPROVAL18 diagnostics, observer configuration and F6 life handling.
- g_installer: sole Senior, G-INTEGRATE13-PREP. Owns installer exact-26300 support and persistent installer-thread recovery helpers. No runtime/client/menu/shared-CMake ownership yet.
- live_approval: LOCAL-REPAIR20-PREP, fresh host check and exact two-DLL rollback baseline only. No obsolete build installation or server start.
- native_visual_research / native_style_assets: bounded blur/category native ABI research. gear_snapshot_audit: next scheduler-consumer edge for native inventory writer exclusion. Read-only production scopes.
- progress_records_doc11: DOC12 external Notion/Linear updates and readback. Main retains canonical files/Git. Testers are reassigned only after the relevant source freeze.

## Fullscreen/Journey local repair

Isolated branches fix/creative-fullscreen-approval-20261009:
E:\Worktrees\enshrouded-creative-fullscreen-20261009 and E:\Worktrees\enshrouded-server-fullscreen-20261009, based on RC6 pair above.

User references in C:\Users\herks\AppData\Local\Temp:
- codex-clipboard-cd651adc-b9fd-4ef8-a781-b5919ee313f0.png: fullscreen catalogue.
- codex-clipboard-429086c1-9f0e-49ef-8a6e-ee4fbbe766e0.png: centered gear modal.
- codex-clipboard-afb3e435-84b2-4e20-ac94-071d5453f0bd.png: Creative/Journey selector.
Ignore marketing captions. Use text tabs, grouped item grid, compact categories, native details, controller controls and confirmed server mode. Final gear level = base + upgrades, bounded by 50/item/player constraints and actual capacity. Isolated protocol2 cannot prove positive-upgrade mutation.

MENU-REFERENCE17/18 transferred five files identically. LOCAL-REPAIR19 compiled both DLLs and passed 27 CTests. VERIFY48B rejected that freeze: category scroll omitted row height and could leave selected row fully clipped; mode pending cleared on normal !fresh snapshots. Outputs in E:\Build\enshrouded-local-repair19-20261009 are explicitly obsolete and were never staged/installed. MENU19 must test actual consumer scroll at boundaries and pending/confirmed/rejected/retired mode transitions. Routine pending freshness is not an authorization rejection.

NATIVE-STYLE24 and independent NATIVE-FRAME25 corroborate fancyBox BA92A0 on the pinned client. Existing Box scope uses begin9437E0/end946CE0. Argument is zeroed 0x58 bytes, int32 style2 at0, glass1 at4, inactive flash+18 and zero optional-count+50. Native style2 caller A4CB6B and compatible Box caller9E66DA prove argument/lifetime path. Full 20-byte prologue: 48 8B C4 55 41 56 48 8D A8 18 FD FF FF 48 81 EC D8 03 00 00. Guard full image/signature and retain fallback. This proves call compatibility, not exact visuals/blur. menuTabList BCD550 is a preset builder, not a custom eight-tab API.

APPROVAL16/17 diagnostics passed VERIFY47B/C; 125 life-reader checks pass. Isolated client hash2326040AA32ADC68F65EA82BDE24E040711BE7EBC0A19B14E852740D4220701F, runtime9BC23420A77A8FD4A961F30541D18713FF131EEDD19C6DC15073C582B64E97E8, life reader17565D2431088C29EC0426E95354E2DF8534ADF1FC5BA71B05620654CA31408C, fixtureE6552B69CE3CA43DDE68CC56ADAA73BDAF0493AED0DD35FA8A27056C1713974B. Bounded HELLO logs and stack-local pointer-free stage/read counters preserve fail-closed behavior. No live approval root cause is established. APPROVAL18 ported them into the feature tree; ongoing gear work supersedes its old client/runtime hashes.

## Local endpoint and rollback boundary

Endpoint E:\Scratch\enshrouded-native-win-protocol2-20261009, name Native verification (protocol2), query127.0.0.1:28737. Password is private in its config; do not print it. The earlier 14:47 join authenticated and produced owner mapping but no Creative approval. Logs ended near15:03 without definitive exit evidence. Elevated16:03 snapshot found no game/server, noUDP28737 and20,769MiB free. Refresh before changing runtime state.

Installed Build8 client remains 1FBD1D5B3A0970C0EC01ECE43B4AD6ABC5FC099BA409D56EB8D9E5725261F890 at D:\Games\SteamLibrary\steamapps\common\Enshrouded\mods\creative_mode\creative_mode.dll. Local server dbghelp.dll remains 5A1AACD9DCCEE4A65CE2798DEDC19B301ABB751F0396B17BAF2DCA2B425C6079. Preserve unrelated client-root EML dbghelp.dll SHA7A1334FADBCF68FA3099D6757AD3E110D7196945CF3A2B102E44F451D0295DCD.

The old client-test-bundle rollback DLL is stale. Next install is only two accepted DLL replacements, after fresh exact backups, stopped-process/port/RAM and guard checks. Do not rerun the ten-file old bundle or replace data/config/worlds. Creative is enabled and standalone Flight disabled; the old standalone DLL has .creative-test-disabled suffix. Preserve all other selection/settings. Launch hidden only after acceptance. XP Share is absent from this focused endpoint; do not claim full hosted composition. Do not ask for crafting until approval works.

## Accepted feature packages and remaining integration

- G-INSTALL12 passed VERIFY49B: 1277 transaction/4169 native checks, 35 server/23 client proofs. Manifest SHAACD706EB22CF125A4689E8CADF675952E4F84CFE757FC69DD1C72389325C1DD7 in server research. Partial-write/failed-rollback now retains threads, identity handle, writer gate and backing until same-installer-thread recover_retained proves full bytes/cache/protection. Production uses aligned16-byte CMPXCHG128 after CPUID/page/whole-neighbor proof; no non-atomic publication fallback. Main authorized exact26300 in private candidate alongside19041..26100, rejecting26299/26301. No game patch yet. Integration must preserve a live recovery thread and avoid loader/heap operations while quiesced.
- G-DISPATCH7/CLIENT8 passed bounded runtime/bridge/ABI review. Native state3 has no mover; approved bridge invokes Dive once. Dispatch sites server187595/client3A60A5 preserve following instructions. Client activation derives from matching authenticated G acknowledgment and current host/life/lease/capability, not fabricated server generation. Full runtime wiring and movement acceptance remain open.
- GEAR-SERIALIZER11 and GEAR-RESULT14 passed VERIFY41/46B. Stack-owned Level/Perk descriptor overlay preserves original payloads/8 arguments. Postcommit customization uncertainty is terminal, warns to check inventory, and cannot auto-retry under a new sequence. Native writer exclusion and factory OOM rollback remain unproven.
- GEAR-OPTIONS16 passed VERIFY54:705 strict core checks and7 package tests. Protocol3 Op11 is canonical item+rarity, authenticated/read-only and bound to life/session/allowlist/Journey/replay/rate. Response echoes op; known zero differs from unavailable,209character envelope. Final base+upgrades is checked before grant/hotbar dispatch. Runtime rechecks fresh native capacity at mutation.
- GEAR-RUNTIME17 passed VERIFY54B and16 strict readiness checks. It reads current native ItemInfo capacity and rechecks frame/owner, with no catalogue fallback. Gear and time capability bits are independent. Serializer failure is logged and gear remains off while ordinary inventory may continue. Native runtime SHA A5B1D26D02AD2C98C5527079F6EB10A7A0926B3996E7DCF00F98446E81C2196A; native_inventory.h4533FD7649FE54E3D019606BA98D2D639AA04E0C495E14B671F28B9AA6AC4BC7; readiness fixture56455AE4C974D895661A37CA9CB9F34C19C57CFA06DBA18D7DAF4C0BF2C58BC5. Registered fixture1/1CTest passed. Shared server CMake released at F435A31050A938ADD2DA271ECA5EA2DF93E03B712E260271E2D1ADCAF77EC5A7.
- GEAR-CLIENT18 must correlate reply.op before consuming pending state. Main also approved retaining last successful capabilities/settings only for correlated canonical nonfatal read-only gear_options rejection, without extending the deadline. Fatal status, expiry and disconnect still invalidate. Cache must avoid requery spam and stale selection/mode/lease results.
- TIME-INTEGRATE12 passed VERIFY51 with strict server DLL and16 selected tests. Private process env XHL_ENABLE_NATIVE_TIME_OBSERVER=1 and private creative-time-allowlist.txt gate observation. First authorized !xhl-time:enable is observe-only20seconds; !xhl-time:status finalizes counts. Do not send another enable/hour/mode during sample. Only later explicit enable after accepted current-scope ordering may enable writes. No live sample exists.
- MENU-LINK15 passed VERIFY52: three observerCPP/twoMASM objects linked with MinHook/bcrypt;9 focused cases. Test macros remain private to test targets. Full client target awaits current UI/client freeze. Private default-off config mods.Creative Mode.private_local_observers; CtrlShiftF10 arms20second pointer-free HUD/drag sample only with foreground/fullimage/usablelife/approval. Revoke/life/session/config off retires it. This is observation, not Rested HUD or drag/delete implementation.
- COST11B/CORE-COST12 passed VERIFY39/42. Atomic status+remainder updates preserve padding; independent craft/build/consume readiness intersects active requests. No crafting/building/consumable gameplay acceptance. ADAPTER-OWNERSHIP2 uses only exact owned hooks, no global disable or foreign adoption.
- INVENTORY-SCHEDULER18 found registered inventory_actions query groups/flags but no proof of cross-writer serialization. Reuse synchronous callback lane, do not invent a generic queue API. Keep inventory_writer_serialization=false. Next research follows scheduler consumers, not repeated metadata enumeration.

## Data and managers

FORGE-DATA5/VERIFY29 stage E:\Scratch\enshrouded-forge-composition-FORGEDATA5-20261009T122756Z; evidence manifest SHA c4fab671aacb4c3f0ee3931aecfc5823eedeb384b0eaa02eea4ac2b9f44a7b14. Client108->111/server70->73 files; only3forge files each. Baseline preserved with intended AutoLoot bone repair and Workshop20x. EMM0.1.2 produced4228/4222 patches, six above each baseline. All6 regional resource proofs pass; placement/production/pickup/save need gameplay. AutoLoot SHAe171920edc367d5fa999740e7128f9461723aa8c8c2f1c83e97fb9730232b303 preserves weakling/hound bones; other Hollow containers need separate checks.

Manager private RC6 input release: https://github.com/Aerox912/enshrouded-client-installer/releases/tag/gameplay-native-candidate-20261009-rc6, asset625455982. Exact563194-byte ZIP SHA36d0ed99408efe7d933903ee41c155d16ebfd8d42d49af1e72f38f08d8e576ad, GitHub digest matches. Repository was verified private; no publicvtag/feed update.

Exact manager commit23ed410 passed run37956399896: stable/admin build and setup succeeded,637Regular/640Admin checks; publish skipped. E:\Build\enshrouded-manager-cloud-23ed410-20261009 contains immutable archives/sidecars/metadata. Regular3376869bytes SHAcf72d8c83e67b42e9a784a584b36fb2e73b1c1d595a30f5f930e92a88c4ef42f; Admin4352574bytes SHAd56a50841114ce73c0adcda13ff437959a0ca1ada6524e1845b4001cb691c87e. VERIFY53 accepted112/112Regular and116/116Admin embedded entries,13/13inputs, exact edition exclusions and3sharedforge files each. No serverDLLs/privateconfiguration/IDs. Scanner numeric matches were PCMhex false positives. EML external loader variant is manifest-only, not a payloadserverDLL. Metadata still packageReady=false. Packaging acceptance does not establish full gameplay or release readiness.

## External records and review

Notion hub https://app.notion.com/p/3f36f2829cab81a08305da19ac154e89; inventory .../3f36f2829cab811eb2b5c0482824a5f4; servers .../3f36f2829cab81adb4abc0555b7b6ce9. Linear composition https://linear.app/aerox912/document/mod-sources-builds-and-server-composition-746e6139767c. Preserve21inventory/21composition/3serverrows. NIC493/496 In Progress;494/495Todo. DOC12 refresh/readback is active, including current cloud acceptance and unfinished runtime/UI state. Existing records are formatting references, not samples of user-authored prose.

Main exclusively owns final visible ChatGPT review, using the authorized Enshrouded project and verified GPT-6 + Pro controls. Prior focused findings are repaired; final full-feature review remains open. Existing review https://chatgpt.com/g/g-p-6ac81a29422881919957c85c01cc79ed-enshrouded/c/6ac81ca6-2224-83ed-870a-54b54b710060. Draft private Creative PR https://github.com/Aerox912/enshrouded-creative-mode/pull/1; no merge.

## Next actions

Accept MENU19 with independent consumer tests, build repaired isolated DLLs, verify fresh backups/guards and install only those two files when the game is stopped. Launch the local endpoint hidden and collect fresh Host Online evidence before asking the user to test F7/Journey/approval. Inspect the bounded diagnostics if approval still fails. Do not claim protocol2 proves final gear/G behavior.

In parallel finish client capacity/op-correlation review and G integration. Complete remaining native HUD/drag/time observations and live feature checks, mixed authorization, partial-fit/persistence and actual Wine multiplayer. Then update exact paired source/artifact pins, manager inputs and cloud packages, perform final GPT-6 Pro review, publish managers and deploy to each freshly empty server with rollback proof. Keep the goal active until all required work is done.
