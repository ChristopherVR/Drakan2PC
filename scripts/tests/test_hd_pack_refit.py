"""Asset-free checks of hd_pack.py's refit step (synthetic textures, no GPU).

The upscaling network may add detail finer than a source texel, but the picture has to stay the source's: its colour
(the network drifts by up to 20 levels on some textures), the grain of textures it flattened, and soft alpha planes
(smoke, clouds) in which it draws shards. The step must leave cut-out edges and transparent filler alone, keep tiling
textures seamless and run once per image.
"""

import argparse
import importlib.util
import json
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


def noise(size, seed, lo=60, hi=200):
    rng = np.random.default_rng(seed)
    rgba = np.zeros((size, size, 4), np.uint8)
    rgba[..., :3] = rng.integers(lo, hi, (size, size, 1))
    rgba[..., 3] = 255
    return rgba


def enlarge(rgba, f=4, mode=Image.BICUBIC):
    rgb = Image.fromarray(rgba[..., :3]).resize((rgba.shape[1] * f, rgba.shape[0] * f), mode)
    a = Image.fromarray(rgba[..., 3]).resize((rgba.shape[1] * f, rgba.shape[0] * f), mode)
    return np.dstack([np.array(rgb), np.array(a)])


def down(hd, f=4):
    h, w = hd.shape[0] // f, hd.shape[1] // f
    return hd[..., :3].astype(np.float32).reshape(h, f, w, f, 3).mean(axis=(1, 3))


