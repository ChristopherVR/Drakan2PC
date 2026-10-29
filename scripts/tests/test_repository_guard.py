"""Source publication checks; synthetic data only, no game required."""

import contextlib
import importlib.util
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    "repository_guard", Path(__file__).resolve().parents[1] / "repository_guard.py"
)
guard = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guard)


class SourceGuardTests(unittest.TestCase):
    def test_authored_source(self):
        for path in [
            "README.md",
            ".githooks/pre-push",
            ".github/workflows/build.yml",
            "runtime/gs/src/renderer.cpp",
            "configs/drakan.toml",
            "ps2recomp/ps2xRecomp/src/lib/ps2_recompiler.cpp",
            "analysis/functions.csv",
        ]:
            with self.subTest(path=path):
                self.assertIsNone(guard.reason(path, b"authored source\n"))

    def test_private_paths(self):
        for path in [
            "game.iso",
            "iso/SCES_500.06",
            "runtime/renamed.dll",
            "analysis/sound/function.c",
            "analysis/vu/microcode.inc",
            "patches/old.patch",
            "ps2recomp/ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc",
            "analysis/sdk_strings.csv",
            "analysis/strings_vaddr.csv",
            "docs/proposals/subtitles/cue_text.json",
            "runtime/mc0/save.dat",
            "../README.md",
            "dist/DrakanPC/drakan.exe",
            "analysis/textures/texture.dds",
            "docs/capture.png",
            "runtime/movie.pss",
        ]:
            with self.subTest(path=path):
                self.assertIsNotNone(guard.reason(path, b"synthetic\n"))

    def test_allowed_binaries_are_pinned(self):
        for path in guard.ALLOWED_BINARIES:
            with self.subTest(path=path):
                self.assertIsNone(guard.reason(path))
                self.assertIsNotNone(guard.reason(path, b"MZswapped"))

    def test_disguised_content(self):
        for data in [
            b"MZsynthetic",
            b"\x7fELFsynthetic",
            b"x" * (2 * 1024 * 1024 + 1),
            b"-----BEGIN " + b"PRIVATE KEY-----",
            b"ghp_" + b"a" * 36,
        ]:
            self.assertIsNotNone(guard.reason("runtime/disguised.txt", data))

    def test_no_game_array_patch(self):
        for data in [
            b"header\nGIT binary patch\nliteral 1\n",
            b"diff --git a/ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc b/ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc\n",
        ]:
            self.assertIsNotNone(guard.reason("docs/proposals/test.patch", data))
        self.assertIsNone(guard.reason("docs/proposals/test.patch", b'+#include "ps2_vu1_image.inc"\n'))

    def test_history_including_deleted_private_file(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)

            def git(*args):
                return subprocess.check_output(["git", *args], cwd=root, stderr=subprocess.DEVNULL).decode().strip()

            git("init")
            git("config", "user.name", "Source audit test")
            git("config", "user.email", "audit@example.invalid")
            (root / "README.md").write_text("Synthetic source test\n")
            git("add", ".")
            git("commit", "-m", "clean")
            clean = git("rev-parse", "HEAD")
            (root / "private.iso").write_bytes(b"synthetic disc, not a game")
            git("add", ".")
            git("commit", "-m", "private")
            git("rm", "private.iso")
            git("commit", "-m", "removed")
            dirty = git("rev-parse", "HEAD")
            for revision, expected in [(clean, 0), (dirty, 1)]:
                updates = f"refs/heads/main {revision} refs/heads/main {'0' * 40}\n"
                with (
                    patch.object(guard, "ROOT", root),
                    patch("sys.stdin", io.StringIO(updates)),
                    contextlib.redirect_stdout(io.StringIO()),
                    contextlib.redirect_stderr(io.StringIO()),
                ):
                    self.assertEqual(guard.pre_push(), expected)
            # History another remote already has is still new to the destination remote.
            git("update-ref", "refs/remotes/other/main", dirty)
            for remote, expected in [("other", 0), ("public", 1)]:
                updates = f"refs/heads/main {dirty} refs/heads/main {'0' * 40}\n"
                with (
                    patch.object(guard, "ROOT", root),
                    patch("sys.stdin", io.StringIO(updates)),
                    contextlib.redirect_stdout(io.StringIO()),
                    contextlib.redirect_stderr(io.StringIO()),
                ):
                    self.assertEqual(guard.pre_push(remote), expected)


if __name__ == "__main__":
    unittest.main()
