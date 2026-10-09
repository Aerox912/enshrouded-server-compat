# Enshrouded mods

Goal: finish verified native Always Flying and Creative delivery, repair reported loot regressions, publish matching Admin/Regular manager builds, and update all three hosted servers when empty. Keep Notion and Linear aligned with verified evidence.

Current release remains on hold. The installed local rc.2 passed user checks for native menu/controller navigation, customized gear level/quality, reconnect persistence, and full-backpack weapon rejection. Partial-fit rollback, mixed approved/denied clients, and actual Wine multiplayer runtime remain open. Public manager 4.4.6 and hosted Workshop 20x baselines remain unchanged.

The intentional respawn implementation is committed on GitHub and passed independent Release verification (all ten CTest targets). Focused GPT-6 Pro source findings are resolved. The remembered F6 preference is separate from live actor authorization; old Creative actions are discarded, and fresh approval is required before automatic resume. The exact rc.4 cloud artifact is verified; all 13 offline Wine executables passed, totaling 889 checks. This version is not installed or gameplay verified.

The narrow bone-pickup patch is committed on GitHub. Current selected-mod client and server data now rebuild successfully with the repaired script. Other Hollow corpse containers use a distinct path and still need live verification. The separate constrained Wine candidate reaches Host_Online, but its Windows loopback endpoint is not yet verified.

Delivery inspection found that manager and server package dependency locks still download older patch inputs despite the rc.4 native metadata naming the new patch source. The next milestone is a shared immutable patch input, verified generated manager/server payloads, and a native dependency rebuild. Then install one matching local test update and obtain gameplay acceptance before publication or hosted deployment. The obsolete scheduled check was deleted; the active thread goal owns continuation.
