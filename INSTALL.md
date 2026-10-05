# Prepare and install a server profile

1. Obtain the exact original mods listed in catalog.json from their authors. Put their ZIPs or extracted files in one local originals directory.
2. Run Prepare-Server.ps1 with that directory and a new output directory. No running game/server installation is modified.
3. Create a clean staging copy of the supported dedicated-server installation. Verify its executable against the catalog hash. Do not reuse already-patched resources: grappling multipliers must be applied once.
4. Copy the prepared files into staging. Obtain EMM 0.1.2 from its pinned upstream release and verify its checksum in dependencies.lock.json. Run `emm.exe run --patch --force -g <staging-directory>` and require a successful exit. Back up and merge profile serverSettings from profile.json into the server's existing JSON; preserve unrelated settings, passwords and permissions.
5. On the target server, wait until no players are connected, stop it, and back up worlds, config, mods, loaders, KFC index and resources. Transfer only the prepared/modified files, preserving unrelated files and the previous XP settings. Preserve the Wine dbghelp native,builtin override.
6. Start the server and verify Host_Online, adapter hooks for Cheeze, and XP Share's HOOK INSTALLED. Test gameplay with matching clients. To revert, stop the empty server and restore the verified backup. Before removing Max Stack, split oversized stacks to stock limits.

The cloud release contains source-built adapter code, our configurations and dependency metadata. It does not contain original mods, patched game resources, worlds, credentials or a copy of the game. No automated live deployment occurs.

Run `Verify-Server.ps1 -ServerDirectory <staging>` to check the executable, prepared files and XP chain. After deployment, `-CheckStartupLogs` also checks available log markers. Use fresh logs from the current start; retained markers from older sessions do not prove current hooks or XP distribution.
