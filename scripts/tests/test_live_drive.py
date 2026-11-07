"""Closed-loop driver logic on synthetic data: no game, no original data."""

import importlib.util
import json
import math
import pathlib
import struct
import sys
import tempfile
import types
import unittest

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "harness" / "live_drive.py"
SPEC = importlib.util.spec_from_file_location("live_drive", SCRIPT)
LD = importlib.util.module_from_spec(SPEC)
sys.modules["live_drive"] = LD
SPEC.loader.exec_module(LD)


class SyntheticWorld:
    """Game context, class records, a registry with Rynn and two NPCs of one class, as the engine lays them out."""

    def __init__(self):
        self.ram = bytearray(LD.RAM_SIZE)
        self.ctx, self.res, self.recs, self.table = 0x700000, 0x710000, 0x720000, 0x730000
        self.put(LD.GAME_CTX, self.ctx)
        self.put(self.ctx + 0x7C, self.table)
        self.put(self.ctx + 0x84, 2)  # highest slot index
        self.put(self.ctx + 0x320, self.res)
        self.put(self.res + 8, self.recs)
        self.put(self.res + 0x10, 2)
        for ix, cls in enumerate([LD.RYNN_CLASS, 0x1003B]):
            self.put(self.recs + ix * 0x24 + 8, cls)
        self.rynn = self.actor(0, 0x800000, 0, (1000.0, 50.0, 2000.0), 0.5)
        self.near = self.actor(1, 0x810000, 1, (3000.0, 60.0, 2000.0), 0.0, hp=150)
        self.far = self.actor(2, 0x820000, 1, (90000.0, 60.0, 2000.0), 0.0, hp=40)
        self.put(self.ctx + 0x4B0, self.rynn)

    def put(self, a, v, fmt="<I"):
        struct.pack_into(fmt, self.ram, a & (LD.RAM_SIZE - 1), v)

    def actor(self, slot, h, class_ix, pos, yaw, hp=None):
        self.put(self.table + slot * 8, h)
        self.put(h, LD.ENTITY_VTABLE)
        self.put(h + 0x10, class_ix, "<H")
        struct.pack_into("<3f", self.ram, h + 0x14, *pos)
        # matrix row 0 = (cos, 0, sin) so atan2(m[2], m[0]) = yaw
        struct.pack_into("<3f", self.ram, h + 0x20, math.cos(yaw), 0.0, math.sin(yaw))
        holder, data = h + 0x1000, h + 0x2000
        self.put(h + 0xFC, holder)
        self.put(holder + 0x10, data)
        self.put(data + 0x90, 0x1234)
        if hp is not None:
            self.put(data + 0x23C, h)
            self.put(data + 0x24C, hp, "<i")
        return h


class EngineReaderTests(unittest.TestCase):
    def setUp(self):
        self.w = SyntheticWorld()
        self.mem = LD.BytesMemory(self.w.ram)

    def test_controlled_actor(self):
        me = LD.controlled(self.mem)
        self.assertEqual(me.handle, self.w.rynn)
        self.assertEqual(me.cls, LD.RYNN_CLASS)
        self.assertEqual(LD._r(me.pos), [1000, 50, 2000])
        self.assertAlmostEqual(me.yaw, 0.5, places=5)

    def test_find_by_class_with_hit_points_and_nearest(self):
        found = LD.find_actors(self.mem, 0x1003B)
        self.assertEqual(sorted(a.handle for a in found), [self.w.near, self.w.far])
        self.assertEqual({a.hp for a in found}, {150, 40})
        self.assertEqual(LD.nearest(found, (1000, 2000)).handle, self.w.near)
        self.assertEqual(LD.nearest(found, (89000, 2000)).handle, self.w.far)
        self.assertEqual(LD.find_actors(self.mem, 0x10010), [])

    def test_hit_points_need_the_back_pointer(self):
        self.assertIsNone(LD.actor_hp(self.mem, self.w.rynn))
        self.assertEqual(LD.actor_hp(self.mem, self.w.near), 150)

    def test_animation_record(self):
        mdl, idx, recs = 0x900000, 0x901000, 0x902000
        self.w.put(self.w.near + 0x50, mdl)
        self.w.put(mdl + 0xDC, idx)
        self.w.put(mdl + 0xE8, recs)
        self.w.put(idx + 2, 3, "<h")  # layer 1 -> record 3
        self.w.put(recs + 3 * 0x80 + 0x50, 0x502FC)
        self.w.put(recs + 3 * 0x80 + 0x58, 1.25, "<f")
        self.assertEqual(LD.anim(self.mem, self.w.near, 1), (0x502FC, 1.25))
        self.assertIsNone(LD.anim(self.mem, self.w.rynn))  # no model

    def test_actor_words(self):
        self.assertEqual(LD.actor_words(self.mem, self.w.rynn, (0x90,)), ["1234"])

    def test_no_context(self):
        mem = LD.BytesMemory(bytearray(LD.RAM_SIZE))
        self.assertIsNone(LD.controlled(mem))
        self.assertEqual(LD.find_actors(mem, 1), [])


