"""Original-asset turntables: fractional mesh units, depth, cutout alpha and PS2 LOD separators. No disc needed."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "analysis/scripts"))
import character_previews as previews
import coop_character_roster as roster


class PreviewTests(unittest.TestCase):
    def test_live_bundle_is_original_geometry_not_frames(self):
        positions = [(-0.1, -0.2, 0), (0.1, -0.2, 0), (0, 0.2, 0)]
        triangles = [((0, 1, 2), ((0, 0), (0, 0), (0, 0)), 0)]
        bundle = previews.model_bytes(positions, triangles, [(1, 1, b"\xff\xff\xff\xff")])
        self.assertEqual(struct.unpack_from("<4s4I", bundle), (b"DCPM", 1, 3, 1, 1))
        self.assertEqual(len(bundle), 108)

    def test_initial_roster_is_fifteen_unique_characters(self):
        self.assertEqual(len(roster.CHARACTER_IDS), 15)
        self.assertEqual(len(set(roster.CHARACTER_IDS)), 15)
        self.assertEqual(roster.CHARACTER_IDS[0], 0)
        self.assertNotIn(roster.AROKH_CLASS, roster.CHARACTER_IDS)
        self.assertNotIn(0xFBC, roster.CHARACTER_IDS)
        self.assertNotIn(0x1667, roster.CHARACTER_IDS)

    def test_fractional_model_units_fill_preview(self):
        positions = [(-0.1, -0.2, 0), (0.1, -0.2, 0), (0, 0.2, 0)]
        triangles = [((0, 1, 2), ((0, 0), (0, 0), (0, 0)), 0)]
        pixels = previews.render(positions, triangles, [(1, 1, b"\xff\x00\x00\xff")], 0)
        self.assertGreater(sum(bool(a) for a in pixels[3::4]), 5000)
        self.assertEqual(pixels[(120 * previews.WIDTH + 80) * 4 : (120 * previews.WIDTH + 80) * 4 + 4], b"\xff\0\0\xff")

    def test_depth_and_transparent_texels(self):
        positions = [(-0.1, -0.2, z) for z in (0, 0.1)]
        positions = [(-0.1, -0.2, 0), (0.1, -0.2, 0), (0, 0.2, 0), (-0.1, -0.2, 0.1), (0.1, -0.2, 0.1), (0, 0.2, 0.1)]
        uv = ((0, 0), (0, 0), (0, 0))
        triangles = [((3, 4, 5), uv, 1), ((0, 1, 2), uv, 0)]
        center = (120 * previews.WIDTH + 80) * 4
        textures = [(1, 1, b"\xff\0\0\xff"), (1, 1, b"\0\xff\0\xff")]
        pixels = previews.render(positions, triangles, textures, 0)
        self.assertEqual(pixels[center : center + 4], b"\0\xff\0\xff")
        textures[1] = (1, 1, b"\0\xff\0\0")
        pixels = previews.render(positions, triangles, textures, 0)
        self.assertEqual(pixels[center : center + 4], b"\xff\0\0\xff")

    def test_lod_polygon_separator(self):
        header = bytes(64) + struct.pack("<H", 2)
        rows = struct.pack("<Hf6I", 1, 0, 1, 0, 0, 10, 0, 8)
        rows += struct.pack("<Hf6I", 1, 100, 1, 0, 10, 5, 9, 4)
        self.assertEqual(previews.lod0_count(header + rows, 15, 13), 8)
        with self.assertRaises(ValueError):
            previews.lod0_count(header + rows, 5, 13)

    def test_noncombat_and_flying_animation_fallbacks(self):
        def character(fields):
            return {"fields": [{"name": k, "type_name": "animation", "default": v} for k, v in fields.items()]}

        mage = roster.npc_clips(character({"Base Idle": 5, "Walk": 6, "Run": 7}))
        self.assertEqual((mage["Idle"], mage["Walk"], mage["Run"], mage["Attack"]), (5, 6, 7, 0))
        dragon = roster.npc_clips(character({"Hover": 12, "Flight": 13, "Crash": 14}))
        self.assertEqual((dragon["Idle"], dragon["Walk"], dragon["Run"], dragon["Death"]), (12, 13, 13, 14))


if __name__ == "__main__":
    unittest.main()
