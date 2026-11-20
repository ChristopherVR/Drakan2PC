# Release gate: scan the files about to be published with Windows Defender (fresh signatures) and fail on any threat.
# Used by .github/workflows/release.yml before the release is created. Scan only: nothing is quarantined.
[CmdletBinding()]
param([Parameter(Mandatory)][string[]]$Targets)
$ErrorActionPreference = 'Stop'
$scanner = Join-Path $env:ProgramFiles 'Windows Defender/MpCmdRun.exe'
if (-not (Test-Path -LiteralPath $scanner)) { throw 'Windows Defender (MpCmdRun.exe) was not found.' }
# The update can fail transiently on hosted runners (another update already running), so retry before falling back to
# the signatures on the runner image, and say which was used.
$updated = $false
foreach ($attempt in 1..5) {
    & $scanner -SignatureUpdate
    if ($LASTEXITCODE -eq 0) { $updated = $true; break }
    Write-Host "Signature update failed (attempt $attempt of 5, exit $LASTEXITCODE)."
    if ($attempt -lt 5) { Start-Sleep -Seconds (15 * $attempt) }
}
$note = if ($updated) { 'signatures updated in this run' } else { 'runner image signatures; the update failed' }
foreach ($target in $Targets) {
    $resolved = (Resolve-Path -LiteralPath $target).Path
    $output = (& $scanner -Scan -ScanType 3 -File $resolved -DisableRemediation 2>&1) -join "`n"
    $code = $LASTEXITCODE
    Write-Host $output
    if ($code -ne 0 -or ($output -match '(?im)^\s*Threats:\s*(\d+)\s*$' -and [int]$Matches[1] -ne 0)) {
        throw "Windows Defender did not confirm a clean scan of '$target' (exit $code)."
    }
    Write-Host "Windows Defender: no threats in $target ($note)."
}
