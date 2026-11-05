"""The loading-freeze report must identify the last completed phase on the requested map."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
from arena_movement import transition_progress


class TransitionProgressTest(unittest.TestCase):
    level = "Level 1E - Inquisitor's Lair"
    saved = "title action Continue(1)\nafterLevelLoad level='Surdana' path='a' fromSave=1 ok=1\n"
    title = "overlay: title session 2 opened\n"
    launch = f"multiplayer action JoinMatch\nmultiplayer launch: New Game with '{level}'\n"
    entry = f"beforeLevelLoad level='{level}' path='a' fromSave=0 ok=1\n"
    success = f"afterLevelLoad level='{level}' path='a' fromSave=0 ok=1\n"

    def progress(self, log):
        return transition_progress(log, "client", self.level)

    def test_completed(self):
        self.assertEqual(
            self.progress(self.saved + self.title + self.launch + self.entry + self.success + "script quit"),
            "completed",
        )

    def test_fresh_campaign_requires_playable_surdana_before_return(self):
        intro = "afterLevelLoad level='Level 0 - Intro' path='intro' fromSave=0 ok=1\n"
        surdana = "afterLevelLoad level='Level 1A - Surdana' path='surdana' fromSave=0 ok=1\n"
        match = self.title + self.launch + self.entry + self.success + "script quit"
        self.assertEqual(
            transition_progress(intro + surdana + match, "client", self.level, from_save=False), "completed"
        )
        self.assertEqual(
            transition_progress(intro + match, "client", self.level, from_save=False), "singleplayer_load_incomplete"
        )

    def test_stale_title_and_load_do_not_count(self):
        self.assertEqual(self.progress(self.title + self.saved + self.launch), "return_to_title_missing")
        self.assertEqual(
            self.progress(self.saved + self.title + self.launch + self.success + self.entry),
            "multiplayer_load_return_missing",
        )

    def test_distinguish_wrong_map_load_failure_and_post_load_stall(self):
        start = self.saved + self.title + self.launch
        self.assertEqual(self.progress(start.replace(self.level, "Other map")), "unexpected_multiplayer_level")
        self.assertEqual(
            self.progress(start + self.entry + self.success.replace("ok=1", "ok=0")), "multiplayer_load_failed"
        )
        self.assertEqual(self.progress(start + self.entry + self.success), "post_load_script_incomplete")


if __name__ == "__main__":
    unittest.main()
