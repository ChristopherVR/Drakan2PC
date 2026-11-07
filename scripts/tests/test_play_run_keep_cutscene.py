"""play_run.py --keep-cutscene must export the env flag that stops the autopress taps after the first level load."""

import subprocess
import sys
import unittest
from pathlib import Path

HARNESS = Path(__file__).resolve().parents[1] / "harness" / "play_run.py"
GLUE = Path(__file__).resolve().parents[2] / "ps2recomp" / "ps2xRuntime" / "src" / "lib" / "drakan_input_glue.cpp"


class KeepCutsceneTest(unittest.TestCase):
    def test_option_is_documented(self):
        out = subprocess.run(
            [sys.executable, str(HARNESS), "--help"], capture_output=True, text=True, check=True
        ).stdout
        self.assertIn("--keep-cutscene", out)

    def test_flag_reaches_the_runtime(self):
        self.assertIn("DRAKAN_AUTOPRESS_STOP_AFTER_LOAD", HARNESS.read_text(encoding="utf-8"))
        self.assertIn("DRAKAN_AUTOPRESS_STOP_AFTER_LOAD", GLUE.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
