# INSTALL157 preview-removal retry source bundle

This carrier contains the accepted isolated installer rollback change, its source baseline, focused fixture tests, unified patch, and compact evidence/provenance. It contains no game binaries, manager payload, raw build logs, or secrets.

The installer repair moves marker creation into the guarded Remove transaction, retains the immediate game-closed check, restores and verifies package/config state after failures, removes only a newly created verified-empty marker, and preserves recovery data if rollback cannot complete. The tests use cloned fixtures and cover first-move failure, mid-transaction rollback, config replacement failure, successful retries, and retained recovery data on rollback failure.

Patch paths are relative to the native source root. Check/apply from that directory with autocrlf disabled for exact patch reconstruction:

    git -c core.autocrlf=false apply --check <path-to-INSTALL157-remove-rollback.patch>
    git -c core.autocrlf=false apply <path-to-INSTALL157-remove-rollback.patch>

The adjacent .gitattributes uses exact -text rules for byte-hashed source, patch, evidence, and provenance files so autocrlf=true cannot rewrite their stored bytes. Scratch verification applies the same patch with autocrlf=true and an output .gitattributes that marks the two output PowerShell paths -text.

Run the focused test from this directory with the supplied fixture path and an isolated output directory. Independent VERIFY155 reacceptance remains pending. No live game install/removal is part of this bundle.