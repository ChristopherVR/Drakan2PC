# Differential tester driver: build the generator (in the recompiler tree rc/), generate the cases, compile the runner, run it.
#   powershell -File scripts/harness/stability/stab_difftest.ps1 [-Variants 24] [-States 64] [-Seed 1] [-Filter x] [-ShowAll]
param([ValidateRange(1,4096)][int]$Variants = 24, [ValidateRange(1,1000000)][int]$States = 64, [int]$Seed = 1, [string]$Filter = "", [switch]$ShowAll, [switch]$NoGen, [int]$Show = 3)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$src = "$root\ps2recomp"
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "Visual Studio C++ tools not found" }
$cm = "$vsPath\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$b = "$root\build\recomp_stab"
$out = "$b\difftest"
New-Item -ItemType Directory -Force $out | Out-Null
if (-not $NoGen) {
  & $cm -S "$root" --preset recompiler -B "$b\rc" *> "$b\difftest_cfg.log"
  if ($LASTEXITCODE) { throw "generator configure failed; see $b\difftest_cfg.log" }
  & $cm --build "$b\rc" --config Release --target ps2_difftest_gen -- /m /v:m *> "$b\difftest_build.log"
  if ($LASTEXITCODE) { Get-Content "$b\difftest_build.log" -Tail 30; throw "gen build failed" }
  & "$b\rc\bin\Release\ps2_difftest_gen.exe" $out --variants $Variants --seed $Seed --elf "$root\iso\SCES_500.06"
  if ($LASTEXITCODE) { throw "gen failed" }
}
$vc = "$vsPath\VC\Auxiliary\Build\vcvars64.bat"
$inc = "/I`"$src\ps2xRuntime\include`" /I`"$src\ps2xIOP\include`" /I`"$src\ps2xRecomp\difftest`" /I`"$src\ps2xRuntime\src\lib\Kernel`""
$cmd = "call `"$vc`" >nul && cd /d `"$out`" && cl /nologo /std:c++20 /EHsc /O2 /MP /Zc:preprocessor- $inc /Fe:difftest_run.exe `"$src\ps2xRecomp\difftest\difftest_run.cpp`" difftest_cases_*.cpp difftest_table.cpp > cl.log 2>&1"
cmd /c $cmd
if ($LASTEXITCODE) { Get-Content "$out\cl.log" -Tail 40; throw "runner build failed" }
$ra = @("--states", $States, "--seed", $Seed, "--show", $Show)
if ($Filter) { $ra += @("--filter", $Filter) }
if ($ShowAll) { $ra += "--verbose" }
& "$out\difftest_run.exe" @ra
exit $LASTEXITCODE
