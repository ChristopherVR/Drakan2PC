"""Asset-free checks of hd_pack.py's converter-source handling (no GPU: the Real-ESRGAN call is replaced by a nearest x4).

Converted (Order of the Flame) textures hand the HD step the PC source pixels, which can be smaller than the guest texture
(terrain: 64 px source, 128 px guest) or another shape (a texture the converter squared). The pack image must still be
the guest size times the factor, tiling textures must stay seamless, cut-out holes keep their bled colour, and images
made from an older kind of source are redone once.
"""

import argparse
import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "analysis" / "scripts"


def load_pack_module():
    sys.path.insert(0, str(SCRIPTS))
    try:
        spec = importlib.util.spec_from_file_location("hd_pack", SCRIPTS / "hd_pack.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module
    finally:
        sys.path.remove(str(SCRIPTS))


try:
    PACK = load_pack_module()
    import numpy as np
    from PIL import Image
except ImportError:  # numpy / Pillow missing: the builder itself cannot run either
    PACK = None


def fake_esrgan(calls):
    def run(exe, model, indir, outdir, scale=4, gpu=None, tile=0):
        calls.append((model, os.path.basename(indir)))
        for f in os.listdir(indir):
            im = Image.open(os.path.join(indir, f))
            im.resize((im.width * 4, im.height * 4), Image.NEAREST).save(os.path.join(outdir, f))

    return run


@unittest.skipIf(PACK is None, "hd_pack.py imports failed")
class SourceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.work = Path(self.tmp.name) / "work"
        self.out = Path(self.tmp.name) / "out"
        (self.work / "src").mkdir(parents=True)
        self.calls = []
        self.saved = PACK.run_esrgan
        PACK.run_esrgan = fake_esrgan(self.calls)

    def tearDown(self):
        PACK.run_esrgan = self.saved
        self.tmp.cleanup()

    def upscale(self, index, small="enlarge"):
        json.dump(index, open(self.work / "index.json", "w"))
        a = argparse.Namespace(
            exe="realesrgan.exe", work=str(self.work), out=str(self.out), limit=0, chunk=50, scale=4, gpu=None,
            small_source=small,
        )  # fmt: skip
        return PACK.cmd_upscale(a)

    def entry(self, h, rgba, guest, **kw):
        Image.fromarray(rgba, "RGBA").save(self.work / "src" / (h + ".png"))
        e = dict(w=guest[0], h=guest[1], rgb=h, src=["x#1"], ui=False, **kw)
        if kw.get("exact"):
            PACK.mark_exact(e, rgba.shape[1], rgba.shape[0])
        return e

    def test_small_opaque_source_is_enlarged_then_upscaled_once_by_default(self):
        rng = np.random.default_rng(1)
        tile = np.dstack([rng.integers(0, 255, (16, 16, 3), dtype=np.uint8), np.full((16, 16), 255, np.uint8)])
        index = {"aa": self.entry("aa", tile, (32, 32), exact=True)}
        self.assertIsNone(self.upscale(index))
        out = np.array(Image.open(self.out / "aa.png"))
        self.assertEqual(out.shape, (128, 128, 4))
        self.assertNotIn(("realesrgan-x4plus", "rgb_x4_2_in"), self.calls)
        # the network saw the 32 px enlargement (fake upscaler: nearest x4, so 4 x 4 blocks of one colour)
        self.assertTrue((out[0:4, 0:4] == out[0, 0]).all())
        self.assertIn("aa@2", (self.out / "exact_done.txt").read_text().split())

    def test_esrgan_choice_runs_the_network_twice(self):
        rng = np.random.default_rng(2)
        tile = np.dstack([rng.integers(0, 255, (16, 16, 3), dtype=np.uint8), np.full((16, 16), 255, np.uint8)])
        index = {"ab": self.entry("ab", tile, (32, 32), exact=True)}
        self.upscale(index, small="esrgan")
        self.assertEqual(np.array(Image.open(self.out / "ab.png")).shape, (128, 128, 4))
        self.assertIn(("realesrgan-x4plus", "rgb_x4_2_in"), self.calls)

    def test_lanczos_choice_skips_the_second_pass(self):
        tile = np.full((16, 16, 4), 200, np.uint8)
        index = {"bb": self.entry("bb", tile, (32, 32), exact=True)}
        self.upscale(index, small="lanczos")
        self.assertEqual(np.array(Image.open(self.out / "bb.png")).shape, (128, 128, 4))
        self.assertNotIn(("realesrgan-x4plus", "rgb_x4_2_in"), self.calls)

    def test_squared_cut_out_keeps_bled_hole_colour_and_coverage(self):
        # 16 x 8 source of a texture the converter squared to 16 x 16; left quarter is a hole
        src = np.zeros((8, 16, 4), np.uint8)
        src[..., 0], src[..., 3] = 180, 255
        src[:, :4, 3] = 0
        index = {"cc": self.entry("cc", src, (16, 16), exact=True)}
        self.upscale(index)
        out = np.array(Image.open(self.out / "cc.png"))
        self.assertEqual(out.shape, (64, 64, 4))
        hole = out[:, :12]
        self.assertTrue((hole[..., 3] < 128).all())
        self.assertTrue((out[:, 20:, 3] > 128).all())
        # the colour under the hole is the bled red, not black (a premultiplied resize would zero it)
        self.assertGreater(int(hole[..., 0].min()), 100)

    def test_resize_padded_keeps_a_tiling_texture_seamless(self):
        x = np.linspace(0, 2 * np.pi, 32, endpoint=False)
        row = (127 + 120 * np.sin(x)).astype(np.uint8)
        img = np.dstack([np.tile(row, (32, 1))] * 3)

        def seam_ratio(out):
            out = out.astype(float)[..., 0]
            seam = np.abs(out[:, 0] - out[:, -1]).mean()
            near = (np.abs(out[:, 1] - out[:, 0]).mean() + np.abs(out[:, -1] - out[:, -2]).mean()) / 2
            return seam / near

        self.assertLess(seam_ratio(PACK.resize_padded(img, (64, 64), True, True)), 1.3)
        plain = np.array(Image.fromarray(img).resize((64, 64), Image.LANCZOS))
        self.assertGreater(seam_ratio(plain), 1.5)  # what the padding prevents

    def test_older_exact_entries_are_redone_once(self):
        e = {"exact": True}
        self.assertFalse(PACK.exact_current(e, 64, 64))  # version 1 (no exactv): redo
        PACK.mark_exact(e, 64, 64)
        self.assertTrue(PACK.exact_current(e, 64, 64))
        self.assertFalse(PACK.exact_current(e, 128, 128))  # another source size: redo
        self.assertEqual(PACK.exact_token("ff", e), "ff@2")
        self.assertEqual(PACK.exact_token("ff", {"exact": True}), "ff")


if __name__ == "__main__":
    unittest.main()
