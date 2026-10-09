#!/usr/bin/env python3
"""Fail-closed preparation for local Enshrouded gameplay candidates.

This tool only verifies and assembles files under an explicitly supplied output
directory. Its plan-local-test subcommand emits a plan; it never copies files into a
game/server tree, starts processes, or publishes anything.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import tempfile
import zipfile
from pathlib import Path, PurePosixPath
from typing import Any


class PrepError(ValueError):
    """An input failed a release-preparation invariant."""


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def read_json(path: Path) -> dict[str, Any]:
    try:
        value = json.loads(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PrepError(f"Could not read JSON input: {path}") from error
    if not isinstance(value, dict):
        raise PrepError(f"Expected a JSON object: {path}")
    return value


def require_sha256(value: Any, label: str) -> str:
    if not isinstance(value, str) or len(value) != 64:
        raise PrepError(f"{label} must be a 64-character SHA-256 hex digest")
    try:
        int(value, 16)
    except ValueError as error:
        raise PrepError(f"{label} must be hexadecimal") from error
    return value.lower()


GEM_FORGE_CLIENT_PATHS = {
    "client/game/mods/creative-gem-forges/mod.json",
    "client/game/mods/creative-gem-forges/src/mod.lua",
    "client/game/mods/creative-gem-forges/src/forge_data.lua",
}
GEM_FORGE_SERVER_PATHS = {name.replace("client/game/", "server/game/", 1) for name in GEM_FORGE_CLIENT_PATHS}
GEM_FORGE_CANDIDATE_PATHS = GEM_FORGE_CLIENT_PATHS | GEM_FORGE_SERVER_PATHS


def validate_gem_forge_candidate(
    manifest: dict[str, Any],
    archive_files: dict[str, bytes],
    declared_files: dict[str, dict[str, Any]],
) -> bool:
    """Validate the optional exact shared EML module shipped to both game roots."""
    module_paths = {
        name for name in archive_files
        if name.startswith(("client/game/mods/creative-gem-forges/", "server/game/mods/creative-gem-forges/"))
    }
    if not module_paths:
        if manifest.get("sharedModules") is not None:
            raise PrepError("Shared module metadata is present without Gem Forge files")
        return False
    if module_paths != GEM_FORGE_CANDIDATE_PATHS:
        missing = sorted(GEM_FORGE_CANDIDATE_PATHS - module_paths)
        extra = sorted(module_paths - GEM_FORGE_CANDIDATE_PATHS)
        raise PrepError(f"Gem Forge candidate inventory mismatch; missing={missing}, extra={extra}")

    shared_modules = manifest.get("sharedModules")
    if not isinstance(shared_modules, dict) or set(shared_modules) != {"creativeGemForges"}:
        raise PrepError("Gem Forge files require the pinned sharedModules manifest")
    module = shared_modules["creativeGemForges"]
    if not isinstance(module, dict) or type(module.get("schema")) is not int or module["schema"] != 1 or module.get("moduleId") != "creative-gem-forges":
        raise PrepError("Gem Forge shared module metadata is invalid")
    if not isinstance(module.get("moduleVersion"), str) or not module["moduleVersion"].strip():
        raise PrepError("Gem Forge module version is missing")
    require_sha256(module.get("sourceItemsJsonSha256"), "Gem Forge source items hash")
    require_sha256(module.get("forgeMetadataSha256"), "Gem Forge source metadata hash")
    targets = module.get("intendedTargets")
    if not isinstance(targets, dict) or targets.get("clientEditions") != ["Admin", "Regular"] or targets.get("serverProfiles") != ["normal", "cheeze"]:
        raise PrepError("Gem Forge shared module targets are invalid")

    nested = module.get("files")
    if not isinstance(nested, list):
        raise PrepError("Gem Forge shared module manifest has no files array")
    nested_by_path: dict[str, dict[str, Any]] = {}
    for entry in nested:
        if not isinstance(entry, dict) or not isinstance(entry.get("path"), str) or entry["path"] in nested_by_path:
            raise PrepError("Gem Forge shared module manifest contains a malformed or duplicate file")
        nested_by_path[entry["path"]] = entry
    if set(nested_by_path) != GEM_FORGE_CANDIDATE_PATHS:
        raise PrepError("Gem Forge shared module manifest does not list the exact six files")

    for name in sorted(GEM_FORGE_CANDIDATE_PATHS):
        record = declared_files.get(name)
        nested_record = nested_by_path[name]
        if not isinstance(record, dict) or record.get("adminOnly") is not False or record.get("audience") != "shared":
            raise PrepError(f"Gem Forge file must be declared shared and available to both editions: {name}")
        if nested_record != record:
            raise PrepError(f"Gem Forge component manifest differs from candidate manifest: {name}")

    for client_name in sorted(GEM_FORGE_CLIENT_PATHS):
        server_name = client_name.replace("client/game/", "server/game/", 1)
        client_record = declared_files[client_name]
        server_record = declared_files[server_name]
        if (client_record.get("size"), client_record.get("sha256")) != (server_record.get("size"), server_record.get("sha256")):
            raise PrepError(f"Gem Forge client/server file hashes differ: {client_name}")
        if archive_files[client_name] != archive_files[server_name]:
            raise PrepError(f"Gem Forge client/server file bytes differ: {client_name}")
    return True


def safe_archive_path(name: str) -> str:
    if not isinstance(name, str) or not name or "\\" in name or name.startswith("/"):
        raise PrepError(f"Unsafe archive path: {name!r}")
    raw_parts = name.split("/")
    if any(part in ("", ".", "..") or ":" in part for part in raw_parts):
        raise PrepError(f"Unsafe archive path: {name!r}")
    path = PurePosixPath(name)
    if any(part in ("", ".", "..") for part in path.parts):
        raise PrepError(f"Unsafe archive path: {name!r}")
    return path.as_posix()


def read_zip_files(path: Path, label: str) -> dict[str, bytes]:
    try:
        with zipfile.ZipFile(path) as archive:
            files: dict[str, bytes] = {}
            names: set[str] = set()
            folded: set[str] = set()
            for info in archive.infolist():
                name = safe_archive_path(info.filename.rstrip("/")) if info.is_dir() else safe_archive_path(info.filename)
                if info.is_dir():
                    continue
                if name in names or name.casefold() in folded:
                    raise PrepError(f"{label} has duplicate or case-colliding path: {name}")
                names.add(name)
                folded.add(name.casefold())
                mode = info.external_attr >> 16
                if stat.S_ISLNK(mode):
                    raise PrepError(f"{label} contains a symbolic link: {name}")
                try:
                    files[name] = archive.read(info)
                except (OSError, zipfile.BadZipFile, RuntimeError) as error:
                    raise PrepError(f"Could not read {label} member: {name}") from error
    except (OSError, zipfile.BadZipFile) as error:
        raise PrepError(f"Could not open {label}: {path}") from error
    return files


def verify_candidate(
    archive_path: Path,
    expected_sha256: str,
    expected_source_commit: str,
    artifact_version: str,
) -> tuple[dict[str, Any], dict[str, bytes], str]:
    expected_sha256 = require_sha256(expected_sha256, "Expected candidate SHA-256")
    actual_sha256 = sha256_file(archive_path)
    if actual_sha256 != expected_sha256:
        raise PrepError(f"Candidate archive SHA-256 mismatch: {actual_sha256}")

    files = read_zip_files(archive_path, "candidate archive")
    raw_manifest = files.get("manifest.json")
    if raw_manifest is None:
        raise PrepError("Candidate archive has no manifest.json")
    try:
        manifest = json.loads(raw_manifest.decode("utf-8-sig"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise PrepError("Candidate manifest is not valid UTF-8 JSON") from error
    if not isinstance(manifest, dict) or manifest.get("schema") != 1:
        raise PrepError("Candidate manifest schema is unsupported")
    if manifest.get("sourceCommit") != expected_source_commit:
        raise PrepError("Candidate source commit does not match the supplied pin")
    if manifest.get("version") != artifact_version:
        raise PrepError("Candidate version does not match --artifact-version")
    if manifest.get("repository") != "Aerox912/enshrouded-creative-mode":
        raise PrepError("Candidate repository is not the expected Creative source")
    if not isinstance(manifest.get("packageReady"), bool):
        raise PrepError("Candidate manifest must declare packageReady")

    entries = manifest.get("files")
    if not isinstance(entries, list):
        raise PrepError("Candidate manifest files must be an array")
    declared: dict[str, dict[str, Any]] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            raise PrepError("Candidate manifest contains a malformed file record")
        name = safe_archive_path(entry.get("path"))
        if name == "manifest.json" or name in declared:
            raise PrepError(f"Candidate manifest duplicates or self-lists {name}")
        declared[name] = entry
    archive_files = set(files) - {"manifest.json"}
    if archive_files != set(declared):
        missing = sorted(set(declared) - archive_files)
        unlisted = sorted(archive_files - set(declared))
        raise PrepError(f"Candidate file inventory mismatch; missing={missing}, unlisted={unlisted}")
    for name, entry in declared.items():
        data = files[name]
        if entry.get("size") != len(data):
            raise PrepError(f"Candidate member size mismatch: {name}")
        expected = require_sha256(entry.get("sha256"), f"Candidate member hash for {name}")
        if sha256_bytes(data) != expected:
            raise PrepError(f"Candidate member SHA-256 mismatch: {name}")
    validate_gem_forge_candidate(manifest, files, declared)

    dependencies = manifest.get("dependencies")
    if not isinstance(dependencies, dict):
        raise PrepError("Candidate dependencies are missing")
    supported_game = dependencies.get("supportedGame")
    if not isinstance(supported_game, dict):
        raise PrepError("Candidate supported-game pin is missing")
    require_sha256(supported_game.get("clientSha256"), "Supported client SHA-256")
    require_sha256(supported_game.get("serverSha256"), "Supported server SHA-256")
    if "README.txt" not in files:
        raise PrepError("Candidate archive has no README.txt notice source")
    return manifest, files, actual_sha256


def lock_file_map(lock: dict[str, Any], archive_files: dict[str, bytes], label: str) -> None:
    records = lock.get("files")
    if not isinstance(records, list):
        raise PrepError(f"{label} lock has no files array")
    declared: dict[str, dict[str, Any]] = {}
    for record in records:
        if not isinstance(record, dict):
            raise PrepError(f"{label} lock contains a malformed file record")
        name = safe_archive_path(record.get("path"))
        if name in declared:
            raise PrepError(f"{label} lock duplicates {name}")
        declared[name] = record
    if set(declared) != set(archive_files):
        missing = sorted(set(declared) - set(archive_files))
        extra = sorted(set(archive_files) - set(declared))
        raise PrepError(f"{label} lock/archive inventory mismatch; missing={missing}, extra={extra}")
    for name, record in declared.items():
        data = archive_files[name]
        if record.get("size") != len(data):
            raise PrepError(f"{label} lock size mismatch: {name}")
        if sha256_bytes(data) != require_sha256(record.get("sha256"), f"{label} lock hash for {name}"):
            raise PrepError(f"{label} lock SHA-256 mismatch: {name}")


def deterministic_zip(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            safe_archive_path(name)
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (stat.S_IFREG | 0o644) << 16
            archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def write_new_output(output_dir: Path, payloads: dict[str, bytes]) -> None:
    output_dir = output_dir.absolute()
    if output_dir.exists():
        raise PrepError(f"Output directory already exists; refusing to replace it: {output_dir}")
    if not output_dir.parent.is_dir():
        raise PrepError(f"Output parent directory must already exist: {output_dir.parent}")
    temporary = Path(tempfile.mkdtemp(prefix=f".{output_dir.name}.tmp-", dir=output_dir.parent))
    try:
        for name, data in payloads.items():
            if Path(name).name != name:
                raise PrepError(f"Output filename must be a simple name: {name}")
            (temporary / name).write_bytes(data)
        if output_dir.exists():
            raise PrepError(f"Output directory appeared while preparing: {output_dir}")
        os.rename(temporary, output_dir)
    except BaseException:
        if temporary.exists() and temporary.parent == output_dir.parent and temporary.name.startswith(f".{output_dir.name}.tmp-"):
            shutil.rmtree(temporary)
        raise


def assemble_manager_inputs(
    *,
    candidate: Path,
    candidate_sha256: str,
    source_commit: str,
    artifact_version: str,
    prior_archive: Path,
    prior_lock_path: Path,
    dependencies_lock_path: Path,
    manager_version: str,
    manager_tag: str,
    output_dir: Path,
    components_notice: Path | None = None,
) -> dict[str, Any]:
    manifest, candidate_files, candidate_digest = verify_candidate(
        candidate, candidate_sha256, source_commit, artifact_version
    )
    prior_lock = read_json(prior_lock_path)
    if prior_lock.get("schema") != 1 or not isinstance(prior_lock.get("nativeGameplay"), dict):
        raise PrepError("Prior manager lock lacks the expected nativeGameplay record")
    prior_files = read_zip_files(prior_archive, "prior manager input archive")
    if prior_lock.get("size") != prior_archive.stat().st_size:
        raise PrepError("Prior manager archive size does not match its lock")
    prior_archive_sha = require_sha256(prior_lock.get("sha256"), "Prior manager archive hash")
    if sha256_file(prior_archive) != prior_archive_sha:
        raise PrepError("Prior manager archive SHA-256 does not match its lock")
    lock_file_map(prior_lock, prior_files, "Prior manager")

    merged = dict(prior_files)
    previous_manifest = prior_lock["nativeGameplay"]
    previous_native_files = previous_manifest.get("files")
    if not isinstance(previous_native_files, list):
        raise PrepError("Prior nativeGameplay manifest has no files array")
    removed_native: set[str] = set()
    for record in previous_native_files:
        if not isinstance(record, dict) or not isinstance(record.get("path"), str):
            raise PrepError("Prior nativeGameplay manifest contains a malformed file record")
        path = record["path"]
        if not path.startswith("client/game/"):
            continue
        manager_path = safe_archive_path(path[len("client/"):])
        data = merged.get(manager_path)
        if data is None:
            raise PrepError(f"Prior native client file is missing from manager archive: {manager_path}")
        if len(data) != record.get("size") or sha256_bytes(data) != require_sha256(record.get("sha256"), f"Prior native file hash for {path}"):
            raise PrepError(f"Prior native client file does not match its manifest: {manager_path}")
        removed_native.add(manager_path)
    for path in removed_native:
        del merged[path]

    native_client_files: dict[str, bytes] = {}
    for candidate_path, data in candidate_files.items():
        if candidate_path == "manifest.json" or candidate_path == "README.txt":
            continue
        if not candidate_path.startswith("client/game/"):
            continue
        manager_path = safe_archive_path(candidate_path[len("client/"):])
        if manager_path in merged:
            raise PrepError(f"Candidate would overwrite a non-native prior input: {manager_path}")
        native_client_files[manager_path] = data
    if not native_client_files:
        raise PrepError("Candidate has no client/game files to add to manager inputs")
    merged.update(native_client_files)
    merged["notices/Native-gameplay.txt"] = candidate_files["README.txt"]
    if "notices/Gameplay-components.txt" not in merged:
        if components_notice is None or not components_notice.is_file():
            raise PrepError("Prior inputs have no Gameplay-components notice; supply --components-notice")
        merged["notices/Gameplay-components.txt"] = components_notice.read_bytes()

    deps = read_json(dependencies_lock_path)
    if deps.get("schema") != 1 or not isinstance(deps.get("releases"), dict):
        raise PrepError("Dependencies lock schema is unsupported")
    deps["version"] = manager_version

    output_dir = output_dir.absolute()
    if output_dir.exists() or not output_dir.parent.is_dir():
        raise PrepError("Output directory must not exist and its parent must already exist")
    archive_fd, archive_name = tempfile.mkstemp(prefix="gameplay-inputs-", suffix=".zip", dir=output_dir.parent)
    os.close(archive_fd)
    archive_temp = Path(archive_name)
    try:
        deterministic_zip(archive_temp, merged)
        archive_data = archive_temp.read_bytes()
    finally:
        archive_temp.unlink(missing_ok=True)

    native_lock_files = [
        {
            "path": name,
            "size": len(data),
            "sha256": sha256_bytes(data),
            "adminOnly": name.startswith(("game/mods/creative_mode/", "game/mods/flight_mod/")),
        }
        for name, data in sorted(merged.items())
    ]
    generated_lock = {
        "schema": 1,
        "repository": prior_lock.get("repository"),
        "tag": manager_tag,
        "asset": prior_lock.get("asset", "gameplay-inputs.zip"),
        "size": len(archive_data),
        "sha256": sha256_bytes(archive_data),
        "nativeGameplay": manifest,
        "files": native_lock_files,
    }
    report = {
        "schema": 1,
        "taskId": "REL-2",
        "candidate": {
            "path": str(candidate.absolute()),
            "sha256": candidate_digest,
            "sourceCommit": manifest["sourceCommit"],
            "version": manifest["version"],
            "runId": manifest.get("runId"),
            "packageReady": manifest["packageReady"],
        },
        "priorManagerInputs": {
            "path": str(prior_archive.absolute()),
            "sha256": prior_archive_sha,
            "preservedFiles": len(prior_files) - len(removed_native),
            "removedSupersededNativeClientFiles": sorted(removed_native),
        },
        "output": {
            "managerVersion": manager_version,
            "managerTag": manager_tag,
            "archiveSha256": generated_lock["sha256"],
            "archiveSize": generated_lock["size"],
            "fileCount": len(merged),
        },
        "promotionAuthorized": False,
    }
    output_payloads = {
        "gameplay-inputs.zip": archive_data,
        "gameplay.lock.json": (json.dumps(generated_lock, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
        "dependencies.lock.json": (json.dumps(deps, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
        "assembly-report.json": (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode("utf-8"),
    }
    write_new_output(output_dir, output_payloads)
    return report


def verify_runner(runner_path: Path, downloads_path: Path, installer_source: Path) -> dict[str, Any]:
    try:
        downloads = json.loads(downloads_path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise PrepError(f"Could not read runner pin file: {downloads_path}") from error
    if not isinstance(downloads, list):
        raise PrepError("Runner pin file must be a JSON array")
    matches = [entry for entry in downloads if isinstance(entry, dict) and entry.get("Id") == "eml-patcher"]
    if len(matches) != 1:
        raise PrepError("Runner pin file must contain exactly one eml-patcher entry")
    pin = matches[0]
    expected_sha = require_sha256(pin.get("Hash"), "Pinned EML/EMM runner hash")
    if pin.get("Size") != runner_path.stat().st_size or sha256_file(runner_path) != expected_sha:
        raise PrepError("Local EML/EMM runner does not match downloads.json")
    source = installer_source.read_text(encoding="utf-8-sig")
    if not all(fragment in source for fragment in (
        'Safe(payload, "tools/emm.exe")',
        "new ProcessStartInfo(exe,",
        "run --patch --force -g",
        '" + Root + "',
        "WorkingDirectory = Root",
        'Directory.CreateDirectory(Safe(Root, "export"))',
    )):
        raise PrepError("Installer source no longer matches the verified EMM invocation")
    return {
        "name": pin.get("Name"),
        "path": str(runner_path.absolute()),
        "size": runner_path.stat().st_size,
        "sha256": expected_sha,
        "sourceUrl": pin.get("Url"),
        "installerSource": str(installer_source.absolute()),
        "installerSourceSha256": sha256_file(installer_source),
        "commandTemplate": f'"{runner_path.absolute()}" run --patch --force -g "<game-root>"',
        "workingDirectory": "<game-root>",
        "precondition": "Create <game-root>\\export before invoking; use a clean matched game/server baseline and selected mods.",
    }


def make_local_install_plan(
    *,
    candidate: Path,
    candidate_sha256: str,
    source_commit: str,
    artifact_version: str,
    client_root: Path,
    server_root: Path,
    backup_root: Path,
    output: Path,
) -> dict[str, Any]:
    manifest, candidate_files, digest = verify_candidate(candidate, candidate_sha256, source_commit, artifact_version)
    client_root = client_root.resolve(strict=True)
    server_root = server_root.resolve(strict=True)
    backup_root = backup_root.absolute()
    output = output.absolute()
    if client_root == server_root or client_root in server_root.parents or server_root in client_root.parents:
        raise PrepError("Client and test-server roots must be separate trees")
    if not backup_root.parent.is_dir() or backup_root.exists():
        raise PrepError("Rollback root parent must exist and rollback root must not exist")
    if client_root in backup_root.parents or server_root in backup_root.parents:
        raise PrepError("Rollback root must be outside both active install roots")
    if backup_root in client_root.parents or backup_root in server_root.parents:
        raise PrepError("Rollback root must not contain either active install root")
    if not output.parent.is_dir() or output.exists():
        raise PrepError("Plan output parent must exist and output path must not exist")
    if output == client_root or output == server_root or client_root in output.parents or server_root in output.parents:
        raise PrepError("Plan output must be outside both active install roots")
    if output == backup_root or backup_root in output.parents or output in backup_root.parents:
        raise PrepError("Plan output and rollback root must be separate")

    dependencies = manifest["dependencies"]["supportedGame"]
    for root, executable, field in (
        (client_root, "enshrouded.exe", "clientSha256"),
        (server_root, "enshrouded_server.exe", "serverSha256"),
    ):
        path = root / executable
        if not path.is_file() or sha256_file(path) != dependencies[field].lower():
            raise PrepError(f"Supported game executable mismatch: {path}")

    replacements: list[tuple[str, str, bool]] = [
        ("client/game/mods/creative_mode/creative_mode.dll", "mods/creative_mode/creative_mode.dll", False),
        ("client/game/mods/creative_mode/mod.json", "mods/creative_mode/mod.json", False),
        ("client/game/vmkeys.dll", "vmkeys.dll", False),
        ("server/creative/dbghelp.dll", "dbghelp.dll", False),
    ]
    if any(name in candidate_files for name in GEM_FORGE_CANDIDATE_PATHS):
        replacements.extend(
            (name, name.split("/game/", 1)[1], True)
            for name in sorted(GEM_FORGE_CANDIDATE_PATHS)
        )
    planned_files = []
    for archive_path, relative_target, optional_target in replacements:
        data = candidate_files.get(archive_path)
        if data is None:
            raise PrepError(f"Candidate lacks required test replacement: {archive_path}")
        root = server_root if archive_path.startswith("server/") else client_root
        target = root / PurePosixPath(relative_target)
        before_exists = target.exists()
        if target.is_symlink() or (before_exists and not target.is_file()):
            raise PrepError(f"Replacement target is not a regular file: {target}")
        if not optional_target and not before_exists:
            raise PrepError(f"Replacement target is missing: {target}")
        planned_files.append({
            "archivePath": archive_path,
            "target": str(target),
            "beforeExists": before_exists,
            "beforeSha256": sha256_file(target) if before_exists else None,
            "candidateSha256": sha256_bytes(data),
            "candidateSize": len(data),
            "rollbackAction": "restore-original" if before_exists else "remove-candidate-file",
        })

    flight_client = client_root / "mods" / "flight_mod" / "flight_mod.dll"
    if flight_client.exists():
        raise PrepError("Standalone Flight client mod is present; combined Creative test requires it disabled")
    required_server_paths = ["enshrouded_server.json", "creative-allowlist.txt", "test-world"]
    optional_server_paths = ["flight-allowlist.cfg"]
    opaque_backups = []
    for relative in required_server_paths:
        path = server_root / relative
        if not path.exists():
            raise PrepError(f"Required server rollback input is missing: {path}")
        opaque_backups.append({"path": relative, "type": "directory" if path.is_dir() else "opaque-file"})
    for relative in optional_server_paths:
        path = server_root / relative
        if path.exists():
            opaque_backups.append({"path": relative, "type": "opaque-file"})

    plan = {
        "schema": 1,
        "taskId": "REL-2",
        "mode": "plan-only",
        "candidate": {
            "path": str(candidate.absolute()),
            "sha256": digest,
            "sourceCommit": manifest["sourceCommit"],
            "version": manifest["version"],
            "runId": manifest.get("runId"),
            "packageReady": manifest["packageReady"],
        },
        "supportedGame": dependencies,
        "clientRoot": str(client_root),
        "testServerRoot": str(server_root),
        "rollbackRoot": str(backup_root),
        "replacementFiles": planned_files,
        "opaqueServerBackups": opaque_backups,
        "excludedCandidateFiles": [
            name for name in sorted(candidate_files)
            if name.startswith(("client/game/mods/flight_mod/", "server/flight/", "server/creative-allowlist", "server/flight-allowlist"))
        ],
        "gates": [
            "Obtain parent authorization before applying any replacement.",
            "Close the game and test server, then confirm no test-server listener remains.",
            "Reverify both executable hashes and every beforeSha256 immediately before backup.",
            "Create a new rollback root; back up every existing replacement target, server settings, allowlists, and the entire test-world; record absent replacement targets so rollback removes only files created by this candidate; verify backups before replacement.",
            "Install only the candidate files listed in replacementFiles; keep standalone Flight disabled.",
            "Capture startup and gameplay evidence; restore from the verified backup after acceptance or failure.",
            "Keep the separate server on port 15637 untouched and do not expose this test to public networks.",
        ],
        "executionPerformed": False,
        "parentApprovalRequired": True,
    }
    output_data = (json.dumps(plan, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    output_fd, output_name = tempfile.mkstemp(prefix=f".{output.name}.tmp-", dir=output.parent)
    os.close(output_fd)
    temporary = Path(output_name)
    try:
        temporary.write_bytes(output_data)
        if output.exists():
            raise PrepError(f"Plan output appeared while preparing: {output}")
        os.rename(temporary, output)
    except BaseException:
        if temporary.exists() and temporary.parent == output.parent and temporary.name.startswith(f".{output.name}.tmp-"):
            temporary.unlink()
        raise
    return plan


def add_candidate_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--candidate", required=True, type=Path)
    parser.add_argument("--candidate-sha256", required=True)
    parser.add_argument("--source-commit", required=True)
    parser.add_argument("--artifact-version", required=True)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    runner = commands.add_parser("verify-runner", help="verify the pinned local EML/EMM patcher and print its invocation")
    runner.add_argument("--runner", required=True, type=Path)
    runner.add_argument("--downloads-json", required=True, type=Path)
    runner.add_argument("--installer-source", required=True, type=Path)

    assemble = commands.add_parser("assemble-inputs", help="merge a pinned candidate over an exactly matched prior manager input archive")
    add_candidate_arguments(assemble)
    assemble.add_argument("--prior-archive", required=True, type=Path)
    assemble.add_argument("--prior-lock", required=True, type=Path)
    assemble.add_argument("--dependencies-lock", required=True, type=Path)
    assemble.add_argument("--manager-version", required=True)
    assemble.add_argument("--manager-tag", required=True)
    assemble.add_argument("--components-notice", type=Path)
    assemble.add_argument("--output-dir", required=True, type=Path)

    plan = commands.add_parser("plan-local-test", help="write a backed-up local-test plan without changing any target")
    add_candidate_arguments(plan)
    plan.add_argument("--client-root", required=True, type=Path)
    plan.add_argument("--server-root", required=True, type=Path)
    plan.add_argument("--rollback-root", required=True, type=Path)
    plan.add_argument("--output", required=True, type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        if args.command == "verify-runner":
            result = verify_runner(args.runner, args.downloads_json, args.installer_source)
        elif args.command == "assemble-inputs":
            result = assemble_manager_inputs(
                candidate=args.candidate,
                candidate_sha256=args.candidate_sha256,
                source_commit=args.source_commit,
                artifact_version=args.artifact_version,
                prior_archive=args.prior_archive,
                prior_lock_path=args.prior_lock,
                dependencies_lock_path=args.dependencies_lock,
                manager_version=args.manager_version,
                manager_tag=args.manager_tag,
                output_dir=args.output_dir,
                components_notice=args.components_notice,
            )
        else:
            result = make_local_install_plan(
                candidate=args.candidate,
                candidate_sha256=args.candidate_sha256,
                source_commit=args.source_commit,
                artifact_version=args.artifact_version,
                client_root=args.client_root,
                server_root=args.server_root,
                backup_root=args.rollback_root,
                output=args.output,
            )
    except (PrepError, OSError) as error:
        parser_error = argparse.ArgumentParser(prog="release_prep.py")
        parser_error.error(str(error))
        return 2
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
