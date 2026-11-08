"""Multiplayer map pictures (scripts/harness/map_previews.py): file names, map list, crop and frame check. No game needed."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
import map_previews as mp

ROOT = Path(__file__).resolve().parents[2]


class MapPreviewTests(unittest.TestCase):
    def test_safe_name_matches_the_game(self):
        # drakan_settings_host.cpp mapSafeName; the files the old captures wrote use the same names.
        self.assertEqual(mp.safe_name("Surdana Meadow"), "Surdana_Meadow")
        self.assertEqual(mp.safe_name("City of the Damned: Skeleton Square"), "City_of_the_Damned__Skeleton_Square")
        self.assertEqual(mp.safe_name("Inquisitor's Lair: The Depths"), "Inquisitor_s_Lair__The_Depths")
        self.assertEqual(mp.safe_name("a-b_cé"), "a-b_c_")

    def test_built_in_arenas_are_listed_with_their_level(self):
        maps = mp.arenas((ROOT / "configs" / "mp_maps.ini").read_text(encoding="utf-8"))
        self.assertGreaterEqual(len(maps), 3)
        for m in maps:
            self.assertTrue(m["name"] and m["level"], m)
        # A [mode] section is not a map, and its keys do not leak into the arena before it.
        text = "[arena a]\nname = A\nlevel = L\n[mode m]\nname = M\n[arena b]\nlevel = L2 ; note\n"
        self.assertEqual(
            [(m["id"], m.get("name"), m["level"]) for m in mp.arenas(text)], [("a", "A", "L"), ("b", None, "L2")]
        )

    def test_crop_drops_the_hud_strip_and_is_16_9(self):
        x0, y0, x1, y1 = mp.crop_box(1600, 900)
        self.assertEqual(y0, 0)
        self.assertLessEqual(y1, int(900 * mp.KEEP_TOP))
        self.assertAlmostEqual((x1 - x0) / (y1 - y0), 16 / 9, places=2)
        self.assertEqual(x0, 1600 - x1)  # centred
        x0, _, x1, y1 = mp.crop_box(960, 720)  # a 4:3 window: full width, shorter
        self.assertEqual((x0, x1), (0, 960))
        self.assertEqual(y1, 540)

    def test_black_and_flat_frames_are_rejected(self):
        w, h = 64, 36
        self.assertFalse(mp.frame_ok(w, h, 3, bytes(w * h * 3))[0])
        self.assertFalse(mp.frame_ok(w, h, 3, bytes([120]) * (w * h * 3))[0])
        stripes = bytes(v for y in range(h) for x in range(w) for v in [((x // 4) % 2) * 200 + 20] * 3)
        self.assertTrue(mp.frame_ok(w, h, 3, stripes)[0])

    def test_png_round_trip_and_crop(self):
        w, h = 32, 18
        px = bytes(v for y in range(h) for x in range(w) for v in (x * 8, y * 14, ((x + y) % 2) * 255))
        with tempfile.TemporaryDirectory() as d:
            src, dst = Path(d) / "shot.png", Path(d) / "out" / "Map.png"
            mp.write_png(src, w, h, px)
            self.assertEqual(mp.read_png(src.read_bytes()), (w, h, 3, px))
            ok, why = mp.make_picture(src, dst)
            self.assertTrue(ok, why)
            cw, ch, _, out = mp.read_png(dst.read_bytes())
            x0, _, x1, y1 = mp.crop_box(w, h)
            self.assertEqual((cw, ch), (x1 - x0, y1))
            self.assertEqual(out[:3], px[x0 * 3 : x0 * 3 + 3])


if __name__ == "__main__":
    unittest.main()
