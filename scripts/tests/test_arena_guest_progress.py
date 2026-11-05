"""Catch the archived arena failure where UDP stats continue after the EE stops."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
from arena_two_instances import guest_progress_checks


class ArenaGuestProgressTest(unittest.TestCase):
    def test_network_heartbeats_do_not_establish_guest_progress(self):
        checks = guest_progress_checks(
            "[guest-branch:missing-target] tid=1 target=0x0\n"
            "[netplay 153.859] stats: rtt 52.2 ms, snapshots 18.2/s\n"
            "[netplay 158.990] stats: rtt 124.7 ms, snapshots 16.0/s\n"
        )
        self.assertFalse(checks["no missing guest branch target"])
        self.assertFalse(checks["guest input script reached quit"])

    def test_native_script_completes_without_guest_fault(self):
        self.assertTrue(all(guest_progress_checks("[in] script quit at frame 9959\n").values()))

    def test_later_exit_does_not_hide_fault(self):
        checks = guest_progress_checks("[guest-branch:missing-target] tid=1\n[in] script quit at frame 9959\n")
        self.assertFalse(checks["no missing guest branch target"])


if __name__ == "__main__":
    unittest.main()
