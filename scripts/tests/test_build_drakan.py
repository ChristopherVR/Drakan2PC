"""The Windows builders run end to end up to the point a real ISO or compiler is needed. No game data, no real build.

- Every builder .ps1 parses.
- Build-Drakan.ps1's retry policy (Invoke-BuildWithRetry, Build-Policy.ps1) against a stub Build-LocalGame.ps1.
- Build-Drakan.ps1 -CheckOnly from a temporary copy of the builder scripts with a synthetic ISO.
- Build-LocalGame.ps1 -PrepareOnly with a synthetic ISO: all preflight runs and the ISO reader rejects the file.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BUILDTOOL = ROOT / "scripts/buildtool"
PWSH = shutil.which("pwsh")
WINDOWS = sys.platform == "win32"


def quote(path):
    return "'" + str(path).replace("'", "''") + "'"


def pwsh(body, cwd=None, timeout=120, env=None):
    return subprocess.run(
        [PWSH, "-NoProfile", "-NonInteractive", "-Command", f"$ErrorActionPreference = 'Stop'; {body}"],
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=cwd,
        env=env,
    )


@unittest.skipUnless(PWSH, "PowerShell 7 (pwsh) not on PATH")
class BuilderParseTests(unittest.TestCase):
    def test_builder_scripts_parse(self):
        scripts = sorted(BUILDTOOL.glob("*.ps1"))
        self.assertIn("Build-Drakan.ps1", {path.name for path in scripts})
        listing = ", ".join(quote(path) for path in scripts)
        result = pwsh(
            f"""
            $bad = foreach ($file in @({listing})) {{
                $tokens = $null; $errors = $null
                [Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$errors) | Out-Null
                foreach ($e in $errors) {{ "$file`:$($e.Extent.StartLineNumber): $($e.Message)" }}
            }}
            if ($bad) {{ $bad; exit 1 }}
            """
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_retry_matches_the_compile_step_title(self):
        # Invoke-BuildWithRetry only retries 'Compiling the game*'; the step must keep that title.
        text = (BUILDTOOL / "Build-LocalGame.ps1").read_text(encoding="utf-8")
        self.assertIn('Write-BuildStep "Compiling the game', text)
        self.assertIn("Write-BuildStep 'Setting up the play folder'", text)
        drakan = (BUILDTOOL / "Build-Drakan.ps1").read_text(encoding="utf-8")
        self.assertIn("Invoke-BuildWithRetry (Join-Path $PSScriptRoot 'Build-LocalGame.ps1')", drakan)


STUB = r"""
param([string]$IsoPath, [string]$WorkDir, [int]$Jobs, [int]$CompilerProcesses, [switch]$Incremental)
$calls = Join-Path $PSScriptRoot 'calls.jsonl'
@{ jobs = $Jobs; processes = $CompilerProcesses; incremental = [bool]$Incremental } | ConvertTo-Json -Compress |
    Add-Content -LiteralPath $calls
