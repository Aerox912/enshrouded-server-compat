# COST-7 native consumable cost evidence

## Pinned images and scope

The static checks target server SHA-256
`001c1b40ed091d8c1aee583adde3800d7c858ae2c7f4dff54fca2938b2be1637` and
client SHA-256 `af2f5a1227911d8aa06b3908d6bd0211838211cae14ea91099cb57d0df990781`.
The client source evidence is `native-cost-preview-evidence.md` from CLIENT-COST1.
This package does not install hooks, update runtime support bits, or claim
gameplay parity.

## Paired native removal sites

The client used-consumable call at RVA `0x222450` and fired-consumable call at
`0x1E61C4` each call client `inventory.removeItems` at `0x3822B0`. The pinned
server counterparts call `0x163E70` at `0x5C980` and `0x30CE4` respectively.
The corresponding return PCs are `0x5C985` and `0x30CE9`.

The caller ranges match by size and call offset:

| Action | Client PDATA range / offset | Server PDATA range / offset | Native owner source |
| --- | --- | --- | --- |
| Used | `0x221EB9..0x222897`, `+0x597` | `0x5C3E9..0x5CDC7`, `+0x597` | Server query entity from `0x5D5130`, copied to remover `R8D` |
| Fired | `0x1E5F20..0x1E7CBA`, `+0x2A4` | `0x30A40..0x327DA`, `+0x2A4` | Server query entity from `0x5D5130`, copied to remover `R8D` |

The paired local call windows have identical instruction bytes after masking
only their direct-call displacements. Each caller checks the native result
status at offset `+0` and remainder at `+4` after removal. The offline verifier
also confirms the server remover has six direct caller sites; the other four
remain outside this exact-site policy.

## Registration and unresolved firing edge

The used path is registered. Constructor code calls descriptor getter
`0xA72CB0` at `0x1D6A1E`; the returned descriptor at `0x1319530` is named
`actor_apply_buff`, and its callback pointer at `+0x10` is `0x5C3A0`. The
constructor passes that descriptor to registration helper `0x5F7AF0` at
`0x1D6A2C`. The callback starts a `0xA0`-byte query and reaches the used-site
body at `0x5C3E9`.

The fired removal site is in helper `0x30A40`; its three direct callers are
`0x33897`, `0x33E57`, and `0x34062`, all within PDATA range
`0x33289..0x34258`. This proves the exact helper and owner source, but the
pinned image does not establish a registration edge from that enclosing
wrapper to a registered player/action system. The separately registered
`server_player_ranged_weapon` callback is `0xA8880`; a static edge from it to
`0x33289` or `0x30A40` is not proven. Keep fired-consumable activation and
capability reporting unavailable until that enclosing callback relationship
is established.

The minimum remaining diagnostic is read-only observation at the fired remover
site: record the actual caller return PC, the current query context's world and
`0x5D5130` owner, the remover's `R8D` owner, and the enclosing registered
callback entry active on that native stack. Then bind that owner through the
existing authenticated full-Identity/liveness path and current effects lease.

## Hook-free adapter seam

`src/native_consumable_cost.hpp/.cpp` supplies `ActionScope` and
`invoke_native_removal`. The host must scope the exact native action wrapper,
provide the current query-world/owner reader, resolve that compact owner via
`authenticated_owner`, check actor liveness, and supply a nonrecursive snapshot
of the current owner-bound `free_consumables` lease. The policy requires the
actual native return PC and matching action kind, checks that native query owner
equals the remover's `R8D`, then repeats query, identity, liveness, and lease
checks before allowing the skip.

Only the two native result fields proven by the caller are set to zero. An
allowed call skips the removal function and returns its result pointer; a
denied or unrelated call invokes the original with all six ABI arguments
unchanged. This leaves native item eligibility, action outputs, transaction
commit/rollback, and all nonlisted removal callers under native control.
`ActionKind::fired` is a candidate seam only; it is not ready for hook
integration before the registration edge above is closed.

## Verification

`tools/verify-native-consumable-cost.py` passed for both pinned images. It checks
hashes, paired call targets and local instruction windows, query-owner source,
function ranges/offsets, native result fields, used callback registration, all
three direct fired-helper callers, and the complete six-site server remover
call list.

The new portable C++ adapter and `tests/native_consumable_cost_tests.cpp` built
with MSVC 14.44, `/std:c++20 /W4 /WX /EHsc /MT /O2`, and passed 23 focused
checks for exact return-site/action scoping, owner aliasing/mismatch, unknown
or expired lease, non-consumable effects, unknown effect bits, lifecycle/life
changes during authorization, query cursor changes, nested scopes, exact native
argument forwarding, and result-field preservation. These are static and
portable policy tests. No server process, hook, empty-inventory use, ordinary
player payment, failure rollback, or gameplay scenario was exercised.
