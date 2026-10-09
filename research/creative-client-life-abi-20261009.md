# G-LIFECYCLE-4: copied client current-world and local Actor life

Scope: a synchronous, read-only helper for the pinned client, plus a local approval binding policy. No hooks, native calls, process access, gameplay activation, wire changes, shared Creative sources, Git state, or frozen G movement helpers changed in this package.

Pinned client: SHA256 `AF2F5A1227911D8AA06B3908D6BD0211838211CAE14EA91099CB57D0DF990781`, PE timestamp `6A4236C8`, image size `2DA7000`, preferred base `140000000`. The caller must verify the full hash of the loaded client once and keep its module base/hash result fixed for that loaded image. The maintained verifier checks full hash and PE identity before layout checks; there is no CLI bypass.

## Native source chain

The GameApplication singleton is inline at image RVA `1F07CC0`. The constructor at `771C86` passes that address to initialization; leaf getter `778C50` returns it with LEA. Do not treat that location as a pointer global.

GameApplication+`250` is the current outer Client: native `78CD27` reads it, and the creation fragment `788660` calls Client constructor `765A10` before publishing it at `78866F`. Client+`52888` is the session wrapper, published at `774C22`. Native `78A89E..78A8BC` requires a nonnull wrapper and nonzero wrapper+`20` before reading wrapper+`180`, the current scene.

Scene+`3439C0` is the ClientSimulationState, as checked in native `771240..771262`. That native leaf later mutates UI fields, so the helper never calls it. Scene+`1C0` is supplied to the simulation constructor input+`88`: `77C553` reads it, `77C561` stores it into stack input root+`88`, and `77C6ED` calls constructor `324E30`; `77C6F9` publishes its result to scene+`3439C0`. Constructor `324E77..324E86` copies input+`88` into simulation+8. The helper requires scene and simulation world pointers to agree and repeats the entire chain after the life read. This proof is independent of query registration order or an inferred query-world pointer.

The existing bounded LocalPlayerData decoder proves the local entity: exact name/hash registry match, current world global registry+`6D0F80`, current entity ID at LocalPlayerData+4. Native local ownership predicate `336D50` compares that 32-bit ID with the current entity. Its code/registration evidence is included by the verifier.

Current world's execution root first pointer at world+`CC4218` points to its own registry world+`928`: constructor `8988B2` forms this address and `898956` publishes it. Native `8B7D28..8B7D6A` reads component array world+`930`, count world+`938`, and iterates record stride `100`. Native `8F472E..8F4759` indexes that array using the type index and reads record+`28` reflection metadata; metadata+`20` supplies the logged native type name. The helper selects exactly one record whose metadata is the pinned `keen::ecs::Actor` descriptor at RVA `17BC900` (name, full type name, size `E10` checked statically). The selected array ordinal is the native type index. No guessed Actor hash or query component order is used.

Native `8C7340` resolves the component using execution entity map+`148` (world+`CC4360`). `8C93E0` proves the 32-bit native entity hash, open addressing, bitmap+8, capacity+`10`, keys+`28`, occupancy+`54`. Native `8C7378` reads entity record pointers through values+`40`. Entity record+`18` supplies layout, +`20` component storage, +`30` stride. Native `8C7396..8C73FC` proves the type-index presence bitmap, component offsets at layout+`84`, group indices at layout+`A84`, and address formula storage + group*stride + offset. The helper copies this bounded resolution and repeats map, location and registry evidence. It makes no native function call and retains no pointer for later dereference.

Client Actor state helper `1DFA50..1DFA90` loads currentState+`BF8`. With Actor+`1B1` bit0, its effective state is `(currentState | stateAtBD0) & ~stateAtBD8`. The helper copies and repeats all four fields. Its life gate conservatively denies Dead bit7 or Spawning bit12 in either raw or effective state, so prediction cannot clear an authoritative death gate. Other state bits do not independently authorize any capability.

