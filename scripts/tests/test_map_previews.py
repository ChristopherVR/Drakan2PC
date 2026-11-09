"""Multiplayer map pictures (scripts/harness/map_previews.py): file names, map list, crop and frame check. No game needed."""

import io
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock

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


class FailFastTests(unittest.TestCase):
    """A level that never loads leaves the game on its Loading screen; the harness must not wait for the timeout."""

    LOAD_FAILED = (
        "[lifecycle 17.368] afterLevelLoad level='OotF - Alwarren of Death (air)' path='cdrom0:/x.lvl' fromSave=0 "
        "ok=0 result=-4 mount=0->0 flushes=0\n"
    )
    LOAD_OK = "[lifecycle 17.368] afterLevelLoad level='Surdana' path='x' fromSave=0 ok=1 result=0 mount=0->0\n"

    def test_a_level_that_fails_to_load_stops_the_run(self):
        self.assertEqual(mp.run_problem(self.LOAD_FAILED, False, 20), "the level did not load")
        self.assertEqual(mp.run_problem(self.LOAD_OK, False, 20), "")

    def test_mod_maps_need_the_packs_loaded(self):
        builtin = "[arena] maps: 3 from built-in\n"
        self.assertEqual(mp.run_problem(builtin, True, 10), "", "too early to tell")
        self.assertEqual(mp.run_problem(builtin, True, 60), "the mod packs were not loaded")
        self.assertEqual(mp.run_problem("[mods] disc profile pal hash 1, folder x\n", True, 60), "")
        self.assertEqual(mp.run_problem(builtin, False, 60), "", "built-in maps need no packs")

    def run_main(self, results):
        """main() over a play folder with a pack holding two maps; capture() answers from `results`."""
        calls = []
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / "dist"
            (src / "mods").mkdir(parents=True)
            ini = "[arena a1]\nname = One\nlevel = L1\n[arena a2]\nname = Two\nlevel = L2\n"
            with zipfile.ZipFile(src / "mods" / "pack.zip", "w") as z:
                z.writestr("maps/m.ini", ini)

            def fake_capture(m, a, exe):
                calls.append(m["id"])
                return results.pop(0)

            argv = [
                "map_previews.py",
                "--only-mods",
                "--src",
                str(src),
                "--out",
                str(Path(temp) / "out"),
                "--exe",
                "x.exe",
            ]
            with (
                mock.patch.object(sys, "argv", argv),
                mock.patch.object(mp, "prepare_folder", lambda *args: Path("x.exe")),
                mock.patch.object(mp, "capture", fake_capture),
                mock.patch.object(sys, "stdout", io.StringIO()) as out,
            ):
                code = mp.main()
        return code, calls, out.getvalue()

    def test_missing_packs_skip_the_other_pack_maps_instead_of_waiting_for_each(self):
        code, calls, out = self.run_main([(None, "the mod packs were not loaded")])
        self.assertEqual(code, 1)
        self.assertEqual(calls, ["a1"], "one run only: no retry, and the second pack map is not started")
        self.assertIn("skipped", out)
        self.assertIn("map pictures missing", out)

    def test_a_level_that_does_not_load_is_not_retried(self):
        code, calls, out = self.run_main([(None, "the level did not load"), (None, "the level did not load")])
        self.assertEqual(code, 1)
        self.assertEqual(calls, ["a1", "a2"], "each map is tried once")
        self.assertIn("the level did not load", out)


if __name__ == "__main__":
    unittest.main()
