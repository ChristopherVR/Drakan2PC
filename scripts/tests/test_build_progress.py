"""Build-Progress.ps1: the compile counter, the compile plan read from CMake's vcxproj files, the tracker that tells
running from finished files, and the review of an interrupted build folder. Synthetic folders only, no compiler."""

import json
import os
import tempfile
import time
import unittest
from pathlib import Path

from test_build_drakan import PWSH, pwsh, quote

ROOT = Path(__file__).resolve().parents[2]
PROGRESS = ROOT / "scripts/buildtool/Build-Progress.ps1"

RUNNER = """<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <IntDir Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">ps2EntryRunner.dir\\Debug\\</IntDir>
    <IntDir Condition="'$(Configuration)|$(Platform)'=='Release|x64'">ps2EntryRunner.dir\\Release\\</IntDir>
  </PropertyGroup>
  <ItemGroup>
    <ClCompile Include="{src}\\main.cpp" />
  </ItemGroup>
  <ItemGroup>
    <ProjectReference Include="{build}\\lib\\ps2_runtime.vcxproj" />
  </ItemGroup>
</Project>
"""

LIBRARY = """<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup>
    <IntDir Condition="'$(Configuration)|$(Platform)'=='Release|x64'">ps2_runtime.dir\\Release\\</IntDir>
  </PropertyGroup>
  <ItemGroup>
    <ClCompile Include="{src}\\ps2_vu1_aot_0.cpp" />
    <ClCompile Include="{src}\\Stubs\\RPC.cpp">
      <ObjectFileName>$(IntDir)/Stubs/RPC.cpp.obj</ObjectFileName>
    </ClCompile>
    <ClCompile Include="{src}\\glfw.c" />
  </ItemGroup>
</Project>
"""


def run(body, timeout=120):
    result = pwsh(f". {quote(PROGRESS)}; {body}", timeout=timeout)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return result.stdout


def make_build(root):
    """A runtime-build folder with two projects and four sources; returns (runtime_build, objects by source name)."""
    src = root / "src"
    build = root / "work/runtime-build"
    (src / "Stubs").mkdir(parents=True)
    (build / "runner").mkdir(parents=True)
    (build / "lib").mkdir(parents=True)
    for name in ("main.cpp", "ps2_vu1_aot_0.cpp", "Stubs/RPC.cpp", "glfw.c"):
        (src / name).write_text("// synthetic\n", encoding="utf-8")
    (build / "runner/ps2EntryRunner.vcxproj").write_text(RUNNER.format(src=src, build=build), encoding="utf-8")
    (build / "lib/ps2_runtime.vcxproj").write_text(LIBRARY.format(src=src), encoding="utf-8")
    objects = {
        "main.cpp": build / "runner/ps2EntryRunner.dir/Release/main.obj",
        "ps2_vu1_aot_0.cpp": build / "lib/ps2_runtime.dir/Release/ps2_vu1_aot_0.obj",
        "RPC.cpp": build / "lib/ps2_runtime.dir/Release/Stubs/RPC.cpp.obj",
        "glfw.c": build / "lib/ps2_runtime.dir/Release/glfw.obj",
    }
    return build, objects


def write_object(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"obj")
    later = time.time() + 5
    os.utime(path, (later, later))


