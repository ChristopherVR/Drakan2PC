"""The Volcanic route scenario's pure parts on synthetic data: no game, no original data."""

import importlib.util
import math
import pathlib
import struct
import sys
import unittest

HARNESS = pathlib.Path(__file__).resolve().parents[1] / "harness"
sys.path.insert(0, str(HARNESS))
SPEC = importlib.util.spec_from_file_location("ootf_volcanic_route", HARNESS / "ootf_volcanic_route.py")
VR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VR)
LD = VR.LD


class SyntheticRynn:
    """Game context, class records and Rynn with carry cells, laid out as the engine does."""

    def __init__(self, items):
        self.ram = bytearray(LD.RAM_SIZE)
        ctx, res, recs, table = 0x700000, 0x710000, 0x720000, 0x730000
        self.put(LD.GAME_CTX, ctx)
        self.put(ctx + 0x7C, table)
        self.put(ctx + 0x84, 0)
        self.put(ctx + 0x320, res)
        self.put(res + 8, recs)
        self.put(res + 0x10, 1 + len(items))
        self.put(recs + 8, LD.RYNN_CLASS)
        rynn, holder, data = 0x800000, 0x801000, 0x802000
        self.put(table, rynn)
        self.put(rynn, LD.ENTITY_VTABLE)
        self.put(rynn + 0x10, 0, "<H")
        self.put(rynn + 0xFC, holder)
        self.put(holder + 0x10, data)
        self.put(ctx + 0x4B0, rynn)
        for i, (cell, equipped, resource) in enumerate(items):
            ix, item = 1 + i, 0x900000 + i * 0x1000
            self.put(recs + ix * 0x24, resource)
            self.put(item + 0x10, ix, "<H")
            at = data + 0x584 + 0x30 + cell * 16
            self.put(at, equipped)
            self.put(at + 8, item)
        self.put(data + 0x584 + 0x30 + 9 * 16 + 8, 1)  # a small tag word, not an item handle

    def put(self, a, v, fmt="<I"):
        struct.pack_into(fmt, self.ram, a & (LD.RAM_SIZE - 1), v)


class CarryCellTests(unittest.TestCase):
    def test_cells_resources_and_equipped_word(self):
        mem = LD.BytesMemory(SyntheticRynn([(0, 0, 0x346C3), (1, 1, VR.RUNEBLADE_RES)]).ram)
        self.assertEqual(VR.carry_cells(mem), [(0, 0, 0x346C3), (1, 1, VR.RUNEBLADE_RES)])
        self.assertEqual(VR.carried(mem), [0x346C3, VR.RUNEBLADE_RES])

    def test_no_context(self):
        self.assertEqual(VR.carried(LD.BytesMemory(bytearray(LD.RAM_SIZE))), [])


class BladeStepTests(unittest.TestCase):
    def test_passes_only_with_the_blade_carried_and_its_timer_gone(self):
        self.assertTrue(VR.blade_step_passed([0x346C3, VR.RUNEBLADE_RES], [783], []))
        # E through the unmelted ice took nothing: the timer never ran (2026-11-08 vr5)
        self.assertFalse(VR.blade_step_passed([0x346C3], [783], [783]))
        # carried but the timer still waiting, or never found: not a pass
        self.assertFalse(VR.blade_step_passed([VR.RUNEBLADE_RES], [783], [783]))
        self.assertFalse(VR.blade_step_passed([VR.RUNEBLADE_RES], [], []))

    def test_the_blade_resource_read_from_the_pack_counts_not_the_old_constant(self):
        # 2026-11-12 proof2: the blade was carried as 0x346b2 and its timer had run; the fixed 0x346a3 called it a failure
        self.assertTrue(VR.blade_step_passed([0x346B2], [1158], [], 0x346B2))
        self.assertFalse(VR.blade_step_passed([0x346B2], [1158], []))
        self.assertFalse(VR.blade_step_passed([0x346C3], [1158], [], 0x346B2))

    def test_resource_of_reads_the_handle_through_the_class_records(self):
        mem = LD.BytesMemory(SyntheticRynn([(0, 0, 0x346C3), (1, 0, 0x346B2)]).ram)
        self.assertEqual(VR.resource_of(mem, 0x901000), 0x346B2)  # the second item handle
        self.assertEqual(VR.resource_of(mem, 0x900000), 0x346C3)


class GeometryTests(unittest.TestCase):
    def test_beside_faces_the_target(self):
        at = (1000.0, 50.0, 2000.0)
        for ang in (0, 90, 180, -90, 45):
            x, z, yaw = VR.beside(at, ang, 300)
            self.assertAlmostEqual(math.hypot(x - at[0], z - at[2]), 300, places=6)
            fx, fz = LD.forward_of(yaw)
            self.assertGreater(fx * (at[0] - x) + fz * (at[2] - z), 299)
        x, z, _ = VR.beside(at, 90, 300)
        self.assertAlmostEqual(x, 700)  # the 90 degree side is -x

    def test_spot_toward_lies_between_target_and_centre(self):
        x, z, yaw = VR.spot_toward((0.0, 0.0, 0.0), (0.0, 0.0, 10000.0), 4000)
        self.assertAlmostEqual(x, 0)
        self.assertAlmostEqual(z, 4000)
        fx, fz = LD.forward_of(yaw)
        self.assertLess(fz, -0.99)  # faces back toward the target

    def test_tunnel_ends_over_the_altar(self):
        self.assertEqual(len(VR.TUNNEL), 14)
        self.assertLess(LD.dist_xz(VR.TUNNEL[-1], VR.ALTAR), 1500)


class ArgsTests(unittest.TestCase):
    def test_own_options_are_split_off(self):
        rest, opts = VR.split_args(["--name", "x", "--from", "blade", "--to", "rift", "--save-each", "--shots"])
        self.assertEqual(rest, ["--name", "x"])
        self.assertEqual((opts["from"], opts["to"], opts["save-each"], opts["shots"]), ("blade", "rift", True, True))

    def test_steps_must_be_in_order(self):
        with self.assertRaises(SystemExit):
            VR.split_args(["--from", "rift", "--to", "blade"])
        with self.assertRaises(SystemExit):
            VR.split_args(["--from", "nowhere"])


if __name__ == "__main__":
    unittest.main()
