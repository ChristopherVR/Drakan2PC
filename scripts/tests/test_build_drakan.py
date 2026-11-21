"""The Windows builders run end to end up to the point a real ISO or compiler is needed. No game data, no real build.

- Every builder .ps1 parses.
- Build-Drakan.ps1's retry policy (Invoke-BuildWithRetry, Build-Policy.ps1) against a stub Build-LocalGame.ps1.
- Build-Drakan.ps1 -CheckOnly from a temporary copy of the builder scripts with a synthetic ISO.
- Build-LocalGame.ps1 -PrepareOnly with a synthetic ISO: all preflight runs and the ISO reader rejects the file.
"""

import json
import os
import re
import shutil
import string
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
        encoding="utf-8",
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


class BuilderLanguageTests(unittest.TestCase):
    def test_each_message_has_all_five_locales_and_the_same_format_arguments(self):
        catalog = json.loads((BUILDTOOL / "build-messages.json").read_text(encoding="utf-8"))

        def fields(text):
            return sorted(field for _, field, _, _ in string.Formatter().parse(text) if field is not None)

        for key, messages in catalog.items():
            with self.subTest(key=key):
                self.assertEqual(set(messages), {"en", "de", "fr", "it", "es"})
                self.assertTrue(all(messages.values()))
                for translation in messages.values():
                    self.assertEqual(fields(translation), fields(messages["en"]))


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
        if not node or tuple(int(part) for part in version.strip().split(".")[:2]) < (22, 18):
            self.skipTest("Build-Drakan.ps1 -CheckOnly needs Node.js 22.18+ on PATH")
        if not shutil.which("git"):
            self.skipTest("Build-Drakan.ps1 -CheckOnly needs Git on PATH outside the kit")
        with tempfile.TemporaryDirectory() as temp:
            # A copy, so the remembered ISO, build options and build-log.txt land in the temporary folder.
            copy = Path(temp) / "builder kit with an intentionally long installation path for the warning check"
            shutil.copytree(BUILDTOOL, copy / "scripts/buildtool", ignore=shutil.ignore_patterns("__pycache__"))
            iso = synthetic_iso(temp)
            python_dir = str(Path(sys.executable).parent)
            env = dict(os.environ, PATH=python_dir + os.pathsep + os.environ["PATH"])
            result = pwsh(
                "if ($PSStyle) { $PSStyle.OutputRendering = 'Ansi' }; "
                f"& {quote(copy / 'scripts/buildtool/Build-Drakan.ps1')} -IsoPath {quote(iso)} -CheckOnly "
                "-CpuAffinity 0x1 -Cores 0 -VuBlocks Whole; "
                "if ($PSStyle -and $PSStyle.OutputRendering -ne 'Ansi') { throw 'Rendering preference changed' }",
                env=env,
                timeout=300,
            )
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            self.assertIn("Ready to build.", output)
            self.assertRegex(output, r"Compiler +Up to 1 worker, \d+ GB RAM, normal priority, CPU mask 0x1")
            self.assertLess(output.index("System check"), output.index("Game files"))
            self.assertLess(output.index("Game files"), output.index("Build plan"))
            self.assertIn("Use original textures", output)
            self.assertIn("The installation path has", output)
            self.assertNotIn("\x1b", output)
            self.assertTrue((copy / "build/iso-path.txt").is_file())
            options = json.loads((copy / "build/build-options.json").read_text())
            self.assertEqual(options["cpuAffinity"], "0x1")
            self.assertEqual(options["vuBlocks"], "Whole")
            self.assertRegex(output, r"VU1 mode +Whole blocks")

    def kit_copy(self, temp, bundled=False):
        """A copy of the builder scripts, so the remembered ISO, build options and build-log.txt stay in temp."""
        node = shutil.which("node")
        version = (
            subprocess.run([node, "-p", "process.versions.node"], capture_output=True, text=True).stdout if node else ""
        )
        if not node or tuple(int(part) for part in version.strip().split(".")[:2]) < (22, 18):
            self.skipTest("Build-Drakan.ps1 needs Node.js 22.18+ on PATH")
        copy = Path(temp) / "kit"
        shutil.copytree(BUILDTOOL, copy / "scripts/buildtool", ignore=shutil.ignore_patterns("__pycache__"))
        if bundled:
            # What the zip kit and the installer carry instead of fetching with Git.
            (copy / "scripts/buildtool/ps2_recomp.exe").write_bytes(b"MZ")
            (copy / "deps/raylib").mkdir(parents=True)
        return copy

    def test_zip_kit_with_bundled_sources_needs_no_git(self):
        with tempfile.TemporaryDirectory() as temp:
            copy = self.kit_copy(temp, bundled=True)
            iso = synthetic_iso(temp)
            no_git = [d for d in os.environ["PATH"].split(os.pathsep) if d and not (Path(d) / "git.exe").is_file()]
            env = dict(os.environ, PATH=os.pathsep.join([str(Path(sys.executable).parent), *no_git]))
            result = pwsh(
                f"& {quote(copy / 'scripts/buildtool/Build-Drakan.ps1')} -IsoPath {quote(iso)} -CheckOnly",
                env=env,
                timeout=300,
            )
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode, 0, output)
            self.assertIn("Bundled with the builder", output)
            self.assertNotIn("Git on PATH", output)
            self.assertIn("Ready to build.", output)

    def test_hd_plan_reuses_a_local_pack_by_default_and_accepts_an_explicit_pack(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp).resolve()
            copy = self.kit_copy(temp)
            iso = synthetic_iso(temp)
            script = copy / "scripts/buildtool/Build-Drakan.ps1"
            play = temp / "Play folder"
            local_pack = copy / "local/textures"
            play_pack = play / "textures"
            local_pack.mkdir(parents=True)
            play_pack.mkdir(parents=True)
            (local_pack / "alias.txt").write_text("metadata only", encoding="utf-8")
            (play_pack / "0123456789abcdef.png").write_bytes(b"replacement")
            command = f"& {quote(script)} -IsoPath {quote(iso)} -DistDirectory {quote(play)} -CheckOnly"
            result = pwsh(command)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(result.stdout, r"HD textures +Reuse " + re.escape(str(play_pack)))
            (local_pack / "abcdef0123456789.dds").write_bytes(b"replacement")
            result = pwsh(command)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(result.stdout, r"HD textures +Reuse " + re.escape(str(local_pack)))
            result = pwsh(command + f" -TextureDirectory {quote(play_pack)}")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(result.stdout, r"HD textures +Reuse " + re.escape(str(play_pack)))
            result = pwsh(command + " -HdTextures on")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Generate locally after the build", result.stdout)
            result = pwsh(command)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("Generate locally after the build", result.stdout)
            result = pwsh(command + " -HdTextures auto")
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertRegex(result.stdout, r"HD textures +Reuse " + re.escape(str(local_pack)))
            result = pwsh(command + f" -TextureDirectory {quote(play / 'missing')}")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("HD texture folder not found", result.stderr)
            self.assertIn("Session log", result.stdout)

    def test_console_language_is_remembered_and_auto_falls_back_to_english(self):
        with tempfile.TemporaryDirectory() as temp:
            copy = self.kit_copy(temp)
            script = copy / "scripts/buildtool/Build-Drakan.ps1"
            command = f"& {quote(script)} -IsoPath {quote(synthetic_iso(temp))} -CheckOnly -Plain"
            for language, heading in (
                ("de", "Systemprüfung"),
                ("it", "Verifica del sistema"),
                ("es", "Comprobación del sistema"),
                ("fr", "Vérification du système"),
            ):
                with self.subTest(language=language):
                    result = pwsh(command + f" -Language {language}")
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn(heading, result.stdout)
                    self.assertNotIn("\x1b", result.stdout)
            remembered = pwsh(command)
            self.assertEqual(remembered.returncode, 0, remembered.stderr)
            self.assertIn("Prêt à compiler.", remembered.stdout)
            unsupported = pwsh(
                "[CultureInfo]::CurrentUICulture = [CultureInfo]::GetCultureInfo('ja-JP'); "
                + command
                + " -Language auto"
            )
            self.assertEqual(unsupported.returncode, 0, unsupported.stderr)
            self.assertIn("Ready to build.", unsupported.stdout)

    def test_first_run_asks_where_the_game_goes_and_remembers_it(self):
        with tempfile.TemporaryDirectory() as temp:
            copy = self.kit_copy(temp)
            script = copy / "scripts/buildtool/Build-Drakan.ps1"
            options_file = copy / "build/build-options.json"
            env = dict(os.environ, PATH=str(Path(sys.executable).parent) + os.pathsep + os.environ["PATH"])
            # -CheckOnly never asks, and leaves the question open for the real run.
            result = pwsh(f"& {quote(script)} -IsoPath {quote(synthetic_iso(temp))} -CheckOnly", env=env, timeout=300)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertNotIn("playFolder", json.loads(options_file.read_text()))
            long_temp = Path(temp).resolve()  # the script expands 8.3 short names such as CHRIST~1
            drive = long_temp.anchor
            for answer, expected in ((str(long_temp / "Games/Drakan"), str(long_temp / "Games/Drakan")),
                                     (drive, str(Path(drive) / "DrakanPC"))):  # fmt: skip
                with self.subTest(answer=answer):
                    options_file.unlink()
                    # A missing ISO stops the run right after the options are saved, before anything is built.
                    result = subprocess.run(
                        [PWSH, "-NoProfile", "-File", str(script), "-IsoPath", str(Path(temp) / "missing.iso")],
                        input=answer + "\n",
                        capture_output=True,
                        text=True,
                        timeout=300,
                        env=env,
                    )
                    output = result.stdout + result.stderr
                    self.assertIn("Where should the game go?", output)
                    self.assertRegex(output, r"Play folder +" + re.escape(expected))
                    self.assertIn("ISO not found", output)
                    self.assertEqual(json.loads(options_file.read_text())["playFolder"], expected)
            # Remembered: a later run does not ask again.
            result = subprocess.run(
                [PWSH, "-NoProfile", "-File", str(script), "-IsoPath", str(Path(temp) / "missing.iso")],
                input="",
                capture_output=True,
                text=True,
                timeout=300,
                env=env,
            )
            self.assertNotIn("Where should the game go?", result.stdout + result.stderr)

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
