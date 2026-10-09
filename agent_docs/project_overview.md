# Project overview

This repository builds a dedicated-server compatibility adapter and packages the Normal and Cheeze server profiles. Original mods are obtained from their authors and are not redistributed. The supported target is dedicated Windows x64 game build 1024233 with SHA-256 `001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637`; unsupported binaries fail closed.

Normal packages Global XP Share and EMBER features. Cheeze adds six XHL extras: Vein Mining, Auto Loot, Max Stack 65535, Unlimited Gifting Range, Rested Enhanced and Grappling Hook Pull 2x. Both retain altar requirements; Cheeze retains disabled item durability. Matching clients are required for XHL gameplay and UI features.

Always Flying and Creative are separate development experiments. They are not part of the public server packages. Public 4.4.6 remains current; no new native package is ready for release or deployment. Respawn acceptance requires flight to suspend while dead or spawning, reauthorize after a live respawn in the same authenticated connection, and reset on disconnect or revocation. The newest respawn candidate has no runtime acceptance yet.

Prepare changes in a clean staging copy, verify the executable and package, and follow [INSTALL.md](../INSTALL.md). A hosted update requires an empty server, a complete rollback backup, fresh startup evidence and gameplay acceptance. Green local or offline checks do not establish multiplayer acceptance.
