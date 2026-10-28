import copy
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
from combat_parry_recipe import recipe
from combat_parry_report import analyze


def sample(returned=0):
    state = dict(
        f=100,
        player=123,
        attacker=456,
        source=789,
        incoming=10,
        hp=100,
        hp_before=100,
        result=0,
        complete=True,
        b28=0x41,
        ab0=0x4004000,
        shield_class=0,
        ed0_class=0x1000E,
        facing=0.9,
        props_valid=True,
        start=0.0,
        duration=0.8,
        anim_time=0.2,
    )
    return [
        dict(kind="installed", version=1),
        dict(state, kind="damage-enter", id=1, hit=1),
        dict(state, kind="reduce-enter", id=2, hit=1),
        dict(state, kind="reduce-exit", id=2, hit=1, result=returned),
        dict(state, kind="damage-exit", id=1, hit=1, result=returned, hp=100 - returned),
    ]


class ReportTests(unittest.TestCase):
    def test_leaf_hp_setter_across_parent_yields(self):
        rows = sample()
        rows[0]["version"] = 2
        rows = [rows[0], rows[2], rows[3], dict(rows[1], kind="hp-enter", id=2), dict(rows[4], kind="hp-exit", id=2)]
        self.assertTrue(analyze(rows)["timed_dagger_parry_established"])
        self.assertTrue(analyze(rows)["reducer_calls"][0]["hp_update_verified"])
        rows[-1]["complete"] = False
        self.assertFalse(analyze(rows)["damage_reduction_established"])

    def test_actual_reduction_required(self):
        self.assertTrue(analyze(sample())["timed_dagger_parry_established"])
        self.assertFalse(analyze(sample(10))["damage_reduction_established"])

    def test_unfinished_reducer_and_parent_rejected(self):
        for index in (3, 4):
            rows = sample()
            rows[index]["complete"] = False
            self.assertFalse(analyze(rows)["damage_reduction_established"])

    def test_hp_mismatch_rejected(self):
        rows = sample()
        rows[-1]["hp"] = 90
        self.assertFalse(analyze(rows)["damage_reduction_established"])

    def test_damage_not_attributed_to_other_parent(self):
        rows = sample()
        rows[2]["hit"] = 3
        self.assertFalse(analyze(rows)["damage_reduction_established"])

    def test_missing_duplicate_and_truncated_rejected(self):
        for rows in [sample()[1:], [*sample(), copy.copy(sample()[3])], [*sample(), dict(kind="truncated")]]:
            self.assertFalse(analyze(rows)["damage_reduction_established"])

    def test_damage_reduction_is_distinct_from_weapon_parry(self):
        for key, value in [
            ("b28", 0x40),
            ("facing", 0.4),
            ("props_valid", False),
            ("anim_time", 0.8),
            ("shield_class", 0x10090),
        ]:
            rows = sample()
            rows[2][key] = value
            self.assertTrue(analyze(rows)["damage_reduction_established"])
            self.assertFalse(analyze(rows)["timed_dagger_parry_established"])

    def test_recipes_bounded_released_and_no_attacks(self):
        for mode in ("neutral", "held", "tap"):
            for phase in (0, 23):
                lines = recipe(mode, phase).splitlines()[1:]
                frames = [int(line.split()[0]) for line in lines]
                self.assertEqual(frames, sorted(frames))
                self.assertEqual(lines[-1], "320 quit")
                self.assertNotIn("button down 1", "\n".join(lines))
                self.assertIn("310 button up 2", lines)


if __name__ == "__main__":
    unittest.main()
