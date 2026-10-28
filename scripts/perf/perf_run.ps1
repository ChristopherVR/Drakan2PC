# One timed game run with PS2X_PERF=1; the stderr log lands in <BuildDir>/run_<Tag>.log (summarise with perf_summary.py).
#   pwsh scripts/perf/perf_run.ps1 [-Tag base] [-Seconds 75] [-Gs d3d11] [-Extra "VAR=value;VAR2=value"] [-BuildDir build/recomp_perf]
param([string]$Tag="base",[int]$Seconds=75,[string]$Gs="d3d11",[string]$Extra="",[string]$Exe="drakan.exe",[string]$BuildDir="")
$ErrorActionPreference="Continue"
$RepoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
if (-not $BuildDir) { $BuildDir = "$RepoRoot/build/recomp_perf" }
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path
Set-Location "$BuildDir/bin"
$env:PS2X_PERF="1"; $env:PS2X_GS=$Gs; $env:DRAKAN_AUDIO="0"; $env:DRAKAN_AUTOPRESS="2500"
$env:PS2X_MAX_SECONDS="$Seconds"
if ($Extra) { foreach ($kv in $Extra.Split(";")) { if ($kv) { $p=$kv.Split("="); Set-Item "env:$($p[0])" $p[1] } } }
cmd /c ".\$Exe `"$RepoRoot/iso/SCES_500.06`" > `"$BuildDir/run_$Tag.log`" 2>&1"
