import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location("jump_check", Path(__file__).parents[1] / "harness" / "jump_check.py")
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


def row(x=0, y=0, up=0, kind=1, state="ffffffff"):
    return f"[in-pos] f=1 pos=({x} {y} 0) st={state} spd=1200 mv=101/0/{up} jt={kind} camPos=(0 0 0) dt=0.03\n"


def trial(kind, travel):
    return row() + row(up=1) + row(x=travel, y=100, up=1, kind=kind) + row(x=travel, kind=kind)


class JumpCheckTests(unittest.TestCase):
    def test_moving_and_stationary_jumps(self):
        text = trial(2, 500) + trial(2, 500) + trial(1, 0) + "[in] script quit"
        self.assertTrue(report.check(text)["passed"])

    def test_old_vertical_walk_is_rejected(self):
        text = trial(2, 500) + trial(1, 0) + trial(1, 0) + "[in] script quit"
        self.assertFalse(report.check(text)["passed"])

    def test_jump_type_without_travel_is_not_a_pass(self):
        text = trial(2, 0) + trial(2, 0) + trial(1, 0) + "[in] script quit"
        self.assertFalse(report.check(text)["passed"])

    def test_vertical_dodge_with_stale_jump_type_is_rejected(self):
        walk = row() + row(up=1) + row(x=500, y=100, up=1, kind=2, state="00000013") + row()
        text = trial(2, 500) + walk + trial(1, 0) + "[in] script quit"
        self.assertFalse(report.check(text)["passed"])

    def test_missing_or_incomplete_evidence(self):
        for text in ("", "[in] script quit", trial(2, 500), trial(2, 500) * 3):
            with self.subTest(text=text):
                self.assertFalse(report.check(text)["passed"])


if __name__ == "__main__":
    unittest.main()
