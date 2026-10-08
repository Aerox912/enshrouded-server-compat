# Workshop Speed deployment, 7 October 2026

The user authorized Workshop Speed 20x on Enshrouded, IKEA and Soulrend before manager publication. After initially choosing to wait, the user explicitly authorized restarting Enshrouded with its active player connected. The user also authorized publication of the accompanying apps.

## Observed deployment status

| Server | ID | Status | Current start (UTC) | Rollback archive in server root |
|---|---|---|---|---|
| IKEA | c70c80c3 | Applied, hashes and startup verified | 19:11:24 | archive-2026-10-07T190341Z.tar.gz |
| Soulrend | ea6b2409 | Applied, hashes and startup verified | 19:15:48 | archive-2026-10-07T191338Z.tar.gz |
| Enshrouded | 1650d1ae | Applied, hashes and startup verified | 19:49:31 | archive-2026-10-07T194739Z.tar.gz |

All three servers reached Host_Online. Their fresh adapter logs confirm the five existing Vein Mining hooks and extra Auto Loot/gifting hooks. Their fresh XP logs confirm HOOK INSTALLED and multiplier 1.00. No new loader or server configuration was installed. Actual in-game Workshop timing and multiplayer acceptance remain unverified.

Enshrouded was completed after the user explicitly authorized an immediate restart despite Maevey still being connected. Its rollback archive contains 87 files including the world and both previous resources; previous resource hashes matched and 70 existing mod files matched the staged inputs. Installed resources were downloaded and both hashes below matched. All three Workshop files were present. Fresh logs at 19:49:31-32 UTC confirmed the existing hooks. The background deployment check was paused after completion.

## Package and evidence

Local staging root: `E:\Scratch\enshrouded-workshop-deploy-20261007`.

- `prepared`: rebuilt from the verified clean KFC backup and existing seven mod inputs, with Workshop added. Grapple distance was applied once from stock values.
- `prepare.log`: all original mods processed successfully; 320 timed recipes divided by 20, 1,634 untimed recipes unchanged, 1,954 recipes checked.
- `package`: only the two patched resources and the three original Workshop files. No executable, world, loader, server configuration or Creative payload.
- `workshop-update.zip`: already uploaded to each server root. Do not publish this archive: it contains game resources and original mod files for these private installations.
- `update-manifest.json`: exact size and SHA-256 of each deployment file.
- `backups/IKEA`, `backups/Soulrend` and `backups/Enshrouded`: downloaded rollback archives with worlds, mods, configuration and loaders. Contents and the existing mod inputs were checked before applying the update.
- `evidence/IKEA`, `evidence/Soulrend` and `evidence/Enshrouded`: downloaded fresh adapter and XP logs.
- `deployment.json`: machine-readable completion and verification status for all three servers.

Installed resource hashes, verified by downloading the deployed files:

```text
88bd0c402c9c254f67f6d36e2fbe3acecd900373d419331f95516fb74ffef54a  enshrouded_server.kfc
d43b3fd43b58b652bfc7c2c6bc6295e60a5257b8d75737d34c19045f3db88e1a  enshrouded_server.kfc_resources
```

Verified previous resource hashes for the Enshrouded update:

```text
ebf544887a90868395999bdda999e9c00d0d59feb82d2de73edfb15eae21e5cb  enshrouded_server.kfc
8cd666f42a4d398af6e78a712066487c0d02fadf59fe4294f6601178f607bf7c  enshrouded_server.kfc_resources
```

## Procedure used for Enshrouded

Used the authenticated Kinetic panel at `https://kineticpanel.net/server/1650d1ae/`. Fresh Workspace console records still showed an active remote player, so the user was asked whether to wait or restart. The user explicitly chose restart. No broadcast message was sent.

After that authorization, used Stop and verified Start was enabled. In File Manager, archived mods, savegame, GlobalXPShare.original.dll, dbghelp.dll, enshrouded_server.json, enshrouded_server.kfc, enshrouded_server.kfc_resources and safeprobe_config.ini. Downloaded and verified the complete rollback archive, including the expected previous resource hashes and existing mod inputs.

Right-click the already uploaded workshop-update.zip and use Unarchive. Download the resulting two resources and verify the installed hashes above. Confirm mods/XHL-Workshop-Speed-20x contains mod.json, config.txt and src/mod.lua. Start the server, require fresh Host_Online plus the adapter and XP hook markers, save the evidence and update deployment.json and this document. Preserve Wine's existing dbghelp native,builtin override and all server settings.

The browser upload UI has two steps: Upload opens a modal, then Browse files opens the file chooser. Wait for the modal before the second click. Use absolute `E:/...` paths in chooser.setFiles to avoid JavaScript backslash escaping. No additional upload should be needed unless the existing archive is missing or invalid.

## Release boundary

Admin and Regular managers 4.4.3 were published after all three server deployments:
https://github.com/Aerox912/enshrouded-client-releases/releases/tag/v4.4.3

Source commit: `0b8a86677fd6be48d2719d7cbca9853dfbf57ba0` in the private installer repository. Cloud run `37678678596` passed both builds, their lifecycle/UI checks, real Setup installation/reinstallation/uninstallation and publication. Published-update run `37679082014` passed actual older-manager migration through the legacy bridge. Local builds passed 579 Regular and 581 Admin checks. The public archives and all four signed feeds were downloaded and verified; evidence is in `E:\Build\enshrouded-manager-4.4.3\published\verification.json`. Legacy `channels/*` remain at 4.2.1.

Creative has a tested policy/protocol core and manager integration; no working native game adapter, menu or deployable DLL exists. Its Admin card stays disabled and both released packages exclude Creative. Workshop timing and mixed-client gameplay acceptance remain unverified. The private server resource archive was not published.
