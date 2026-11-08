"""Pictures of mod-pack maps: discovery in mods/*.zip and injection into the pack (synthetic packs, no game needed)."""

import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
import map_previews as mp
import pack_map_previews as pk

INI = "[arena a-1]\nname = Alpha (air)\nlevel = OotF - Alpha (air)\n"


def make_pack(path: Path, ini: str = INI) -> None:
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("mod.toml", 'id = "x"\nbuild = "pinned"\n')
        z.writestr("maps/ALPHA.ini", ini)


class PackPreviewTests(unittest.TestCase):
    def test_enabled_packs_are_discovered(self):
        with tempfile.TemporaryDirectory() as d:
            mods = Path(d) / "mods"
            mods.mkdir()
            make_pack(mods / "on-pack.zip")
            make_pack(mods / "off-pack.zip", "[arena b]\nname = B\nlevel = L\n")
            (mods / "mods.ini").write_text("[mods]\noff-pack = off\n", encoding="utf-8")
            found = mp.mod_arenas(Path(d))
            self.assertEqual([(m["id"], m["pack"]) for m in found], [("a-1", "on-pack.zip")])

    def test_pictures_are_injected_once(self):
        with tempfile.TemporaryDirectory() as d:
            pics = Path(d) / "pics"
            pics.mkdir()
            pack = Path(d) / "p.zip"
            make_pack(pack)
            self.assertEqual(pk.add_previews(pack, pics), [])  # no picture yet
            (pics / "Alpha__air_.png").write_bytes(b"png1")
            self.assertEqual(pk.add_previews(pack, pics), ["previews/Alpha__air_.png"])
            self.assertEqual(pk.add_previews(pack, pics), [])  # idempotent
            with zipfile.ZipFile(pack) as z:
                self.assertEqual(z.read("previews/Alpha__air_.png"), b"png1")
                self.assertIn(b"pinned", z.read("mod.toml"))
            (pics / "Alpha__air_.png").write_bytes(b"png2")
            self.assertEqual(pk.add_previews(pack, pics), ["previews/Alpha__air_.png"])
            with zipfile.ZipFile(pack) as z:
                self.assertEqual(z.read("previews/Alpha__air_.png"), b"png2")
                self.assertEqual(len([n for n in z.namelist() if n.startswith("previews/")]), 1)


if __name__ == "__main__":
    unittest.main()
