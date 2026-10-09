import hashlib
import json
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import release_prep


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_zip(path: Path, files: dict[str, bytes]) -> None:
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)


def temporary_directory() -> tempfile.TemporaryDirectory[str]:
    return tempfile.TemporaryDirectory(dir=Path(__file__).parent)


def candidate_fixture(root: Path) -> tuple[Path, str, str, str, dict[str, bytes]]:
    source = "1234567890abcdef1234567890abcdef12345678"
    version = "0.3.0-rc.test"
    files = {
        "README.txt": b"Native test candidate notice\n",
        "client/game/mods/creative_mode/creative_mode.dll": b"new creative dll",
        "client/game/mods/creative_mode/mod.json": b"{\"name\":\"creative\"}\n",
        "client/game/vmkeys.dll": b"new vmkeys",
        "server/creative/dbghelp.dll": b"new creative proxy",
        "server/flight/dbghelp.dll": b"separate flight proxy",
    }
    client_exe = b"supported client exe"
    server_exe = b"supported server exe"
    manifest = {
        "schema": 1,
        "version": version,
        "packageReady": False,
        "sourceCommit": source,
        "repository": "Aerox912/enshrouded-creative-mode",
        "runId": "12345",
        "dependencies": {
            "schema": 1,
            "supportedGame": {
                "clientSha256": digest(client_exe),
                "serverSha256": digest(server_exe),
            },
        },
        "files": [
            {"path": name, "size": len(data), "sha256": digest(data)}
            for name, data in sorted(files.items())
        ],
    }
    archive_path = root / "candidate.zip"
    write_zip(archive_path, {**files, "manifest.json": json.dumps(manifest).encode("utf-8")})
    return archive_path, digest(archive_path.read_bytes()), source, version, files


