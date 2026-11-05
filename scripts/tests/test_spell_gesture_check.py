"""Reject incomplete and routing-only spell gesture evidence."""

import copy
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

with patch.object(sys, "path", [str(Path(__file__).parents[1] / "harness"), *sys.path]):
    import spell_gesture_check as checker


def evidence():
    samples = {}
    for controls in ("wasd", "arrows"):
        histories = {
            "before": [],
            "prepared": [],
            "step1": [[-1, 0]],
            "step2": [[-1, 0], [1, 0]],
            "recognized": [[-1, 0], [1, 0]],
            "released": [],
            "projectile": [],
        }
        for label, history in histories.items():
            samples[f"ram_{controls}_{label}"] = {
                "actor": 0x123456,
                "rynn_available": True,
                "summon_state": (
                    0
                    if label in ("before", "released")
                    else 4
                    if label == "projectile"
                    else 5
                    if label in ("step2", "recognized")
                    else 2
                ),
                "history": history,
                "processed": ""
                if label in ("step2", "recognized")
                else "".join("L" if x < 0 else "R" for x, y in history),
                "selected_spell": 0xFFFFFFFF if label == "prepared" else 0,
                "spells": [{"index": 0, "level": 1, "sequence": "LR"}],
                "mana": 85 if label == "projectile" else 100,
                "projectiles": [{"handle": "0x456789"}] if label == "projectile" else [],
            }
    return samples


class SpellGestureCheckTests(unittest.TestCase):
    def test_consumed_queue_preserves_processed_native_string(self):
        data = bytearray(0x2000000)

        def put(address, value):
            struct.pack_into("<I", data, address, value)

        context, handle, inner, player = 0x600000, 0x610000, 0x620000, 0x630000
        for address, value in (
            (0x5565D4, context),
            (context + 0x4B0, handle),
            (handle + 0xFC, inner),
            (inner + 0x10, player),
        ):
            put(address, value)
        data[player + 0x1034 : player + 0x1037] = b"LR\0"
        profile = {
            "identity_addresses": True,
            "camera": {"context_actor": 0x4B0, "handle_inner": 0xFC, "inner_data": 0x10},
        }
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "ram.ram"
            path.write_bytes(data)
            with patch.object(checker, "combat_report", return_value={"rynn_available": True}):
                sample = checker.snapshot(path, profile)
        self.assertEqual(sample["processed"], "LR")
        self.assertEqual(sample["history"], [])

    def test_generated_recipe_uses_native_input_and_labels(self):
        recipe = checker.gesture_recipe("ULR")
        for controls in ("wasd", "arrows"):
            for label in ("before", "prepared", "step1", "step2", "step3", "recognized", "released", "projectile"):
                self.assertIn(f"dump ram_{controls}_{label}", recipe)
        self.assertIn("key down A", recipe)
        self.assertIn("key down LEFT", recipe)
        self.assertNotIn("poke", recipe)

    def test_complete_native_evidence(self):
        self.assertTrue(all(checker.check_evidence(evidence(), "LR").values()))

    def test_final_clear_requires_native_ready_and_correct_selection(self):
        for field, value in (("summon_state", 0), ("selected_spell", 3)):
            samples = evidence()
            samples["ram_wasd_recognized"][field] = value
            self.assertFalse(checker.check_evidence(samples, "LR")["wasd_recognized_history"])

    def test_missing_projectile_keeps_native_acceptance_separate(self):
        samples = evidence()
        for sample in samples.values():
            sample["projectiles"] = []
        checks = checker.check_evidence(samples, "LR")
        projectile = {k: checks.pop(k) for k in list(checks) if k.endswith("_spell_launched")}
        self.assertTrue(all(checks.values()))
        self.assertFalse(any(projectile.values()))

    def test_no_crash_alone_fails(self):
        self.assertFalse(all(checker.check_evidence({}, "LR").values()))

    def test_routing_without_recognizer_history_fails(self):
        samples = evidence()
        for sample in samples.values():
            sample["history"] = []
            sample["processed"] = ""
        self.assertFalse(all(checker.check_evidence(samples, "LR").values()))

    def test_no_launch_or_stale_projectile_fails(self):
        for stale in (False, True):
            samples = evidence()
            if stale:
                samples["ram_wasd_before"]["projectiles"] = copy.deepcopy(samples["ram_wasd_projectile"]["projectiles"])
            else:
                samples["ram_wasd_projectile"]["projectiles"] = []
            self.assertFalse(checker.check_evidence(samples, "LR")["wasd_spell_launched"])

    def test_identical_horizontal_directions_fail(self):
        samples = evidence()
        for sample in samples.values():
            sample["history"] = [[abs(x), y] for x, y in sample["history"]]
            sample["processed"] = "R" * len(sample["history"])
        checks = checker.check_evidence(samples, "LR")
        self.assertFalse(checks["wasd_gesture_1_L"])

    def test_missing_release_and_changed_actor_fail(self):
        samples = evidence()
        samples["ram_wasd_released"]["summon_state"] = 3
        samples["ram_arrows_step1"]["actor"] += 1
        checks = checker.check_evidence(samples, "LR")
        self.assertFalse(checks["wasd_native_release"])
        self.assertFalse(checks["arrows_same_actor"])


if __name__ == "__main__":
    unittest.main()
