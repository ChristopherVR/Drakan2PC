"""Asset-free checks of the HD movie pack's frame key (must match drakan_fmv_hd.cpp frameKey byte for byte)."""

import importlib.util
import sys
import unittest
import zlib
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "analysis" / "scripts"


def load_pack_module():
    sys.path.insert(0, str(SCRIPTS))
    try:
        spec = importlib.util.spec_from_file_location("fmv_hd_pack", SCRIPTS / "fmv_hd_pack.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.remove(str(SCRIPTS))


try:
    PACK = load_pack_module()
except ImportError:  # numpy / Pillow missing: the builder itself cannot run either
    PACK = None


@unittest.skipIf(PACK is None, "fmv_hd_pack.py imports failed")
class FrameKeyTests(unittest.TestCase):
    def test_key_covers_the_bytes_idec_consumes(self):
        body = bytes(range(7, 200)) + b"\x00\x00"  # macroblock data with zero padding before the end marker
        frame = b"\x22" + body + b"\x00\x00\x01\xb0"
        self.assertEqual(PACK.frame_key(frame), "%08x%08x" % (zlib.crc32(body), len(body)))

    def test_flags_byte_does_not_change_the_key(self):
        body = b"\x12\x34\x56\x78" * 8
        self.assertEqual(
            PACK.frame_key(b"\x20" + body + b"\x00\x00\x01\xb0"), PACK.frame_key(b"\x22" + body + b"\x00\x00\x01\xb0")
        )

    def test_frame_without_end_marker_is_rejected(self):
        with self.assertRaises(ValueError):
            PACK.frame_key(b"\x22\x01\x02\x03")


if __name__ == "__main__":
    unittest.main()
