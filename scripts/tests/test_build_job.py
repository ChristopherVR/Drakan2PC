"""Ending the builder must end the compilers it started (scripts/buildtool/Build-Job.ps1). Real processes, no game data."""

import hashlib
import importlib.util
import os
import shutil
import subprocess
import time
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
JOB = ROOT / "scripts/buildtool/Build-Job.ps1"
PWSH = shutil.which("pwsh")


def alive(pid: int) -> bool:
    result = subprocess.run(
        ["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"], capture_output=True, text=True, check=False
    )
    return f'"{pid}"' in result.stdout


@unittest.skipUnless(PWSH and os.name == "nt", "needs Windows and PowerShell 7")
class BuildJobTests(unittest.TestCase):
    def start_builder(self, script: str):
        """A PowerShell that enters the job, starts a long-running child and prints its PID; returns (process, child pid)."""
        command = (
            f". '{JOB}'; $in = Enter-BuildJob; "
            "if (-not $in) { 'NOJOB'; exit 0 }; "
            "$child = Start-Process -FilePath ping.exe -ArgumentList '-n','120','127.0.0.1' -PassThru -WindowStyle Hidden; "
            f"'PID=' + $child.Id; {script}; Start-Sleep -Seconds 120"
        )
        builder = subprocess.Popen([PWSH, "-NoProfile", "-Command", command], stdout=subprocess.PIPE, text=True)
        line = builder.stdout.readline().strip()
        if line == "NOJOB":
            builder.wait(5)
            self.skipTest("this environment does not allow a job object for the test process")
        self.addCleanup(builder.stdout.close)
        self.assertTrue(line.startswith("PID="), line)
        child = int(line[4:])
        self.addCleanup(
            lambda: subprocess.run(["taskkill", "/F", "/T", "/PID", str(child)], capture_output=True, check=False)
        )
        self.addCleanup(
            lambda: subprocess.run(["taskkill", "/F", "/T", "/PID", str(builder.pid)], capture_output=True, check=False)
        )
        return builder, child

    def wait_gone(self, pid: int, seconds: float = 10) -> bool:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if not alive(pid):
                return True
            time.sleep(0.3)
        return False

    def test_killing_the_builder_ends_its_children(self):
        builder, child = self.start_builder("")
        self.assertTrue(alive(child))
        subprocess.run(["taskkill", "/F", "/PID", str(builder.pid)], capture_output=True, check=True)
        builder.wait(10)
        self.assertTrue(self.wait_gone(child), "the compiler stand-in kept running after the builder ended")

    def test_a_released_job_leaves_children_running_for_the_launched_game(self):
        builder, child = self.start_builder("Disable-BuildJobKill")
        time.sleep(1.5)  # the child line is printed before the release runs
        subprocess.run(["taskkill", "/F", "/PID", str(builder.pid)], capture_output=True, check=True)
        builder.wait(10)
        time.sleep(2)
        self.assertTrue(alive(child), "Disable-BuildJobKill must let a launched game outlive the builder")


class WiringTests(unittest.TestCase):
    def test_both_builders_enter_the_job_and_launch_releases_it(self):
        drakan = (ROOT / "scripts/buildtool/Build-Drakan.ps1").read_text(encoding="utf-8")
        local = (ROOT / "scripts/buildtool/Build-LocalGame.ps1").read_text(encoding="utf-8")
        for text in (drakan, local):
            self.assertIn("Build-Job.ps1", text)
            self.assertIn("Enter-BuildJob", text)
        self.assertLess(drakan.index("Enter-BuildJob"), drakan.index("Start-Transcript"), "before anything is started")
        launch = drakan[drakan.index("if ($LaunchWhenDone") :]
        self.assertLess(launch.index("Disable-BuildJobKill"), launch.index("Start-Process"))

    def test_the_helper_sets_kill_on_close(self):
        text = JOB.read_text(encoding="utf-8")
        self.assertIn("0x2000", text)  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        self.assertIn("AssignProcessToJobObject", text)


class SettingsPicturesTests(unittest.TestCase):
    def test_every_picture_is_listed_and_pinned_and_none_is_missing_from_the_list(self):
        spec = importlib.util.spec_from_file_location("repository_guard", ROOT / "scripts/repository_guard.py")
        guard = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(guard)
        art = ROOT / "scripts/buildtool/kit/art/settings"
        listed = {}
        for line in (ROOT / "scripts/buildtool/kit/art/settings.sha256").read_text(encoding="utf-8").splitlines():
            digest, _, rel = line.partition("  ")
            listed[rel] = digest
        files = {p.relative_to(art).as_posix(): p for p in art.rglob("*.png")}
        self.assertEqual(sorted(files), sorted(listed), "the manifest and the folder list the same pictures")
        self.assertEqual(len(files), 106)
        for rel, path in files.items():
            data = path.read_bytes()
            name = f"scripts/buildtool/kit/art/settings/{rel}"
            self.assertEqual(hashlib.sha256(data).hexdigest(), listed[rel], rel)
            self.assertIsNone(guard.reason(name, data), rel)
        sample = next(iter(files))
        self.assertIsNotNone(
            guard.reason(f"scripts/buildtool/kit/art/settings/{sample}", b"changed"), "a changed picture is refused"
        )
        self.assertIsNotNone(
            guard.reason("scripts/buildtool/kit/art/settings/extra/new.png", b"x"), "an unlisted one too"
        )

    def test_the_build_copies_missing_pictures_only(self):
        build = (ROOT / "scripts/buildtool/Build-LocalGame.ps1").read_text(encoding="utf-8")
        block = build[build.index("kit/art/settings") : build.index("kit/art/maps")]
        self.assertIn("previews/settings", block)
        self.assertIn("Test-Path -LiteralPath $target", block)
        self.assertIn("continue", block, "an existing picture is never replaced")


if __name__ == "__main__":
    unittest.main()
