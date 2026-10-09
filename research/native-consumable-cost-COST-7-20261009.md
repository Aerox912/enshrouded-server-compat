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

## Registered used action and unresolved fired-wrapper attribution

The used path is registered. Constructor code calls descriptor getter
`0xA72CB0` at `0x1D6A1E`; the returned descriptor at `0x1319530` is named
`actor_apply_buff`, and its callback pointer at `+0x10` is `0x5C3A0`. The
constructor passes that descriptor to registration helper `0x5F7AF0` at
`0x1D6A2C`. The callback starts a `0xA0`-byte query and reaches the used-site
body at `0x5C3E9`.

The fired removal site is in helper `0x30A40`; its three direct callers are
`0x33897`, `0x33E57`, and `0x34062`, all within PDATA range
`0x33289..0x34258`. The pinned image does not establish a named registration
edge from that enclosing wrapper to a registered player/action system. The
separately registered `server_player_ranged_weapon` callback is `0xA8880`; a
static edge from it to `0x33289` or `0x30A40` is not proven. COST10's bounded
authorization uses the verified `0x30A40` helper scope plus the exact
`0x30CE9` removal return PC, native owner forwarding, current full identity,
actor liveness, and repeated lease checks. It does not claim a named enclosing
registration, and the diagnostic keeps `callback_registration_proven=false`.

The read-only fired probe records the actual caller return PC, query context
world and `0x5D5130` owner, remover `R8D` owner, and any enclosing frame in the
known function range. A named registered callback cannot be established by the
pinned image; that remains a reporting limitation rather than an authorization
input.

## Runtime adapter and default-off shared hook owner

`src/native_consumable_cost.hpp/.cpp` supplies the authorization policy.
`src/native_consumable_diagnostic.cpp` owns one hook set for used action
`0x5C3A0`, fired helper `0x30A40`, and remover `0x163E70`;
`src/native_consumable_runtime_adapter.hpp` contains the action/removal wrapper
called by the detours and the adapter tests. `prepare_free_consumables_hooks`
prepares all three sites, while `enable_free_consumables` activates the full
set only after all trampolines are valid. The optional read-only probe shares
this hook owner and cannot run alongside free-consumables mode. Both modes
default off. The effects callback and backing state must remain valid for the
process lifetime after preparation because retained detours can outlive a
failed removal or disable.

At runtime the adapter reads query world and owner through the pinned helper,
resolves the compact owner through `authenticated_owner`, checks actor
liveness, and supplies a nonrecursive snapshot of the current owner-bound
`free_consumables` lease. Policy requires the exact native return PC and
matching action kind, checks query owner against remover `R8D`, then repeats
query, identity, liveness, and lease checks before permitting the skip.

Only the two native result fields proven by the caller are set to zero, after
the result span is checked writable and the write is guarded. An allowed call
skips removal and returns its result pointer. Denied, missing-query, mismatch,
unknown, inactive, or unwritable cases call the original once with all six ABI
arguments unchanged. Native item eligibility, action outputs, transaction
commit/rollback, and the other four removal callers remain native-controlled.

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

COST10's adapter stub tests use the same inline wrappers called by the detours.
They cover used/fired scopes, nested removal, exact six-argument forwarding,
owner/query/return-site denial, missing lease, activation loss, result-write
rejection, and inactive/diagnostic pass-through. The runtime object and tests
compile locally; no hook was prepared or enabled and no gameplay parity is
claimed. Empty-inventory arrow/consumable use, ordinary-player costs, and
forced-failure rollback remain gameplay acceptance items outside this package.