The client-life verifier now binds those bit positions to the pinned executable's `keen::actor::StateFlag` reflection descriptor at RVA `1B2D230`. It checks the qualified-name pointer at `1B2D250`, the reflected field count (62) at `1B2D278`, and the enum-table pointer at `1B2D290` to RVA `1B23720` with `28`-byte records. It verifies the `Dead` record at `1B23838` has value 7 and the `Spawning` record at `1B23900` has value 12. These observed names, RVAs, and values are persisted in `research/creative-client-life-static-evidence-20261009.json`; the `reader.dead_bit` and `reader.spawning_bit` values come from those checks.

## API and integration contract

`src/creative_client_life.hpp` exposes:

```cpp
std::optional<LocalLifeSnapshot> read_pinned_client_life(
    Read&& safely_copy_bytes, std::uintptr_t loaded_module_base, bool full_hash_verified);
```

Read has signature `bool(uintptr_t, void*, size_t)`, copies exactly the requested bytes, returns false on failure, and must not throw. The API returns copied numeric identity tags (client/session/scene/simulation/world/local entity/current Actor storage), raw/effective state, and `alive()`. Keep tags internal; never export or log them. Tags are equality evidence and confer no pointer lifetime. Do not dereference a stored tag or cache the alive result. Invoke synchronously at accepted approval/publication and immediately before the effect. The provider still checks current host connection, trusted server capability/lease, deadline, and relevant state/activation epoch before and after reading, under its existing mutex or equivalent serialization.

`LocalLifeBinding` is an optional policy helper, used under that provider lock. `bind(token, freshSnapshot)` binds a newly accepted approval to the copied identity. Tokens are strictly increasing provider-local accepted-capability tokens, never a wire ServerIdentity or a protocol nonce. Unchanged active heartbeat may use the same token. Missing/dead/changed identity during `check()` retires it. Failed initial acceptance consumes its token as well. Retired/stale tokens cannot bind again; a newly accepted approval must receive a later local token and a fresh snapshot. Explicit host/revocation clearing also retires the active token. G activation generation remains distinct and must follow the existing G contract; this helper does not advance it on ordinary unchanged heartbeat.

Bounds: at most 4,096 safe copy attempts, component count<=1,280, entity table power-of-two capacity<=1,048,576, entity probes<=64. Existing LocalPlayerData decoder retains its smaller registry bounds. Unsupported/ambiguous/missing/changed/read-failed evidence returns unavailable. No borrowed sample survives a callback. Repeated copies detect observed inconsistency; they cannot prove absence of undetectable engine ABA or supply a native respawn counter. The binding retires observed death/spawn/identity change. Runtime gameplay and actual native record population remain separate acceptance.

## Verification and replay

`tools/test_creative_client_life.py` builds only `tests/creative_client_life_tests.cpp` in its own marked E: build root, MSVC Release, C++20, `/W4 /WX /EHsc`. Run with the configured CMake path:

```powershell
& 'C:\Users\herks\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' tools/test_creative_client_life.py --build-dir E:\Build\enshrouded-client-life-test-20261009 --cmake 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
```

Observed: 68 fixture checks, isolated CTest 1/1 pass. Cases include every null world-chain link, inactive session, inconsistent world crosscheck, missing/duplicate Actor metadata, invalid tables/overflow/collision bounds, before/after chain/map/layout/local entity mutations, raw/predicted Dead/Spawning, prediction mask mutation, replaced Actor storage with the same local entity, new approval binding, unknown/death/revocation retirement, stale heartbeat, and failed initial acceptance.

The maintained pure-Python `tools/verify_creative_client_life.py --client <pinned client exe> --json <output>` validates 22 whole-instruction native anchors with exact unwind expectations, Actor and StateFlag metadata, and existing local ownership evidence. Output: `research/creative-client-life-static-evidence-20261009.json`. Observed full pinned-input verification passes. `tests/test_creative_client_life.py` covers all22 signature/unwind mutations, wrong hash/PE identity, metadata and type-name drift, and both StateFlag values. Synthetic fixtures are explicitly separate from the real pinned binary run.

Source/static evidence is ready for independent review. Client provider integration, in-game crafting, death/respawn gating, peer reconnect and G movement acceptance are not claimed by this package.
