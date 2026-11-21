import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import urllib.request
import zipfile
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "buildtool"))
import bootstrap_linux
import drakan_update_linux as updater
import prepare_launch


def builder_zip(path, entries):
    with zipfile.ZipFile(path, "w") as archive:
        for name, contents in entries.items():
            archive.writestr(name, contents)


class BuilderReleaseTests(unittest.TestCase):
    def test_bootstrap_only_checks_releases_when_enabled_in_the_play_folder(self):
        with tempfile.TemporaryDirectory() as temporary:
            play = Path(temporary)
            iso = play / "Drakan.iso"
            iso.touch()
            with (
                patch.object(sys, "argv", ["bootstrap_linux.py", str(iso), "--play-dir", str(play)]),
                patch.object(sys, "platform", "linux"),
                patch.object(bootstrap_linux.platform, "machine", return_value="x86_64"),
                patch.dict(os.environ, {"DRAKAN_CHECK_FOR_UPDATES": ""}),
                patch.object(bootstrap_linux, "dependencies"),
                patch.object(bootstrap_linux, "tools", return_value=("pwsh", "python", "node")),
                patch.object(bootstrap_linux, "run"),
                patch.object(bootstrap_linux, "builder_release_status", return_value=None) as check_release,
            ):
                bootstrap_linux.main()
                ini = play / "drakan.ini"
                ini.write_text("[general]\ncheck_for_updates = 0\n", encoding="utf-8")
                bootstrap_linux.main()
                check_release.assert_not_called()
                ini.write_text("[general]\ncheck_for_updates = 1\n", encoding="utf-8")
                bootstrap_linux.main()
                check_release.assert_called_once_with(bootstrap_linux.ROOT)

    def test_checks_only_versioned_builders_and_compares_stable_tags_numerically(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            marker = folder / "DrakanPC-release.txt"
            with patch.object(updater, "latest_release", return_value="v0.1.10") as latest:
                self.assertIsNone(updater.builder_release_status(folder))
                marker.write_text("v0.1.9-preview\n", encoding="ascii")
                self.assertIsNone(updater.builder_release_status(folder))
                latest.assert_not_called()
                for version, state in (("v0.1.9", "behind"), ("v0.1.10", "current"), ("v0.2.0", "ahead")):
                    with self.subTest(version=version):
                        marker.write_text(version + "\n", encoding="ascii")
                        self.assertEqual(
                            updater.builder_release_status(folder),
                            {
                                "state": state,
                                "installed": version,
                                "latest": "v0.1.10",
                                "url": f"{updater.RELEASES}/tag/v0.1.10",
                            },
                        )
                        latest.assert_called_with(timeout=3)

    def test_unavailable_release_check_preserves_the_installed_version(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            (folder / "DrakanPC-release.txt").write_text("v0.1.9\n", encoding="ascii")
            with patch.object(updater, "latest_release", side_effect=TimeoutError):
                status = updater.builder_release_status(folder)
            self.assertEqual(status, {"state": "offline", "installed": "v0.1.9", "latest": None, "url": None})


class LinuxUpdateTests(unittest.TestCase):
    def test_checksum_and_redirect_are_scoped_to_release_asset(self):
        digest = "a" * 64
        text = f"{digest}  DrakanPC-Builder.zip\n{'b' * 64}  DrakanPC-Setup.exe\n"
        self.assertEqual(updater.expected_digest(text), digest)
        for invalid in (text + text, text.replace("DrakanPC-Builder.zip", "wrong.zip")):
            with self.assertRaises(ValueError):
                updater.expected_digest(invalid)
        redirect = updater.AssetRedirect()
        origin = urllib.request.Request(f"{updater.RELEASES}/download/v0.1.2/{updater.ASSET}")
        with self.assertRaises(ValueError):
            redirect.redirect_request(
                origin, None, 302, "", {}, "https://release-assets.githubusercontent.com.evil.test/a"
            )

    def test_archive_rejects_traversal_symlinks_and_wrong_release(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            archive = root / "builder.zip"
            safe = {"Kit/DrakanPC-release.txt": "v0.1.2\n", "Kit/scripts/buildtool/bootstrap_linux.py": ""}
            builder_zip(archive, safe)
            updater.extract_builder(archive, root / "verified", "v0.1.2")
            with self.assertRaises(ValueError):
                updater.extract_builder(archive, root / "wrong-version", "v0.1.3")
            builder_zip(archive, safe | {"Kit/../../escape": "bad"})
            with self.assertRaises(ValueError):
                updater.extract_builder(archive, root / "traversal", "v0.1.2")
            builder_zip(archive, safe)
            with zipfile.ZipFile(archive, "a") as source:
                link = zipfile.ZipInfo("Kit/link")
                link.external_attr = (stat.S_IFLNK | 0o777) << 16
                source.writestr(link, "../../escape")
            with self.assertRaises(ValueError):
                updater.extract_builder(archive, root / "symlink", "v0.1.2")
            self.assertFalse((root / "escape").exists())

    def test_managed_release_keeps_only_unchanged_source_timestamps(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            old = root / "source"
            new = root / "staged"
            old.mkdir()
            new.mkdir()
            (old / "same").write_text("1")
            (old / "changed").write_text("1")
            (old / "removed").write_text("old only")
            (new / "same").write_text("1")
            (new / "changed").write_text("2")
            os.utime(old / "same", (1234, 1234))
            os.utime(old / "changed", (1234, 1234))
            updater.replace_sources(new, old, root / "previous")
            self.assertEqual((old / "same").stat().st_mtime, 1234)
            self.assertNotEqual((old / "changed").stat().st_mtime, 1234)
            self.assertFalse((old / "removed").exists())

    def test_relocation_updates_only_original_managed_elf(self):
        with tempfile.TemporaryDirectory() as temporary:
            ini = Path(temporary) / "drakan.ini"
            ini.write_bytes(
                b"; heading\r\n[general]\r\nelf = /old/game ; note\r\nvolume = 4\r\n[other]\r\nelf = /old/game\r\n"
            )
            prepare_launch.relocate_managed_elf(ini, "/old/game", "/new/game")
            self.assertEqual(
                ini.read_bytes(),
                b"; heading\r\n[general]\r\nelf = /new/game ; note\r\nvolume = 4\r\n[other]\r\nelf = /old/game\r\n",
            )
            prepare_launch.relocate_managed_elf(ini, "/old/game", "/other/game")
            self.assertIn(b"elf = /new/game", ini.read_bytes())

    @unittest.skipUnless(sys.platform == "linux" and hasattr(os, "pidfd_open"), "Linux process handoff")
    def test_handoff_occurs_only_after_valid_archive_and_preserves_old_game_on_build_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            play = root / "Drakan PC"
            play.mkdir()
            iso = root / "Drakan.iso"
            iso.write_bytes(b"fixture")
            shutil.copy2(shutil.which("sleep"), play / "drakan")
            (play / "DrakanPC-release.txt").write_text("v0.1.1\n")
            (play / "drakan.ini").write_text("volume = 4\n")
            (play / "drakan-update.json").write_text(json.dumps({"iso": str(iso), "elf": str(root / "old"), "jobs": 2}))
            player = subprocess.Popen([play / "drakan", "60"])
            handoffs = []

            def ready():
                handoffs.append("READY")
                player.terminate()
                player.wait(timeout=5)

            def valid_archive(tag, archive):
                builder_zip(
                    archive,
                    {
                        "Kit/DrakanPC-release.txt": tag,
                        "Kit/scripts/buildtool/bootstrap_linux.py": "raise SystemExit(7)",
                    },
                )

            try:
                with patch.object(updater, "download_archive", side_effect=ValueError("checksum failure")):
                    with self.assertRaisesRegex(ValueError, "checksum failure"):
                        updater.install(play, "v0.1.2", player.pid, ready)
                    self.assertEqual(handoffs, [])
                    self.assertIsNone(player.poll())
                with (
                    patch.object(updater, "download_archive", side_effect=valid_archive),
                    patch.object(updater, "display_progress"),
                    patch.object(updater, "notify"),
                    self.assertRaises(subprocess.CalledProcessError),
                ):
                    updater.install(play, "v0.1.2", player.pid, ready)
                self.assertEqual(handoffs, ["READY"])
                self.assertEqual(updater.read_tag(play), "v0.1.1")
                self.assertEqual((play / "drakan.ini").read_text(), "volume = 4\n")
                self.assertIn("Update failed", (root / "Drakan PC.update.log").read_text())
            finally:
                if player.poll() is None:
                    player.terminate()
                    player.wait(timeout=5)


if __name__ == "__main__":
    unittest.main()
