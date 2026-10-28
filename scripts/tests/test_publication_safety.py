"""Source-tree policy regression tests."""

import unittest

from repository_guard import ROOT, reason

# Built at runtime so this test file does not itself trip the tracked-file key scan.
PRIVATE_KEY_MARKER = b"-----BEGIN " + b"PRIVATE KEY-----"


class SourcePublicationTests(unittest.TestCase):
    def test_nvidia_binary_is_not_allowlisted(self):
        self.assertIsNotNone(reason("third_party/nvidia/dlss/nvngx_dlss.dll"))
        self.assertIsNotNone(reason("third_party/nvidia/dlss/nvngx_dlss.dll", b"MZ" + bytes(40)))

    def test_legitimate_proprietary_inputs_remain_forbidden(self):
        names = (
            "iso/Drakan.iso",
            "analysis/vu/vu1_micromem.bin",
            "runtime/data/SCES_500.06",
            "analysis/input/decompiled.c",
            "ps2recomp/ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc",
            "assets/ENGLISH/LEVELS.RRC",
            "analysis/iop/iso_listing.txt",
        )
        for name in names:
            with self.subTest(path=name):
                self.assertIsNotNone(reason(name))

    def test_unrecognized_binaries_and_private_key_patterns(self):
        self.assertIsNotNone(reason("runtime/native/data.txt", b"MZ" + bytes(80)))
        self.assertIsNotNone(reason("runtime/native/data.txt", b"\x7fELF" + bytes(80)))
        self.assertIsNotNone(reason("runtime/native/source.txt", PRIVATE_KEY_MARKER))

    def test_original_icon_is_hash_pinned(self):
        icon = "ps2recomp/ps2xRuntime/src/res/drakan.ico"
        self.assertIsNone(reason(icon))
        self.assertIsNotNone(reason(icon, b"not-the-reviewed-icon"))

    def test_third_party_notices_are_allowed(self):
        self.assertIsNone(reason("THIRD_PARTY.md", b"# Third-party licences\n"))

    def test_local_builder_no_longer_requires_vendored_dlss_binary(self):
        builder = (ROOT / "scripts/buildtool/Build-LocalGame.ps1").read_text(encoding="utf-8")
        self.assertNotIn("third_party/nvidia/dlss/nvngx_dlss.dll", builder)
        self.assertIn("local/nvidia/dlss/nvngx_dlss.dll", builder)

    def test_nvidia_dll_ignore_exception_is_gone(self):
        ignored = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertNotIn("!/third_party/nvidia/dlss/nvngx_dlss.dll", ignored)


if __name__ == "__main__":
    unittest.main()
