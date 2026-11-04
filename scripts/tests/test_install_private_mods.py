import importlib.util
import tempfile
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "install_private_mods", Path(__file__).parents[1] / "buildtool/install_private_mods.py"
)
MOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MOD)


class PrivateModInstallTest(unittest.TestCase):
    def test_preserves_unrelated_activation_and_installs_pack(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source, target = root / "source", root / "target"
            source.mkdir()
            target.mkdir()
            (source / "ootf.zip").write_bytes(b"fixture")
            (source / "mods.ini").write_text("[mods]\nootf = on\n")
            (target / "mods.ini").write_text("[mods]\ncustom = off\n")
            MOD.install(source, target)
            self.assertEqual((target / "ootf.zip").read_bytes(), b"fixture")
            self.assertIn("custom = off", (target / "mods.ini").read_text())
            self.assertIn("ootf = on", (target / "mods.ini").read_text())
            MOD.install(source / "ootf.zip", target, "ootf")
            self.assertIn("custom = off", (target / "mods.ini").read_text())


if __name__ == "__main__":
    unittest.main()
