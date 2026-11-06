"""Every builder script runs under the builder kit's own embeddable Python, with its ._pth exactly as the kit ships it.

The tools come from `python scripts/buildtool/package_builder_kit.py --fetch-tools build/kit-tools` (pinned and
SHA-256 checked in scripts/buildtool/kit/installer.lock.json); set DRAKAN_KIT_TOOLS to use another folder. Without
them the test skips. The installer build runs the same smoke test on its staged copy and refuses to package on failure.
"""

import importlib.util
import os
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("package_builder_kit", ROOT / "scripts/buildtool/package_builder_kit.py")
kit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(kit)
TOOLS = Path(os.environ.get("DRAKAN_KIT_TOOLS") or ROOT / "build/kit-tools")
PYTHON = TOOLS / "python/python.exe"
FETCH = "python scripts/buildtool/package_builder_kit.py --fetch-tools build/kit-tools"


@unittest.skipUnless(PYTHON.is_file(), f"kit Python not unpacked ({FETCH})")
class KitToolsTests(unittest.TestCase):
    def test_pth_is_the_shipped_one(self):
        # The kit leaves the embeddable ._pth alone: no script folder, no site-packages. Tests must see the same.
        pth = next(PYTHON.parent.glob("python3*._pth")).read_text(encoding="utf-8").split()
        self.assertIn(".", pth)
        self.assertIn("#import", pth)
        self.assertNotIn("import", pth)

    def test_builder_scripts_run_with_the_kit_tools(self):
        formats = (ROOT / "editor/packages/formats/src/ootfanim.ts").is_file()
        node = formats and (TOOLS / "node/node.exe").is_file()
        kit.smoke_test(ROOT, TOOLS, node=node)
        if not node:
            self.skipTest("Python part passed; Node part needs the formats submodule and the kit Node.js")


if __name__ == "__main__":
    unittest.main()
