"""Reject incomplete receipts and average-FPS-only performance claims."""

import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "software_frame_report", Path(__file__).resolve().parents[1] / "perf/software_frame_report.py"
)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class PerformanceReceiptTests(unittest.TestCase):
    def test_native_window_preserves_both_histograms(self):
        lines = []
        for second in range(40):
            count = second * 32
            lines.append(
                f"[frame-metrics] stamp={second} flips={count} updates={count} gameSeconds={second} "
                f"invalid=0 uploads={count} uploadErrors=0 bins=0,0,0,{count},0,0,0,0 "
                f"uploadBins=0,0,0,{count},0,0,0,0"
            )
        receipt = report.native_receipt(
            "\n".join(lines), scenario="native grass", target="Linux", binary_sha256="b" * 64
        )
        self.assertEqual(receipt["start"]["wall_seconds"], 9)
        self.assertEqual(report.assess(receipt)["status"], "pass")
        with self.assertRaises(ValueError):
            report.native_receipt("[frame-metrics] stamp=5", scenario="bad", target="Linux", binary_sha256="b" * 64)

    def receipt(self):
        return {
            "scenario": "grass",
            "target": "browser",
            "binary_sha256": "a" * 64,
            "start": {
                "wall_seconds": 10,
                "frames": 0,
                "game_seconds": 0,
                "metrics": [0] * 12 + [1],
                "upload_bins": [0] * 8,
            },
            "end": {
                "wall_seconds": 40,
                "frames": 960,
                "game_seconds": 30,
                "metrics": [0, 0, 0, 960, 0, 0, 0, 0, 960, 0, 960, 0, 1],
                "upload_bins": [0, 0, 0, 960, 0, 0, 0, 0],
            },
        }

    def test_steady_gameplay_passes(self):
        self.assertEqual(report.assess(self.receipt())["status"], "pass")

    def test_average_fps_cannot_hide_stalls(self):
        receipt = self.receipt()
        receipt["end"]["metrics"][3:5] = [800, 160]
        self.assertEqual(report.assess(receipt)["status"], "fail")

    def test_simulation_or_delivery_can_fail_independently(self):
        for field in ("clock", "uploads", "invalid", "errors"):
            with self.subTest(field=field):
                receipt = self.receipt()
                if field == "clock":
                    receipt["end"]["game_seconds"] = 29
                else:
                    receipt["end"]["metrics"][{"uploads": 10, "invalid": 9, "errors": 11}[field]] = (
                        800 if field == "uploads" else 1
                    )
                    if field == "uploads":
                        receipt["end"]["upload_bins"][3] = 800
                self.assertEqual(report.assess(receipt)["status"], "fail")

    def test_fast_renderer_cannot_hide_stalled_presentation(self):
        receipt = self.receipt()
        receipt["end"]["upload_bins"][3:5] = [800, 160]
        self.assertEqual(report.assess(receipt)["status"], "fail")

    def test_missing_disabled_reset_or_short_capture_is_inconclusive(self):
        for field in ("missing", "disabled", "reset", "short", "histogram", "nan", "empty"):
            with self.subTest(field=field):
                receipt = self.receipt()
                if field == "missing":
                    del receipt["binary_sha256"]
                elif field == "disabled":
                    receipt["start"]["metrics"][12] = 0
                elif field == "reset":
                    receipt["start"]["metrics"][8] = 1000
                elif field == "short":
                    receipt["end"]["wall_seconds"] = 15
                elif field == "histogram":
                    receipt["end"]["frames"] = 1000
                elif field == "empty":
                    receipt["end"]["frames"] = 1
                    receipt["end"]["metrics"][3] = 0
                else:
                    receipt["end"]["game_seconds"] = float("nan")
                self.assertEqual(report.assess(receipt)["status"], "inconclusive")
