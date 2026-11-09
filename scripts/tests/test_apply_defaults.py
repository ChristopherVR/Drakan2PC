"""The installer's "Game options" page: starting values written into the play folder's ini files. Synthetic inis only."""

import importlib.util
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("apply_defaults", ROOT / "scripts/buildtool/apply_defaults.py")
defaults = importlib.util.module_from_spec(spec)
spec.loader.exec_module(defaults)

MAIN_INI = (
    "[general]\n; 1 = a second weapon in the left hand\ndual_wield = 1\n"
    "; native = the game's own title menu, modern = the PC main menu\ntitle_menu = native  ; note\nother = 5\n"
)
INPUT_INI = "[mouse]\n; ootf: left click fires\nbow_aim = native\nsensitivity = 1.0\n"


class SetKeyTests(unittest.TestCase):
    def test_only_the_value_changes(self):
        text, previous = defaults.set_key(MAIN_INI, "title_menu", "modern")
        self.assertEqual(previous, "native")
        self.assertEqual(text, MAIN_INI.replace("title_menu = native  ; note", "title_menu = modern  ; note"))

    def test_a_missing_key_leaves_the_text(self):
        text, previous = defaults.set_key(MAIN_INI, "nothing", "1")
        self.assertIsNone(previous)
        self.assertEqual(text, MAIN_INI)

    def test_comment_lines_and_similar_keys_are_not_matched(self):
        text = "; dual_wield = 9\nxdual_wield = 3\ndual_wield = 1\n"
        new, previous = defaults.set_key(text, "dual_wield", "0")
        self.assertEqual(previous, "1")
        self.assertEqual(new, "; dual_wield = 9\nxdual_wield = 3\ndual_wield = 0\n")


class ApplyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.play = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        (self.play / "drakan.ini").write_text(MAIN_INI, encoding="utf-8", newline="")
        (self.play / "drakan_input.ini").write_text(INPUT_INI, encoding="utf-8", newline="")

    def test_each_choice_sets_its_key(self):
        report = defaults.apply(self.play, "modern", "ootf", "off")
        main = (self.play / "drakan.ini").read_text(encoding="utf-8")
        self.assertIn("title_menu = modern", main)
        self.assertIn("dual_wield = 0", main)
        self.assertIn("other = 5", main)
        self.assertIn("bow_aim = ootf", (self.play / "drakan_input.ini").read_text(encoding="utf-8"))
        self.assertEqual(len(report), 3)

    def test_original_menus_and_the_ps2_bow_are_the_native_values(self):
        defaults.apply(self.play, "modern", "ootf", "on")
        defaults.apply(self.play, "original", "native", "on")
        self.assertIn("title_menu = native", (self.play / "drakan.ini").read_text(encoding="utf-8"))
        self.assertIn("dual_wield = 1", (self.play / "drakan.ini").read_text(encoding="utf-8"))
        self.assertIn("bow_aim = native", (self.play / "drakan_input.ini").read_text(encoding="utf-8"))

    def test_nothing_given_changes_nothing(self):
        self.assertEqual(defaults.apply(self.play), [])
        self.assertEqual((self.play / "drakan.ini").read_text(encoding="utf-8"), MAIN_INI)

    def test_crlf_and_a_bom_are_kept(self):
        raw = b"\xef\xbb\xbf[general]\r\ntitle_menu = native\r\ndual_wield = 1\r\n"
        (self.play / "drakan.ini").write_bytes(raw)
        defaults.apply(self.play, "modern")
        self.assertEqual((self.play / "drakan.ini").read_bytes(), raw.replace(b"native", b"modern"))

    def test_missing_files_and_keys_are_reported_not_fatal(self):
        (self.play / "drakan_input.ini").unlink()
        (self.play / "drakan.ini").write_text("[general]\n", encoding="utf-8")
        report = defaults.apply(self.play, "modern", "ootf", "on")
        self.assertEqual(len(report), 3)
        self.assertTrue(all("left alone" in line for line in report))

    def test_cli_exit_codes(self):
        self.assertEqual(defaults.main(["--play", str(self.play), "--menus", "modern"]), 0)
        self.assertEqual(defaults.main(["--play", str(self.play / "nope")]), 1)


if __name__ == "__main__":
    unittest.main()
