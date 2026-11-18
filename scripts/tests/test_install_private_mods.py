import importlib.util
import tempfile
import unittest
import zipfile
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

    def test_refuses_an_ootf_pack_that_would_orphan_saves_or_drop_migrations(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            target = root / "play"
            source = root / "ootf-levels.zip"

            def make_pack(build, revision):
                with zipfile.ZipFile(source, "w") as archive:
                    archive.writestr("mod.toml", f'id = "ootf-levels"\nbuild = "{build}"\nsave_revision = {revision}\n')

            make_pack("original", 2)
            MOD.install(source, target)
            installed = target / source.name
            original = installed.read_bytes()
            for build, revision in [("changed", 3), ("original", 1)]:
                make_pack(build, revision)
                with self.assertRaisesRegex(ValueError, "installed Order of the Flame pack was kept"):
                    MOD.install(source, target)
                self.assertEqual(installed.read_bytes(), original)
            make_pack("original", 3)
            MOD.install(source, target)
            self.assertEqual(installed.read_bytes(), source.read_bytes())


if __name__ == "__main__":
    unittest.main()
