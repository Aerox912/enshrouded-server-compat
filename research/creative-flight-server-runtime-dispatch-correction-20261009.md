# G-DISPATCH7 material dispatch correction

Status: **corrected, self-checked, frozen default-off source; uninstalled**.
No hook, capability advertisement, shared startup, client-life change, Git
mutation, or gameplay action occurred. The current implementation and bounded
verification are documented in `creative-flight-server-runtime-abi-20261009.md`.

The original G-SERVER-RUNTIME5 synthetic fixture manually invoked a
`flying_mover()` while DynamicLocomotion.state=3. The pinned native dispatcher
never takes that route. Its 505 synthetic checks could not establish the hook
position. G-DISPATCH6/7 replaces that seam with an actual assembled CALL bridge
at the verified MOV pair, restores native state, and resumes the unchanged
CMP/JA/table dispatch. Approved G state3 explicitly invokes the private Dive
helper once within the retained native mover callback; state4 and denied rows
keep their native branches and the post-dispatch continuation.

## Exact paired native evidence

Pinned server SHA256 `001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637`, PE `69FDECC9`, size `1DA7000`.

At server `187595`, native loads DynamicLocomotion pointer `[RBP+38]`, state byte+`3D`, then dispatches through 11 uint32 RVAs at table `18775C`:

| State | Target RVA |
|---|---|
| 0 | 1875AF |
| 1 | 1875C1 |
| 2 | 1875D3 |
| **3** | **187619** |
| 4 | 1875E5 |
| 5..8 | 187619 |
| 9 | 1875F7 |
| 10 | 187609 |

`1875E5` supplies the actual row at `[RBP-60]` and original second argument at `[RBP+1B8]`, then calls `17F5A0` at `1875F0`. This is the **state4 glider path**, not state3 Flying. State3 targets `187619` directly, the post-dispatch continuation. That continuation calls native `17EA40` with the same row/second argument and performs remaining component/counter work. An approved G helper must preserve it.

Pinned client SHA256 `AF2F5A1227911D8AA06B3908D6BD0211838211CAE14EA91099CB57D0DF990781`, PE `6A4236C8`, size `2DA7000`.

At client `3A60A5`, paired native dispatch uses table `3A626C`:

| State | Target RVA |
|---|---|
| 0 | 3A60BF |
| 1 | 3A60D1 |
| 2 | 3A60E3 |
| **3** | **3A6129** |
| 4 | 3A60F5 |
| 5..8 | 3A6129 |
| 9 | 3A6107 |
| 10 | 3A6119 |

The state4 call at `3A6100` targets `39E2B0`. State3 bypasses it. Paired post-dispatch continuation `3A6129` calls `39D750`. Paired explicit Dive target is `39D190` (server `17E480`), already verified by the frozen helper evidence.

## Implemented bounded correction

The viable G movement entry is the mid-function dispatch site, after native per-row preparation: server `187595`, client `3A60A5`. The frozen callsite bridge supplies the actual stack row and original second-argument object, preserve the native register/flags/stack contract, invoke private Dive only for current approved G state3, and resume the unchanged native dispatcher and its `187619` / `3A6129` continuation. Denied/non-G rows execute the native original dispatch once unchanged. This site is not a callable C++ function and cannot be represented honestly by a fake `ServerMover` original.

The separate runtime's scoped outer callbacks, iterator observation, full Identity/G-generation checks, single-use command policy, private gravity row restoration, phase observer and stop drain are reusable, and the movement entry and fixture have been corrected. The phase observer remains read-only/default off. Native loop/order observation remains a separate isolated acceptance step, not a fabricated simulation-frame counter.

Additional verified server facts obtained in this package: constructor `59A3F2` forms world+`928`; `59A496` publishes it at world+`CC4218`. Query begin `5DC8DC` writes the query-context pointer into row[0]. Owner leaf `5D5130` uses current query cursor. Gravity descriptor `11A0370` is `keen::ecs::Gravity`, size `14`; its reflected fields are direction0, valueC, isActive10. Native `7E850..7E859` gates the gravity row on byte+10. These support the independent scoped reader/gravity adapter work, but do not repair the disproven movement hook by themselves.
