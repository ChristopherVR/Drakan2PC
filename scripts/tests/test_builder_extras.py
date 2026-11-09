"""The builder's optional extras (Drakan: Order of the Flame, HD textures); synthetic files only, no game data, no network."""

import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("extras", ROOT / "scripts/buildtool/extras.py")
extras = importlib.util.module_from_spec(spec)
sys.modules["extras"] = extras  # dataclasses look their module up
spec.loader.exec_module(extras)
LOCK = ROOT / "scripts/buildtool/kit/installer.lock.json"
ISS = ROOT / "scripts/buildtool/kit/DrakanPC-Setup.iss"
BUILD_DRAKAN = ROOT / "scripts/buildtool/Build-Drakan.ps1"


def make_install(folder: Path, upper: bool = False) -> Path:
    folder.mkdir(parents=True, exist_ok=True)
    (folder / ("DRAGON.RRC" if upper else "Dragon.rrc")).write_bytes(b"rrc")
    (folder / ("MOUNTAIN WORLD" if upper else "Mountain World")).mkdir()
    return folder


def make_iso(path: Path, offset: int = 0) -> Path:
    data = bytearray(offset + 17 * 2048)
    data[offset + 16 * 2048 : offset + 16 * 2048 + 6] = b"\x01CD001"
    path.write_bytes(bytes(data))
    return path


class GpuTests(unittest.TestCase):
    def test_tiers(self):
        for name, tier in [
            ("NVIDIA GeForce RTX 5080", "fast"),
            ("NVIDIA GeForce RTX 4060 Laptop GPU", "fast"),
            ("AMD Radeon RX 7900 XTX", "fast"),
            ("NVIDIA GeForce RTX 3070", "good"),
            ("AMD Radeon RX 6700 XT", "good"),
            ("NVIDIA GeForce GTX 1660 SUPER", "modest"),
            ("Intel(R) UHD Graphics 630", "integrated"),
            ("AMD Radeon(TM) Graphics", "integrated"),
            ("Microsoft Basic Render Driver", "none"),
            ("Some New Card 9000", "unknown"),
            ("", "unknown"),
        ]:
            with self.subTest(name=name):
                self.assertEqual(extras.classify_gpu(name).tier, tier)

    def test_best_gpu_prefers_the_discrete_card_and_skips_unusable_ones(self):
        gpu = extras.best_gpu(["Intel(R) UHD Graphics 630", "NVIDIA GeForce RTX 3060", "Microsoft Basic Render Driver"])
        self.assertEqual(gpu.tier, "good")
        self.assertFalse(extras.best_gpu(["Microsoft Basic Render Driver"]).usable)
        self.assertEqual(extras.best_gpu([]).tier, "unknown")

    def test_notes_warn_about_integrated_and_missing_gpus(self):
        self.assertTrue(any("integrated" in n for n in extras.gpu_notes(extras.classify_gpu("Intel UHD Graphics"))))
        self.assertTrue(any("Vulkan" in n for n in extras.gpu_notes(extras.classify_gpu("Basic Display Adapter"))))


