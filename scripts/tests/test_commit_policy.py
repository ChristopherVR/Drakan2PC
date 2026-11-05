import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from commit_policy import check_message


class CommitPolicyTest(unittest.TestCase):
    def test_accepts_scoped_subjects(self):
        for message in (
            "fix(game): Fix VIF1 readback for CHCR.DIR=0",
            "feat(editor,save-editor): Read German disc images\n\nBody text.\n",
            "perf(game)!: Drop the software renderer fallback",
            "docs(repo): Record release smoke results",
            "Merge branch 'risky-change'",
            "fixup! fix(game): Fix jumps",
            "# comment from git\nci(repo): Pin git-cliff",
        ):
            self.assertEqual(check_message(message), [], message)

    def test_rejects_free_form_subject(self):
        self.assertTrue(check_message("Fix VIF1 readback for CHCR.DIR=0"))

    def test_rejects_missing_or_unknown_scope(self):
        self.assertTrue(check_message("fix: Fix jumps"))
        self.assertTrue(check_message("fix(runtime): Fix jumps"))
        self.assertTrue(check_message("fix(game,repo): Fix jumps"))
        self.assertTrue(check_message("fix(game,game): Fix jumps"))

    def test_rejects_style_violations(self):
        self.assertTrue(check_message("feature(game): Fix jumps"))
        self.assertTrue(check_message("fix(game): fix jumps"))
        self.assertTrue(check_message("fix(game): Fix jumps."))
        self.assertTrue(check_message("fix(game): " + "Fix " * 30))
        self.assertTrue(check_message("fix(game): Fix jumps\nno blank line"))
        self.assertTrue(check_message("fix(game): Fix jumps — at high rates"))


if __name__ == "__main__":
    unittest.main()
