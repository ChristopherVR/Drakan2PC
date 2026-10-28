# Build-time experiments for the generated code (runtime library untouched). Results: <BuildDir>/bench_build.log
#   pwsh scripts/perf/perf_bench_build.ps1 [-BuildDir build/recomp_perf]
# Needs the private dev layout described in scripts/harness/build_game.ps1 (rc/, rt/, gen/, config.toml).
param([string]$BuildDir = '')
$RepoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
if (-not $BuildDir) { $BuildDir = "$RepoRoot/build/recomp_perf" }
$root = (Resolve-Path -LiteralPath $BuildDir).Path
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cm = $cmakeCommand.Source }
else {
  $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
  $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
  if (-not $vs) { throw 'Visual Studio C++ tools not found' }
  $cm = "$vs/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
}
Set-Location $root
$log = "$root\bench_build.log"
function Bench($name, $opt, $fpf) {
  # wipe stale output (the recompiler does not delete files of an older batch size)
  Remove-Item "$root\gen\*" -Force -ErrorAction SilentlyContinue
  # regenerate if the batch size changed
  $cfg = Get-Content config.toml -Raw
  $cfg = [regex]::Replace($cfg, "functions_per_file = \d+", "functions_per_file = $fpf")
  Set-Content config.toml $cfg -NoNewline
  & .\rc\bin\Release\ps2_recomp.exe config.toml *> recomp.log
  & $cm -S "$RepoRoot" -B rt "-DPS2X_GENERATED_OPT=$opt" *> $null
  $sw = [Diagnostics.Stopwatch]::StartNew()
  & $cm --build rt --config Release --target ps2_generated --clean-first -- /m /v:m *> "$root\bench_$name.log"
  $tGen = $sw.Elapsed.TotalSeconds
  $sw.Restart()
  & $cm --build rt --config Release --target ps2EntryRunner -- /m /v:m *>> "$root\bench_$name.log"
  $tLink = $sw.Elapsed.TotalSeconds
  $exe = Get-Item "$root\bin\drakan.exe"
  $tus = (Get-ChildItem "$root\gen\ps2_recomp_*.cpp").Count
  $mb = [math]::Round(((Get-ChildItem "$root\gen\*.cpp","$root\gen\*.h" | Measure-Object Length -Sum).Sum)/1MB,1)
  "{0,-14} opt={1,-12} fpf={2,-4} TUs={3,-3} src={4}MB  generated-lib={5:N0}s  link={6:N0}s  exe={7}MB" -f $name,$opt,$fpf,$tus,$mb,$tGen,$tLink,[math]::Round($exe.Length/1MB,1) | Tee-Object -FilePath $log -Append
}
Bench "O2_300"    "/O2"        300
Bench "Od_150"    "/Od"        150
Bench "O2_150"    "/O2"        150
