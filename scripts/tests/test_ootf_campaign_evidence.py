import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
from ootf_coop_campaign import analyse, verdict


class CampaignEvidenceTest(unittest.TestCase):
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