class StatusBlockTests(unittest.TestCase):
    def test_unpack_matches_the_runtime_layout(self):
        raw = struct.pack(LD.LiveStatus.FORMAT, b"DRAKAN_LIVE_V1", 0x1234000, 0x2000000, 77, 5, -1, 1, 9, 3)
        self.assertEqual(len(raw), 72)  # 16 + 8 + 8 + 8 + 8 + 4 + 4 + 8 + 8, no padding (struct LiveStatus)
        st = LD.LiveStatus.unpack(raw)
        self.assertEqual((st.rdram, st.frame, st.play_run, st.screen, st.gameplay), (0x1234000, 77, 5, -1, True))
        self.assertEqual((st.live_applied, st.seq), (9, 3))
        with self.assertRaises(ValueError):
            LD.LiveStatus.unpack(b"x" * 72)


class GeometryTests(unittest.TestCase):
    def test_place_yaw_convention(self):
        # yaw 0 faces -z, -pi/2 faces +x
        self.assertAlmostEqual(LD.yaw_to((0, 0, 0), (0, 0, -10)), 0.0)
        self.assertAlmostEqual(LD.yaw_to((0, 0, 0), (10, 0, 0)), -math.pi / 2)
        self.assertAlmostEqual(LD.yaw_to((0, 0), (0, 10)), math.pi)

    def test_wrap(self):
        self.assertAlmostEqual(LD.wrap(3 * math.pi / 2), -math.pi / 2)
        self.assertAlmostEqual(LD.wrap(-3 * math.pi / 2), math.pi / 2)

    def test_dist_xz_ignores_height(self):
        self.assertEqual(LD.dist_xz((0, 100, 0), (3, -50, 4)), 5)


class TurnControllerTests(unittest.TestCase):
    def test_counts_move_toward_the_target_in_a_simulated_loop(self):
        ctl = LD.TurnController(rad_per_count=-0.002, gain=0.8, max_counts=120)
        yaw, want = 0.0, 2.5
        for _ in range(60):
            yaw = LD.wrap(yaw + ctl.counts(want - yaw) * -0.002)
        self.assertLess(abs(LD.wrap(want - yaw)), ctl.deadband + 1e-6)

    def test_limit_and_deadband(self):
        ctl = LD.TurnController(rad_per_count=-0.002, max_counts=50)
        self.assertEqual(ctl.counts(3.0), -50)
        self.assertEqual(ctl.counts(-3.0), 50)
        self.assertEqual(ctl.counts(math.radians(0.5)), 0)

    def test_calibration_keeps_the_sign(self):
        self.assertAlmostEqual(LD.TurnController.calibrate(0.0, -0.3, 150), -0.002)
        self.assertAlmostEqual(LD.TurnController.calibrate(3.1, -3.1, 100), (2 * math.pi - 6.2) / 100)
        self.assertIsNone(LD.TurnController.calibrate(1.0, 1.0, 150))
        self.assertIsNone(LD.TurnController.calibrate(1.0, 2.0, 0))