@unittest.skipUnless(PWSH, "PowerShell 7 (pwsh) not on PATH")
class CounterTests(unittest.TestCase):
    def test_counter_has_no_padding(self):
        out = run("Format-CompileCounter 1 299; Format-CompileCounter 7 0; Format-CompileCounter 12 3")
        self.assertEqual(out.split(), ["[1/299]", "[7]", "[12]"])

    def test_durations(self):
        out = run(
            "Format-Duration ([TimeSpan]::FromSeconds(45)); Format-Duration ([TimeSpan]::FromMinutes(11.5)); "
            "Format-Duration ([TimeSpan]::FromMinutes(65))"
        )
        self.assertEqual(out.strip().splitlines(), ["45 s", "11 min", "1 h 05 min"])

    def test_estimates_prefer_this_pcs_measurements(self):
        out = run(
            "(Get-CompileEstimate 'ps2_vu1_aot_2.cpp' @{}).TotalMinutes; "
            "(Get-CompileEstimate 'ps2_vu1_aot_2.cpp' @{ 'ps2_vu1_aot_2.cpp' = 1500 }).TotalMinutes; "
            "$null -eq (Get-CompileEstimate 'main.cpp' @{})"
        )
        self.assertEqual(out.split(), ["6", "25", "True"])


@unittest.skipUnless(PWSH, "PowerShell 7 (pwsh) not on PATH")
class PlanTests(unittest.TestCase):
    def test_plan_follows_references_and_object_names(self):
        with tempfile.TemporaryDirectory() as temp:
            build, objects = make_build(Path(temp))
            out = run(f'$p = Get-CompilePlan {quote(build)}; $p | ForEach-Object {{ "$($_.Name)|$($_.Object)" }}')
            plan = dict(line.split("|", 1) for line in out.strip().splitlines())
            self.assertEqual(set(plan), set(objects))
            for name, path in objects.items():
                self.assertEqual(Path(plan[name]).resolve(), path.resolve(), name)

    def test_pending_counts_missing_and_stale_objects(self):
        with tempfile.TemporaryDirectory() as temp:
            build, objects = make_build(Path(temp))
            write_object(objects["main.cpp"])
            write_object(objects["RPC.cpp"])
            stale = objects["glfw.c"]
            write_object(stale)
            os.utime(stale, (time.time() - 3600, time.time() - 3600))
            # cl's placeholder: created empty when it starts on a file, filled in when it finishes.
            write_object(objects["ps2_vu1_aot_0.cpp"])
            objects["ps2_vu1_aot_0.cpp"].write_bytes(b"")
            out = run(f"(Get-PendingCompiles (Get-CompilePlan {quote(build)})).Name")
            self.assertEqual(sorted(out.split()), ["glfw.c", "ps2_vu1_aot_0.cpp"])


@unittest.skipUnless(PWSH, "PowerShell 7 (pwsh) not on PATH")
class TrackerTests(unittest.TestCase):
    def test_finished_when_the_object_is_written(self):
        with tempfile.TemporaryDirectory() as temp:
            build, objects = make_build(Path(temp))
            out = run(
                f"""
                $t = New-CompileTracker (Get-CompilePlan {quote(build)}) 4 @{{}}
                $start = (Get-Date).AddSeconds(-30)
                Add-CompileStart $t 'ps2_vu1_aot_0.cpp' $start
                Add-CompileStart $t 'main.cpp' $start
                "running $($t.Running.Count) done $((Update-CompileTracker $t $start).Count)"
                $null = New-Item -ItemType Directory -Force -Path {quote(objects["main.cpp"].parent)}
                $null = New-Item -ItemType Directory -Force -Path {quote(objects["ps2_vu1_aot_0.cpp"].parent)}
                Set-Content -LiteralPath {quote(objects["main.cpp"])} -Value obj
                # The empty placeholder cl writes while it is still compiling does not count.
                [IO.File]::WriteAllBytes({quote(objects["ps2_vu1_aot_0.cpp"])}, [byte[]]@())
                $finished = Update-CompileTracker $t $start.AddMinutes(2)
                "finished $($finished.Name) running $($t.Running.Count) oldest $((Get-OldestCompile $t).Name)"
                Format-RunningCompile (Get-OldestCompile $t) $start.AddMinutes(4)
                """
            )
            lines = out.strip().splitlines()
            self.assertEqual(lines[0], "running 2 done 0")
            self.assertEqual(lines[1], "finished main.cpp running 1 oldest ps2_vu1_aot_0.cpp")
            self.assertEqual(lines[2], "ps2_vu1_aot_0.cpp, 4 min so far (usually about 6 min)")

    def test_unknown_files_finish_once_the_slots_are_full(self):
        out = run(
            """
            $t = New-CompileTracker @() 2 @{}
            $now = Get-Date
            foreach ($name in 'a.c', 'b.c', 'c.c', 'd.c') { Add-CompileStart $t $name $now }
            $done = Update-CompileTracker $t $now
            "$($done.Name -join ',') | $($t.Running.Name -join ',') | $($t.Finished)"
            """
        )
        self.assertEqual(out.strip(), "a.c,b.c | c.c,d.c | 2")


