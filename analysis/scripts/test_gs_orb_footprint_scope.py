"""CPU bounds on a shader-only orb correction; no asset or device required."""

import math
import unittest

from gs_hud_edge_audit import sprite_axis_samples


class OrbFootprintScope(unittest.TestCase):
    def test_atlas_clamp_cannot_exclude_internal_perimeter(self):
        # Captured atlas is 128 wide. Its perimeter at x88 is internal.
        samples = sprite_axis_samples(13, 73, 28.5, 88.5, pixel_center=0.5, scale=4)
        selected = [min(127, max(0, math.floor(u))) for _, u in samples]
        self.assertEqual(selected.count(88), 2)

    def test_aem_cannot_suppress_opaque_white_ct32_content(self):
        # TEXA only expands CT24/16. Even in those formats AEM suppresses black.
        pixel = (255, 255, 255, 127)
        self.assertNotEqual(pixel[:3], (0, 0, 0))
        self.assertGreater(pixel[3], 0)

    def test_same_uv_derivative_needs_endpoint_metadata(self):
        # Native draw A stops before x88; B deliberately includes it. At the
        # same local coordinate both have identical UV and screen derivative.
        a = sprite_axis_samples(13, 73, 28.5, 88.5)
        b = sprite_axis_samples(13, 74, 28.5, 89.5)
        self.assertEqual(a, b[:-1])
        self.assertNotIn(88, [math.floor(u) for _, u in a])
        self.assertEqual(math.floor(b[-1][1]), 88)
        enlarged_a = dict(sprite_axis_samples(13, 73, 28.5, 88.5, pixel_center=0.5, scale=4))
        enlarged_b = dict(sprite_axis_samples(13, 74, 28.5, 89.5, pixel_center=0.5, scale=4))
        border = [(p, u) for p, u in enlarged_a.items() if math.floor(u) == 88]
        self.assertEqual(len(border), 2)
        for p, u in border:
            self.assertEqual(enlarged_b[p], u)

    def test_native_bounds_would_preserve_intentional_outline(self):
        # A possible future draw-scoped policy needs the original endpoints.
        # This is an oracle, not a production correction or scale4 GS claim.
        for end, uv_end, expected in [(73, 88.5, 87), (74, 89.5, 88)]:
            native = sprite_axis_samples(13, end, 28.5, uv_end)
            lo, hi = min(u for _, u in native), max(u for _, u in native)
            enlarged = sprite_axis_samples(13, end, 28.5, uv_end, pixel_center=0.5, scale=4)
            constrained = [math.floor(min(hi, max(lo, u))) for _, u in enlarged]
            self.assertEqual(max(constrained), expected)

    def test_colour_or_alpha_mask_would_remove_authored_outline(self):
        # The included perimeter of draw B has exactly the same RGBA as A's.
        unwanted = intended = (255, 255, 255, 127)
        self.assertEqual(unwanted, intended)
        for reject in [lambda p: p[:3] == (255, 255, 255), lambda p: p[3] == 127]:
            self.assertTrue(reject(unwanted))
            self.assertTrue(reject(intended))


if __name__ == "__main__":
    unittest.main()
