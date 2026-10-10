# UI43 shared-host repair source bundle

This bundle contains the two source patches for the accepted local UI43 shared-host candidate. It contains no build products, game binaries/assets, staging fixtures, private allowlist files, or secrets. The accepted build and test evidence remains at the paths listed in `manifest.json`.

## Inputs and application

- Creative source must start at commit `8fa0ebb3b177f3182a6d78da21b265094046ffc5`. Apply `creative-ui43.patch` from the Creative source root.
- MagicStorage input is the `native/magic-storage` source directory. Its two files were untracked at repository `E:\Repos\enshrouded-mod-patches` HEAD `7dc8953df88e44fa6fe12a41b247e0475dad64b1`; therefore use the exact per-file baseline SHA-256 values in `manifest.json` before applying. Do not infer this baseline from the Git index.
- The carrier repository is `E:\Repos\enshrouded-server-compat`. The supplied server base `5f173c49875cc65c8d7903062dfc6ed904896758` exists. The observed carrier checkout was at `972b939c30ed6f004045a1ab368e4bf302971497` with unrelated pre-existing dirty files. The source patches are against the Creative and MagicStorage inputs above, not against the carrier repository's current checkout.

Apply only to isolated copies of those inputs. For Windows line-ending stability, disable Git's automatic CRLF conversion during apply. The MagicStorage patch has zero context to keep unrelated reviewed-hash rows out of the bundle, so Git requires `--unidiff-zero`:

```powershell
git -C $CreativeScratch -c core.autocrlf=false -c core.whitespace=cr-at-eol apply --check E:\Repos\enshrouded-server-compat\research\ui43-shared-host-repair\creative-ui43.patch
git -C $CreativeScratch -c core.autocrlf=false -c core.whitespace=cr-at-eol apply E:\Repos\enshrouded-server-compat\research\ui43-shared-host-repair\creative-ui43.patch

git -C $MagicStorageScratch -c core.autocrlf=false -c core.whitespace=cr-at-eol apply --unidiff-zero --check E:\Repos\enshrouded-server-compat\research\ui43-shared-host-repair\magic-storage-ui43.patch
git -C $MagicStorageScratch -c core.autocrlf=false -c core.whitespace=cr-at-eol apply --unidiff-zero E:\Repos\enshrouded-server-compat\research\ui43-shared-host-repair\magic-storage-ui43.patch
```

`manifest.json` records each input and accepted candidate file hash. Reconstructing the seven accepted files in isolated scratch copies matched all seven candidate hashes exactly. The same source and patch scan found no SteamID-shaped literal, private-key block, bearer token, or credential assignment.

## Existing verification and limits

The producer evidence reports Release builds, Creative CTest 15/15, Storage/host CTest 7/7, and 26 installer-fixture checks passed. The exact CTest log paths and SHA-256 hashes, candidate evidence hash, and external DLL/host hashes are in `manifest.json`; none of those binaries or fixtures are copied here. At that capture, the producer's named PE inspection scripts were not run because `pefile` was unavailable. Later, VERIFY126 independently matched four host sites. The producer evidence also predates APP127, which installed the accepted local trio and repaired the receipt; APP127 verified installed pins, six receipt files, and three originals. These later results are distinct from the source bundle capture. Hosted gameplay acceptance remains pending.