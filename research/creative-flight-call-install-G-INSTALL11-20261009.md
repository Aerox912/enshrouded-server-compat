# G-INSTALL11-EVIDENCE frozen installer handoff

G-INSTALL10-CONTINUE implementation plus the bounded G-INSTALL11 evidence delta.
Only four new installer files and focused new tests/research are owned here.
No shared CMake/runtime/client/Git change or game process patch/install was made.
Canonical repository worktree registration confirms this checkout at
5cbe7fd327744ad5ac481a5de6c549432bae2378, feature/creative-parity-20261009.

## Outcome and contract

The opt-in installer hashes the complete main-module file, verifies mapped PE
headers and frozen dispatch/unwind/adapter bytes against that pinned file, makes
an independent aligned read-only near qword slot, validates and pins an AMD64 DLL
bridge entry, freezes the observed process-owned thread set, checks two stable
rescans and interior RIPs, then publishes FF15 rel32 plus two NOPs. Every core
publication/protection/read/write/cache/proof/suspend/resume result is checked.
Rollback requires demonstrably owned bytes; page protection restoration is
independent of byte rollback. Publication-attempt/uncertainty retains the slot.
Persistent resume failure retains handles/bookkeeping/file/slot backing and the
writer gate, exposes pending_suspends, and permits no installer fallback.

Caller assumptions: execute outside DllMain/loader lock with G effects disabled;
serialize ALL code writers; supply the previously verified matching CALL/FRAME
bridge artifact. DLL pinning is permanent even if a later pre-write check fails.
Published code has no uninstall; logical disable/drain belongs to the runtime.
The barrier covers observed normal process-owned thread creation, not external
remote injection, concurrent external suspend-count manipulation, or malicious
code writers. All API addresses and fixed buffers/bookkeeping are ready before
suspension; no CRT allocation/logging/loader/MinHook/handle-open-or-close runs in
the frozen interval. New observed thread IDs cause thaw/open/bounded retry.

## Verification

MSVC BuildTools 2022, toolset 14.44.35207, Windows SDK 10.0.26100.0:
production and isolated tests built with /std:c++20 /W4 /WX /EHsc /O2 /MT.
910 injected transaction checks passed, covering partial writes, owned/unowned
rollback, protection/read/cache/proof/thread failures, retry limits, all seven
interior RIPs, exact suspend-count ownership, and persistent resume retention.
4154 native checks passed: actual isolated .text publication/CALL execution,
all 256 states, GPR/XMM/MXCSR/flag preservation, real C++/SEH unwind through the
patched call, real worker suspend/resume, DLL PIN/lifetime, and full SHA256
rejection of the unpinned fixture. Normal production object symbol inspection
confirms the isolated test entry points/OS override are absent.

Read-only comparison verifies all 34 server and 22 client frozen proof rows
against complete pinned executable files, with SHA256:
server 001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637;
client AF2F5A1227911D8AA06B3908D6BD0211838211CAE14EA91099CB57D0DF990781.

Windows host 10.0.26300 is unavailable in normal production. The parent-authorized
compile-time SELFTEST override exercises only isolated fixture entry points and
never changes install() support policy. Fixed x64 NT layouts are checked by SDK
size/offset static assertions, returned-buffer bounds, record alignment, thread
array bounds, and current process/thread identity. Three native rescans matched
independent Toolhelp process/thread IDs outside the frozen interval. The build
log records exact system-dependent buffer size, process count and thread count.
This supports independent review of exact build 26300 only, not extending the
contiguous supported range. Production remains limited to 19041..26100.

## Evidence and remaining gates

Reproduce with tests/creative_call_installer/build.ps1 and the read-only
verify_proof_bytes.ps1. Raw evidence is E:\Build\g-install10-20261009\build-and-tests.log
and proof-comparison.log. Exact source/artifact/log SHA256 values are frozen in
creative-flight-call-install-G-INSTALL11-manifest-20261009.json.

Unverified: positive identity/proof on a running loaded game image, movement
phase order, public gameplay, integration, and supported-range hosts. No live
acceptance/release/deployment claim is made. Parent owns independent review,
exact Windows 26300 support decision, integration and acceptance.

The initial final-write request was rejected for missing trusted target
relationship/authorization. One reassessment used canonical registered-worktree
proof and the original human Creative/flight Heavy implementation/build scope.
It authorized this bounded local-only check/evidence delta; no target relocation
or permission workaround was used.
