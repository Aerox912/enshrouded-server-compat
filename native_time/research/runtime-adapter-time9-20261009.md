# TIME9 runtime adapter integration

The adapter composes the time command writer with the existing read-only observer
through one hook backend. RuntimeAdapter::install calls ReadOnlyObserver::install
once with a bridge backend; that backend maps the six observer sites to one
underlying detour each: 0x5DC7F0, 0xCACE0, 0xC9670, 0x8507C0, 0x861790, and
0x8614B0. It does not install a second 0xCACE0 hook.

0x5DC7F0 is shared with other callback consumers. The bridge calls its native
original directly when there is no active time callback frame, or when the
callback context does not match the current 0xCACE0 / 0xC9670 frame. That path
does not resolve a world, read host state, or record an observer query. Inside
a matching time callback, the bridge invokes the observer detour once, then
captures world and clock values from that same query output. It issues no
second native query and keeps the query view only in the callback stack frame.

The adapter starts disabled. Explicit observe-only startup installs the observer
but denies commands. enable_command_writes requires an accepted, completed
bounded phase sample and the host's private time capability. Each callback
re-resolves the current world, scope, owner identity, and clock values. Commands
carry the complete flight identity and opaque WorldScope; clock pointers do not
enter persistent adapter state. Hour writes and scale writes are preflighted by
apply_plan_once and use the owning daytime callback. The time capability is
separate from Creative flight permission.

Scope retirement now marks the scope unusable before removing its queued
commands. Submission checks the accepted scope both before and after enqueueing;
if retirement wins that race, the just-enqueued scope commands are removed and
the submission is denied. A regression assertion covers both hour and mode
commands after retirement.

The adapter and test use fixed-capacity event, scope, and command storage. The
retired-scope table holds 32 tokens; exhausting it invalidates the phase and
fails closed. The host still must rotate scope tokens on observed world/session
or owner lifecycle changes. This covers observed transitions and does not prove
that the game allocator cannot destroy and reuse a world between observations.

## Verification

The isolated fixture used CMake/CTest 3.31.6-msvc6, Visual Studio 17 2022 x64, MSVC
19.44.35228.0, and Windows SDK 10.0.26100.0. It compiled the focused runtime
C++20 runtime adapter executable in Release with /W4, /WX, /EHsc, and /guard:cf. The fixture
references the repository sources directly and did not modify the shared
CMakeLists.txt.

The clean build succeeded for runtime_adapter_tests.cpp, runtime_adapter.cpp,
callback_writer.cpp, time_control.cpp, read_only_observer.cpp,
flight_session.cpp, and flight_identity.cpp. CTest passed
native_time_runtime_adapter (1/1). The executable's separate --create-failure
path also passed, covering partial hook creation cleanup. The CTest path runs
the synthetic phase validator and runtime forwarding/policy checks, including
single hook ownership, out-of-scope query passthrough, default-off policy,
identity and capability gates, callback writes, restoration, and post-stop
forwarding.

Captured outputs and exact source hashes are under
E:\Build\enshrouded-time9-runtime-20261009:
- time9-build-output.log
- time9-test-output.log
- time9-toolchain-output.log
- time9-sha256.txt

These checks use synthetic hooks and memory only. No live server, MinHook
installation, proxy startup, clock write, shared CMake change, Git mutation, or
deployment was performed. The host still must rotate scope tokens on observed
world/session or owner lifecycle changes; these tests do not prove world
allocator non-reuse or live server behavior.