class PathFollowerTests(unittest.TestCase):
    def test_waypoints_are_passed_in_order(self):
        path = LD.PathFollower([LD.Waypoint((0, 0, 0), 100), LD.Waypoint((1000, 0, 0), 100)])
        path.update((500, 0, 0))
        self.assertEqual(path.index, 0)  # the first point is not skipped by being near the second
        path.update((50, 0, 0))
        self.assertEqual(path.index, 1)
        path.update((950, 0, 20))
        self.assertTrue(path.done)

    def test_a_waypoint_can_need_a_height(self):
        path = LD.PathFollower([LD.Waypoint((0, 0, 0), 100, ymin=500.0), LD.Waypoint((1000, 0, 0), 100)])
        path.update((0, 100, 0))
        self.assertEqual(path.index, 0)  # under the landing
        path.update((0, 600, 0))
        self.assertEqual(path.index, 1)

    def test_pure_pursuit_aims_ahead_on_the_leg(self):
        path = LD.PathFollower([LD.Waypoint((0, 0, 0), 10), LD.Waypoint((1000, 0, 0), 10)], lookahead=200)
        path.update((0, 0, 0))
        ax, az = path.aim_point((300, 0, 80))  # off the leg to +z: aims back onto it, 200 ahead of the projection
        self.assertAlmostEqual(ax, 500)
        self.assertAlmostEqual(az, 0)
        self.assertGreater(path.cross_track((300, 0, 80)), 0)
        self.assertLess(path.cross_track((300, 0, -80)), 0)

    def test_simulated_walk_follows_a_spiral(self):
        pts = [
            LD.Waypoint((1000 * math.cos(a), 0, 1000 * math.sin(a)), 120)
            for a in [i * math.radians(20) for i in range(19)]
        ]
        path = LD.PathFollower(pts, lookahead=150)
        ctl = LD.TurnController(rad_per_count=-0.002, gain=0.8, max_counts=200)
        x, z, yaw = 1000.0, -50.0, LD.yaw_to((1000, -50), (pts[1].p[0], pts[1].p[2]))
        worst = 0.0
        for _ in range(600):
            path.update((x, 0, z))
            if path.done:
                break
            ax, az = path.aim_point((x, 0, z))
            yaw = LD.wrap(yaw + ctl.counts(LD.yaw_to((x, z), (ax, az)) - yaw) * -0.002)
            x, z = x - 40 * math.sin(yaw), z - 40 * math.cos(yaw)  # forward = (-sin, -cos) in the place convention
            worst = max(worst, abs(math.hypot(x, z) - 1000))
        self.assertTrue(path.done)
        self.assertLess(worst, 200)

    def test_load_json(self):
        with tempfile.TemporaryDirectory() as d:
            p = pathlib.Path(d) / "w.json"
            p.write_text(json.dumps([[1, 2, 3], {"p": [4, 5, 6], "r": 50, "ymin": 7}]))
            path = LD.PathFollower.load(p, radius=99)
        self.assertEqual([(w.p, w.r, w.ymin) for w in path.points], [((1, 2, 3), 99, None), ((4, 5, 6), 50, 7)])
        with self.assertRaises(ValueError):
            LD.PathFollower([])


