# Project structure

- `src/` — production adapter, profile extras, proxy, and opt-in flight/native development code.
- `tests/` — cloud rejection/checksum checks, native unit harnesses, and separate game-dependent integration programs.
- `ci/` — build and packaging automation.
- `tools/` — server preparation and verification scripts, plus flight investigation helpers.
- `research/` — compatibility findings, runtime investigation, and release evidence. `native-release-review-20261009.md` is the current review record.
- `build/` — local build support and generated build area.
- `vendor/` — vendored MinHook dependency.
- `README.md`, `INSTALL.md` — profile scope, supported executable, preparation, staging and deployment procedure.
- `flight-deployment.json` — release readiness and hosted-server deployment record; verify it against newer review evidence before relying on candidate fields.
- `version.txt`, `CMakeLists.txt`, `dependencies.lock.json` — project version, target configuration and pinned dependency data.
- `agent_docs/` — concise canonical project context and active task records.

The opt-in Creative experiment expects a sibling `enshrouded-creative-mode` source tree; the flight experiment uses headers and sample sources from the sibling `shroudtopia` tree. These dependencies are referenced by CMake configuration and are not vendored here.