class ReleasePrepTests(unittest.TestCase):
    def test_verified_candidate_assembly_preserves_base_and_removes_stale_native_file(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            candidate, candidate_hash, source, version, new_files = candidate_fixture(root)
            old_native = {
                "game/mods/creative_mode/creative_mode.dll": b"old creative",
                "game/mods/flight_mod/flight_mod.dll": b"stale flight",
            }
            base_files = {
                "game/mods/XHL-Workshop-Speed-20x/config.txt": b"workshop config",
                "game/mods/XHL-Workshop-Speed-20x/mod.json": b"workshop metadata",
                "game/mods/XHL-Workshop-Speed-20x/src/mod.lua": b"workshop lua",
                "notices/Gameplay-components.txt": b"existing component notice",
                **old_native,
            }
            previous_native = {
                "schema": 1,
                "version": "0.3.0-rc.old",
                "files": [
                    {"path": f"client/{name}", "size": len(data), "sha256": digest(data)}
                    for name, data in old_native.items()
                ],
            }
            prior_archive = root / "prior.zip"
            write_zip(prior_archive, base_files)
            prior_lock = {
                "schema": 1,
                "repository": "Aerox912/enshrouded-client-installer",
                "tag": "old-candidate",
                "asset": "gameplay-inputs.zip",
                "size": prior_archive.stat().st_size,
                "sha256": digest(prior_archive.read_bytes()),
                "nativeGameplay": previous_native,
                "files": [
                    {"path": name, "size": len(data), "sha256": digest(data), "adminOnly": False}
                    for name, data in sorted(base_files.items())
                ],
            }
            prior_lock_path = root / "prior-lock.json"
            prior_lock_path.write_text(json.dumps(prior_lock), encoding="utf-8")
            dependencies = root / "dependencies.json"
            dependencies.write_text(json.dumps({"schema": 1, "version": "4.4.6", "releases": {"minimap": {"tag": "v1"}}}), encoding="utf-8")

            output = root / "assembled"
            report = release_prep.assemble_manager_inputs(
                candidate=candidate,
                candidate_sha256=candidate_hash,
                source_commit=source,
                artifact_version=version,
                prior_archive=prior_archive,
                prior_lock_path=prior_lock_path,
                dependencies_lock_path=dependencies,
                manager_version="4.4.7",
                manager_tag="candidate-test",
                output_dir=output,
            )

            merged = release_prep.read_zip_files(output / "gameplay-inputs.zip", "test output")
            self.assertEqual(merged["game/mods/XHL-Workshop-Speed-20x/config.txt"], b"workshop config")
            self.assertEqual(merged["game/mods/XHL-Workshop-Speed-20x/src/mod.lua"], b"workshop lua")
            self.assertEqual(merged["notices/Gameplay-components.txt"], b"existing component notice")
            self.assertEqual(merged["notices/Native-gameplay.txt"], new_files["README.txt"])
            self.assertEqual(merged["game/mods/creative_mode/creative_mode.dll"], new_files["client/game/mods/creative_mode/creative_mode.dll"])
            self.assertNotIn("game/mods/flight_mod/flight_mod.dll", merged)
            lock = json.loads((output / "gameplay.lock.json").read_text(encoding="utf-8"))
            self.assertEqual(lock["sha256"], digest((output / "gameplay-inputs.zip").read_bytes()))
            self.assertEqual({item["path"] for item in lock["files"]}, set(merged))
            self.assertTrue(next(item for item in lock["files"] if item["path"].endswith("creative_mode.dll"))["adminOnly"])
            deps = json.loads((output / "dependencies.lock.json").read_text(encoding="utf-8"))
            self.assertEqual(deps["version"], "4.4.7")
            self.assertFalse(report["promotionAuthorized"])
            self.assertFalse(report["candidate"]["packageReady"])

    def test_candidate_and_prior_archive_mismatches_fail_closed(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            candidate, candidate_hash, source, version, _ = candidate_fixture(root)
            with self.assertRaisesRegex(release_prep.PrepError, "Candidate archive SHA-256 mismatch"):
                release_prep.verify_candidate(candidate, "0" * 64, source, version)

            prior = root / "prior.zip"
            write_zip(prior, {"game/file.txt": b"actual"})
            lock = root / "prior.json"
            lock.write_text(json.dumps({
                "schema": 1,
                "size": prior.stat().st_size,
                "sha256": digest(prior.read_bytes()),
                "files": [{"path": "game/file.txt", "size": 6, "sha256": "0" * 64}],
            }), encoding="utf-8")
            with self.assertRaisesRegex(release_prep.PrepError, "lock size mismatch|lock SHA-256 mismatch"):
                release_prep.lock_file_map(release_prep.read_json(lock), release_prep.read_zip_files(prior, "prior"), "Prior manager")

    def test_local_install_plan_is_read_only_and_keeps_private_config_opaque(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            candidate, candidate_hash, source, version, files = candidate_fixture(root)
            client = root / "client"
            server = root / "server"
            client.mkdir()
            server.mkdir()
            (client / "enshrouded.exe").write_bytes(b"supported client exe")
            (server / "enshrouded_server.exe").write_bytes(b"supported server exe")
            (client / "mods/creative_mode").mkdir(parents=True)
            (client / "mods/creative_mode/creative_mode.dll").write_bytes(b"old client DLL")
            (client / "mods/creative_mode/mod.json").write_bytes(b"old client metadata")
            (client / "vmkeys.dll").write_bytes(b"old keys")
            (server / "dbghelp.dll").write_bytes(b"old server DLL")
            (server / "enshrouded_server.json").write_text('{"private":"never include this"}', encoding="utf-8")
            (server / "creative-allowlist.txt").write_text("private-user-id", encoding="utf-8")
            (server / "test-world").mkdir()
            (root / "rollback").mkdir()
            backup = root / "rollback/new-backup"
            output = root / "plans/local-plan.json"
            output.parent.mkdir()
            before = {str(path): digest(path.read_bytes()) for path in (
                client / "mods/creative_mode/creative_mode.dll",
                client / "mods/creative_mode/mod.json",
                client / "vmkeys.dll",
                server / "dbghelp.dll",
            )}

            plan = release_prep.make_local_install_plan(
                candidate=candidate,
                candidate_sha256=candidate_hash,
                source_commit=source,
                artifact_version=version,
                client_root=client,
                server_root=server,
                backup_root=backup,
                output=output,
            )

            self.assertEqual(plan["mode"], "plan-only")
            self.assertFalse(plan["executionPerformed"])
            self.assertTrue(plan["parentApprovalRequired"])
            self.assertEqual(len(plan["replacementFiles"]), 4)
            self.assertFalse(backup.exists())
            self.assertNotIn("never include this", output.read_text(encoding="utf-8"))
            self.assertNotIn("private-user-id", output.read_text(encoding="utf-8"))
            for path, original_hash in before.items():
                self.assertEqual(digest(Path(path).read_bytes()), original_hash)
            self.assertEqual(files["client/game/vmkeys.dll"], b"new vmkeys")

    def test_runner_pin_and_command_are_verified_against_source(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            runner = root / "emm.exe"
            runner.write_bytes(b"pinned tool bytes")
            pin = [{"Id": "eml-patcher", "Name": "EML 0.1.2 patcher", "Url": "https://example.invalid/emm.exe", "Hash": digest(runner.read_bytes()), "Size": runner.stat().st_size}]
            pins = root / "downloads.json"
            pins.write_text(json.dumps(pin), encoding="utf-8")
            source = root / "Installer.cs"
            source.write_text(
                r'''Safe(payload, "tools/emm.exe");
var psi = new ProcessStartInfo(exe, "run --patch --force -g \"" + Root + "\"") {
WorkingDirectory = Root;
Directory.CreateDirectory(Safe(Root, "export"));''',
                encoding="utf-8",
            )
            result = release_prep.verify_runner(runner, pins, source)
            self.assertIn('run --patch --force -g "<game-root>"', result["commandTemplate"])
            self.assertEqual(result["sha256"], digest(runner.read_bytes()))


if __name__ == "__main__":
    unittest.main()
