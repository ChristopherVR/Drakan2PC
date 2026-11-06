"""Every Python script the game builders run must import under the kit's embeddable Python.

The embeddable distribution's ._pth file keeps the script's own folder off sys.path, so a sibling import that works
with a normal Python fails in the released builder kit. `python -I` reproduces that. No game data is needed.
"""

import re
import subprocess
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
BUILDERS = [REPO / "scripts/buildtool/Build-LocalGame.ps1", REPO / "scripts/buildtool/Build-Drakan.ps1"]
SEARCH = [REPO, REPO / "scripts/buildtool", REPO / "scripts"]


def builder_scripts():
    found = set()
    for builder in BUILDERS:
        for name in re.findall(r"'([\w/]+\.py)'", builder.read_text(encoding="utf-8")):
            for base in SEARCH:
                if (base / name).is_file():
                    found.add((base / name).resolve())
                    break
    return sorted(found)


class BuildScriptImportTests(unittest.TestCase):
    def test_builders_reference_scripts(self):
        names = {path.name for path in builder_scripts()}
        self.assertIn("character_previews.py", names)
        self.assertIn("prepare_iso.py", names)

    def test_scripts_import_without_their_folder_on_sys_path(self):
        for script in builder_scripts():
            with self.subTest(script=script.relative_to(REPO).as_posix()):
                result = subprocess.run(
                    [sys.executable, "-I", str(script), "--help"],
                    capture_output=True,
                    text=True,
                    timeout=120,
                )
                self.assertNotRegex(result.stderr, r"ModuleNotFoundError|ImportError", result.stderr[-2000:])


if __name__ == "__main__":
    unittest.main()
