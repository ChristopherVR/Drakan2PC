"""Builder kit packaging; synthetic entries only, no game data."""

import importlib.util
import json
import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_builder_kit", ROOT / "scripts/buildtool/package_builder_kit.py")
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)


class BuilderKitTests(unittest.TestCase):
    def test_developer_material_is_left_out(self):
        entries = [
            ("BUILDING.md", b"build"),
            ("docs/journal/2026-10-24/note.md", b"note"),
            ("docs/reference/hd-textures.md", b"ref"),
            (".github/workflows/build.yml", b"ci"),
            ("CLAUDE.md", b"agents"),
        ]
        kept = kit.select(entries, kit.load_guard())
        self.assertEqual(sorted(kept), ["BUILDING.md", "docs/reference/hd-textures.md"])

    def test_game_data_is_refused(self):
        for name, data in [
            ("ps2recomp/ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc", b"x"),
            ("analysis/SCES_500.06", b"\x7fELF"),
            ("runtime/gs/src/blob.cpp", b"MZ\x90\x00"),
        ]:
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                kit.select([(name, data)], kit.load_guard())

    def test_zip_has_one_top_level_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "kit.zip"
            kit.write_zip(path, "DrakanPC-Builder", {"Build-Drakan.cmd": b"@echo off\r\n", "a/b.txt": b"b"})
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(archive.namelist(), ["DrakanPC-Builder/Build-Drakan.cmd", "DrakanPC-Builder/a/b.txt"])

    def test_root_files_exist(self):
        for source in [
            *kit.KIT_ROOT_FILES.values(),
            *kit.INSTALLER_ROOT_FILES.values(),
            kit.KIT / "DrakanPC-Setup.iss",
        ]:
            self.assertTrue(source.is_file(), source)
        self.assertIn(b"\r\n", kit.KIT_ROOT_FILES["Build-Drakan.cmd"].read_bytes())
        self.assertTrue((ROOT / kit.ICON).is_file())

    def test_tool_zip_with_one_folder_is_flattened(self):
        with tempfile.TemporaryDirectory() as temp:
            for layout, expected in [("node-v1-win-x64/node.exe", "node.exe"), ("pwsh.exe", "pwsh.exe")]:
                with self.subTest(layout=layout):
                    archive_path = Path(temp) / "tool.zip"
                    with zipfile.ZipFile(archive_path, "w") as archive:
                        archive.writestr(layout, b"x")
                    target = Path(temp) / "tools" / expected
                    kit.extract_tool(archive_path, target.parent / "out")
                    self.assertTrue((target.parent / "out" / expected).is_file())
                    shutil.rmtree(target.parent)

    def test_builder_scripts_are_found(self):
        names = {path.relative_to(ROOT).as_posix() for path in kit.builder_scripts(ROOT)}
        for name in ("scripts/buildtool/prepare_iso.py", "analysis/scripts/character_motion.py"):
            self.assertIn(name, names)

    @unittest.skipUnless((ROOT / "editor/packages/formats/.git").exists(), "formats submodule not checked out")
    def test_snapshot_includes_submodule_sources(self):
        # character_motion_retarget.ts imports these at build time; git archive alone leaves them out.
        names = {name for name, _ in kit.snapshot("HEAD")}
        for name in ("analysis/scripts/character_motion_retarget.ts", "editor/packages/formats/src/ootfanim.ts"):
            self.assertIn(name, names)

    def test_installer_inputs_are_pinned(self):
        lock = json.loads((kit.KIT / "installer.lock.json").read_text(encoding="utf-8"))
        for name in ("python", "pwsh", "node", "ffmpeg"):
            with self.subTest(name=name):
                self.assertTrue(lock[name]["url"].startswith("https://"))
                self.assertRegex(lock[name]["sha256"], r"^[0-9a-f]{64}$")
        self.assertRegex(lock["raylib"]["commit"], r"^[0-9a-f]{40}$")
        # The CMake fallback download and the bundled copy must be the same archive.
        runtime = (ROOT / "ps2recomp/ps2xRuntime/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(f"URL_HASH SHA256={lock['ffmpeg']['sha256']}", runtime)

    def test_workflows_that_package_the_kit_check_out_submodules(self):
        # The kit packs submodule sources; a checkout without them fails the release (run 42180772185).
        workflows = sorted((ROOT / ".github/workflows").glob("*.yml"))
        packaging = [path for path in workflows if "package_builder_kit.py" in path.read_text(encoding="utf-8")]
        self.assertTrue(packaging)
        for path in packaging:
            with self.subTest(workflow=path.name):
                self.assertIn("submodules: recursive", path.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
