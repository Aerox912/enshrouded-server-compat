# APP167 Storage-only preview source bundle

This source-only package records the accepted Storage-only installer mode, its complete baseline and candidate scripts, the candidate reconstruction patch, and compact test evidence. It contains no DLLs, game files, manager payload, raw build logs, secrets, or live receipts.

## Candidate basis

The patch targets `research/ui43-shared-host-repair/storage-only-preview/candidate/magic-storage/`. That path is a carrier fixture for replaying this package. It is not an actual native mod source path and is not a live install directory. The patch reconstructs the accepted candidate from the bundled `baseline/magic-storage/` files. It does not apply directly to the current `preview-removal-retry` carrier tree.

To replay, create a disposable repository root, copy this package's `.gitattributes` to `research/ui43-shared-host-repair/storage-only-preview/.gitattributes`, copy `baseline/magic-storage/` to `research/ui43-shared-host-repair/storage-only-preview/candidate/magic-storage/`, then run from the disposable repository root:

```powershell
git -c core.autocrlf=true apply --check E:\Repos\enshrouded-server-compat\research\ui43-shared-host-repair\storage-only-preview\APP167.patch
git -c core.autocrlf=true apply E:\Repos\enshrouded-server-compat\research\ui43-shared-host-repair\storage-only-preview\APP167.patch
```

The candidate source, baseline, evidence, and patch have `-text` rules so Git's automatic line-ending conversion cannot alter the pinned bytes. `manifest.json` contains SHA-256 values for all baseline/candidate scripts and evidence, plus the generated patch hash. A disposable repository checkout with `core.autocrlf=true` preserved all 17 then-pinned files exactly; `.gitattributes` itself was CRLF-normalized as expected, and its `-text` rules remained active. The final package also includes that checkout report. A follow-up checkout of the final package passed with all 18 pinned payload files exact.

## Pins and verification

The package records the accepted Storage DLL and manifest pins, Creative pin `befee002983183009129efb76aff80dcb017f5933ea6da6b05aaa7d652ab6250`, and shared-host pin `b6d9e4757d4305bd9aba4325ebd31c3c4e2a31ac6931999f327fe3dbd22f6b46`. APP167 producer evidence includes 15 Storage-only checks, 26 full-preview checks, and four removal rollback fault cases. The parent-reported independent VERIFY170 result adds four supplemental gates; `evidence/verify170-summary.json` marks that summary as handoff-derived because its detailed verifier artifact was not supplied in the source bundle.

No live attach, game write, manager write, or Git delivery was performed as part of SOURCE175. Independent review remains required before any actual attach.