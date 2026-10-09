# G-INSTALL12-ROLLBACK frozen repair

Supersedes the G-INSTALL11 installer safety handoff. VERIFY49 showed that a
four-byte failed publication followed by failed make-writable rollback could
resume observed threads onto a mixed instruction. Retaining only the slot was
insufficient. This package repairs that path and adds native atomic publication.
Only assigned installer files, focused tests and this research evidence changed;
there is no runtime integration, shared CMake/Git change or game process patch.

## Production guarantee and recovery ownership

The generic transaction now requires durable caller-owned Recovery bookkeeping.
After EVERY path, fresh site proof must identify exactly the complete original
or complete installed eight bytes, with checked cache flush and restoration of
original protection after any attempted write. A final fresh read checks that
proof again after cache/protection operations and before any resume attempt.
Mixed/unknown bytes or unresolved cache/protection retain all remaining owned
suspends and handles. Result.recovery_required is independent of pending_suspends
so even a zero-peer unsafe site retains backing and the production writer gate.

The production context is reachable through retained preallocated state. The
original installer thread may call recover_retained() for ONE bounded attempt;
no new enumeration/open/suspend/CRT allocation/logging/loader operation occurs.
Recovery only overwrites a proven owned fragment, records a newly reported
partial recovery fragment, and preserves foreign bytes. It resumes only after
whole-site/cache/protection proof. Failed reads/protections/writes/cache or invalid
recovery callbacks retain ownership. Already discharged increments are never
repeated. Persistent failures intentionally keep the process quiesced; this is
an operational failure, not a usable installation or an automatic fallback.
The caller must not allocate/log/load/close handles/create threads during this
retained interval. No uninstall was introduced; published/uncertain slot and
bridge backing remain process-lifetime resources.

Native publication no longer uses WriteProcessMemory for executable bytes. Both
pinned sites have offset five in a 16-byte aligned block: server RVA187590/site
187595 and client RVA3A60A0/site3A60A5. Entire original blocks are frozen proofs
against the complete pinned files. Their exact bytes are identical:
90 00 00 00 EF 48 8B 45 38 0F B6 48 3D 83 F9 0A.
prepare_atomic checks CPUID leaf1 ECX bit13, offset/alignment, committed MEM_IMAGE
execute-read region bounds and same-page coverage BEFORE suspension. The desired
block replaces only offsets5..12 and preserves offsets0..4 and13..15. A checked
_InterlockedCompareExchange128 compares and stores the WHOLE block; rollback is
the reverse whole-block compare/exchange. Compare mismatch stores nothing; SEH
fault returns failure. No non-atomic executable-store fallback exists. Reads
used for release/recovery prove the entire native block, including every neighbor,
against original/installed blocks. Generic partial-write faults still retain
suspension ownership, even though normal native CAS publication cannot split.

Microsoft intrinsic contract: https://learn.microsoft.com/en-us/cpp/intrinsics/interlockedcompareexchange128
The production object disassembly contains lock cmpxchg16b. This is a physical
store guarantee under the checked hardware/mapping conditions, not a barrier
against external injection or malicious concurrent code writers.

## Frozen evidence

MSVC BuildTools2022 toolset14.44.35207 / Windows SDK10.0.26100.0, production and
selftest builds: /std:c++20 /W4 /WX /EHsc /O2 /MT. No warning/coverage guard was
weakened. Production object checks confirm selftest APIs/OS override are absent.

1277 injected transaction checks passed. The exact VERIFY49 combination returns
rollback_failure, original_restored=false, pending_suspends=2, resumed_threads=0,
remaining_frozen=2, RX restored and mixed site retained. The unchanged verifier
source's old vulnerability predicate is now false (expected exit3). New regression
then exercises failed recovery read, protection, partial recovery write and cache
flush, followed by successful proof-based recovery. Every resume callback in the
fault harness independently asserts complete bytes, restored protection and cache
proof. Zero-peer ownership and resume-only retry also have explicit regression.

4169 native checks passed: actual CAS128 forward/reverse publication; mismatch
at each of all eight neighboring bytes stores nothing; alignment rejection;
actual isolated CALL/256states/GPR/XMM/MXCSR/flags/C++ and SEH unwind, real observed
worker freeze/resume, read-only slot, DLL PIN/lifetime and full-file hash rejection.
The actual executable fixture uses offset five and verifies all neighbor bytes.

Read-only frozen proof comparison passes 35 server and 23 client rows against
complete pinned files. Hashes remain server
001C1B40ED091D8C1AEE583ADDE3800D7C858AE2C7F4DFF54FCA2938B2BE1637 and client
AF2F5A1227911D8AA06B3908D6BD0211838211CAE14EA91099CB57D0DF990781.
Host26300 NT snapshot: 1,696,080 bytes, 925 checked process records, six fixture
process threads, three rescans matching independent Toolhelp identities. Normal
production26300 remains unavailable; isolated SELFTEST override only. No support
range was widened.

Reproduce owned tests with tests/creative_call_installer/build.ps1 and read-only
verify_proof_bytes.ps1. Raw logs, tester-repro EXE/log and native codegen are in
E:\Build\g-install12-20261009. Exact ten source/test hashes and nine artifact/log
hashes are in creative-flight-call-install-G-INSTALL12-manifest-20261009.json.

## Parent acceptance and residual scope

Independent VERIFY49 review must recheck the repaired failure/recovery state
machine and atomic preconditions before production acceptance. Parent owns that
coordination; this executor did not modify the tester's source or contact peers.
No positive loaded-game proof, movement phase ordering, public gameplay,
supported-range-host validation, runtime integration, release or deployment was
performed. Caller must still supply the accepted matching CALL/FRAME DLL bridge,
execute outside loader lock with G effects disabled, serialize ALL code writers,
and preserve the retained frozen interval. Ordinary observed process-owned thread
creation is covered; remote injection and external suspend-count manipulation are
outside the contract. Exact26300 support remains a separate parent decision.
