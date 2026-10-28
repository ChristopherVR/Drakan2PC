"""Source-only native GS vs D3D11 sprite convention fixtures. No device.

The GS oracle uses integer sample positions and top/left-inclusive bounds.
These fixtures deliberately avoid uncertain GS DDA precision edge cases.
They do not claim that the current renderer passes a GPU pixel regression.
"""

import math
import unittest
from fractions import Fraction as F

from gs_hud_edge_audit import sprite_axis_samples


def texels(samples):
    return [math.floor(u) for _, u in samples]


def pixels(samples):
    return [p for p, _ in samples]


class NativeSpriteConvention(unittest.TestCase):
    def test_half_texel_authored_point_sprite(self):
        args = (0, 4, 0.5, 4.5)
        gs = sprite_axis_samples(*args)
        current = sprite_axis_samples(*args, pixel_center=0.5)
        self.assertEqual(texels(gs), [0, 1, 2, 3])
        self.assertEqual(texels(current), [1, 2, 3, 4])

    def test_geometry_conversion_matches_gs_at_native_scale(self):
        for args in [
            (0, 4, 0.5, 4.5),
            (0.0625, 4.0625, 0.5, 4.5),
            (0.5, 4.5, 4.5, 0.5),
            (4, 0, 4.5, 0.5),
            (-0.0625, 3.9375, 0.5, 8.5),
        ]:
            with self.subTest(args=args):
                self.assertEqual(
                    sprite_axis_samples(*args), sprite_axis_samples(*args, pixel_center=0.5, position_shift=0.5)
                )

    def test_fractional_coverage_needs_position_conversion(self):
        args = (0.0625, 4.0625, 0.5, 4.5)
        self.assertEqual(pixels(sprite_axis_samples(*args)), [1, 2, 3, 4])
        self.assertEqual(pixels(sprite_axis_samples(*args, pixel_center=0.5)), [0, 1, 2, 3])
        # A UV change cannot repair the set of pixels covered by the primitive.
        self.assertEqual(pixels(sprite_axis_samples(*args, pixel_center=0.5, uv_shift=-0.5)), [0, 1, 2, 3])

    def test_both_native_top_left_and_bottom_right_rules(self):
        self.assertEqual(pixels(sprite_axis_samples(0.5, 4.5, 0, 4)), [1, 2, 3, 4])
        self.assertEqual(pixels(sprite_axis_samples(0, 4, 0, 4)), [0, 1, 2, 3])
        self.assertEqual(pixels(sprite_axis_samples(0.0625, 4.0625, 0, 4)), [1, 2, 3, 4])

    def test_constant_uv_would_be_corrupted_by_global_half_texel(self):
        args = (0, 4, 1, 1)
        self.assertEqual(texels(sprite_axis_samples(*args)), [1] * 4)
        self.assertEqual(texels(sprite_axis_samples(*args, pixel_center=0.5)), [1] * 4)
        self.assertEqual(texels(sprite_axis_samples(*args, pixel_center=0.5, uv_shift=-0.5)), [0] * 4)

    def test_texture_delta_depends_on_slope(self):
        for args in [(0, 4, 0.5, 8.5), (0, 4, 4.5, 0.5), (0, 4, 2.5, 4.5), (0, 4, 1, 1)]:
            slope = F(str(args[3] - args[2])) / F(str(args[1] - args[0]))
            with self.subTest(args=args):
                gs = sprite_axis_samples(*args)
                shifted = sprite_axis_samples(*args, pixel_center=0.5, uv_shift=-slope / 2)
                self.assertEqual(gs, shifted)
        args = (0, 4, 0.5, 8.5)  # two texels per pixel: needs -1 texel, not -0.5
        self.assertNotEqual(
            texels(sprite_axis_samples(*args)), texels(sprite_axis_samples(*args, pixel_center=0.5, uv_shift=-0.5))
        )

    def test_reversed_texture_needs_positive_uv_correction(self):
        args = (0, 4, 4.5, 0.5)
        expected = sprite_axis_samples(*args)
        self.assertEqual(expected, sprite_axis_samples(*args, pixel_center=0.5, uv_shift=0.5))
        self.assertNotEqual(texels(expected), texels(sprite_axis_samples(*args, pixel_center=0.5, uv_shift=-0.5)))

    def test_linear_texel_centres_are_half_integer(self):
        # Bilinear weights are formed from frac(u-0.5). The authored GS sample
        # u=0.5 hits a texel centre; the current D3D sample u=1 blends neighbours.
        gs = sprite_axis_samples(0, 4, 0.5, 4.5)
        current = sprite_axis_samples(0, 4, 0.5, 4.5, pixel_center=0.5)
        self.assertEqual([u - F(1, 2) - math.floor(u - F(1, 2)) for _, u in gs], [0] * 4)
        self.assertEqual([u - F(1, 2) - math.floor(u - F(1, 2)) for _, u in current], [F(1, 2)] * 4)

    def test_native_orb_highlight_does_not_sample_authored_border(self):
        xs = texels(sprite_axis_samples(13, 73, 28.5, 88.5))
        ys = texels(sprite_axis_samples(436, 494, 60.5, 2.5))
        self.assertEqual((min(xs), max(xs)), (28, 87))
        self.assertEqual((min(ys), max(ys)), (3, 60))
        self.assertFalse(set(xs) & {27, 88})
        self.assertFalse(set(ys) & {2, 61})

    def test_native_origin_conversion_alone_does_not_solve_scale4(self):
        xs = texels(sprite_axis_samples(13, 73, 28.5, 88.5, pixel_center=0.5, scale=4, position_shift=0.5))
        ys = texels(sprite_axis_samples(436, 494, 60.5, 2.5, pixel_center=0.5, scale=4, position_shift=0.5))
        self.assertEqual((xs.count(88), ys.count(2)), (2, 2))
        # Native GS has no scale4 sample grid. Continuous enlarged geometry
        # still visits samples between the last native centre and its endpoint.

    def test_existing_seam_case_cannot_distinguish_origins(self):
        for args in [(8, 40.625, 0, 64), (42, 80, 0, 64), (170, 340.625, 0, 256), (342, 512, 0, 256)]:
            self.assertEqual(pixels(sprite_axis_samples(*args)), pixels(sprite_axis_samples(*args, pixel_center=0.5)))
        # Existing seamTest checks y134 and y132, but never row133 at start133.5.
        self.assertEqual(pixels(sprite_axis_samples(133.5, 168, 0, 64))[0], 134)
        self.assertEqual(pixels(sprite_axis_samples(133.5, 168, 0, 64, pixel_center=0.5))[0], 133)

    def test_intentionally_included_border_is_preserved_by_native_rule(self):
        # Cropping/removing a certain texture colour or edge would break this.
        self.assertIn(88, texels(sprite_axis_samples(13, 74, 28.5, 89.5)))


if __name__ == "__main__":
    unittest.main()
