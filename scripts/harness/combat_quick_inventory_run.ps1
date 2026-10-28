param(
    [Parameter(Mandatory=$true)][string]$DecompRoot,
    [string]$BinDir = '',
    [ValidateSet('direct','pad','equipped')][string]$Mode = 'direct',
    [string]$Name = ('quick-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [int]$TimeoutSeconds = 90
)
$ErrorActionPreference = 'Stop'
$DecompRoot = (Resolve-Path -LiteralPath $DecompRoot).Path
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Invalid evidence name' }
$build = "$DecompRoot/build/recomp_combat"
if (-not $BinDir) { $BinDir = "$build/bin" }
$BinDir = (Resolve-Path -LiteralPath $BinDir).Path
$cards = "$build/quick-cards/$Name"
$out = Join-Path (Split-Path -Parent $BinDir) "t/$Name"
if ((Test-Path -LiteralPath $cards) -or (Test-Path -LiteralPath $out)) { throw 'Evidence already exists' }
New-Item -ItemType Directory -Path $cards | Out-Null
Copy-Item -LiteralPath "$DecompRoot/iso/SCES_500.06" -Destination "$cards/SCES_500.06"
foreach ($card in 'mc0','mc1') {
    Copy-Item -LiteralPath "$DecompRoot/iso/$card" -Destination "$cards/$card" -Recurse
}
function SaveHashes {
    @(Get-ChildItem "$DecompRoot/iso/mc0","$DecompRoot/iso/mc1" -File -Recurse |
        Sort-Object FullName | Get-FileHash | Select-Object Path,Hash) | ConvertTo-Json
}
$before = SaveHashes
$script = switch ($Mode) {
    'direct' { "$PSScriptRoot/recipes/combat_quick_inventory.txt" }
    'pad' { "$PSScriptRoot/recipes/combat_quick_inventory_pad.txt" }
    'equipped' { "$PSScriptRoot/recipes/combat_quick_inventory_equipped.txt" }
}
$envValues = @{
    DRAKAN_ISO = (Join-Path (Split-Path -Parent $DecompRoot) "Drakan - The Ancients' Gates.iso")
    DRAKAN_COMBAT_CALL_TRACE = '1'; PS2X_WATCHDOG = '5'; PS2X_TRACE_RING = '1'
}
$oldValues = @{}
if ($Mode -eq 'equipped') {
    $envValues['DRAKAN_SND_DBG']='1'
    $envValues['DRAKAN_SND_LOG']='3'
    $envValues['DRAKAN_AUDIO_DUMP']="$out/dagger.wav"
}
try {
    foreach ($key in $envValues.Keys) {
        $oldValues[$key] = [Environment]::GetEnvironmentVariable($key,'Process')
        [Environment]::SetEnvironmentVariable($key,$envValues[$key],'Process')
    }
    & "$PSScriptRoot/combat_diagnostic_run.ps1" -BinDir $BinDir -Elf "$cards/SCES_500.06" -Script $script -Name $Name -TimeoutSeconds $TimeoutSeconds
} finally {
    if (Test-Path -LiteralPath $out) {
        Copy-Item -LiteralPath "$BinDir/drakan.log" -Destination "$out/runtime.log" -ErrorAction SilentlyContinue
        $before | Set-Content -LiteralPath "$out/user-saves-before.json"
        $after = SaveHashes
        $after | Set-Content -LiteralPath "$out/user-saves-after.json"
        @{ mode=$Mode; isolated_elf="$cards/SCES_500.06"; extra_environment=$envValues;
           user_saves_unchanged=($before -ceq $after) } | ConvertTo-Json -Depth 3 |
            Set-Content -LiteralPath "$out/quick-manifest.json"
        Write-Output "User saves unchanged: $($before -ceq $after)"
    }
    foreach ($key in $oldValues.Keys) { [Environment]::SetEnvironmentVariable($key,$oldValues[$key],'Process') }
}
