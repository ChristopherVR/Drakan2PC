"""Asset-free CLI regressions for command/cached-pad delivery evidence."""

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "harness" / "combat_trace_summary.py"


def audit(frame, pad):
    return f"[combat-audit] f={frame} screen=-1 pad={pad:04x} sticks=128,128,128,128\n"


class CombatTraceSummaryTests(unittest.TestCase):
    def summarize(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.log"
            path.write_text(text, encoding="utf-8")
            result = subprocess.run([sys.executable, str(SCRIPT), str(path)], capture_output=True, text=True)
        self.assertIn(result.returncode, (0, 1), result.stderr)
        self.assertEqual(result.stderr, "")
        return result.returncode, json.loads(result.stdout)

    def test_observed_pad_press_and_release_succeed_without_commands(self):
        code, summary = self.summarize(audit(1, 0) + audit(2, 0x80) + audit(3, 0) + "[in] script quit at frame 4\n")
        self.assertEqual(code, 0)
        self.assertEqual(summary["delivery_source"], "cached_pad_audit")
        self.assertEqual(summary["cached_pad_edges"]["square"], {"down": [2], "up": [3]})
        self.assertEqual(summary["held_pad_at_end"], [])

    def test_held_pad_fails(self):
        code, summary = self.summarize(audit(1, 0x20) + "[in] script quit at frame 2\n")
        self.assertEqual(code, 1)
        self.assertEqual(summary["held_pad_at_end"], ["circle"])

    def test_absent_delivery_evidence_fails(self):
        for text in ("[in] script quit at frame 2\n", audit(1, 0) + "[in] script quit at frame 2\n"):
            with self.subTest(text=text):
                code, summary = self.summarize(text)
                self.assertEqual(code, 1)
                self.assertEqual(summary["delivery_source"], "unavailable")

    def test_legitimate_command_evidence_still_succeeds(self):
        code, summary = self.summarize(
            "[cmd] f=1 cmd=0x4006 act=0x124 DOWN ms=1 -> 1\n"
            "[cmd] f=2 cmd=0x4006 act=0x124 up ms=2 -> 1\n"
            "[in] script quit at frame 3\n"
        )
        self.assertEqual(code, 0)
        self.assertEqual(summary["delivery_source"], "commands")
        self.assertEqual(summary["commands"]["4006"]["down"], 1)
        self.assertEqual(summary["commands"]["4006"]["up"], 1)

    def test_missing_quit_or_held_command_cannot_use_neutral_pad_to_pass(self):
        for suffix in ("", "[cmd] f=4 cmd=0x4006 act=0x124 DOWN ms=4 -> 1\n[in] script quit at frame 5\n"):
            with self.subTest(suffix=suffix):
                code, _ = self.summarize(audit(1, 0x80) + audit(2, 0) + suffix)
                self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
