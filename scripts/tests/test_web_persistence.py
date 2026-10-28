"""Browser save durability checks against the shipped preRun implementation."""

import shutil
import subprocess
import unittest
from pathlib import Path


class WebPersistenceTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node.js is needed for browser persistence checks")
    def test_file_close_and_restore_safety(self):
        result = subprocess.run(
            [shutil.which("node"), str(Path(__file__).with_name("web_persistence_cases.js"))],
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
