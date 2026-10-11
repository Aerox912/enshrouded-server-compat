# Creative and Vein rollout, 2026-10-11

The authorized Creative input/Journey and Vein Mining update is deployed on Soulrend, Enshrouded and IKEA. Both companion managers are published as [4.4.10](https://github.com/Aerox912/enshrouded-client-releases/releases/tag/v4.4.10). Storage is excluded pending a separate repair after its failed native selection test.

| Server | Installed files | Preserved captured files | Result |
| --- | ---: | ---: | --- |
| Soulrend | 9 | 79 | Online; startup markers and all installed hashes verified |
| Enshrouded | 2 | 86 | Online; startup markers and all installed hashes verified |
| IKEA | 9 | 78 | Online; startup markers and all installed hashes verified |

Each server was gracefully stopped and had a completed full locked backup before replacement. Player-count gates were removed at the user's explicit instruction. Existing worlds, server configuration, XP chain/settings and Workshop data were preserved. Soulrend and IKEA received the same approved Creative players as Enshrouded at the user's request; no extra standalone Flight or world-time permissions were introduced. This public report intentionally omits addresses, private identifiers, allowlist contents and backup locations.

Fresh logs confirmed five Vein gameplay/input hooks, radius-ready state, Creative inventory authorization and Steam transport, XP HOOK INSTALLED, and Host_Online. The accepted server adapter SHA-256 is b882ce0665f9694527ee6907347e27caf29efece77c9b2fbcd35520f38023fbf; expanded server XHL is 4921a0009dbb1f9fcca79be355a4e3fb70481be4b811b1f1eba1c4bcbab936f7. Supported server executable and chained XP DLL were checked against their pinned baseline. Original replaced files remain recoverable; backup restoration was not rehearsed.

Manager source c86746b967870fc60630aa552a38be575b4d4710 passed [tag CI38151549882](https://github.com/Aerox912/enshrouded-client-installer/actions/runs/38151549882): 748 Regular and 751 Admin lifecycle/UI checks plus input and setup checks. [Published-update smoke38151865742](https://github.com/Aerox912/enshrouded-client-installer/actions/runs/38151865742) passed the older-manager migration and real signed 4.4.10 executable handoff. Both public ZIPs and executable hashes were checked against four signed feeds at feed commit bbf914de660afebf788ca375d5c05def8e1120a5.

- Regular ZIP SHA-256: 533fa7d4f0558ee2e978e71ebfeefd7593ad4aef140ae75ad47d7112092fd55f.
- Admin ZIP SHA-256: 8148ddcbc4c83305f0a029e6d38a2fa446ae906d6d5b4d0629a2c09728be08cb.
- Native integration ZIP SHA-256: 97facbacaa502cd7de6d79f76dbc6094981166f60c99ab5389b9a3cbe6be2d50.
- Creative client SHA-256: 1824f9d036e01bacef17c49f550d2351ccc1d6a0ebb3814f0ab81980819400cb.
- Shared host SHA-256: 66afcfd8f70adc6fbfb9f9c106808ea4c99eed3564950d05123e7e528b128567.

The 4.4.9 promotion was correctly refused before mutation because the Admin feed already used that version. A version/readme-only bump to 4.4.10 retained the verified native payload and anti-rollback checks. Incompatible standalone Flight is no longer selectable and is retired through the tested transaction path; settings and failed-migration rollback are preserved.

The exact accepted source snapshot is now preserved in the private Creative repository under tag creative-vein-accepted-source-20261011. Its archive SHA-256 is 44226518cd14485f8a60c00072506c56be7e21c338819792383ef406592a76ea. All 1,411 archived files match the frozen source/dependency manifests; seven excluded SDK libraries have hash-verified reconstruction pins in Shroudtopia 195e6189e7e695a06f5cb4ca6830c0b08cc3ac8f. The historical Creative base commit is not the complete accepted source; the source archive manifest is authoritative. No new rebuild is claimed by this preservation step.

Prior user testing confirmed F7 keyboard/mouse capture, Journey mode and radius cycling/persistence. Startup and file verification do not establish sustained Soulrend performance, resource payout/inventory boundaries or reconnect behavior. Those remain live acceptance work. Storage's crash, native transfers and wheel/controller scrolling remain separate repair scope.
