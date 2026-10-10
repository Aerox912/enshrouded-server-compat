# Current handoff

Deployment: enshrouded-respawn-loot-20261009, Heavy route. Full intake and Workflow reconciliation are complete; do not repeat. Main owns decisions, Git delivery and the three state documents. Archivist closure is pending final distribution/source handoff. Full parity is not complete.

## Authority

Enshrouded and fixed Admin delivery are current priorities. IKEA/Soulrend are deferred. The user explicitly approved shared public Admin updater testing publication after disclosure that all Admin installs receive it. Regular/legacy feeds and Nexus remain unchanged. The old automation was deleted; do not recreate. Full-parity package_ready=false remains.

Deploy empty only: direct Steam occupancy checks are approved, including immediate pre-stop recheck. Failed, ambiguous or nonzero results leave the server running. Preserve worlds, saves, Workshop 20x, other mods/settings, loaders and Wine overrides. Private approved IDs never belong in reusable packages.

## Admin 4.4.8

Application source da9c1b42ab5e68e7fb01432d9e44a8ee8a27b40c, branch release/native-gameplay-4.4.7. Owner 716 Admin checks and independent 11 preview plus 19 recovery checks passed. Exact-head build/smoke:
https://github.com/Aerox912/enshrouded-client-installer/actions/runs/38016267205

Private release: https://github.com/Aerox912/enshrouded-client-installer/releases/tag/private-preview-4.4.8-ui43-da9c1b4
Public prerelease: https://github.com/Aerox912/enshrouded-client-releases/releases/tag/admin-preview-4.4.8-ui43-da9c1b4
Public anonymous ZIP and sidecar downloads match. Prior releases remain intact.

ZIP Enshrouded-Mod-Manager-Admin-4.4.8.zip, 4,417,543 bytes, SHA-256 230c1ef4ccf34fcbde5b3ed98f368fa99e58ca89cdd7a83729dcbbde0695c7df.
Application SHA-256 3ee36d1408fc7b43d52fa9fb5b4c73da839f81f88a4c0e5a339bee36b712823f.
Setup SHA-256 1939995f062866c19e5117d741c587deb28c3ce165afc9390b5711b84b749c1d.
Evidence: E:\Build\enshrouded-manager-ci128-da9c1b4-20261010\artifact-verification.md.

