"""Synthetic tests for compiler-only archive upload guards."""

import hashlib
import io
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from buildtool.audit_packages import SOURCE_ZIP, check, validate_zip


def archive(entries):
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w") as zf:
        for name, data in entries.items():
            zf.writestr(name, data)
    return output.getvalue()


class CompilerArchivePolicyTests(unittest.TestCase):
    def setUp(self):
        self.source = archive(
            {
                "PS2Recomp/ps2recomp/LICENSE": b"GNU GPL 3\n",
                "PS2Recomp/ps2xRecomp/src/compiler.cpp": b"int main() { return 0; }\n",
                "recipe.json": b"{}",
            }
        )
        self.compiler = b"MZ" + bytes(64)
        self.tool = archive(
            {
                "ps2_recomp.exe": self.compiler,
                "LICENSE-PS2Recomp.txt": b"GNU GPL 3\n",
                "NOTICES.txt": b"Compiler only.\n",
                "manifest.json": json.dumps({"compiler_sha256": hashlib.sha256(self.compiler).hexdigest()}).encode(),
                SOURCE_ZIP: self.source,
            }
        )

    def test_compiler_and_corresponding_source_accepted(self):
        self.assertIn("ps2_recomp.exe", validate_zip(self.tool, source=False))
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            tool_file = root / "ps2-recomp-windows-x64.zip"
            source_file = root / SOURCE_ZIP
            tool_file.write_bytes(self.tool)
            source_file.write_bytes(self.source)
            sums = "\n".join(
                f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}" for p in (tool_file, source_file)
            )
            (root / "SHA256SUMS.txt").write_text(sums + "\n")
            check(root)

    def test_reject_nvidia_runtime_and_game_executable(self):
        for name in ("nvngx_dlss.dll", "drakan.exe", "SCES_500.06.elf", "ENGLISH/LEVELS.RRC"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                validate_zip(archive({name: b"MZ" + bytes(64)}), source=False)

    def test_reject_embedded_microcode(self):
        with self.assertRaises(ValueError):
            validate_zip(
                archive({"PS2Recomp/ps2xRecomp/src/generated.inc": b"static kVu1Image[512];"}),
                source=True,
            )

    def test_reject_source_without_copyleft_license(self):
        with self.assertRaises(ValueError):
            validate_zip(archive({"PS2Recomp/tool.cpp": b"int x;\n"}), source=True)


if __name__ == "__main__":
    unittest.main()
