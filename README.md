# Dedicated-server compatibility and profiles

Builds the maintained compatibility adapter and packages the complete Normal and Cheeze server profiles. Original mods are separate author downloads and are never redistributed in these archives.

Normal includes Global XP Share at 1x and EMBER magic furniture, production stations, buff refresh, no-build-zone removal and building in Shroud fog. Cheeze adds the six XHL mods: Vein Mining, Auto Loot, Max Stack 65535, Unlimited Gifting Range, Rested Enhanced and Grappling Hook Pull 2x. Both retain altar requirements. Cheeze retains disabled item durability.

Use `Prepare-Server.ps1` with an originals folder and a new output directory. It verifies the original files and produces the correct loader arrangement. Follow INSTALL.md to patch a clean staging copy before deploying. The scripts do not connect to a hosted server.

Only dedicated Windows x64 game build 1024233 is supported, SHA-256 `001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637`. Unsupported binaries fail closed. Matching clients remain necessary for the XHL gameplay/UI features.

GitHub CI compiles all native targets and runs the cloud rejection/checksum checks without game files. Existing adapter_tests and extras_tests remain separate integration programs requiring the supported game executable and original plugins. A green cloud build does not establish multiplayer acceptance.

Global XP Share remains original. Normal uses its dbghelp.dll; Cheeze uses this adapter's dbghelp.dll and loads the original as GlobalXPShare.original.dll. Preserve the per-executable Wine override `dbghelp=native,builtin`. Confirm HOOK INSTALLED in XP Share's own log, then test XP distribution between two players who have each earned XP after joining.
