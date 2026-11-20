"""The builder kit must not look like a trojan downloader to antivirus heuristics (issue #14).

Avast/AVG ("Malware-gen"), WithSecure and Defender's cloud check flagged DrakanPC-Builder.zip, which holds no binaries:
the scripts themselves were scored. This keeps the patterns those rules key on out of every script the kit ships.
"""

import importlib.util
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_builder_kit", ROOT / "scripts/buildtool/package_builder_kit.py")
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)

# Split so this file does not match itself.
PATTERNS = re.compile(
    "|".join(
        [
            "Invoke-" + "WebRequest",
            "Invoke-" + "RestMethod",
            "Invoke-" + "Expression",
            "Start-" + "BitsTransfer",
            r"\.Download" + r"(File|String|Data)\(",
            "Encoded" + "Command",
            r"WindowStyle\s+" + "Hidden",
        ]
    ),
    re.IGNORECASE,
)
# PowerShell and batch, plus Python (which can build PowerShell command lines). Browser code is out of scope.
SCRIPT_SUFFIXES = (".ps1", ".psm1", ".py", ".cmd", ".bat")


def shipped_scripts():
    names = subprocess.check_output(["git", "-C", str(ROOT), "ls-files", "-z"], text=True).split("\0")
    return [n for n in names if n.endswith(SCRIPT_SUFFIXES) and not kit.excluded(n)]


class KitAntivirusPatternTests(unittest.TestCase):
    def test_developer_powershell_is_left_out(self):
        self.assertTrue(kit.excluded("scripts/harness/combat_diagnostic_run.ps1"))
        self.assertTrue(kit.excluded("ps2recomp/ps2xRuntime/tests/run_object_motion.ps1"))
        self.assertFalse(kit.excluded("scripts/buildtool/Build-Drakan.ps1"))
        self.assertFalse(kit.excluded("scripts/harness/map_previews.py"))

    def test_shipped_scripts_have_no_downloader_patterns(self):
        hits = []
        for name in shipped_scripts():
            path = ROOT / name
            if not path.is_file() or path.resolve() == Path(__file__).resolve():
                continue
            for number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
                if PATTERNS.search(line):
                    hits.append(f"{name}:{number}: {line.strip()}")
        self.assertEqual(hits, [], "the builder kit would ship script patterns antivirus engines flag")


if __name__ == "__main__":
    unittest.main()