@unittest.skipUnless(PWSH, "PowerShell 7 (pwsh) not on PATH")
class ReviewTests(unittest.TestCase):
    def review(self, work, runtime, exe):
        out = run(
            f"$r = Get-BuildReview {quote(work)} {quote(runtime)} {quote(exe)}; $r | "
            'ForEach-Object { "$($_.Step)|$($_.Ok)|$($_.Detail)" }'
        )
        return [line.split("|") for line in out.strip().splitlines()]

    def test_interrupted_compile(self):
        with tempfile.TemporaryDirectory() as temp:
            temp = Path(temp)
            build, objects = make_build(temp)
            work = build.parent
            (work / "local-input.json").write_text(json.dumps({"region": "pal", "boot": "SCES_500.06"}))
            (work / "SCES_500.06").write_bytes(b"elf")
            (work / "config.toml").write_text("")
            (work / "generated").mkdir()
            (work / "generated/ps2_recomp_0000.cpp").write_text("")
            (work / "recompile.stamp").write_text("x")
            (build / "CMakeCache.txt").write_text("")
            runtime = temp / "ps2recomp"
            (runtime / "ps2xRuntime/src/lib/vu/aot").mkdir(parents=True)
            (runtime / "ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc").write_text("")
            write_object(objects["main.cpp"])
            rows = self.review(work, runtime, work / "bin/drakan.exe")
            self.assertEqual([row[1] for row in rows], ["True", "True", "True", "True", "False"])
            self.assertEqual(rows[1][2], "1 generated C++ files")
            self.assertEqual(rows[4][2], "1 of 4 files compiled")

    def test_half_translated_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp) / "work"
            work.mkdir()
            (work / "local-input.json").write_text(json.dumps({"region": "pal", "boot": "SCES_500.06"}))
            (work / "SCES_500.06").write_bytes(b"elf")
            (work / "config.toml").write_text("")
            (work / "generated").mkdir()
            (work / "generated/ps2_recomp_0000.cpp").write_text("")
            rows = self.review(work, Path(temp) / "none", work / "bin/drakan.exe")
            self.assertEqual(rows[0][1], "True")
            self.assertEqual(rows[1][1:], ["False", "1 generated files, but the translation did not finish"])
            self.assertEqual([row[1] for row in rows[2:]], ["False", "False", "False"])

    def test_state_file_round_trip(self):
        with tempfile.TemporaryDirectory() as temp:
            out = run(
                f"""
                Write-BuildState {quote(temp)} @{{ step = 5; steps = 6; title = 'Compiling the game'; status = 'running' }}
                Write-BuildState {quote(temp)} @{{ status = 'failed'; error = 'cl.exe failed (2)' }}
                $s = Read-BuildState {quote(temp)}
                "$($s.step)|$($s.title)|$($s.status)|$($s.error)"
                """
            )
            self.assertEqual(out.strip(), "5|Compiling the game|failed|cl.exe failed (2)")

    def test_state_is_not_written_without_a_build_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            missing = Path(temp) / "missing"
            run(f"Write-BuildState {quote(missing)} @{{ status = 'running' }}")
            self.assertFalse(missing.exists())


if __name__ == "__main__":
    unittest.main()
