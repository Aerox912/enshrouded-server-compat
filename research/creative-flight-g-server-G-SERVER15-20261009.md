# G-SERVER15 command lifetime repair and integrated server record

Task ID: `G-SERVER15-FREEZE`  
Date: 2026-10-09

The server-local phase command reader no longer returns a view into a dead
stack buffer. `server_g_read_local_command` fills a caller-owned fixed
`std::array<char,128>`; `server_g_process_local_command` keeps that array alive
through parsing and handling. The 1..127-byte acceptance bound and duplicate
command suppression are unchanged. VERIFY57 accepted the bounded source/build
behavior. Review pointer: `src/native_runtime.cpp`, lines 670-694, under
`server_g_read_local_command` and `server_g_process_local_command`.

The reader change was source-reviewed and compiled, not exercised end-to-end
through the local file in a running server. No live server or game was started,
patched, installed, or deployed. Phase ordering and gameplay acceptance remain
live gates. The observer's private sampling path still requires safe settled
publication and a local `observe-phase-v1 N` command; production movement and
capability remain closed pending exact evidence acceptance and current-binding
revalidation.

## Verification

The integrated five-test CTest set passed 5/5 in the G-SERVER14 build:

- `creative_native_gear_options_cache`
- `creative_flight_g_authority`
- `creative_flight_g_integration`
- `creative_flight_server_runtime`
- `creative_flight_dispatch_bridge`

After the command lifetime repair, strict MSVC Release rebuilt
`creative_server_dev` and the authority/runtime tests. The two relevant CTests
passed 2/2: `creative_flight_g_authority` and
`creative_flight_server_runtime`. The five-test result predates the local
reader patch; the updated native translation unit was compiled into the DLL.
No extra test run was made solely for this documentation freeze.

## Frozen references

The current Creative native runtime SHA256 is
`91949743580CAE0F07DFDBB77123BCD7A89D82A9F4D4B8A300E607A22F8D86B4`.
The eight server G authority, integration, runtime, and transaction helper
source hashes are listed individually in the companion manifest.

The rebuilt artifact is
`E:\Build\enshrouded-g-server14-wire-20261009\Release\creative_server_dev.dll`,
SHA256 `CF0EFEFB1DC3B70E2712D9938BA5ED1ED4E314AFE03360F5D55FFB992B485D84`.
The accepted server CMake snapshot is SHA256
`A32B4EC872C4A622E67B74A8117BDEAE9A20C1295E7858EF7618C57B4F088318`. The
shared CMake file was released to approval_repair for client wiring; this
manifest records the accepted server snapshot rather than later concurrent
edits.

The historical G-INTEGRATE13 report and manifest were preserved unchanged:

- `creative-flight-g-integration-G-INTEGRATE13-20261009.md`:
  `D5AF81E0EBD5711A9CC3864AAB144654FE4E72F32EA634A1D91ED6DD5CD9C46C`
- `creative-flight-g-integration-G-INTEGRATE13-manifest-20261009.json`:
  `955520D10AF82D58D849DFAD45BED27B03FC28F5419313D7C3A8258DD737D358`

The appended module record is in
`creative-flight-server-runtime-abi-20261009.md`; its SHA256 is recorded in
the companion manifest.
