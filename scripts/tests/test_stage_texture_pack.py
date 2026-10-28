"""HD staging preserves player files and reuses the same pack on each target."""

import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

MODULE = Path(__file__).resolve().parents[1] / "buildtool/stage_texture_pack.py"
SPEC = importlib.util.spec_from_file_location("stage_texture_pack", MODULE)
PACK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACK)


class StageTests(unittest.TestCase):
    def test_incremental_pack_copy_keeps_other_assets(self):
        with tempfile.TemporaryDirectory() as scratch:
            source, target = Path(scratch) / "source", Path(scratch) / "target"
            source.mkdir()
            target.mkdir()
            (source / "0123456789abcdef.png").write_bytes(b"image")
            (source / "alias.txt").write_bytes(b"aliases")
            (source / "notes.txt").write_bytes(b"not a pack file")
            (target / "player.ini").write_bytes(b"preserve")
            self.assertEqual(PACK.stage(source, target), 2)
            copied = target / "0123456789abcdef.png"
            timestamp = int(copied.stat().st_mtime)
            os.utime(copied, (timestamp, timestamp))
            self.assertEqual(PACK.stage(source, target), 0)
            self.assertFalse((target / "notes.txt").exists())
            self.assertEqual((target / "player.ini").read_bytes(), b"preserve")
            (source / "0123456789abcdef.png").write_bytes(b"updated image")
            self.assertEqual(PACK.stage(source, target), 1)
            self.assertEqual((target / "0123456789abcdef.png").read_bytes(), b"updated image")
            self.assertEqual(PACK.stage(source, source), 0)


if __name__ == "__main__":
    unittest.main()
