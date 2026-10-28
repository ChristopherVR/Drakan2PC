# Freeze check: N perf_run.ps1 runs with the watchdog on; reports runs whose EE thread went idle (ee_thread_cpu=0%).
#   pwsh scripts/perf/perf_stab.ps1 [-N 6] [-Seconds 40] [-Extra "VAR=value"] [-Tag stab] [-BuildDir build/recomp_perf]
param([int]$N=6,[int]$Seconds=40,[string]$Extra="",[string]$Tag="stab",[string]$BuildDir="")
$RepoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
if (-not $BuildDir) { $BuildDir = "$RepoRoot/build/recomp_perf" }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
for ($i=1; $i -le $N; $i++) {
  & "$PSScriptRoot/perf_run.ps1" -Tag "${Tag}_$i" -Seconds $Seconds -Extra "PS2X_WATCHDOG=5;$Extra" -BuildDir $BuildDir
  $log = Get-Content "$BuildDir/run_${Tag}_$i.log" -ErrorAction SilentlyContinue
  $perf = $log | Select-String "^\[perf " | ForEach-Object { $_.Line }
  $last = $perf | Select-Object -Last 3
  $dead = ($last | Where-Object { $_ -match "ee_thread_cpu=0%" }).Count
  $fl = ($last | Select-Object -First 1) -replace ".*(flips2/s=[0-9.]+ vblank/s=[0-9.]+).*",'$1'
  $bad = ($log | Select-String "bad-resume|missing-target|exception|Error during" | Select-Object -First 2 | ForEach-Object { $_.Line })
  "run $i : frozen=$($dead -ge 2) last: $fl  $bad"
}