class EstimateTests(unittest.TestCase):
    def test_plan_steps(self):
        self.assertEqual(extras.plan(None, False), [])
        self.assertEqual(extras.plan("install", False), ["ootf-data", "ootf-convert"])
        self.assertEqual(extras.plan("image", False), ["ootf-source", "ootf-data", "ootf-convert"])
        self.assertEqual(extras.plan(None, True), ["hd-tools", "hd-index", "hd-upscale"])
        self.assertEqual(extras.plan(None, True, tools_ready=True), ["hd-index", "hd-upscale"])
        self.assertEqual(
            extras.plan("install", True, True),
            ["ootf-data", "ootf-convert", "hd-index", "hd-upscale", "hd-upscale-ootf"],
        )

    def test_only_upscale_steps_scale_with_the_graphics_card(self):
        keys = extras.plan("image", True)
        fast = extras.estimate(keys, extras.classify_gpu("NVIDIA GeForce RTX 4070"), ootf=True)
        slow = extras.estimate(keys, extras.classify_gpu("Intel UHD Graphics"), ootf=True)
        by_key = {line.key: (line.low, line.high) for line in fast.lines}
        slow_key = {line.key: (line.low, line.high) for line in slow.lines}
        for key in keys:
            if extras.STEPS[key].gpu:
                self.assertAlmostEqual(slow_key[key][0], by_key[key][0] * 10)
            else:
                self.assertEqual(slow_key[key], by_key[key])

    def test_a_timed_step_replaces_the_reference_figure(self):
        est = extras.estimate(["ootf-convert", "hd-upscale"], extras.classify_gpu("RTX 4070"), {"ootf-convert": 150.0})
        convert = est.lines[0]
        self.assertTrue(convert.measured)
        self.assertAlmostEqual(convert.low, 2.5)
        self.assertEqual(convert.low, convert.high)
        self.assertFalse(est.lines[1].measured)

    def test_disk_adds_up(self):
        gpu = extras.classify_gpu("RTX 4070")
        self.assertEqual(extras.estimate(extras.plan("image", False), gpu, ootf=True).disk_gb, 1.0)
        self.assertEqual(extras.estimate(extras.plan(None, True), gpu).disk_gb, 2.0)
        self.assertEqual(extras.estimate(extras.plan("image", True), gpu, ootf=True).disk_gb, 4.0)

    def test_minutes_are_readable(self):
        fmt = extras.format_minutes
        self.assertEqual(fmt(0.2, 0.5), "under a minute")
        self.assertEqual(fmt(2.5, 5), "3 to 5 min")
        self.assertEqual(fmt(10, 15), "10 to 15 min")
        self.assertEqual(fmt(12, 12), "about 12 min")
        self.assertEqual(fmt(0.5, 3), "up to 3 min")
        self.assertEqual(fmt(100, 150), "1.7 to 2.5 h")
        self.assertEqual(fmt(30, 45), "30 to 45 min")

    def test_estimate_text_lists_every_step_and_a_total(self):
        est = extras.estimate(extras.plan("image", True), extras.classify_gpu("RTX 4070"), ootf=True)
        text = extras.format_estimate(est)
        for line in est.lines:
            self.assertIn(line.title, text)
        self.assertIn("Total", text)
        self.assertIn("Disk space", text)

    def test_timings_file_round_trip_ignores_unknown_steps_and_bad_json(self):
        with tempfile.TemporaryDirectory() as temp:
            timings = extras.Timings(Path(temp) / "sub" / "t.json")
            self.assertEqual(timings.load(), {})
            timings.record("ootf-convert", 168.04)
            timings.record("not-a-step", 1)
            self.assertEqual(timings.load(), {"ootf-convert": 168.0})
            timings.path.write_text("{broken", encoding="utf-8")
            self.assertEqual(timings.load(), {})

    def test_cli_estimate_prints_and_emits_json(self):
        for argv in (["estimate", "--ootf", "--hd", "--gpu", "NVIDIA GeForce RTX 4070"], ["estimate"]):
            out = io.StringIO()
            with self.subTest(argv=argv), mock.patch.object(sys, "stdout", out):
                self.assertEqual(extras.main(argv), 0)
            self.assertTrue(out.getvalue().strip())
        out = io.StringIO()
        with mock.patch.object(sys, "stdout", out):
            extras.main(["estimate", "--hd", "--ootf", "--tools-ready", "--gpu", "RTX 4070", "--json"])
        data = json.loads(out.getvalue())
        self.assertEqual(next(step["key"] for step in data["steps"]), "ootf-source")
        self.assertGreater(data["high"], data["low"])


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def test_install_folder_any_letter_case_or_one_level_up(self):
        install = make_install(self.dir / "a" / "Drakan")
        self.assertEqual(extras.classify_source(install), ("install", install))
        self.assertEqual(extras.classify_source(install.parent), ("install", install))
        upper = make_install(self.dir / "b", upper=True)
        self.assertEqual(extras.classify_source(upper)[0], "install")

    def test_disc_folder_with_the_cabinet(self):
        disc = self.dir / "disc"
        (disc / "SETUP").mkdir(parents=True)
        cab = disc / "SETUP" / "DATA1.CAB"
        cab.write_bytes(b"ISc(")
        self.assertEqual(extras.classify_source(disc), ("disc-folder", cab))
        lower = self.dir / "disc2"
        (lower / "setup").mkdir(parents=True)
        (lower / "setup" / "data1.cab").write_bytes(b"ISc(")
        self.assertEqual(extras.classify_source(lower)[0], "disc-folder")

    def test_images_and_zips(self):
        iso = make_iso(self.dir / "x.iso")
        mdx = make_iso(self.dir / "DRAKAN.mdx", 0x40)
        self.assertEqual(extras.classify_source(iso), ("image", iso))
        self.assertEqual(extras.classify_source(mdx), ("image", mdx))
        one = self.dir / "Drakan.zip"
        with zipfile.ZipFile(one, "w") as z:
            z.writestr("DRAKAN.mdx", b"x")
        self.assertEqual(extras.classify_source(one), ("image", one))

    def test_cue_bin_rips_are_images(self):
        raw = bytearray(18 * 2352)
        raw[16 * 2352 + 17 : 16 * 2352 + 22] = b"CD001"
        cue = 'FILE "Game (Track 1).bin" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n'
        (self.dir / "Game (Track 1).bin").write_bytes(bytes(raw))
        (self.dir / "Game.cue").write_text(cue, encoding="utf-8")
        for name in ("Game.cue", "Game (Track 1).bin"):
            with self.subTest(name=name):
                self.assertEqual(extras.classify_source(self.dir / name)[0], "image")
        rip = self.dir / "rip.zip"
        with zipfile.ZipFile(rip, "w") as z:
            z.writestr("Game/Game.cue", cue)
            z.writestr("Game/Game (Track 1).bin", bytes(raw))
        self.assertEqual(extras.classify_source(rip), ("image", rip))
        (self.dir / "Game (Track 1).bin").unlink()
        with self.assertRaises(extras.ExtrasError) as caught:
            extras.classify_source(self.dir / "Game.cue")
        self.assertIn("not next to it", str(caught.exception))
        (self.dir / "audio.bin").write_bytes(bytes(40000))
        with self.assertRaises(extras.ExtrasError):
            extras.classify_source(self.dir / "audio.bin")

    def test_deep_check_reads_the_cabinet_and_names_the_problem(self):
        disc = self.dir / "disc.iso"
        make_iso(disc)

        state = {"names": [], "missing": False}

        class Cab:
            @staticmethod
            def read_image(path):
                return bytes(17 * 2048)

            @staticmethod
            def iso_find(image, base, name):
                if state["missing"]:
                    raise FileNotFoundError(name)
                return 0, 4

            @staticmethod
            def read_cab(data):
                return [("", name, 0, 1, 1, 0) for name in state["names"]]

        with mock.patch.object(extras, "ootf_cab", lambda: Cab):
            state["names"] = ["Dragon.rrc", "Drakan.exe"]
            self.assertIn("2 files", extras.verify_source(disc))
            state["names"] = ["setup.exe"]
            with self.assertRaises(extras.ExtrasError) as caught:
                extras.verify_source(disc)
            self.assertIn("not Drakan: Order of the Flame", str(caught.exception))
            state["missing"] = True
            with self.assertRaises(extras.ExtrasError) as caught:
                extras.verify_source(disc)
            self.assertIn("SETUP/DATA1.CAB", str(caught.exception))

    def test_deep_check_of_an_install_needs_no_image(self):
        install = make_install(self.dir / "Drakan")
        self.assertIn("install folder", extras.verify_source(install))

    def test_cli_deep_check_exit_codes(self):
        install = make_install(self.dir / "Drakan")
        with mock.patch.object(sys, "stdout", io.StringIO()):
            self.assertEqual(extras.main(["check-source", "--deep", str(install)]), 0)
        with mock.patch.object(sys, "stderr", io.StringIO()):
            self.assertEqual(extras.main(["check-source", "--deep", str(self.dir / "nope.zip")]), 1)

    def test_bad_sources_say_what_to_choose(self):
        two = self.dir / "two.zip"
        with zipfile.ZipFile(two, "w") as z:
            z.writestr("a.iso", b"x")
            z.writestr("b.mdx", b"x")
        empty = self.dir / "empty"
        empty.mkdir()
        fake = self.dir / "fake.iso"
        fake.write_bytes(b"\0" * 100000)
        notzip = self.dir / "broken.zip"
        notzip.write_bytes(b"nope")
        for path in (two, empty, fake, notzip, self.dir / "missing", self.dir / "readme.txt"):
            (self.dir / "readme.txt").write_text("x", encoding="utf-8")
            with self.subTest(path=path.name), self.assertRaises(extras.ExtrasError) as caught:
                extras.classify_source(path)
            self.assertTrue(str(caught.exception))
        with self.assertRaises(extras.ExtrasError) as caught:
            extras.classify_source(empty)
        self.assertIn("Dragon.rrc", str(caught.exception))

    def test_cli_check_source_exit_codes(self):
        install = make_install(self.dir / "Drakan")
        with mock.patch.object(sys, "stdout", io.StringIO()):
            self.assertEqual(extras.main(["check-source", str(install)]), 0)
        with mock.patch.object(sys, "stderr", io.StringIO()):
            self.assertEqual(extras.main(["check-source", str(self.dir / "nope")]), 1)

    def test_prepare_source_uses_an_install_in_place(self):
        install = make_install(self.dir / "Drakan")
        self.assertEqual(extras.prepare_source(install, self.dir / "work"), install)
        self.assertFalse((self.dir / "work").exists())

    def test_prepare_source_unpacks_a_disc_once_and_checks_the_result(self):
        disc = self.dir / "disc"
        (disc / "SETUP").mkdir(parents=True)
        (disc / "SETUP" / "DATA1.CAB").write_bytes(b"ISc(")
        calls = []

        class FakeCab:
            @staticmethod
            def find_cab_file(folder):
                return disc / "SETUP" / "DATA1.CAB"

            @staticmethod
            def extract_cab(data, out):
                calls.append(out)
                make_install(out)
                return 2

        with mock.patch.object(extras, "ootf_cab", lambda: FakeCab):
            first = extras.prepare_source(disc, self.dir / "work", log=lambda *_: None)
            again = extras.prepare_source(disc, self.dir / "work", log=lambda *_: None)
        self.assertEqual(first, again)
        self.assertEqual(len(calls), 1, "an unchanged source is not unpacked twice")

        class WrongCab(FakeCab):
            @staticmethod
            def extract_cab(data, out):
                out.mkdir(parents=True)
                (out / "other.txt").write_text("x", encoding="utf-8")
                return 1

        with mock.patch.object(extras, "ootf_cab", lambda: WrongCab), self.assertRaises(extras.ExtrasError):
            extras.prepare_source(disc, self.dir / "work2", log=lambda *_: None)


