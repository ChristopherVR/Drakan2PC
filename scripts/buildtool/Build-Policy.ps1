# Dot-sourced by Build-Drakan.ps1: how parallel the player build runs, and when a failed build is tried again.
# Kept apart from the script so scripts/tests can run it without a real build.

# How many MSBuild projects (Jobs) and cl /MP processes per project the player builder starts.
# Measured (docs/journal/2026-11-06/issue-1-kit-build-parallelism.md): a generated-code cl process peaks at ~1.3 GB, a
# VU1 AOT slice at ~17 GB; the slices compile one at a time and nothing else compiles beside them. Cores 0 is automatic:
# half the usable logical cores, at most 4. Usable cores are the affinity mask's, when one is set.
function Get-BuildParallelism([int]$Cores, [int]$LogicalCores, [int]$MemoryGb, [int]$UsableCores = $LogicalCores) {
    $perProcessGb = 2
    $usable = [Math]::Max(1, [Math]::Min($UsableCores, $LogicalCores))
    $memoryCap = [Math]::Max(1, [int][Math]::Floor($MemoryGb / $perProcessGb))
    if ($Cores -gt 0) {
        # Total compiler load is about projects x processes: two projects only when there are cores for both.
        $budget = [Math]::Min($Cores, $usable)
        $jobs = if ($budget -ge 4 -and $MemoryGb -ge 17 + $perProcessGb * [int][Math]::Floor($budget / 2) + 6) { 2 } else { 1 }
        $processes = [Math]::Max(1, [Math]::Min([int][Math]::Floor($budget / $jobs), $memoryCap))
    } else {
        $budget = $usable
        $processes = [Math]::Max(1, [Math]::Min([Math]::Min(4, [int][Math]::Floor($usable / 2)), $memoryCap))
        $jobs = if ($usable -ge 2 -and $MemoryGb -ge 17 + $perProcessGb * $processes + 6) { 2 } else { 1 }
    }
    return [pscustomobject]@{ Usable = $usable; Budget = $budget; Jobs = $jobs; CompilerProcesses = $processes }
}

# Runs Build-LocalGame.ps1 (or a test stub) once; a failure while compiling is tried once more, incrementally, with one
# project and half the compiler processes, because most compile failures are the compiler running out of memory. A
# failure in any other step (a script error, a missing file) would only fail again after the same wait, so it is
# rethrown, as is any failure before the ISO was read ($Prepared, WorkDir/local-input.json, missing).
# Build-LocalGame.ps1 names its current step in $global:DrakanBuildStep.
function Invoke-BuildWithRetry([string]$Builder, [hashtable]$BuildArgs, [string]$Prepared) {
    $global:DrakanBuildStep = $null
    try {
        & $Builder @BuildArgs
    } catch {
        if (-not (Test-Path -LiteralPath $Prepared -PathType Leaf)) { throw }
        if ($global:DrakanBuildStep -and $global:DrakanBuildStep -notlike 'Compiling the game*') { throw }
        Write-Warning "The build stopped: $_"
        Write-Host "`n== Trying again with less parallel compiling" -ForegroundColor Cyan
        $BuildArgs.Incremental = $true
        $BuildArgs.Jobs = 1
        $processes = if ($BuildArgs.ContainsKey('CompilerProcesses')) { [int]$BuildArgs.CompilerProcesses } else { 2 }
        $BuildArgs.CompilerProcesses = [Math]::Max(1, [int][Math]::Floor($processes / 2))
        & $Builder @BuildArgs
    }
}
