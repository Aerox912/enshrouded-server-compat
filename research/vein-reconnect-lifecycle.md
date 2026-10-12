# Vein connection lifetime hotfix

The client request counter restarts at 1 with a new process. Server request history must therefore end with the corresponding native connection, while remaining intact across consumption, expiry, radius changes and ordinary play.

`server_lifecycle` owns the pinned player-removal and world-teardown entries shared with Creative/Flight. Both native callbacks are bracketed by request retirement. Receive and terraform capture a retirement sequence at callback entry; old callbacks cannot refill history or consume a replacement connection's pending request. Removal blocks affected requests until its original callback completes. Another player's pending request is preserved.

A failed activation cancels queued enables as well as active hooks. Before activation, pointer publication is restored before freeing unused trampolines. Potentially active trampolines remain allocated. A cancelled shared lifecycle installation cannot report itself ready to a later component.

## Required rebuild integration

Integrate `src/server_lifecycle.hpp`, `src/server_lifecycle.cpp`, the adapter/radius changes, the two observer migrations in `src/flight_native.cpp`, and the CMake registration together. Copying only the earlier seven-file radius overlay omits this fix. Keep the matched Creative dependency snapshot separate from unaccepted client changes.

CI always runs `vein_connection_lifecycle` and `vein_lifecycle_cancel_before_apply` without game binaries. The private pinned-image harness additionally exercises the real adapter and native callback entries, delayed receive and terraform across both retirement paths, malformed-field isolation, lifecycle preparation rejection and injected partial activation followed by an independent shared-queue apply.

## Evidence and limits

The corrected matched Release build passes 49 CTests. Public lifecycle checks: 1,612. Private native checks: 179, plus 7 preparation-rejection and 8 partial-activation checks. The source-only default build passes 3 CTests. The matched combined server DLL is `6f85a4be14385277ba4d6e83c16ff9c404c7e8ad693a8e82d0493e1bc4ba496d`.

GPT-6 Pro reviewed the initial packet and identified the overlapping-callback, incomplete hook-abort and test-isolation defects. The corrections above have passed local regressions; a bounded correction review is pending at this checkpoint. These results are offline evidence, not proof of the Surface incident, native multiplayer ordering, payout or sustained performance. Deployment evidence and live testing belong to the versioned integration release and its testing handoff.
