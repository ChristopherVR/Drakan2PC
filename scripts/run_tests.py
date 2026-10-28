"""Run every Python test in the repository with the stdlib runner.

Usage: python scripts/run_tests.py [-v]
Tests that need local game data skip themselves instead of failing.
"""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEST_DIRS = [ROOT / "scripts" / "tests", ROOT / "analysis" / "scripts"]


def main() -> int:
    suite = unittest.TestSuite()
    for directory in TEST_DIRS:
        # discover() puts a non-package start directory on sys.path, so sibling imports resolve.
        suite.addTests(
            unittest.TestLoader().discover(str(directory), pattern="test_*.py", top_level_dir=str(directory))
        )
    verbosity = 2 if "-v" in sys.argv[1:] else 1
    result = unittest.TextTestRunner(verbosity=verbosity).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
