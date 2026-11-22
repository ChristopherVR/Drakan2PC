"""Builder kit packaging; synthetic entries only, no game data."""

import importlib.util
import json
import re
import shutil
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_builder_kit", ROOT / "scripts/buildtool/package_builder_kit.py")
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)


class BuilderKitTests(unittest.TestCase):
    def test_setup_locales_cover_the_wizard_and_installed_help(self):
        wizard = (kit.KIT / "DrakanPC-Setup.iss").read_text(encoding="utf-8")
        keys = set(re.findall(r"CustomMessage\('([A-Za-z]+)'\)", wizard))
        keys.update(re.findall(r"\{cm:([A-Za-z]+)\}", wizard))
        keys.remove("CreateDesktopIcon")  # supplied by Inno Setup
        locales = re.findall(r'^Name: "([a-z]{2})"; MessagesFile:', wizard, re.MULTILINE)
        self.assertEqual(set(locales), {"en", "de", "fr", "it", "es"})
        english = {}
        for locale in locales:
            with self.subTest(locale=locale):
                folder = kit.KIT / "locales" / locale
                messages = dict(
                    line.split("=", 1)
                    for line in (folder / "setup.isl").read_text(encoding="utf-8").splitlines()
                    if "=" in line
                )
                self.assertEqual(set(messages), keys)
                self.assertTrue(all(messages.values()))
                if locale == "en":
                    english = messages
                for key, message in messages.items():
                    self.assertEqual(
                        sorted(re.findall(r"%[1-9]", message)), sorted(re.findall(r"%[1-9]", english[key]))
                    )
                self.assertTrue((folder / "SETUP-INFO.txt").read_text(encoding="utf-8").strip())
                self.assertTrue((folder / "README.txt").read_text(encoding="utf-8").strip())
                if locale != "en":
                    self.assertEqual(kit.LOCALIZED_READMES[f"readme/{locale}.txt"], folder / "README.txt")

    def test_developer_material_is_left_out(self):
        entries = [
            ("BUILDING.md", b"build"),
            (".clang-format", b"format"),
            (".editorconfig", b"editor"),
            ("runtime/web/.gitignore", b"ignore"),
            ("docs/journal/2026-10-24/note.md", b"note"),
            ("docs/reference/regions.md", b"ref"),
            (".github/workflows/build.yml", b"ci"),
            ("CLAUDE.md", b"agents"),
            ("editor/packages/app/src/main.ts", b"editor app"),
            ("editor/packages/formats/test/formats.test.ts", b"test"),
            ("runtime/gs/tests/gsr_test_synthetic.cpp", b"test"),
            ("scripts/buildtool/Scan-ReleaseDefender.ps1", b"release"),
            ("scripts/buildtool/kit/DrakanPC-Setup.iss", b"installer recipe"),
            ("scripts/buildtool/kit/models-draft/model.json", b"draft model"),
        ]
        kept = kit.select(entries, kit.load_guard())
        self.assertEqual(sorted(kept), ["BUILDING.md", "docs/reference/regions.md"])

    def test_game_data_is_refused(self):
        for name, data in [
            ("ps2recomp/ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc", b"x"),
            ("analysis/functions.csv", b"\x7fELF"),
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

    def test_only_versioned_kits_carry_a_release_tag(self):
        sources = [
            (name, b"") for name in ("CMakeLists.txt", "configs/drakan.toml", kit.BUILDERS[1], kit.INSTALLER_LOCK)
        ]
        with mock.patch.object(kit, "snapshot", return_value=sources):
            unversioned = kit.kit_entries("abc")
            released = kit.kit_entries("abc", release_tag="v0.1.32")
        self.assertEqual(unversioned["DrakanPC-release.txt"], b"")
        self.assertEqual(released["DrakanPC-release.txt"], b"v0.1.32\n")
        self.assertEqual(released["KIT-VERSION.txt"], unversioned["KIT-VERSION.txt"])

    def test_prerelease_and_malformed_tags_are_rejected(self):
        for tag in ("v0.1.32-rc.1", "v00.1.32", "v0.1.32\n", "0.1.32", ""):
            with self.subTest(tag=tag), self.assertRaisesRegex(ValueError, "stable vX.Y.Z"):
                kit.kit_entries("abc", release_tag=tag)

    def test_zip_streams_files_from_disk(self):
        with tempfile.TemporaryDirectory() as temp:
            big = Path(temp) / "ps2_recomp.exe"
            big.write_bytes(b"MZ" + bytes(3 << 20))
            path = Path(temp) / "kit.zip"
            kit.write_zip(path, "K", {"scripts/buildtool/ps2_recomp.exe": big, "a.txt": b"a"})
            with zipfile.ZipFile(path) as archive:
                self.assertEqual(archive.read("K/scripts/buildtool/ps2_recomp.exe"), big.read_bytes())
                self.assertEqual(archive.read("K/a.txt"), b"a")

    def test_zip_carries_the_prebuilt_compiler_and_sources_so_it_needs_no_git(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            compiler = temp / "ps2_recomp.exe"
            compiler.write_bytes(b"MZ")
            raylib = temp / "raylib"
            for name in (
                "src/raylib.h",
                "src/.gitignore",
                "examples/core.c",
                "projects/Makefile",
                ".git/HEAD",
                "CMakeLists.txt",
                "LICENSE",
            ):
                (raylib / name).parent.mkdir(parents=True, exist_ok=True)
                (raylib / name).write_bytes(b"x")
            ffmpeg = temp / "ffmpeg-win64.zip"
            ffmpeg.write_bytes(b"PK")
            fflate = temp / "fflate-0.8.3.tgz"
            fflate.write_bytes(b"tgz")
            lock = {
                "raylib": {"url": "u", "commit": "c"},
                "ffmpeg": {"url": "https://x/ffmpeg-win64.zip"},
                "hd": {"fflate": {"url": "https://x/fflate-0.8.3.tgz"}},
            }
            entries = {"Build-Drakan.cmd": b"@echo off", kit.INSTALLER_LOCK: json.dumps(lock).encode()}
            out = temp / "out"
            out.mkdir()
            with (
                mock.patch.object(kit, "build_compiler", return_value=compiler),
                mock.patch.object(kit, "fetch_raylib", return_value=raylib),
                mock.patch.object(kit, "download", side_effect=[ffmpeg, fflate]),
            ):
                path = kit.build_zip(entries, "0123456789abcdef", out, "K")
            with zipfile.ZipFile(path) as archive:
                names = set(archive.namelist())
            self.assertEqual(
                names,
                {
                    "K/Build-Drakan.cmd",
                    f"K/{kit.INSTALLER_LOCK}",
                    "K/scripts/buildtool/ps2_recomp.exe",
                    "K/deps/raylib/src/raylib.h",
                    "K/deps/raylib/CMakeLists.txt",
                    "K/deps/raylib/LICENSE",
                    "K/deps/ffmpeg-win64.zip",
                    "K/deps/fflate-0.8.3.tgz",
                },
            )
            # Build-Drakan.ps1 skips its Git check when exactly these two are present.
            text = (ROOT / "scripts/buildtool/Build-Drakan.ps1").read_text(encoding="utf-8")
            self.assertIn("Join-Path $PSScriptRoot 'ps2_recomp.exe'", text)
            self.assertIn("Join-Path $RepoRoot 'deps/raylib'", text)

    def test_bundled_ffmpeg_uses_only_headers_import_libraries_and_needed_dlls(self):
        with tempfile.TemporaryDirectory() as temp:
            cache = Path(temp)
            source = cache / "ffmpeg.zip"
            included = {
                *(f"bin/{name}.lib" for name in ("avcodec", "avformat", "avutil", "swresample", "swscale")),
                "bin/avcodec-61.dll",
                "bin/zlib1.dll",
                "include/libavcodec/avcodec.h",
                "licenses/LICENSE.txt",
            }
            with zipfile.ZipFile(source, "w") as archive:
                for name in (*included, "bin/ffmpeg.exe", "bin/avfilter-10.dll", "lib/vpxmt.lib", "share/man/ffmpeg.1"):
                    archive.writestr(name, name.encode())
            with mock.patch.object(kit, "download", return_value=source):
                payload = kit.ffmpeg_files({"kit_dlls": ["avcodec-61.dll", "zlib1.dll"]}, cache)
            self.assertEqual(set(payload), {f"deps/ffmpeg/{name}" for name in included})
            for path, file in payload.items():
                self.assertEqual(file.read_bytes(), path.removeprefix("deps/ffmpeg/").encode())

    def test_root_files_exist(self):
        for source in [
            *kit.KIT_ROOT_FILES.values(),
            *kit.LOCALIZED_READMES.values(),
            kit.KIT / "DrakanPC-Setup.iss",
        ]:
            self.assertTrue(source.is_file(), source)
        self.assertIn(b"\r\n", kit.KIT_ROOT_FILES["Build-Drakan.cmd"].read_bytes())

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
        self.assertEqual([name for name in sorted(names) if kit.excluded(name)], [])

    def test_installer_tool_payload(self):
        for tool, prefix, keep, omit in (
            ("node", "node-v1-win-x64/", ("node.exe", "LICENSE"), ("npm.cmd", "node_modules/npm/package.json")),
            ("python", "", ("python.exe", "python312._pth", "LICENSE.txt"), ("pythonw.exe",)),
            (
                "pwsh",
                "",
                ("pwsh.exe", "ref/System.Runtime.dll", "Modules/Microsoft.PowerShell.Management/test.ps1"),
                ("System.Management.Automation.xml", "Install-PowerShellRemoting.ps1", "preview/pwsh-preview.cmd"),
            ),
        ):
            with self.subTest(tool=tool), tempfile.TemporaryDirectory() as temp:
                archive_path = Path(temp) / "tool.zip"
                with zipfile.ZipFile(archive_path, "w") as archive:
                    for name in (*keep, *omit):
                        archive.writestr(prefix + name, b"x")
                target = Path(temp) / "out"
                kit.extract_tool(archive_path, target, installer_tool=tool)
                files = {file.relative_to(target).as_posix() for file in target.rglob("*") if file.is_file()}
                self.assertEqual(files, set(keep))

    @unittest.skipUnless((ROOT / "editor/packages/formats/.git").exists(), "formats submodule not checked out")
    def test_payload_includes_optional_build_inputs(self):
        # character_motion_retarget.ts imports these at build time; git archive alone leaves them out.
        names = kit.kit_entries("HEAD")
        for name in (
            "analysis/regions/usa/functions.csv",
            "analysis/scripts/character_motion_retarget.ts",
            "analysis/scripts/hd_pack.py",
            "analysis/scripts/ootf_subtitles.py",
            "editor/packages/formats/src/ootfanim.ts",
            "editor/packages/editor-core/schema/drakan-content.schema.json",
            "editor/scripts/ootf-convert.ts",
            "editor/scripts/ootf-speech.ts",
            "ps2recomp/ps2xRuntime/src/res/drakan.ico",
            "runtime/web/index.html",
            "scripts/buildtool/Build-Drakan.sh",
            "scripts/buildtool/kit/subtitles/english.json",
        ):
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
