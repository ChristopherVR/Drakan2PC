"""pbr_pack.py: UE5 conventions (DirectX normals, ORM packing, blend classification) on synthetic textures."""

import importlib.util
import sys
import unittest
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "analysis" / "scripts"))
SPEC = importlib.util.spec_from_file_location("pbr_pack", ROOT / "analysis/scripts/pbr_pack.py")
pbr = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pbr)

OV = dict(pbr.DEFAULTS)


def tex(fn, size=64):
    y, x = np.mgrid[0:size, 0:size]
    g = np.clip(fn(x / size, y / size) * 255, 0, 255).astype(np.uint8)
    return np.dstack([g, g, g, np.full_like(g, 255)])


class PbrTests(unittest.TestCase):
    def test_normal_is_directx_green_down(self):
        # Height rises toward the image bottom (brighter rows below): the surface faces up-image, so DirectX green < 128.
        out = pbr.make_pbr(tex(lambda u, v: v * 0.6 + 0.2), 1.0, OV, "Opaque", False)
        n = out["n"][8:-8, 8:-8].astype(int)
        self.assertLess(n[..., 1].mean(), 126)
        self.assertAlmostEqual(n[..., 0].mean(), 128, delta=3)
        # Height rises to the right: red < 128.
        out = pbr.make_pbr(tex(lambda u, v: u * 0.6 + 0.2), 1.0, OV, "Opaque", False)
        self.assertLess(out["n"][8:-8, 8:-8, 0].mean(), 126)

    def test_flat_texture_gets_flat_normal_and_orm_layout(self):
        out = pbr.make_pbr(tex(lambda u, v: u * 0 + 0.5), 1.0, dict(OV, roughness=0.4, metallic=1.0), "Opaque", False)
        self.assertTrue(out["flat"])
        self.assertTrue((out["n"] == np.array([128, 128, 255])).all())
        orm = out["orm"]
        self.assertEqual(orm.shape[-1], 3)
        self.assertTrue((orm[..., 0] >= 250).all())  # no cavity: AO 1
        self.assertAlmostEqual(orm[..., 1].mean() / 255, 0.4, delta=0.01)
        self.assertTrue((orm[..., 2] == 255).all())
        self.assertEqual(out["bc"].shape[-1], 3)  # opaque: no alpha channel

    def test_albedo_range_and_alpha_kept_for_masked(self):
        rgba = tex(lambda u, v: (u > 0.5) * 1.0)
        rgba[:8, :, 3] = 0
        out = pbr.make_pbr(rgba, 1.0, OV, "Masked", False)
        self.assertEqual(out["bc"].shape[-1], 4)
        self.assertTrue((out["bc"][..., 3][:8] == 0).all())
        rgb = out["bc"][..., :3]
        self.assertGreaterEqual(int(rgb.min()), 29)
        self.assertLessEqual(int(rgb.max()), 241)

    def test_classify(self):
        e = dict(bpp=32, kind=1, aconst=False, a0=0x80, amax=0x80)
        rgba = tex(lambda u, v: u * 0 + 0.5)
        self.assertEqual(pbr.classify(e, rgba, {"LEVEL1ON"})[0], "Opaque")
        cut = rgba.copy()
        cut[:10, :, 3] = 0
        self.assertEqual(pbr.classify(e, cut, {"LEVEL1ON"})[0], "Masked")
        soft = rgba.copy()
        soft[..., 3] = 128
        self.assertEqual(pbr.classify(e, soft, {"LEVEL1ON"})[0], "Translucent")
        self.assertEqual(pbr.classify(e, rgba, {"FX"})[0], "Additive")
        self.assertEqual(pbr.classify(dict(e, bpp=24), cut, {"LEVEL1ON"})[0], "Opaque")

    def test_blur_wraps(self):
        a = np.zeros((16, 16))
        a[0, 0] = 1.0
        b = pbr.blur(a, 1.5)
        self.assertAlmostEqual(b[0, 15], b[0, 1], places=6)
        self.assertAlmostEqual(float(b.sum()), 1.0, places=6)


if __name__ == "__main__":
    unittest.main()