def tgz_bytes(files: dict[str, bytes]) -> bytes:
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as tar:
        for name, data in files.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            tar.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def zip_bytes(files: dict[str, bytes]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as z:
        for name, data in files.items():
            z.writestr(name, data)
    return buffer.getvalue()


class DownloadTests(unittest.TestCase):
    def setUp(self):
        quiet = mock.patch.object(sys, "stdout", io.StringIO())
        quiet.start()
        self.addCleanup(quiet.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)

    def opener_for(self, payload: bytes, calls: list):
        def opener(url):
            calls.append(url)
            return io.BytesIO(payload)

        return opener

    def test_download_checks_the_pinned_hash_and_caches(self):
        payload = b"payload"
        spec = {"url": "https://example.invalid/tool.zip", "sha256": hashlib.sha256(payload).hexdigest()}
        calls: list = []
        first = extras.download(spec, self.dir / "cache", self.opener_for(payload, calls))
        second = extras.download(spec, self.dir / "cache", self.opener_for(payload, calls))
        self.assertEqual(first.read_bytes(), payload)
        self.assertEqual(first, second)
        self.assertEqual(len(calls), 1)

    def test_a_different_file_is_refused_and_deleted(self):
        spec = {"url": "https://example.invalid/tool.zip", "sha256": "0" * 64}
        with self.assertRaises(extras.ExtrasError) as caught:
            extras.download(spec, self.dir / "cache", self.opener_for(b"tampered", []))
        self.assertIn("SHA-256", str(caught.exception))
        self.assertFalse((self.dir / "cache" / "tool.zip").exists())

    def test_a_corrupt_cached_file_is_fetched_again(self):
        payload = b"good"
        spec = {"url": "https://example.invalid/tool.zip", "sha256": hashlib.sha256(payload).hexdigest()}
        (self.dir / "cache").mkdir()
        (self.dir / "cache" / "tool.zip").write_bytes(b"bad")
        calls: list = []
        path = extras.download(spec, self.dir / "cache", self.opener_for(payload, calls))
        self.assertEqual(path.read_bytes(), payload)
        self.assertEqual(len(calls), 1)

    def test_a_tls_failure_falls_back_to_the_system_downloader_but_still_checks_the_hash(self):
        payload = b"via curl"
        spec = {"url": "https://example.invalid/tool.zip", "sha256": hashlib.sha256(payload).hexdigest()}

        def system(url, destination):
            destination.write_bytes(payload)
            return True

        with (
            mock.patch.object(extras.urllib.request, "urlopen", side_effect=OSError("CERTIFICATE_VERIFY_FAILED")),
            mock.patch.object(extras, "fetch_with_system_tools", system),
        ):
            self.assertEqual(extras.download(spec, self.dir / "c1").read_bytes(), payload)
        with (
            mock.patch.object(extras.urllib.request, "urlopen", side_effect=OSError("down")),
            mock.patch.object(extras, "fetch_with_system_tools", lambda *_: False),
            self.assertRaises(extras.ExtrasError),
        ):
            extras.download(spec, self.dir / "c2")
        spec_bad = dict(spec, sha256="1" * 64)
        with (
            mock.patch.object(extras.urllib.request, "urlopen", side_effect=OSError("down")),
            mock.patch.object(extras, "fetch_with_system_tools", system),
            self.assertRaises(extras.ExtrasError),
        ):
            extras.download(spec_bad, self.dir / "c3")

    def test_archives_cannot_write_outside_the_target(self):
        evil = self.dir / "evil.zip"
        evil.write_bytes(zip_bytes({"../outside.txt": b"x"}))
        with self.assertRaises(extras.ExtrasError):
            extras.safe_extract_zip(evil, self.dir / "out")
        absolute = self.dir / "abs.zip"
        absolute.write_bytes(zip_bytes({"C:/x.txt": b"x"}))
        with self.assertRaises(extras.ExtrasError):
            extras.safe_extract_zip(absolute, self.dir / "out")
        tar = self.dir / "evil.tgz"
        tar.write_bytes(tgz_bytes({"package/../../x": b"x"}))
        with self.assertRaises(extras.ExtrasError):
            extras.safe_extract_tgz(tar, self.dir / "out2")
        link = io.BytesIO()
        with tarfile.open(fileobj=link, mode="w:gz") as handle:
            info = tarfile.TarInfo("package/link")
            info.type = tarfile.SYMTYPE
            info.linkname = "/etc/passwd"
            handle.addfile(info)
        (self.dir / "link.tgz").write_bytes(link.getvalue())
        with self.assertRaises(extras.ExtrasError):
            extras.safe_extract_tgz(self.dir / "link.tgz", self.dir / "out3")

    def test_zip_strip_drops_the_top_folder(self):
        archive = self.dir / "a.zip"
        archive.write_bytes(zip_bytes({"top/a/b.txt": b"b", "top/c.txt": b"c"}))
        extras.safe_extract_zip(archive, self.dir / "out", strip=1)
        self.assertEqual((self.dir / "out/a/b.txt").read_bytes(), b"b")
        self.assertEqual((self.dir / "out/c.txt").read_bytes(), b"c")


class NodeModuleTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        (self.root / "editor/packages/formats/src").mkdir(parents=True)
        (self.root / "editor/packages/editor-core/src").mkdir(parents=True)
        (self.root / "editor/scripts").mkdir(parents=True)
        (self.root / "editor/packages/formats/src/index.ts").write_text(
            "export const one: number = 1;\n", encoding="utf-8"
        )
        (self.root / "editor/packages/editor-core/src/index.ts").write_text(
            "import { one } from '@drakan/formats';\nexport const two: number = one + 1;\n", encoding="utf-8"
        )
        self.lock = {"hd": {"fflate": {"url": "https://example.invalid/fflate-0.8.3.tgz", "sha256": "x"}}}
        self.fetched: list = []

    def fetch(self, spec, cache):
        self.fetched.append(spec["url"])
        archive = cache / "fflate.tgz"
        cache.mkdir(parents=True, exist_ok=True)
        archive.write_bytes(
            tgz_bytes({"package/package.json": b'{"name":"fflate"}', "package/lib/index.cjs": b"module.exports={}"})
        )
        return archive

    def test_stubs_and_fflate_are_created_once(self):
        made = extras.ensure_node_modules(self.root, self.lock, self.root / "cache", self.fetch)
        self.assertEqual(sorted(made), ["@drakan/editor-core", "@drakan/formats", "fflate"])
        modules = self.root / "editor/node_modules"
        self.assertTrue((modules / "fflate/package.json").is_file())
        self.assertIn(
            "packages/formats/src/index.ts", (modules / "@drakan/formats/index.js").read_text(encoding="utf-8")
        )
        self.assertEqual(extras.ensure_node_modules(self.root, self.lock, self.root / "cache", self.fetch), [])
        self.assertEqual(len(self.fetched), 1)

    def test_an_npm_install_is_left_alone(self):
        for name in ("@drakan/formats", "@drakan/editor-core", "fflate"):
            folder = self.root / "editor/node_modules" / name
            folder.mkdir(parents=True)
            (folder / "package.json").write_text("{}", encoding="utf-8")
        self.assertEqual(extras.ensure_node_modules(self.root, self.lock, self.root / "cache", self.fetch), [])
        self.assertEqual(self.fetched, [])

    def test_missing_editor_sources_are_reported(self):
        (self.root / "editor/packages/formats/src/index.ts").unlink()
        with self.assertRaises(extras.ExtrasError):
            extras.ensure_node_modules(self.root, self.lock, self.root / "cache", self.fetch)

    def node_ok(self) -> str | None:
        node = shutil.which("node")
        if not node:
            return None
        version = subprocess.run(
            [node, "-p", "process.versions.node"], capture_output=True, text=True, check=False
        ).stdout
        try:
            major, minor = (int(part) for part in version.strip().split(".")[:2])
        except ValueError:
            return None
        return node if (major, minor) >= (22, 7) else None

    def test_node_resolves_the_stubs_like_the_converter_does(self):
        node = self.node_ok()
        if not node:
            self.skipTest("Node.js 22.7 or newer not on PATH")
        extras.ensure_node_modules(self.root, self.lock, self.root / "cache", self.fetch)
        probe = self.root / "editor/scripts/probe.ts"
        probe.write_text(
            "import { one } from '@drakan/formats';\nimport { two } from '@drakan/editor-core';\nconsole.log(one, two);\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            [node, "--experimental-transform-types", "--no-warnings", str(probe)],
            capture_output=True,
            text=True,
            cwd=self.root,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.split(), ["1", "2"])


class ConverterTests(unittest.TestCase):
    def setUp(self):
        quiet = mock.patch.object(sys, "stdout", io.StringIO())
        quiet.start()
        self.addCleanup(quiet.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.root = self.dir / "kit"
        (self.root / "scripts/buildtool/kit").mkdir(parents=True)
        shutil.copyfile(LOCK, self.root / "scripts/buildtool/kit/installer.lock.json")
        (self.root / "KIT-VERSION.txt").write_text("commit abc\n", encoding="utf-8")
        self.install = make_install(self.dir / "Drakan")
        self.iso = self.dir / "game.iso"
        self.iso.write_bytes(b"iso")
        self.work = self.root / "build/ootf"

    def fake_data(self):
        class Data:
            @staticmethod
            def is_complete(data, template):
                return True

            @staticmethod
            def extract(iso, data, template):
                raise AssertionError("not needed: is_complete is true")

        return Data

    def test_command_line(self):
        cmd = extras.converter_command("node", self.install, self.dir / "d", self.iso, self.dir / "o.zip", hd=False)
        joined = " ".join(cmd)
        for part in ("--campaign 1", "--template LEVEL1NA", "--id ootf-levels", f"--iso {self.iso}", "--ootf", "--ps2"):
            self.assertIn(part, joined)
        self.assertIn("ootf-convert.ts", joined)
        self.assertNotIn("--hd", cmd)
        hd = extras.converter_command("node", self.install, self.dir / "d", self.iso, self.dir / "o.zip", hd=True)
        self.assertEqual(hd[hd.index("--hd-sources-only") + 1], "1")
        self.assertIn("--hd", hd)

    def test_converter_script_knows_the_flag_the_builder_passes(self):
        source = (ROOT / "editor/scripts/ootf-convert.ts").read_text(encoding="utf-8")
        self.assertIn("hd-sources-only", source)

    def test_state_decides_when_to_convert_again(self):
        mods = self.work / "mods"
        mods.mkdir(parents=True)
        (mods / "ootf-levels.zip").write_bytes(b"zip")
        state = extras.convert_state(self.install, self.iso, False, "v1")
        self.assertTrue(extras.convert_needed(self.work, state), "no state yet")
        (self.work / "convert-state.json").write_text(json.dumps(state), encoding="utf-8")
        self.assertFalse(extras.convert_needed(self.work, state))
        self.assertTrue(extras.convert_needed(self.work, state, force=True))
        self.assertTrue(extras.convert_needed(self.work, dict(state, code="v2")), "converter code changed")
        self.assertTrue(extras.convert_needed(self.work, dict(state, hd=True)), "lossless HD sources are new")
        (self.work / "convert-state.json").write_text(json.dumps(dict(state, hd=True)), encoding="utf-8")
        self.assertFalse(extras.convert_needed(self.work, state), "HD sources left over are harmless")
        (mods / "ootf-levels.zip").unlink()
        self.assertTrue(extras.convert_needed(self.work, state), "the pack is gone")

    def run_convert(self, runs: list, code: int = 0, hd: bool = False, **kwargs):
        def fake_run(cmd, log_file, cwd, env=None):
            runs.append((cmd, cwd, env))
            out = Path(cmd[cmd.index("--out") + 1])
            log_file.parent.mkdir(parents=True, exist_ok=True)
            log_file.write_text(
                "warning: x\nwrote pack (23 levels, 1 files)\n" if code == 0 else "boom\nit failed\n", encoding="utf-8"
            )
            if code == 0:
                out.write_bytes(b"zip")
                if hd:
                    (out.parent / (out.name + ".hdsrc")).mkdir()
            return code

        with (
            mock.patch.object(extras, "ps2_data", self.fake_data),
            mock.patch.object(extras, "ensure_node_modules", lambda *a, **k: []),
            mock.patch.object(extras, "run_logged", fake_run),
        ):
            return extras.convert_ootf(self.install, self.iso, self.work, "node", hd=hd, root=self.root, **kwargs)

    def test_convert_writes_the_pack_the_mod_list_and_state_and_skips_when_unchanged(self):
        runs: list = []
        mods = self.run_convert(runs)
        self.assertEqual(mods, self.work / "mods")
        self.assertTrue((mods / "ootf-levels.zip").is_file())
        self.assertIn("ootf-levels = on", (mods / "mods.ini").read_text(encoding="utf-8"))
        self.assertEqual(len(runs), 1)
        self.assertEqual(runs[0][1], self.root, "the converter runs from the builder root")
        self.assertEqual(runs[0][2]["DRAKAN_ISO"], str(self.iso))
        self.run_convert(runs)
        self.assertEqual(len(runs), 1, "nothing changed, so the pack is not rebuilt")
        self.run_convert(runs, force=True)
        self.assertEqual(len(runs), 2)
        self.assertIn("ootf-convert", extras.Timings(self.work.parent / "extras-timings.json").load())

    def test_asking_for_hd_converts_again_once_to_keep_the_lossless_sources(self):
        runs: list = []
        self.run_convert(runs)
        self.run_convert(runs, hd=True)
        self.assertEqual(len(runs), 2)
        self.assertIn("--hd", runs[1][0])
        self.run_convert(runs, hd=True)
        self.assertEqual(len(runs), 2)

    def test_a_failing_converter_reports_the_log_and_leaves_no_state(self):
        with self.assertRaises(extras.ExtrasError) as caught:
            self.run_convert([], code=1)
        self.assertIn("it failed", str(caught.exception))
        self.assertFalse((self.work / "convert-state.json").exists())

    def test_missing_iso_is_reported_before_any_work(self):
        with (
            mock.patch.object(extras, "run_logged", side_effect=AssertionError("must not run")),
            self.assertRaises(extras.ExtrasError),
        ):
            extras.convert_ootf(self.install, self.dir / "none.iso", self.work, "node", root=self.root)

    def test_cli_ootf_returns_one_with_a_message_on_failure(self):
        err = io.StringIO()
        with mock.patch.object(sys, "stderr", err):
            code = extras.main(
                ["ootf", "--iso", str(self.iso), "--source", str(self.dir / "missing"), "--work", str(self.work)]
            )
        self.assertEqual(code, 1)
        self.assertIn("not converted", err.getvalue())


class CoverArtTests(unittest.TestCase):
    """The campaign cover is the one tracked picture: pinned in the guard, bundled by the kit, passed to the converter."""

    def guard(self):
        spec_guard = importlib.util.spec_from_file_location("repository_guard", ROOT / "scripts/repository_guard.py")
        guard = importlib.util.module_from_spec(spec_guard)
        spec_guard.loader.exec_module(guard)
        return guard

    def test_the_cover_is_a_png_pinned_by_the_guard(self):
        path = ROOT / extras.COVER
        data = path.read_bytes()
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        self.assertEqual(int.from_bytes(data[16:20], "big"), 7680)
        self.assertEqual(int.from_bytes(data[20:24], "big"), 4800)
        guard = self.guard()
        self.assertEqual(guard.ALLOWED_BINARIES[extras.COVER], hashlib.sha256(data).hexdigest())
        self.assertIsNone(guard.reason(extras.COVER, data))
        self.assertEqual(guard.reason(extras.COVER, data + b"x"), "allowed binary differs from its pinned SHA-256")
        self.assertIsNotNone(guard.reason("scripts/buildtool/kit/art/other.png", data), "no other picture is allowed")

    def test_the_kit_ships_the_cover(self):
        spec_kit = importlib.util.spec_from_file_location(
            "package_builder_kit", ROOT / "scripts/buildtool/package_builder_kit.py"
        )
        kit = importlib.util.module_from_spec(spec_kit)
        spec_kit.loader.exec_module(kit)
        kept = kit.select([(extras.COVER, (ROOT / extras.COVER).read_bytes())], kit.load_guard())
        self.assertIn(extras.COVER, kept)

    def test_bundled_cover_is_found_or_absent(self):
        self.assertEqual(extras.bundled_cover(ROOT), ROOT / extras.COVER)
        with tempfile.TemporaryDirectory() as temp:
            self.assertIsNone(extras.bundled_cover(Path(temp)))

    def test_the_converter_gets_the_cover_and_a_changed_cover_converts_again(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            install = make_install(root / "Drakan")
            iso = root / "game.iso"
            iso.write_bytes(b"iso")
            cover = root / "cover.png"
            cover.write_bytes(b"one")
            command = extras.converter_command("node", install, root / "d", iso, root / "o.zip", False, cover)
            self.assertEqual(command[command.index("--cover") + 1], str(cover))
            self.assertNotIn(
                "--cover", extras.converter_command("node", install, root / "d", iso, root / "o.zip", False)
            )
            work = root / "work"
            (work / "mods").mkdir(parents=True)
            (work / "mods" / "ootf-levels.zip").write_bytes(b"zip")
            state = extras.convert_state(install, iso, False, "v1", cover)
            (work / "convert-state.json").write_text(json.dumps(state), encoding="utf-8")
            self.assertFalse(extras.convert_needed(work, state))
            cover.write_bytes(b"two")
            self.assertTrue(extras.convert_needed(work, extras.convert_state(install, iso, False, "v1", cover)))
            self.assertTrue(extras.convert_needed(work, extras.convert_state(install, iso, False, "v1", None)))


class HdToolsTests(unittest.TestCase):
    def setUp(self):
        quiet = mock.patch.object(sys, "stdout", io.StringIO())
        quiet.start()
        self.addCleanup(quiet.stop)
        self.temp = tempfile.TemporaryDirectory()
        self.dir = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.root = self.dir / "kit"
        (self.root / "scripts/buildtool/kit").mkdir(parents=True)
        shutil.copyfile(LOCK, self.root / "scripts/buildtool/kit/installer.lock.json")
        self.lock = extras.load_lock(LOCK)
        self.fetched: list = []

    def fetch(self, spec, cache):
        self.fetched.append(spec["url"])
        cache.mkdir(parents=True, exist_ok=True)
        path = cache / spec["url"].rsplit("/", 1)[1]
        if "realesrgan" in spec["url"]:
            path.write_bytes(
                zip_bytes(
                    {
                        "realesrgan-ncnn-vulkan.exe": b"MZ",
                        "models/realesrgan-x4plus.bin": b"m",
                        "models/realesrgan-x4plus.param": b"p",
                    }
                )
            )
        else:
            name = "numpy" if "numpy" in spec["url"] else "PIL"
            path.write_bytes(zip_bytes({f"{name}/__init__.py": b"", f"{name}-1.dist-info/METADATA": b"m"}))
        return path

    def test_everything_is_downloaded_once_when_the_python_has_neither_package(self):
        has = lambda python, site=None: site is not None and (site / "numpy/__init__.py").is_file()
        with (
            mock.patch.object(extras, "python_has_modules", has),
            mock.patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stdout="True\n")),
        ):
            tools = extras.provision_hd_tools(self.root, self.lock, self.dir / "cache", "py", self.fetch)
        self.assertEqual(tools.exe, self.root / "tools/hdpack/realesrgan/realesrgan-ncnn-vulkan.exe")
        self.assertEqual(tools.site, self.root / "tools/hd-site")
        self.assertEqual(len(self.fetched), 3)
        with mock.patch.object(extras, "python_has_modules", has):
            again = extras.provision_hd_tools(self.root, self.lock, self.dir / "cache", "py", self.fetch)
        self.assertEqual(again, tools)
        self.assertEqual(len(self.fetched), 3, "tools that are in place are not downloaded again")

    def test_a_python_with_numpy_and_pillow_needs_no_wheels(self):
        with mock.patch.object(extras, "python_has_modules", lambda python, site=None: True):
            tools = extras.provision_hd_tools(self.root, self.lock, self.dir / "cache", "py", self.fetch)
        self.assertIsNone(tools.site)
        self.assertEqual(len(self.fetched), 1, "only Real-ESRGAN")

    def test_the_pinned_wheels_are_refused_for_another_python_version(self):
        with (
            mock.patch.object(extras, "python_has_modules", lambda python, site=None: False),
            mock.patch.object(subprocess, "run", return_value=subprocess.CompletedProcess([], 0, stdout="False\n")),
            self.assertRaises(extras.ExtrasError) as caught,
        ):
            extras.provision_hd_tools(self.root, self.lock, self.dir / "cache", "py", self.fetch)
        self.assertIn("3.12", str(caught.exception))

    def test_run_hd_indexes_then_upscales_in_two_passes_with_the_site_on_the_path(self):
        folder = self.root / "tools/hdpack/realesrgan"
        (folder / "models").mkdir(parents=True)
        (folder / "realesrgan-ncnn-vulkan.exe").write_bytes(b"MZ")
        (folder / "models/realesrgan-x4plus.bin").write_bytes(b"m")
        calls: list = []

        def run(cmd, env=None, check=False):
            calls.append((cmd, env))
            return subprocess.CompletedProcess(cmd, 0)

        pack = self.dir / "ootf-levels.zip"
        pack.write_bytes(b"zip")
        has = lambda python, site=None: site is not None
        with mock.patch.object(extras, "python_has_modules", has):
            (self.root / "tools/hd-site").mkdir(parents=True)
            textures = extras.run_hd(
                self.dir / "game.iso",
                self.dir / "play",
                self.root / "build/hdpack",
                [pack],
                "py",
                1,
                self.root,
                self.fetch,
                run,
            )
        self.assertEqual(textures, self.dir / "play/textures")
        subcommands = [cmd[3] for cmd, _ in calls]
        self.assertEqual(subcommands, ["extract", "upscale", "modextract", "upscale"])
        for cmd, env in calls:
            self.assertEqual(cmd[:2], ["py", "-I"], "hd_pack runs isolated")
            self.assertEqual(env["DRAKAN_PY_SITE"], str(self.root / "tools/hd-site"))
        upscale = calls[1][0]
        self.assertEqual(upscale[upscale.index("--out") + 1], str(self.dir / "play/textures"))
        self.assertEqual(upscale[upscale.index("--gpu") + 1], "1")
        self.assertEqual(calls[2][0][calls[2][0].index("--zip") + 1], str(pack))
        timed = extras.Timings(self.root / "build/extras-timings.json").load()
        for key in ("hd-index", "hd-upscale", "hd-upscale-ootf"):
            self.assertIn(key, timed)

    def test_a_failed_upscale_is_reported_so_the_build_can_say_how_to_continue(self):
        folder = self.root / "tools/hdpack/realesrgan"
        (folder / "models").mkdir(parents=True)
        (folder / "realesrgan-ncnn-vulkan.exe").write_bytes(b"MZ")
        (folder / "models/realesrgan-x4plus.bin").write_bytes(b"m")
        (self.root / "build/hdpack/src").mkdir(parents=True)
        (self.root / "build/hdpack/index.json").write_text("{}", encoding="utf-8")

        def run(cmd, env=None, check=False):
            return subprocess.CompletedProcess(cmd, 1 if cmd[3] == "upscale" else 0)

        with (
            mock.patch.object(extras, "python_has_modules", lambda python, site=None: True),
            self.assertRaises(extras.ExtrasError) as caught,
        ):
            extras.run_hd(
                self.dir / "g.iso",
                self.dir / "play",
                self.root / "build/hdpack",
                [],
                "py",
                None,
                self.root,
                self.fetch,
                run,
            )
        self.assertIn("again", str(caught.exception))

    def test_hd_pack_reads_the_private_site_folder(self):
        text = (ROOT / "analysis/scripts/hd_pack.py").read_text(encoding="utf-8")
        self.assertIn("DRAKAN_PY_SITE", text)
        self.assertLess(text.index("DRAKAN_PY_SITE"), text.index("import numpy"))


class PolicyTests(unittest.TestCase):
    def test_the_lock_pins_every_extra_download(self):
        lock = json.loads(LOCK.read_text(encoding="utf-8"))
        for name in ("numpy", "pillow", "realesrgan", "fflate"):
            with self.subTest(name=name):
                self.assertTrue(lock["hd"][name]["url"].startswith("https://"))
                self.assertRegex(lock["hd"][name]["sha256"], r"^[0-9a-f]{64}$")
        python = re.search(r"python-(\d+)\.(\d+)", lock["python"]["url"])
        tag = f"cp{python.group(1)}{python.group(2)}"
        for wheel in ("numpy", "pillow"):
            self.assertIn(tag, lock["hd"][wheel]["url"], "wheels must match the pinned embeddable Python")
            self.assertIn("win_amd64", lock["hd"][wheel]["url"])

    def test_wizard_text_matches_the_estimates(self):
        text = re.sub(r"'\s*\+\s*\n\s*'", "", ISS.read_text(encoding="utf-8"))
        summary = extras.reference_summary()
        for key in ("ootf", "hd", "both"):
            time, disk = summary[key]
            with self.subTest(key=key):
                self.assertIn(time, text)
                self.assertIn(f"{round(disk)} GB", text)

    def test_wizard_passes_the_choices_to_the_builder(self):
        text = ISS.read_text(encoding="utf-8")
        for needle in (
            "-OotfSource",
            "-HdTextures on",
            "-HdTextures off",
            "-OotfSource none",
            "-EnableDlss",
            "HdPage.Values[0]",
            "DlssPage.Values[0]",
        ):
            self.assertIn(needle, text)

    def test_dlss_hd_textures_and_the_other_options_have_their_own_pages(self):
        text = ISS.read_text(encoding="utf-8")
        order = [
            "CreateInputOptionPage(OotfPage.ID",
            "CreateInputOptionPage(DlssPage.ID",
            "CreateInputOptionPage(HdPage.ID",
            "CreateInputQueryPage(OptionsPage.ID",
        ]
        positions = [text.index(item) for item in order]
        self.assertEqual(positions, sorted(positions), "Order of the Flame, DLSS, HD textures, other options, CPU")
        dlss = text[text.index("DlssPage := ") : text.index("HdPage := ")]
        hd = text[text.index("HdPage := ") : text.index("OptionsPage := ")]
        other = text[text.index("OptionsPage := ") : text.index("CpuPage := ")]
        self.assertIn("NVIDIA RTX SDKs License", dlss)
        self.assertNotIn("Real-ESRGAN", dlss)
        self.assertIn("Real-ESRGAN", hd)
        self.assertNotIn("DLSS", hd)
        self.assertNotIn("DLSS", other)
        self.assertNotIn("Real-ESRGAN", other)
        self.assertEqual(other.count("OptionsPage.Add("), 4, "browser, priority, start the game, VU1 blocks")

    def test_the_vu1_option_is_explained(self):
        text = re.sub(r"'\s*\+\s*\n\s*'", "", ISS.read_text(encoding="utf-8"))
        for needle in ("VU1 microcode", "vector processor", "2 GB of memory per compiler", "17 GB", "same speed"):
            self.assertIn(needle, text)

    def test_the_wizard_checks_the_order_of_the_flame_choice_before_the_build(self):
        text = ISS.read_text(encoding="utf-8")
        self.assertIn("function OotfProblem", text)
        self.assertIn("Problem := OotfProblem(", text, "the page cannot be left with a wrong choice")
        self.assertIn("Dragon.rrc", text)
        self.assertIn("DATA1.CAB", text)
        self.assertIn("check-source --deep", text, "the builder's own checker runs once its files are installed")
        self.assertIn("ssPostInstall", text)
        self.assertIn("and not OotfRejected", text, "a rejected choice is not passed to the build")

    def test_builder_script_exposes_and_remembers_the_options(self):
        text = BUILD_DRAKAN.read_text(encoding="utf-8")
        for needle in (
            "[string]$OotfSource",
            "[string]$HdTextures",
            "[int]$HdGpu",
            "'extras.py'",
            "build/ootf",
            "ModDirectory",
            "ootfSource",
            "hdTextures",
            "'--ootf-pack'",
            "check-source --deep",
            "left out of this build",
        ):
            self.assertIn(needle, text)
        # The pack is converted before the long game build, the HD textures made after it.
        self.assertLess(text.index("'ootf'"), text.index("Invoke-BuildWithRetry (Join-Path"))
        self.assertGreater(text.index("'hd'"), text.index("Invoke-BuildWithRetry (Join-Path"))

    def test_builder_script_parses(self):
        pwsh = shutil.which("pwsh")
        if not pwsh:
            self.skipTest("pwsh not on PATH")
        command = (
            "$e=$null;[void][System.Management.Automation.Language.Parser]::ParseFile($env:BUILD_DRAKAN_PS1,[ref]$null,[ref]$e);"
            "if($e){$e|ForEach-Object{$_.Message};exit 1}"
        )
        result = subprocess.run(
            [pwsh, "-NoProfile", "-Command", command],
            capture_output=True,
            text=True,
            check=False,
            env=dict(os.environ, BUILD_DRAKAN_PS1=str(BUILD_DRAKAN)),
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_the_kit_runs_the_script_with_its_embeddable_python(self):
        spec_kit = importlib.util.spec_from_file_location(
            "package_builder_kit", ROOT / "scripts/buildtool/package_builder_kit.py"
        )
        kit = importlib.util.module_from_spec(spec_kit)
        spec_kit.loader.exec_module(kit)
        names = {path.relative_to(ROOT).as_posix() for path in kit.builder_scripts(ROOT)}
        self.assertIn("scripts/buildtool/extras.py", names, "the smoke test must run it under the kit Python")

    def test_the_script_needs_nothing_outside_the_standard_library(self):
        tree = (ROOT / "scripts/buildtool/extras.py").read_text(encoding="utf-8")
        imports = set(re.findall(r"^(?:from|import) ([\w.]+)", tree, re.MULTILINE))
        stdlib = set(sys.stdlib_module_names)
        self.assertEqual({name.split(".")[0] for name in imports} - stdlib - {"__future__"}, set())

    def test_the_shared_extractor_is_the_one_the_harness_uses(self):
        harness = (ROOT / "scripts/harness/ootf_level_run.py").read_text(encoding="utf-8")
        self.assertIn("from ootf_ps2_data import extract, is_complete", harness)


if __name__ == "__main__":
    unittest.main()
