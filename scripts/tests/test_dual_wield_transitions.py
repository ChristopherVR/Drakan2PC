import sys
import unittest
from dataclasses import replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
import dual_wield_check as dw
import dual_wield_transitions as transitions


class TransitionEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.ready = dw.State(
            right=100,
            shield_slot=200,
            on_left_bone=200,
            left_owner=4,
            model_ref=4,
            weapons=[
                dw.Weapon(1, 100, True, dw.CLASS_MELEE, dw.TYPE_WEAPON, 0),
                dw.Weapon(16, 200, True, dw.CLASS_MELEE, dw.TYPE_WEAPON, 0),
            ],
        )

    def test_detects_bow_return_ghost(self):
        self.assertTrue(transitions.dual_ready(self.ready))
        # Observed regression: both inventory cells equipped, but the bow detached the dagger.
        self.assertFalse(transitions.dual_ready(replace(self.ready, on_left_bone=0)))
        self.assertFalse(transitions.dual_ready(replace(self.ready, on_left_bone=300, bow_drawn=True)))

    def test_detects_primary_drop_or_bad_load(self):
        self.assertFalse(transitions.dual_ready(replace(self.ready, right=0)))
        self.assertFalse(transitions.dual_ready(replace(self.ready, left_owner=7)))
        self.assertFalse(transitions.dual_ready(replace(self.ready, shield_slot=100, on_left_bone=100)))
        weapons = [self.ready.weapons[0], replace(self.ready.weapons[1], equipped=False)]
        self.assertFalse(transitions.dual_ready(replace(self.ready, weapons=weapons)))


if __name__ == "__main__":
    unittest.main()
