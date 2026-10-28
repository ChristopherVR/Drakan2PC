"""Check ownership of asynchronous browser GPU results against the shipped worker."""

import shutil
import subprocess
import unittest
from pathlib import Path


class PortableGpuDeliveryTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("node"), "Node.js is needed for GPU delivery checks")
    def test_completion_and_cancellation(self):
        result = subprocess.run(
            [shutil.which("node"), str(Path(__file__).with_name("portable_gpu_delivery_cases.js"))],
            capture_output=True,
            text=True,
            timeout=15,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