$plan = @(Get-Content -LiteralPath (Join-Path $PSScriptRoot 'plan.txt'))
$n = @(Get-Content -LiteralPath $calls).Count
$step = $plan[[Math]::Min($n, $plan.Count) - 1]
if ($step -eq 'ok') { return }
if ($step -ne 'none') { $global:DrakanBuildStep = $step }
throw "stub failure in $step"
"""


@unittest.skipUnless(PWSH, "PowerShell 7 (pwsh) not on PATH")
class RetryPolicyTests(unittest.TestCase):
    def run_policy(self, plan, prepared=True):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            (temp / "Build-LocalGame.ps1").write_text(STUB, encoding="utf-8")
            (temp / "plan.txt").write_text("\n".join(plan) + "\n", encoding="utf-8")
            marker = temp / "local-input.json"
            if prepared:
                marker.write_text("{}", encoding="utf-8")
            result = pwsh(
                f"""
                . {quote(BUILDTOOL / "Build-Policy.ps1")}
                $buildArgs = @{{ IsoPath = 'x.iso'; WorkDir = 'w'; Jobs = 2; CompilerProcesses = 4 }}
                try {{ Invoke-BuildWithRetry {quote(temp / "Build-LocalGame.ps1")} $buildArgs {quote(marker)}; 'RESULT ok' }}
                catch {{ "RESULT failed: $_" }}
                """
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = [json.loads(line) for line in (temp / "calls.jsonl").read_text(encoding="utf-8").splitlines()]
            outcome = next(line for line in result.stdout.splitlines() if line.startswith("RESULT"))
            return calls, outcome

    def test_play_folder_failure_is_not_retried(self):
        calls, outcome = self.run_policy(["Setting up the play folder"])
        self.assertEqual(len(calls), 1)
        self.assertIn("Setting up the play folder", outcome)

    def test_compile_failure_is_retried_once_with_less_parallelism(self):
        calls, outcome = self.run_policy(["Compiling the game (366 files)", "ok"])
        self.assertEqual(outcome, "RESULT ok")
        self.assertEqual(calls[0], {"jobs": 2, "processes": 4, "incremental": False})
        self.assertEqual(calls[1], {"jobs": 1, "processes": 2, "incremental": True})

    def test_second_compile_failure_stops(self):
        calls, outcome = self.run_policy(["Compiling the game", "Compiling the game"])
        self.assertEqual(len(calls), 2)
        self.assertTrue(outcome.startswith("RESULT failed"))

    def test_failure_before_the_iso_was_read_is_not_retried(self):
        calls, outcome = self.run_policy(["Compiling the game", "ok"], prepared=False)
        self.assertEqual(len(calls), 1)
        self.assertTrue(outcome.startswith("RESULT failed"))

    def test_other_steps_are_not_retried(self):
        for step in ("Translating the PS2 game code to C++", "Preparing the runtime files from the game"):
            with self.subTest(step=step):
                calls, _ = self.run_policy([step, "ok"])
                self.assertEqual(len(calls), 1)


def synthetic_iso(folder):
    iso = Path(folder) / "Not Drakan's disc.iso"
    iso.write_bytes(b"\0" * 64 * 2048)
    return iso


@unittest.skipUnless(PWSH and WINDOWS, "Windows builders need Windows and PowerShell 7")
class CheckOnlyTests(unittest.TestCase):
    def test_build_drakan_check_only(self):
        node = shutil.which("node")
        version = subprocess.run([node, "-p", "process.versions.node"], capture_output=True, text=True).stdout
        if not node or tuple(int(part) for part in version.strip().split(".")[:2]) < (22, 7):
            self.skipTest("Build-Drakan.ps1 -CheckOnly needs Node.js 22.7+ on PATH")
        if not shutil.which("git"):
            self.skipTest("Build-Drakan.ps1 -CheckOnly needs Git on PATH outside the kit")
        with tempfile.TemporaryDirectory() as temp:
            # A copy, so the remembered ISO, build options and build-log.txt land in the temporary folder.
            copy = Path(temp) / "kit"
            shutil.copytree(BUILDTOOL, copy / "scripts/buildtool", ignore=shutil.ignore_patterns("__pycache__"))
            iso = synthetic_iso(temp)
            python_dir = str(Path(sys.executable).parent)
            env = dict(os.environ, PATH=python_dir + os.pathsep + os.environ["PATH"])
            result = pwsh(
                f"& {quote(copy / 'scripts/buildtool/Build-Drakan.ps1')} -IsoPath {quote(iso)} -CheckOnly "
                "-CpuAffinity 0x1 -Cores 0",
                env=env,
                timeout=300,
            )
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            self.assertIn("Ready to build.", output)
            self.assertRegex(output, r"Memory: +\d+ GB -> 1 project\(s\) x 1 compiler process\(es\)")
            self.assertTrue((copy / "build/iso-path.txt").is_file())
            self.assertEqual(json.loads((copy / "build/build-options.json").read_text())["cpuAffinity"], "0x1")

    def test_build_local_game_prepare_only_rejects_a_synthetic_iso(self):
        with tempfile.TemporaryDirectory() as temp:
            iso = synthetic_iso(temp)
            work = Path(temp) / "work"
            result = pwsh(
                f"try {{ & {quote(BUILDTOOL / 'Build-LocalGame.ps1')} -IsoPath {quote(iso)} -WorkDir {quote(work)} "
                f"-Python {quote(sys.executable)} -PrepareOnly -Progress -CpuAffinity 0x1 }} "
                'catch { "FAILED [$global:DrakanBuildStep] $_" }',
                timeout=300,
            )
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            # Everything before the ISO reader ran; prepare_iso.py refused the file, nothing was written.
            self.assertIn("FAILED [Reading the game from the ISO]", output)
            self.assertNotIn("ParserError", output)
            self.assertFalse((work / "local-input.json").exists())


if __name__ == "__main__":
    unittest.main()
