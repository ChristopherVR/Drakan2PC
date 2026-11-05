"""Private game harnesses must follow the selected deployment and preserve cards / source assets."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
from netplay_two_instances import ROOT, prepare_folder


class HarnessAssetLinksTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Fresh checkouts have no build directory; the harness requires destinations beneath it.
        (ROOT / "build").mkdir(parents=True, exist_ok=True)

    def test_switching_deployments_refreshes_assets_without_touching_source_or_cards(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            first, second, play = root / "first", root / "second", root / "play"
            for source in (first, second):
                (source / "mods").mkdir(parents=True)
                (source / "mods" / "version.txt").write_text(source.name)
                (source / "drakan.exe").write_text("synthetic executable placeholder; never launched")
            prepare_folder(first, first / "drakan.exe", play)
            (play / "mc0").mkdir()
            (play / "mc0" / "keep.txt").write_text("private save")
            prepare_folder(second, second / "drakan.exe", play)
            self.assertEqual((play / "mods").resolve(), (second / "mods").resolve())
            self.assertEqual((play / "mods" / "version.txt").read_text(), "second")
            self.assertEqual((first / "mods" / "version.txt").read_text(), "first")
            self.assertEqual((play / "mc0" / "keep.txt").read_text(), "private save")

    def test_regular_directories_are_preserved(self):
        with tempfile.TemporaryDirectory(dir=ROOT / "build") as temporary:
            root = Path(temporary)
            source, play = root / "source", root / "play"
            (source / "mods").mkdir(parents=True)
            (source / "drakan.exe").write_text("synthetic executable placeholder; never launched")
            (play / "mods").mkdir(parents=True)
            keep = play / "mods" / "keep.txt"
            keep.write_text("existing data")
            with self.assertRaises(ValueError):
                prepare_folder(source, source / "drakan.exe", play)
            self.assertEqual(keep.read_text(), "existing data")


if __name__ == "__main__":
    unittest.main()