class FightTests(unittest.TestCase):
    def test_policy_without_animations(self):
        pol = LD.FightPolicy(strike_every=2)
        self.assertEqual(pol.decide(5000, 0.0, False), "approach")
        self.assertEqual(pol.decide(1300, math.radians(40), False), "face")
        self.assertEqual(pol.decide(1000, 0.0, False), "retreat")
        self.assertEqual(pol.decide(1300, 0.0, False), "strike")
        self.assertEqual(pol.decide(1300, 0.0, False), "face")  # attack cadence
        self.assertEqual(pol.decide(1300, 0.0, False), "strike")

    def test_policy_backs_off_on_danger_until_safe(self):
        pol = LD.FightPolicy(danger=frozenset({0xA}), opening=frozenset({0xB}), safe=2300, standoff=2200)
        self.assertEqual(pol.decide(1300, 0.0, False, 0xA), "retreat")
        self.assertEqual(pol.decide(2000, 0.0, False, 0xB), "retreat")  # still inside safe after the reach
        self.assertEqual(pol.decide(2400, 0.0, False, 0xB), "approach")  # safe again, standing: close in
        self.assertEqual(pol.decide(1350, 0.0, False, 0xB), "strike")
        self.assertEqual(pol.decide(1350, 0.0, True, 0xB), "retreat")  # a grab

    def test_policy_counter_strikes_into_the_reach(self):
        pol = LD.FightPolicy(counter=frozenset({0xA}), danger=frozenset({0xD}), strike_every=2)
        self.assertEqual(pol.decide(1800, 0.0, False, 0xA), "strike")
        self.assertEqual(pol.decide(1800, 0.0, False, 0xA), "face")
        self.assertEqual(pol.decide(1800, math.radians(30), False, 0xA), "face")
        self.assertEqual(pol.decide(1800, 0.0, False, 0xD), "retreat")  # a grab starts: flee until safe
        self.assertEqual(pol.decide(1900, 0.0, False, 0xA), "retreat")
        self.assertEqual(pol.decide(1800, 0.0, True, 0xA), "retreat")  # held: no counter

    def test_policy_keeps_standoff_while_it_walks(self):
        pol = LD.FightPolicy(opening=frozenset({0xB}), standoff=2200, slack=500)
        self.assertEqual(pol.decide(1800, 0.0, False, 0xC), "retreat")
        self.assertEqual(pol.decide(2400, 0.0, False, 0xC), "face")
        self.assertEqual(pol.decide(3000, 0.0, False, 0xC), "approach")
        self.assertEqual(pol.decide(3000, 0.0, False, None), "approach")

    def test_grab_detector(self):
        g = LD.GrabDetector(rise=300, jump=800)
        self.assertFalse(g.update((0, 100, 0)))
        self.assertFalse(g.update((100, 120, 0), commanded_step=200))
        self.assertTrue(g.update((100, 500, 0)))  # lifted
        g = LD.GrabDetector(rise=300, jump=800)
        g.update((0, 100, 0))
        self.assertTrue(g.update((2000, 100, 0), commanded_step=200))  # thrown

    def test_right_and_unstick_side(self):
        rx, rz = LD.right_of(0.0)  # facing -z: right is +x
        self.assertAlmostEqual(rx, 1.0)
        self.assertAlmostEqual(rz, 0.0)
        # facing -z at x = +500 from the axis: away from it is +x = D; on the other side A
        self.assertEqual(LD.unstick_key((500, 0, 0), 0.0, (0, 0)), "D")
        self.assertEqual(LD.unstick_key((-500, 0, 0), 0.0, (0, 0)), "A")

    def test_relative_start_and_bearing(self):
        pos, yaw = (1000.0, 0.0, 1000.0), math.pi  # facing +z
        x, z, face = LD.relative_start(pos, yaw, "behind", 500)
        self.assertAlmostEqual(x, 1000)
        self.assertAlmostEqual(z, 500)
        self.assertAlmostEqual(LD.wrap(face - yaw_back(pos, (x, z))), 0)
        self.assertAlmostEqual(abs(LD.bearing_from(pos, yaw, (x, 0, z))), math.pi)
        x, z, _ = LD.relative_start(pos, yaw, "front", 500)
        self.assertAlmostEqual(LD.bearing_from(pos, yaw, (x, 0, z)), 0)
        x, z, _ = LD.relative_start(pos, yaw, "right", 500)
        self.assertAlmostEqual(LD.bearing_from(pos, yaw, (x, 0, z)), math.pi / 2)
        self.assertAlmostEqual(x, 500)  # facing +z, right is -x

    def test_key_state_sends_only_changes(self):
        k = LD.KeyState()
        self.assertEqual(k.lines(["W"]), ["0 key down W"])
        self.assertEqual(k.lines(["W"]), [])
        self.assertEqual(k.lines(["S", "A"]), ["0 key up W", "0 key down A", "0 key down S"])
        self.assertEqual(k.lines([]), ["0 key up A", "0 key up S"])


def yaw_back(pos, frm):
    return LD.yaw_to(frm, (pos[0], pos[2]))


class CliTests(unittest.TestCase):
    def test_transitions(self):
        rows = [{"a": 1, "f": 1}, {"a": 1, "f": 2}, {"a": 2, "f": 3}, {"a": 2, "f": 4}, {"a": 1, "f": 5}]
        self.assertEqual([r["f"] for r in LD.transitions(rows, ["a"])], [1, 3, 5])
        rows = [{"g": ["x", 0.1]}, {"g": ["x", 0.2]}, {"g": ["y", 0.3]}]
        self.assertEqual(len(LD.transitions(rows, ["g.0"])), 2)

    def test_post_lines(self):
        text = "# c\n10 key tap F5 # save\n\n20 dump ram_x\n"
        self.assertEqual(LD.post_lines(text), ["10 key tap F5", "20 dump ram_x"])
        with self.assertRaises(ValueError):
            LD.post_lines("0 sync play 10\n")

    def test_env_and_arguments(self):
        a = LD.parse_args(
            ["--name", "x", "--level", "L", "--env", "DRAKAN_PLAYTEST_GOD=1", "fight", "--target-class", "0x1003b"]
        )
        self.assertEqual((a.mission, a.target_class, a.strike_max), ("fight", "0x1003b", 1400))
        env = LD.build_env(a, pathlib.Path("out"), pathlib.Path("out/live.txt"))
        self.assertEqual(env["DRAKAN_INPUT_LIVE"], str(pathlib.Path("out/live.txt")))
        self.assertEqual((env["DRAKAN_START_LEVEL"], env["DRAKAN_PLAYTEST_GOD"]), ("L", "1"))
        bad = types.SimpleNamespace(**{**vars(a), "env": ["NOEQUALS"]})
        with self.assertRaises(SystemExit):
            LD.build_env(bad, pathlib.Path("out"), pathlib.Path("out/live.txt"))


if __name__ == "__main__":
    unittest.main()