DISTRIBUTE130 completed public signed feed promotion. Publisher b703120ed5f09595595bfdcb4c43c813d6b7f528 was independently accepted by VERIFY133; run https://github.com/Aerox912/enshrouded-client-installer/actions/runs/38018482408 passed. Anonymous feed readback verifies Admin 4.4.8, sequence 1791600709 (previous 1791408105), exact package/executable/game pins and production UpdateProtocol signatures. Feed commit 9511f29d03bb314786c069a067216f4ed7a97a2f changes only bundled-channels/admin.json. ZIP/sidecar anonymously verified; public release has exactly those two assets. Friend update application has not been observed.
Current feeds are bundled-channels/*; Admin is now 4.4.8, Regular remains 4.4.6. Legacy channels/normal.json and channels/cheeze.json intentionally remain 4.2.1 migration bridges. README and all other files preserved. Preview packageReady/gameplayVerified flags stay false.

## Completed local repair

APP127 completed the guarded real public-API repair under the shared operation lock, game-closed checks, receipt/package pins and real Dependencies.Prepare. No manual receipt rewrite or execution-policy weakening. Contract is 134 payload copies plus 39 EML tracking paths, totaling 173, with five additions and two Flight retirements.

Status Installed, Creative enabled, standalone Flight disabled, eight original backup hashes verified, no pending repair/removal journal. Receipt SHA-256 00c1f8e7d3e8d323340ce14f0e4e2c60d083097241ddd07d50813f57882a444d.
Admin 4.4.8 is visibly open from C:\Users\herks\AppData\Local\Programs\Enshrouded Admin Mod Manager\Enshrouded-Mods.exe, matching the accepted executable. Registered slot matches; previous 4.4.7 retained.

Old disabled Flight test DLL and inactive MagicStorageProbe 0.1.5 (five files including TSV observations) are preserved under E:\Build\enshrouded-admin-repair127\preserved-artifacts, outside scan roots. Other chat notified; do not reactivate old probe without revalidation.
Operator E:\Build\enshrouded-admin-repair127\AdminRepairOperator.cs SHA-256 25bc7b1496dcf1d9f7cd84c548bb46a218d1451e1b9c5ca11181c2e551c2310d; executable ddfdb2f1e5f51103a39adfefb83dc5f1e53dac3767c9318f11ee742b1586a2d.

CLIENT123 compatible trio is installed; evidence E:\Build\creative-ui43-shared-host-20261010\candidate-evidence.md.
- Creative f5729c9ce7da04ad015d3ee7a8c8a0c28337eaaeb41679d629b27ce161663493.
- Storage 813d47c14b6180569450012e38e677b99924a9b05d8af6ff5afb71fa60905682.
- Shared host b6d9e4757d4305bd9aba4325ebd31c3c4e2a31ac6931999f327fe3dbd22f6b46.
- Backup D:\Games\SteamLibrary\steamapps\common\Enshrouded\.magic-storage-ui-backups\d5c8f6e642f04807ba850a4fa7581266.
All installed pins, six receipt-map files and three preserved originals verified. VERIFY126 accepted five changed Creative files among 121 tracked files; protocol/auth/session unchanged. Config 115/115 passed normal Windows, resolving sandbox failures. Focused lifecycle/menu/input/host/Storage checks passed. Current live F7/F6 acceptance remains open; user asked to test.

Creative includes F6 Always Flying and F7 Creative. Standalone Flight cannot load alongside it. UI131 found malformed restored UI can display both checkboxes, but normal clicks and installer validation enforce exclusivity; clearer restoration UX remains follow-up. G Minecraft-style flight is unavailable. The startup access-denied modal was not reproduced in normal user context; no ACL defect or exact denied path established.

## Hosted Enshrouded

DEPLOY113 A2S 0/16 at 2026-10-10T00:58:45Z, graceful Stop 00:58:49Z. Reused complete locked DEPLOY103 6.26 GiB backup after fresh baseline checks because both panel slots were occupied. Four fresh local rollback copies captured. Nine installed hashes and 76 preserved baseline files verified. Native HostOnline/Run, Steam and hook/config startup passed; A2S 0/16 at 01:14:01Z. IKEA/Soulrend were not deployed.

Evidence E:\Build\enshrouded-deploy103-20261010\DEPLOY113-Enshrouded:
- DEPLOY113-Enshrouded-install-20261010T011156Z.json.
- DEPLOY116-Enshrouded-completion-correction-20261010T0129Z.json corrects old-loader hash typo/omitted resources while preserving original ledger.
VERIFY117 accepted nine reported hashes/sizes and independently hashed four rollback copies against archive; VERIFY121 accepted correction. Neither reread live remote bytes. The 76-preserved count lacks an individual map in retained packet.

First live test failed: F7 waited for approval/F6 unavailable. Server observed authenticated inventory owner then protocol parser rejection around 01:29:14Z; private configuration hashes matched. CLIENT120/source comparison found older Storage preview Creative protocol 1 request 55 bytes/117 chars versus frozen UI43 protocol 2 request 80 bytes/167 chars, response 94 bytes. CLIENT123 repairs local mismatch without server/allowlist changes.

Overlay: dbghelp.dll, KFC/resource pair, Auto Loot Lua, three Forge files and two private allowlists. Three Forge files contain six definitions, levels 12/18/22/32/40/50. Combined loader SHA-256 8e489818048d016ae8a29ac1500f4b492ecd30dc35fffe3d026e72e94cc4c739. Auto Loot provenance mod-patches 7dc8953df88e44fa6fe12a41b247e0475dad64b1. No time allowlist/G path.
Stage E:\Build\enshrouded-rollout96-three-servers-20261010.
Accepted rollback archive E:\Build\enshrouded-rollout83-overlay-20261010\enshrouded-rollout83-overlay-rollback.zip, SHA-256 40414dab0e9fe3ee83addd0721709a3b65cf7712dbfcd88e9ae6c075b09b8a29.
Resource evidence E:\Build\enshrouded-rollout63-resources-20261009. Never use the historical stage missing Workshop 20x.
Rollback restores loader, two resources and old Auto Loot; remove additions only with deployment-creation proof/current hash match, otherwise restore preexisting private files. Preserve all unrelated files/worlds.

UDP query endpoints 172.241.3.131:25587 Enshrouded, :25592 IKEA, :25567 Soulrend. Pinned SFTP host key was explicitly approved first-use trust, not independently provider-verified. WSL pass enshrouded/{enshrouded,ikea,soulrend}/{sftp,panel} holds separate SFTP/panel credentials; never interchange or print secrets.

## Cleanup and source coordination

Keep-one cleanup complete: Enshrouded backup 87bebe8f-57bf-46f1-b48b-2ea710ab6597; IKEA b4521221-4ed9-49ca-8d25-fc58f14bc929; Soulrend 992ee43c-266d-4c49-91ed-638ba4e1cf2f. Ten older backups absent; two Soulrend rows have uncertain individual click causality. Removed displayed size 59.98 GiB is not physical reclamation. Future Soulrend rollout needs fresh baseline/rollback checks.
Local cleanup E:\Build\enshrouded-cleanup-20261010 removed seven minimap snapshots/169 old artifacts; recent Flight/Wine world/config writes, newer logs and current rollback/build inputs retained.

Coordinated chat 01a1220d-08fc-7cf1-9ce0-415e0a4a2b4c owns paused dirty canonical native/magic-storage/**, native/ui-host/** and Creative host integration. Read E:\Repos\enshrouded-mod-patches\native\magic-storage\coordination-handoff.md before further work. ABI 2 host owns four detours/single modal owner/pinned callbacks. Storage transfer commands remain quarantined. SOURCE132 completed the source-only bundle under research/ui43-shared-host-repair/ without binaries, secrets or canonical overwrites. Both patches applied in isolated copies and reproduced all seven accepted source hashes. The four-file bundle excludes its remaining empty locked .verify-work directory. Storage baseline is hash-gated because those original files were untracked; the bundle does not claim a complete Git-only rebuild of the paused project.

UI43 Creative 8fa0ebb3b177f3182a6d78da21b265094046ffc5, server 5f173c49875cc65c8d7903062dfc6ed904896758. Native CI 37996919115/16-member archive verified. F6 keeps intent only in same authenticated connection, suspends dead/spawning authority, requires fresh approval within fixed four-second recovery budget. 159 Wine 10 lifecycle checks/focused GPT-6 Pro review passed; final live respawn remains open. Home popup 252a1cd reports preference changes, not runtime acknowledgement.
Native Settings/catalogue/gear/Journey follow-up and physical Surface 3:2/controller acceptance remain. RC7 protocol 3 must not mix with UI43 protocol 2. Real G, free actions, world time, drag/delete/upgrades and full multiplayer inventory/respawn/bone-pickup acceptance remain unfinished. Unknown Journey data must stay unavailable.

## Final handoff

DISTRIBUTE130, APP127 and SOURCE132 are complete; current user gameplay test remains pending. RECORDS135 completed the final publication/install delta in existing Notion/Linear records, preserving 21 inventory rows and three servers. NIC-495 remains In Progress; no parity/gameplay issue was closed. Evidence: E:\Build\enshrouded-records101-20261010\records101-evidence.md. Urgent public delivery and local repair are complete; broader native-menu/parity work remains open.
Server-compat branch release/native-gameplay-20261009 includes handoff commit e05b5d6 and an additive SOURCE136 packaging correction. Bundle-scoped -text attributes preserve the exact two patch hashes through Git index and checkout with core.autocrlf=true; the checked-out patches reproduce all seven candidate files. No production source changes were made. Preserve untracked pycache and other source. Manager publisher head b703120. Urgent publication/local-repair work is complete; the broader deployment awaits live gameplay acceptance and subsequent menu/parity work.
Notion project 3f36f2829cab81a08305da19ac154e89, inventory 3f36f2829cab811eb2b5c0482824a5f4, servers 3f36f2829cab81adb4abc0555b7b6ce9. Linear composition mod-sources-builds-and-server-composition-746e6139767c, NIC-493/494/495/496. Preserve 21 inventory rows, 21 composition rows, three servers; no full-parity issue closure without evidence.
