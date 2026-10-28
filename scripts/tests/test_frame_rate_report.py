import contextlib
import hashlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "frame_rate_report", Path(__file__).parents[1] / "perf" / "frame_rate_report.py"
)
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


class FrameRateReportTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.before, self.after = [Path(temp.name) / n for n in ("before", "after")]
        for directory, rate in ((self.before, "30"), (self.after, "60")):
            directory.mkdir()
            script, ini = b"clock milliseconds\n12000 quit\n", b"[video]\nvsync=0\n"
            (directory / "sequence.txt").write_bytes(script)
            (directory / "drakan_input.ini").write_bytes(ini)
            self.write(
                directory,
                "manifest.json",
                {
                    "sha256": "a" * 64,
                    "script_sha256": hashlib.sha256(script).hexdigest(),
                    "input_ini_sha256": hashlib.sha256(ini).hexdigest(),
                    "environment": {
                        "PS2X_GS": "d3d11",
                        "PS2X_GS_SCALE": "4",
                        "DRAKAN_DLSS": "quality",
                        "DRAKAN_WORLD_RATE": rate,
                        "DRAKAN_INPUT_SCRIPT": str(directory / "sequence.txt"),
                    },
                },
            )
            self.write(
                directory,
                "completion.json",
                {
                    "exitCode": 0,
                    "scriptQuit": True,
                    "sha256After": "a" * 64,
                    "elapsedSeconds": 40,
                },
            )
            self.write(directory, "save-preservation.json", {"original": True, "source": True, "private": True})
            (directory / "input.log").write_text("[in] script quit at frame 123\n")
            # Exclude early positive and menu windows; weight the final five.
            windows = [(1, 999), (1, 0), (1, 10), (2, 20), (3, 30), (4, 40), (5, 50)]
            (directory / "stderr.log").write_text(
                "\n".join(
                    f"[gsr-pacing] {seconds}s: worldUpdates={world}/s worldPresents={world + 1}/s guestVsyncs=60/s"
                    for seconds, world in windows
                )
            )

    @staticmethod
    def write(directory, name, value):
        (directory / name).write_text(json.dumps(value), encoding="utf-8")

    def change(self, directory, name, **updates):
        value = report.read_json(directory, name)
        value.update(updates)
        self.write(directory, name, value)

    def result(self):
        return report.compare(self.before, self.after)

    def test_valid_weighted_last_five_and_rate_difference_allowed(self):
        result = self.result()
        self.assertTrue(result["comparison_eligible"], result["errors"])
        summary = result["runs"]["after"]
        self.assertEqual(len(summary["selected_windows"]), 5)
        self.assertEqual(summary["selected_seconds"], 15)
        self.assertAlmostEqual(summary["weighted_rates_per_second"]["worldUpdates"], 550 / 15)
        self.assertAlmostEqual(summary["weighted_rates_per_second"]["worldPresents"], 565 / 15)
        self.assertEqual(summary["weighted_rates_per_second"]["guestVsyncs"], 60)
        self.assertEqual(summary["requested_world_rate"], "60")
        self.assertEqual(summary["input_script_clock"], "milliseconds")
        self.assertNotIn("gameplay_speed_pass", result)

    def test_missing_completion(self):
        (self.after / "completion.json").unlink()
        self.assertFalse(self.result()["comparison_eligible"])

    def test_interleaved_non_world_window_only_is_excluded(self):
        path = self.after / "stderr.log"
        old = path.read_text()
        damaged = "[gsr-pacing] 2s: worldUpdates=0.00/s worldPresent[MC] status\n"
        path.write_text(damaged + old)
        self.assertTrue(self.result()["comparison_eligible"])
        path.write_text(damaged.replace("=0.00/s", "=30.00/s") + old)
        self.assertFalse(self.result()["comparison_eligible"])

    def test_mismatched_executable(self):
        self.change(self.after, "manifest.json", sha256="b" * 64)
        self.change(self.after, "completion.json", sha256After="b" * 64)
        self.assertIn("Manifest SHA-256 mismatch: sha256", self.result()["errors"])

    def test_no_positive_world_evidence(self):
        (self.after / "stderr.log").write_text("[gsr-pacing] 2s: worldUpdates=0/s worldPresents=0/s guestVsyncs=60/s\n")
        self.assertFalse(self.result()["comparison_eligible"])
        self.assertIn("found 0", " ".join(self.result()["errors"]))

    def test_missing_script_quit(self):
        (self.after / "input.log").write_text("interrupted\n")
        self.assertFalse(self.result()["comparison_eligible"])

    def test_failed_exit_and_changed_executable(self):
        for updates in ({"exitCode": 1}, {"sha256After": "b" * 64}):
            with self.subTest(updates=updates):
                self.change(self.after, "completion.json", exitCode=0, sha256After="a" * 64)
                self.change(self.after, "completion.json", **updates)
                self.assertFalse(self.result()["comparison_eligible"])

    def test_missing_or_changed_save_evidence(self):
        for saves in ({"original": True, "source": True}, {"original": True, "source": True, "private": False}):
            with self.subTest(saves=saves):
                self.write(self.after, "save-preservation.json", saves)
                self.assertFalse(self.result()["comparison_eligible"])

    def test_render_settings_must_match_including_missing_key(self):
        manifest = report.read_json(self.after, "manifest.json")
        manifest["environment"]["DRAKAN_DLSS"] = "off"
        self.write(self.after, "manifest.json", manifest)
        self.assertIn("Recorded environment mismatch: DRAKAN_DLSS", self.result()["errors"])
        del manifest["environment"]["DRAKAN_DLSS"]
        self.write(self.after, "manifest.json", manifest)
        self.assertFalse(self.result()["comparison_eligible"])

    def test_script_and_input_hashes_must_match(self):
        for name, field in (("sequence.txt", "script_sha256"), ("drakan_input.ini", "input_ini_sha256")):
            with self.subTest(field=field):
                path = self.after / name
                original = path.read_bytes()
                changed = original + b"# changed\n"
                path.write_bytes(changed)
                self.change(self.after, "manifest.json", **{field: hashlib.sha256(changed).hexdigest()})
                self.assertIn(f"Manifest SHA-256 mismatch: {field}", self.result()["errors"])
                path.write_bytes(original)
                self.change(self.after, "manifest.json", **{field: hashlib.sha256(original).hexdigest()})

    def test_nonfinite_rate_rejected(self):
        path = self.after / "stderr.log"
        path.write_text(path.read_text().replace("worldUpdates=50/s", "worldUpdates=nan/s"))
        self.assertFalse(self.result()["comparison_eligible"])

    def test_cli_failure_is_nonzero_json(self):
        (self.after / "completion.json").unlink()
        with contextlib.redirect_stdout(io.StringIO()) as output:
            code = report.main([str(self.before), str(self.after)])
        self.assertEqual(code, 1)
        self.assertFalse(json.loads(output.getvalue())["comparison_eligible"])


if __name__ == "__main__":
    unittest.main()
