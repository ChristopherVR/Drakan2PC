"""Opt-in NVIDIA DLSS SDK download: lock contents and licence gate (no network)."""

import contextlib
import io
import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "buildtool"))
import fetch_dlss_sdk


class DlssSdkLockTests(unittest.TestCase):
    def test_lock_pins_everything_the_build_needs(self):
        lock = fetch_dlss_sdk.load_lock()
        self.assertEqual(lock["repository"], "NVIDIA/DLSS")
        for required in (
            "LICENSE.txt",
            "include/nvsdk_ngx.h",
            "include/nvsdk_ngx_helpers.h",
            "include/nvsdk_ngx_helpers_d3d.h",
            "lib/Windows_x86_64/x64/nvsdk_ngx_d.lib",
            fetch_dlss_sdk.RUNTIME_DLL,
        ):
            self.assertIn(required, lock["files"])
        for digest in lock["files"].values():
            self.assertRegex(digest, r"^[0-9a-f]{64}$")

    def test_nothing_is_downloaded_without_accepting_the_licence(self):
        with (
            mock.patch.object(sys, "argv", ["fetch_dlss_sdk.py"]),
            mock.patch.object(fetch_dlss_sdk, "download") as download,
            contextlib.redirect_stdout(io.StringIO()) as output,
        ):
            self.assertEqual(fetch_dlss_sdk.main(), 2)
        download.assert_not_called()
        self.assertIn("LICENSE.txt", output.getvalue())


if __name__ == "__main__":
    unittest.main()
