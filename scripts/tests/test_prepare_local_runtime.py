"""CPU-only fixtures; contains synthetic ELF data, never real game instructions."""

import contextlib
import hashlib
import importlib.util
import io
import json
import struct
import tempfile
import tomllib
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_local_runtime", ROOT / "scripts/prepare_local_runtime.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def fnv(data):
    result = 0xCBF29CE484222325
    for byte in data:
        result = ((result ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return result


class PrepareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        data = bytearray(prepare.ELF_SIZE)
        data[:6] = b"\x7fELF\x01\x01"
        for index, (offset, size) in enumerate(prepare.VU_CHUNKS):
            data[offset : offset + size] = bytes((position + index) % 251 for position in range(size))
        cls.elf_bytes = bytes(data)
        cls.image = b"".join(data[offset : offset + size] for offset, size in prepare.VU_CHUNKS)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="drakan-runtime-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.elf = self.root / "owned.elf"
        self.elf.write_bytes(self.elf_bytes)
        self.hashes = mock.patch.multiple(
            prepare,
            ELF_SHA256=hashlib.sha256(self.elf_bytes).hexdigest(),
            VU_SHA256=hashlib.sha256(self.image).hexdigest(),
            VU_FNV=fnv(self.image),
        )
        self.hashes.start()
        self.addCleanup(self.hashes.stop)

    def runtime(self, name="runtime"):
        result = self.root / name
        result.mkdir()
        (result / "CMakeLists.txt").write_text("project(Upstream)\n")
        (result / "ps2xRuntime").mkdir()
        (result / "ps2xRuntime/CMakeLists.txt").write_text("# runtime fixture\n")
        for relative in (prepare.VU_RELATIVE, prepare.ICON_RELATIVE):
            (result / relative).parent.mkdir(parents=True)
            ((result / relative).parent / ".keep").write_text("fixture\n")
        return result

    def run_prepare(self, runtime):
        return prepare.prepare_runtime(runtime, self.elf)

    def test_chunk_reconstruction_and_include_words(self):
        self.assertEqual(prepare.extract_vu(self.elf), self.image)
        text = prepare.render_vu_include(self.image).decode()
        self.assertIn("kVu1ImagePairs = 1523", text)
        self.assertIn(f"0x{fnv(self.image):016x}ull", text)
        self.assertEqual(text.count("ull,"), 1523)
        first = struct.unpack_from("<Q", self.image)[0]
        self.assertIn(f"0x{first:016x}ull,", text)

    def test_wrong_elf_hash_writes_nothing(self):
        damaged = bytearray(self.elf_bytes)
        damaged[100] ^= 1
        self.elf.write_bytes(damaged)
        destination = self.root / "absent"
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            self.run_prepare(destination)
        self.assertFalse(destination.exists())

    def test_short_elf_rejected(self):
        self.elf.write_bytes(b"\x7fELF")
        with self.assertRaisesRegex(ValueError, "size"):
            prepare.extract_vu(self.elf)

    def test_vu_hash_rejected(self):
        with mock.patch.object(prepare, "VU_SHA256", "0" * 64):
            with self.assertRaisesRegex(ValueError, "verification"):
                prepare.extract_vu(self.elf)

    def test_fnv_rejected(self):
        with mock.patch.object(prepare, "VU_FNV", 0):
            with self.assertRaisesRegex(ValueError, "FNV"):
                prepare.render_vu_include(self.image)

    def test_missing_assets_only_and_idempotent(self):
        runtime = self.runtime()
        before = (runtime / "CMakeLists.txt").read_bytes()
        result = self.run_prepare(runtime)
        self.assertEqual(len(result["created"]), 2)
        self.assertEqual((runtime / "CMakeLists.txt").read_bytes(), before)
        asset = runtime / prepare.VU_RELATIVE
        stamp = asset.stat().st_mtime_ns
        self.assertEqual(self.run_prepare(runtime)["created"], [])
        self.assertEqual(asset.stat().st_mtime_ns, stamp)

    def test_existing_image_conflict_preserves_everything(self):
        runtime = self.runtime()
        target = runtime / prepare.VU_RELATIVE
        target.write_text("custom existing input")
        with self.assertRaisesRegex(ValueError, "overwrite"):
            self.run_prepare(runtime)
        self.assertEqual(target.read_text(), "custom existing input")
        self.assertFalse((runtime / prepare.ICON_RELATIVE).exists())

    def test_existing_custom_icon_preserved(self):
        runtime = self.runtime()
        target = runtime / prepare.ICON_RELATIVE
        target.write_bytes(b"existing private artwork")
        self.assertEqual(self.run_prepare(runtime)["created"], [prepare.VU_RELATIVE.as_posix()])
        self.assertEqual(target.read_bytes(), b"existing private artwork")

    def test_original_icon_container(self):
        data = prepare.original_icon()
        self.assertEqual(data, prepare.original_icon())
        self.assertEqual(struct.unpack_from("<HHH", data), (0, 1, 1))
        width, height, _, _, planes, bits, length, offset = struct.unpack_from("<BBBBHHII", data, 6)
        self.assertEqual((width, height, planes, bits, offset), (32, 32, 1, 32, 22))
        self.assertEqual(length + offset, len(data))
        self.assertEqual(struct.unpack_from("<Iii", data, offset), (40, 32, 64))

    def test_asset_path_escape_rejected(self):
        runtime = self.runtime().resolve()
        with self.assertRaisesRegex(ValueError, "escapes"):
            prepare.checked_target(runtime, Path("../escaped.ico"))
        self.assertFalse((runtime.parent / "escaped.ico").exists())

    def test_wrapper_cli_assets_only_json(self):
        runtime = self.runtime()
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = prepare.main(["--runtime-source", str(runtime), "--elf", str(self.elf)])
        self.assertEqual(result, 0)
        manifest = json.loads(output.getvalue())
        self.assertTrue(manifest["private_local_output"])
        self.assertEqual(manifest["vu_pairs"], 1523)

    def test_public_config_has_only_relative_input_paths(self):
        config = tomllib.loads((ROOT / "configs/drakan.toml").read_text())
        general = config["general"]
        for key in ("input", "output", "functions_csv", "jump_tables_csv"):
            self.assertFalse(Path(general[key]).is_absolute())
            self.assertNotIn(":", general[key])
        self.assertGreater(len(general["stubs"]), 150)
        self.assertGreater(len(general["entry_points"]), 100)
        self.assertFalse(general["indirect_fallback"])
        self.assertFalse(general["emit_comments"])


if __name__ == "__main__":
    unittest.main()
