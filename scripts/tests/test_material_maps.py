"""Asset-free checks of analysis/scripts/material_maps.py (synthetic textures only).

The maps are derived from the player's textures by fixed filters: these tests pin the conventions other code relies on
(the renderer's shader, the editor, a later engine): which way the normal leans, that tiling textures give tiling maps,
that a flat picture gives a flat normal, the file names and the table the game reads, and that a run is reproducible.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "analysis" / "scripts"


def load_module():
    sys.path.insert(0, str(SCRIPTS))
    try:
        spec = importlib.util.spec_from_file_location("material_maps", SCRIPTS / "material_maps.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.remove(str(SCRIPTS))


try:
    MM = load_module()
    import numpy as np
    from PIL import Image
except ImportError:  # numpy / Pillow missing: the builder itself cannot run either
    MM = None


def bump(size=64, cx=0.5, cy=0.5, radius=0.25):
    """Grey picture with one bright round bump (bright = high)."""
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    d = np.sqrt((x - cx) ** 2 + (y - cy) ** 2)
    v = 90 + 110 * np.clip(1 - d / radius, 0, 1)
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., :3] = v[..., None]
    rgba[..., 3] = 255
    return rgba


def entry(w, h, src="LEVELS/LEVEL1ON/LEVEL1ON.TXD#1", **extra):
    e = dict(w=w, h=h, bpp=8, kind=0, src=[src], ui=False, rgb="0" * 16, aconst=True, a0=128, amax=128, apartial=0.0)
    e.update(extra)
    return e


@unittest.skipIf(MM is None, "material_maps.py imports failed")
class MapTests(unittest.TestCase):
    def test_normal_leans_away_from_a_bump(self):
        m = MM.make_maps(bump(), 1.0, "stone")
        n = m["n"].astype(np.float32) / 255 * 2 - 1
        # left of the bump the surface rises to the right: the normal leans left (-x); right of it, right (+x)
        self.assertLess(n[32, 22, 0], -0.05)
        self.assertGreater(n[32, 42, 0], 0.05)
        # above the bump (smaller row) it leans to the image top (-y), below it to the bottom (+y): green points down
        self.assertLess(n[22, 32, 1], -0.05)
        self.assertGreater(n[42, 32, 1], 0.05)
        self.assertTrue((n[..., 2] > 0.3).all())
        length = np.sqrt((n * n).sum(-1))
        self.assertTrue(np.allclose(length, 1.0, atol=0.02))
        self.assertGreater(int(m["h"][32, 32]), int(m["h"][2, 2]))  # the bump is high in the height map

    def test_flat_picture_gives_flat_maps(self):
        rgba = np.full((32, 32, 4), 120, np.uint8)
        m = MM.make_maps(rgba, 1.0, "surface")
        self.assertTrue(m["flat"])
        self.assertTrue((np.abs(m["n"][..., :2].astype(int) - 128) <= 1).all())
        self.assertTrue((m["n"][..., 2] >= 254).all())
        self.assertTrue((m["orm"][..., 0] >= 254).all())  # no occlusion

    def test_tiling_texture_gives_tiling_maps(self):
        # a bump across the corner: only continuous when the picture wraps
        size = 64
        y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
        v = 128 + 60 * np.cos(2 * np.pi * x) * np.cos(2 * np.pi * y)
        rgba = np.dstack([v, v, v, np.full_like(v, 255)]).astype(np.uint8)
        self.assertEqual(MM.tile_axes(rgba[..., :3]), (True, True))
        m = MM.make_maps(rgba, 1.0, "terrain", (True, True))
        for img in (m["n"].astype(int), m["h"].astype(int)[..., None]):
            seam = np.abs(img[:, 0] - img[:, -1]).mean()
            inner = np.abs(img[:, 1] - img[:, 0]).mean()
            self.assertLessEqual(seam, inner + 2.0)
        # the same picture treated as not tiling does not read across the border
        edge = MM.make_maps(bump(cx=0.02), 1.0, "stone", (False, False))["h"]
        self.assertLess(abs(int(edge[32, 63]) - int(edge[32, 40])), 6)  # the far side stays flat

    def test_scale_does_not_change_the_look(self):
        lo = bump(64)
        hi = np.array(Image.fromarray(lo).resize((256, 256), Image.BICUBIC))
        a = MM.make_maps(lo, 1.0, "stone")["n"].astype(np.float32)
        b = MM.make_maps(hi, 4.0, "stone", (True, True), 4)["n"].astype(np.float32)
        self.assertEqual(a.shape, b.shape)
        self.assertLess(np.abs(a - b).mean(), 6.0)  # same slopes from the source and from its 4x enlargement

    def test_classes(self):
        grey = bump()
        self.assertEqual(MM.classify(entry(64, 64), grey)[0], "stone")
        self.assertEqual(MM.classify(entry(64, 64, src="WORLDCON/PROCTEES/PROCTEES.TXD#3"), grey)[0], "terrain")
        self.assertEqual(MM.classify(entry(64, 64, src="COMMON/NPC/NPCS.TXD#3"), grey)[0], "character")
        self.assertEqual(MM.classify(entry(64, 64, src="COMMON/INTERFCE/INTERFCE.TXD#3", ui=True), grey)[0], "none")
        self.assertEqual(MM.classify(entry(64, 64, src="LEVELS/LEVEL1NA/LEVEL1TS.TXD#3"), grey)[0], "none")
        brown = grey.copy()
        brown[..., 0] = np.clip(grey[..., 0] * 1.0, 0, 255)
        brown[..., 1] = grey[..., 1] * 0.72
        brown[..., 2] = grey[..., 2] * 0.45
        self.assertEqual(MM.classify(entry(64, 64), brown)[0], "wood")
        green = grey.copy()
        green[..., 0] = grey[..., 0] * 0.5
        green[..., 2] = grey[..., 2] * 0.4
        self.assertEqual(MM.classify(entry(64, 64), green)[0], "foliage")
        soft = grey.copy()
        soft[..., 3] = np.linspace(0, 255, 64).astype(np.uint8)[None, :]
        self.assertEqual(MM.classify(entry(64, 64, aconst=False, apartial=0.9), soft)[0], "none")
        flat = np.full((16, 16, 4), 200, np.uint8)
        self.assertEqual(MM.classify(entry(16, 16), flat)[0], "none")

    def build(self, tmp, textures, **options):
        work, hd = Path(tmp) / "work", Path(tmp) / "hd"
        (work / "src").mkdir(parents=True, exist_ok=True)
        hd.mkdir(exist_ok=True)
        index = {}
        for name, (rgba, e, hd_factor) in textures.items():
            Image.fromarray(rgba).save(work / "src" / (name + ".png"))
            if hd_factor:
                big = Image.fromarray(rgba).resize(
                    (rgba.shape[1] * hd_factor, rgba.shape[0] * hd_factor), Image.BICUBIC
                )
                big.save(hd / (name + ".png"))
            index[name] = e
        json.dump(index, open(work / "index.json", "w"))
        args = argparse.Namespace(
            work=str(work), hd=str(hd), out="", scale=2, select="", limit=0, jobs=1, force=False, **options
        )
        self.assertEqual(MM.cmd_build(args), 0)
        return hd / "materials"

    def test_build_writes_the_record_format(self):
        with tempfile.TemporaryDirectory() as tmp:
            textures = {
                "00000000000000a1": (bump(64), entry(64, 64), 4),
                "00000000000000a2": (bump(32), entry(32, 32, src="WORLDCON/PROCTEES/PROCTEES.TXD#9"), 0),
                "00000000000000a3": (bump(32), entry(32, 32, src="COMMON/FX/FX.TXD#1", ui=True), 4),
            }
            out = self.build(tmp, textures)
            doc = json.load(open(out / "materials.json"))
            self.assertEqual(doc["schema"], MM.SCHEMA)
            self.assertEqual(doc["classes"], MM.CLASSES)
            a1 = doc["materials"]["00000000000000a1"]
            self.assertEqual((a1["class"], a1["size"], a1["fromHdImage"]), ("stone", [128, 128], True))
            self.assertEqual(doc["materials"]["00000000000000a2"]["class"], "terrain")
            self.assertEqual(doc["materials"]["00000000000000a3"]["class"], "none")
            for suffix, mode in (("n", "RGB"), ("orm", "RGB"), ("h", "L")):
                with Image.open(out / ("00000000000000a1_%s.png" % suffix)) as im:
                    self.assertEqual((im.size, im.mode), ((128, 128), mode))
                with Image.open(out / ("00000000000000a2_%s.png" % suffix)) as im:
                    self.assertEqual(im.size, (64, 64))
                self.assertFalse((out / ("00000000000000a3_%s.png" % suffix)).exists())
            lines = [ln.split() for ln in open(out / "materials.txt") if not ln.startswith("#")]
            self.assertEqual([ln[0] for ln in lines], ["00000000000000a1", "00000000000000a2"])
            self.assertEqual(lines[0][1:3], [str(MM.CLASSES.index("stone")), "stone"])
            self.assertEqual(lines[1][1:3], [str(MM.CLASSES.index("terrain")), "terrain"])
            self.assertTrue(0.25 <= float(lines[0][3]) <= 1.0)

    def test_build_is_reproducible_and_incremental(self):
        def digest(folder):
            h = hashlib.sha256()
            for f in sorted(os.listdir(folder)):
                h.update(f.encode())
                h.update(open(os.path.join(folder, f), "rb").read())
            return h.hexdigest()

        textures = {
            "00000000000000b1": (bump(64), entry(64, 64), 4),
            "00000000000000b2": (bump(32, 0.3), entry(32, 32), 0),
        }
        with tempfile.TemporaryDirectory() as one, tempfile.TemporaryDirectory() as two:
            a, b = self.build(one, textures), self.build(two, textures)
            self.assertEqual(digest(a), digest(b))
            stamp = os.path.getmtime(a / "00000000000000b1_n.png")
            os.utime(a / "00000000000000b1_n.png", (stamp - 100, stamp - 100))
            self.build(one, textures)  # nothing to do: the map is not written again
            self.assertEqual(os.path.getmtime(a / "00000000000000b1_n.png"), stamp - 100)
            os.remove(a / "00000000000000b1_n.png")
            self.build(one, textures)  # a missing map is made again
            self.assertEqual(digest(a), digest(b))


if __name__ == "__main__":
    unittest.main()