@unittest.skipIf(PACK is None, "hd_pack.py imports failed")
class RefitTests(unittest.TestCase):
    def test_colour_drift_is_removed_and_detail_kept(self):
        src = noise(32, 1)
        hd = enlarge(src)
        fine = np.random.default_rng(2).integers(-12, 13, hd.shape[:2])  # the network's sub-texel detail, zero mean
        fine = fine - np.repeat(np.repeat(down(np.dstack([fine] * 3).astype(np.float32))[..., 0], 4, 0), 4, 1)
        drifted = np.clip(hd[..., :3].astype(np.float32) + fine[..., None] + np.array([14.0, -9.0, 5.0]), 0, 255)
        hd[..., :3] = drifted.astype(np.uint8)
        out, info = PACK.refit_image(src, hd)
        self.assertGreater(info["shift_before"], 8.0)
        self.assertLess(info["shift_after"], 1.0)
        self.assertFalse(info["flattened"])
        # the fine detail survives: what was added is smooth over a source texel
        added = out[..., :3].astype(np.float32) - hd[..., :3].astype(np.float32)
        block = added.reshape(32, 4, 32, 4, 3)
        self.assertLess(float(block.std(axis=(1, 3)).mean()), 1.5)
        self.assertTrue((out[..., 3] == hd[..., 3]).all())

    def test_flattened_texture_gets_its_grain_back(self):
        src = noise(32, 3, 150, 230)
        flat = np.zeros((128, 128, 4), np.uint8)
        flat[..., :3] = int(src[..., 0].mean())
        flat[..., 3] = 255
        out, info = PACK.refit_image(src, flat)
        self.assertTrue(info["flattened"])
        err = np.abs(down(out) - src[..., :3].astype(np.float32)).mean()
        self.assertLess(err, 0.3 * float(src[..., 0].std()))
        self.assertGreater(float(out[..., 0].std()), 0.6 * float(src[..., 0].std()))

    def test_faithful_image_is_left_nearly_alone(self):
        src = noise(32, 4)
        hd = enlarge(src, 4, Image.NEAREST)
        out, info = PACK.refit_image(src, hd)
        self.assertLess(info["shift_before"], 0.01)
        self.assertLessEqual(int(np.abs(out.astype(int) - hd.astype(int)).max()), 1)

    def test_tiling_stays_seamless(self):
        size = 32
        y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
        v = 128 + 70 * np.cos(2 * np.pi * x) * np.cos(2 * np.pi * y)
        src = np.dstack([v, v, v, np.full_like(v, 255)]).astype(np.uint8)
        hd = enlarge(src)
        hd[..., :3] = np.clip(hd[..., :3].astype(int) + 20, 0, 255).astype(np.uint8)
        out, _ = PACK.refit_image(src, hd)
        o = out[..., 0].astype(int)
        seam = np.abs(o[:, 0] - o[:, -1]).mean()
        inner = np.abs(o[:, 1] - o[:, 0]).mean()
        self.assertLessEqual(seam, inner + 1.5)

    def test_cutout_keeps_its_edge_and_filler(self):
        src = noise(32, 5)
        src[:, :12, 3] = 0  # transparent strip; its colour is filler
        src[:, :12, :3] = 255
        hd = enlarge(src, 4, Image.NEAREST)
        hd[:, :48, :3] = 10  # the upscaler bled another colour under the hole
        hd[:, 48:, :3] = np.clip(hd[:, 48:, :3].astype(int) + 12, 0, 255)
        out, info = PACK.refit_image(src, hd)
        self.assertFalse(info["soft_alpha"])
        self.assertTrue((out[..., 3] == hd[..., 3]).all())  # binary alpha is not touched
        visible = np.abs(down(out)[:, 14:] - src[:, 14:, :3].astype(np.float32)).mean()
        self.assertLess(visible, 2.0)  # the visible part is corrected
        self.assertLess(float(out[:, :32, :3].mean()), 40)  # the filler was not dragged toward the white hole colour

    def test_soft_alpha_takes_the_source_plane(self):
        size = 32
        y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
        puff = np.clip(1 - np.sqrt((x - 0.5) ** 2 + (y - 0.5) ** 2) * 2.5, 0, 1) * 255
        src = np.full((size, size, 4), 255, np.uint8)
        src[..., 3] = puff.astype(np.uint8)
        hd = enlarge(src)
        shards = hd.copy()
        shards[40:60, 30:90, 3] = 255  # a stroke the network drew into the alpha
        self.assertTrue(PACK.soft_alpha(src[..., 3]))
        out, info = PACK.refit_image(src, shards)
        self.assertTrue(info["soft_alpha"])
        self.assertLess(np.abs(out[..., 3].astype(int) - hd[..., 3].astype(int)).mean(), 2.0)
        kept, info = PACK.refit_image(src, shards, ui_alpha=True)  # HUD art keeps the network's alpha
        self.assertFalse(info["soft_alpha"])
        self.assertTrue((kept[..., 3] == shards[..., 3]).all())
        edge = np.zeros((size, size), np.uint8)
        edge[8:24, 8:24] = 255
        edge[8, 8:24] = 128  # one anti-aliased rim
        self.assertFalse(PACK.soft_alpha(edge))

    def test_sizes_that_do_not_fit_are_skipped(self):
        src = noise(32, 6)
        self.assertIsNone(PACK.refit_image(src, np.zeros((100, 128, 4), np.uint8))[0])
        self.assertIsNone(PACK.refit_image(src, src.copy())[0])

    def test_command_runs_once_per_image(self):
        with tempfile.TemporaryDirectory() as tmp:
            work, out = Path(tmp) / "work", Path(tmp) / "out"
            (work / "src").mkdir(parents=True)
            out.mkdir()
            src = noise(32, 7)
            hd = enlarge(src)
            hd[..., :3] = np.clip(hd[..., :3].astype(int) + 15, 0, 255).astype(np.uint8)
            name = "00000000000000c1"
            Image.fromarray(src).save(work / "src" / (name + ".png"))
            Image.fromarray(hd).save(out / (name + ".png"))
            Image.fromarray(hd).save(out / "00000000000000c2.png")  # no index entry: not touched
            index = {name: dict(w=32, h=32, src=["LEVELS/X/X.TXD#1"], rgb=name, aconst=True, a0=128, amax=128)}
            json.dump(index, open(work / "index.json", "w"))
            args = argparse.Namespace(work=str(work), out=str(out), refit_out=None, limit=0)
            self.assertEqual(PACK.cmd_refit(args), 0)
            first = (out / (name + ".png")).read_bytes()
            fixed = np.array(Image.open(out / (name + ".png")).convert("RGBA"))
            self.assertLess(abs(float(down(fixed).mean()) - float(src[..., :3].mean())), 1.0)  # the drift is gone
            self.assertTrue(np.array_equal(np.array(Image.open(out / "00000000000000c2.png").convert("RGBA")), hd))
            self.assertIn(name + "@", (out / "refit_done.txt").read_text())
            self.assertEqual(PACK.cmd_refit(args), 0)  # second run: nothing to do
            self.assertEqual((out / (name + ".png")).read_bytes(), first)
            Image.fromarray(hd).save(out / (name + ".png"))  # the upscale step made the image again
            self.assertEqual(PACK.cmd_refit(args), 0)
            again = np.array(Image.open(out / (name + ".png")).convert("RGBA"))
            self.assertTrue(np.array_equal(again, fixed))
            self.assertFalse(any(f.name.startswith("refit-tmp-") for f in out.iterdir()))


if __name__ == "__main__":
    unittest.main()
