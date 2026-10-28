"""Browser relative movement survives fractional counts and capture changes."""

import shutil
import subprocess
import unittest
from pathlib import Path


class WebPointerTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node.js is needed for pointer checks")
    def test_relative_motion(self):
        result = subprocess.run(
            [shutil.which("node"), str(Path(__file__).with_name("web_pointer_cases.js"))],
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
