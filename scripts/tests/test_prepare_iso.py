"""prepare_iso --refresh-config: re-renders config.toml in place without touching anything else."""

import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_iso", ROOT / "scripts/buildtool/prepare_iso.py")
prepare_iso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare_iso)

TEMPLATE = '[general]\ninput = "x"\nfunctions_per_file = 64\nstubs = []\n'


class RefreshConfigTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="drakan-prepare-iso-test-")
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.repo = self.root / "repo"
        (self.repo / "analysis").mkdir(parents=True)
        for name in ("functions.csv", "jumptables.csv"):
            (self.repo / "analysis" / name).write_text("", encoding="utf-8")
        self.template = self.root / "drakan.toml"
        self.template.write_text(TEMPLATE, encoding="utf-8")
        self.work = self.root / "work"
        self.work.mkdir()
        boot = json.loads((ROOT / "scripts/buildtool/toolchain.lock.json").read_text())["supported_game"]["boot"]
        (self.work / boot).write_bytes(b"elf")
        (self.work / "local-input.json").write_text("{}", encoding="utf-8")

    def test_writes_then_leaves_unchanged_config_alone(self):
        self.assertTrue(prepare_iso.refresh_config(self.work, self.template, self.repo))
        config = self.work / "config.toml"
        self.assertIn("functions_per_file = 64", config.read_text(encoding="utf-8"))
        self.assertIn((self.work / "generated").as_posix(), config.read_text(encoding="utf-8"))
        self.assertFalse(prepare_iso.refresh_config(self.work, self.template, self.repo))

    def test_template_change_is_picked_up(self):
        prepare_iso.refresh_config(self.work, self.template, self.repo)
        self.template.write_text(TEMPLATE.replace("64", "32"), encoding="utf-8")
        self.assertTrue(prepare_iso.refresh_config(self.work, self.template, self.repo))
        self.assertIn("functions_per_file = 32", (self.work / "config.toml").read_text(encoding="utf-8"))

    def test_refuses_unprepared_directory(self):
        (self.work / "local-input.json").unlink()
        with self.assertRaises(ValueError):
            prepare_iso.refresh_config(self.work, self.template, self.repo)


if __name__ == "__main__":
    unittest.main()
