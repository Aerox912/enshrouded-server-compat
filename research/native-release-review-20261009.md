# Native release review, 9 October 2026

The requested GPT-6 Pro review ran through ChatGPT Surface Control in the
Enshrouded project. Its initial recommendation was HOLD. The reviewed package
was Creative fd926de with server source 6f789d3. The package remains unready.

## Repairs after review

- Failed authentication for an absent peer no longer interprets handle zero as
  removal of every player. Bulk reset has a separate explicit operation.
- Shared identities carry a lifecycle serial issued by Sessions. Removing an
  owner and immediately observing the same native handles issues a new serial.
  Creative's queued requests, frames and actors compare the complete identity.
- Steam callbacks have synchronized state independent of the deletable mod
  object. Shutdown drains active invocations before clearing callback state.
  The small registered callback allocation is deliberately retained for the
  pinned DLL lifetime so a delayed dispatch cannot access freed storage.
- Successful client hook installation is reused across mod reloads. Creative
  menu hook installation is also idempotent for the same game image.
- Controller output packet numbers now describe the filtered gamepad state,
  including synthetic release when the menu opens while input remains held.

The complete native build passed on Windows. All ten registered CTest targets
passed, including concurrency, same-handle lifecycle and two-peer revocation
regressions. The legacy menu harness also passed all twelve fixture checks.
This is source and harness evidence; loader reload and held-input gameplay on
the exact new package still require acceptance.

Before these repairs, the GitHub native test artifact from run 37853907787
passed 585 checks in Wine 10.0. That includes the legacy menu fixture and a
mapped hook test against the supported server executable. It is not an actual
Wine game-server multiplayer session, and does not validate later binaries.

## Remaining release evidence

1. Verify the native transaction ABI/completion semantics and observe partial-fit
   rollback, customized gear, and save/reconnect persistence.
2. Exercise actual death/respawn and queued grants on rapid same-owner reuse.
   The serial fixes reset invalidation, but does not prove native alive/dead
   observation or simulation-thread ordering at commit.
3. Exercise approved and unapproved clients together, including revocation.
4. Run the exact package in the intended Wine server runtime through loading,
   authenticated transport, hooks, gameplay, shutdown and restart.
5. Verify matching client delivery, fresh empty-server evidence, full rollback
   backups, installed hashes and healthy startup before each hosted deployment.

No new native package has been deployed to Enshrouded, IKEA or Soulrend. Their
existing Workshop 20x baseline remains in place. Public manager publication is
also held. G free flight and the other unimplemented Creative features have
not been promoted to completed functionality.
