import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
from ootf_coop_campaign import analyse, reanalyse, verdict


class CampaignEvidenceTest(unittest.TestCase):
    def test_periodic_input_profile_counts_without_gui_transition(self):
        r = analyse(
            "afterLevelLoad level='Target' ok=1\n[in-log] t=280.00 frame=16800 play player=0x1234 lookCalls=1\n",
            "Target",
        )
        self.assertTrue(r["control"])
        menu = analyse(
            "afterLevelLoad level='Target' ok=1\n[in-log] t=280.00 frame=16800 menu player=0x1234 lookCalls=1\n",
            "Target",
        )
        self.assertFalse(menu["control"])

    def test_control_in_a_later_level_does_not_count(self):
        r = analyse("afterLevelLoad level='Target' ok=1\nafterLevelLoad level='Other' ok=1\n(gameplay=1)\n", "Target")
        self.assertFalse(r["control"])

    def test_reanalysis_preserves_failed_exit_and_chain_requirements(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "chain12" / "a"
            folder.mkdir(parents=True)
            (folder / "stdout_1.log").write_text(
                "afterLevelLoad level='OotF 00 - Intro' ok=1\n(gameplay=1)\nexit taken in 'Intro'\n[in] script quit\n"
            )
            (root / "report.json").write_text(
                json.dumps(
                    {
                        "sessions": [
                            {
                                "label": "chain12",
                                "instances": {"a": {"exit_code": 5, "hang": True, "chain_required": 4}},
                            }
                        ]
                    }
                )
            )
            session = reanalyse(root, root, True)["sessions"][0]
            self.assertFalse(session["pass"])
            self.assertEqual(session["instances"]["a"]["exit_code"], 5)
            self.assertIn("a: hang", session["why"])
            self.assertIn("a: incomplete chain 1/4", session["why"])

    def test_quit_log_without_process_record_cannot_pass(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "chain1" / "a"
            folder.mkdir(parents=True)
            (folder / "stdout_1.log").write_text(
                "afterLevelLoad level='OotF 00 - Intro' ok=1\n(gameplay=1)\nexit taken in 'Intro'\n[in] script quit\n"
            )
            session = reanalyse(root, root, True)["sessions"][0]
            self.assertFalse(session["pass"])
            self.assertIn("a: missing process exit evidence", session["why"])

    def test_watchdog_after_control_fails_even_with_zero_exit(self):
        r = analyse(
            "afterLevelLoad level='Target' ok=1\n(gameplay=1)\n"
            "[watchdog t=311.9s] live thread=1 pc=0x4acdd0\n"
            "[run] PS2X_MAX_SECONDS reached, stopping\n",
            "Target",
        )
        r.update(exit_code=0, hang=False)
        self.assertTrue(r["control"])
        self.assertIn("a: stall", verdict({"a": r}, False)[1])

    def test_missing_instances_fail(self):
        self.assertFalse(verdict({}, False)[0])
        self.assertFalse(verdict({}, True)[0])

    def test_incomplete_chain_fails_despite_gameplay(self):
        r = analyse("afterLevelLoad level='OotF 00 - Intro' ok=1\n(gameplay=1)", None)
        r.update(exit_code=0, hang=False, chain_required=12, chain_completed=1)
        self.assertFalse(verdict({"a": r}, False)[0])

    def test_control_before_target_load_does_not_count(self):
        r = analyse("(gameplay=1)\nafterLevelLoad level='Other' ok=1", "Target")
        self.assertFalse(r["control"])
        self.assertFalse(r["loaded"])

    def test_control_after_target_load_counts(self):
        r = analyse("afterLevelLoad level='Target' ok=1\n(gameplay=1)", "Target")
        self.assertTrue(r["control"])
        self.assertTrue(r["loaded"])
        r.update(exit_code=0, hang=False)
        self.assertTrue(verdict({"a": r}, False)[0])
        self.assertFalse(verdict({"a": r}, True)[0])


if __name__ == "__main__":
    unittest.main()
