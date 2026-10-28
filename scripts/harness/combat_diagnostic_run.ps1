param(
    [Parameter(Mandatory=$true)][string]$BinDir,
    [Parameter(Mandatory=$true)][string]$Elf,
    [string]$Script = "$PSScriptRoot/recipes/combat_sequence.txt",
    [string]$Name = ('combat-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [int]$TimeoutSeconds = 140,
    [switch]$QuietDiagnostics,
    [string]$InputConfig = '',
    [switch]$EnableAudio,
    [switch]$AutoStart,
    [switch]$AudioDump,
    [ValidateRange(0,5)][int]$SoundLog = 0,
    [ValidateRange(0,600)][int]$AuditEvery = 15
)
$ErrorActionPreference = 'Stop'
$BinDir = (Resolve-Path -LiteralPath $BinDir).Path
$Elf = (Resolve-Path -LiteralPath $Elf).Path
$Script = (Resolve-Path -LiteralPath $Script).Path
if (-not $InputConfig) { $InputConfig = "$BinDir/drakan_input.ini" }
$InputConfig = (Resolve-Path -LiteralPath $InputConfig).Path
if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw 'Name must use letters, digits, hyphens or underscores' }
$out = Join-Path (Split-Path -Parent $BinDir) "t/$Name"
if (Test-Path -LiteralPath $out) { throw "Output already exists: $out" }
New-Item -ItemType Directory -Path $out | Out-Null
Copy-Item -LiteralPath $Script -Destination "$out/sequence.txt"
Copy-Item -LiteralPath $InputConfig -Destination "$out/drakan_input.ini"
$values = @{
    PS2X_MAX_SECONDS="$TimeoutSeconds"; PS2X_GS='d3d11'; DRAKAN_AUDIO=([int]$EnableAudio.IsPresent).ToString();
    DRAKAN_AUDIO_DUMP=$(if ($AudioDump) { "$out/audio.wav" } else { '' });
    DRAKAN_SND_LOG="$SoundLog";
    DRAKAN_INPUT_AUTOSTART=([int]$AutoStart.IsPresent).ToString(); DRAKAN_AUTOPRESS='4000'; DRAKAN_AUTOPRESS_NOSTART='1';
    DRAKAN_AUTOPRESS_UNTIL=$(if($AutoStart){'90000'}else{'30000'}); DRAKAN_INPUT_SCRIPT="$out/sequence.txt";
    DRAKAN_INPUT_INI="$out/drakan_input.ini";
    DRAKAN_INPUT_LOGFILE="$out/input.log"; DRAKAN_SHOT_DIR=$out;
    DRAKAN_CMD_LOG='1'; DRAKAN_COMBAT_LOG='0'; DRAKAN_COMBAT_AUDIT="$AuditEvery";
    DRAKAN_CONSOLE='0'; DRAKAN_PAUSE_UNFOCUSED='0'
}
if ($QuietDiagnostics) {
    $values.DRAKAN_CMD_LOG = '0'
    $values.DRAKAN_COMBAT_AUDIT = '0'
}
$effective = @{}
foreach ($key in @('PS2X_GS_SCALE','PS2X_GS_PACING','PS2X_GS_STATS',
    'DRAKAN_WINDOW_WIDTH','DRAKAN_WINDOW_HEIGHT','DRAKAN_DLSS','DRAKAN_DLSS_NEURAL',
    'DRAKAN_NR_TEST_SEQ','DRAKAN_DLSS_LOG','DRAKAN_WS_LOG','DRAKAN_WS_ASPECT','DRAKAN_FULLSCREEN','DRAKAN_SND_DBG',
    'DRAKAN_INVENTORY_TEXT_CAPTURE','DRAKAN_INVENTORY_TEXT_LOG','DRAKAN_INVENTORY_TEXT_LIMIT','GSR_TEXUP_LOG',
    'DRAKAN_NATIVE_FONT_ATLAS','DRAKAN_NATIVE_FONT_TTF',
    'DRAKAN_INVENTORY_VIEWPORT_LOG','GSR_INVENTORY_DRAWLOG','GSR_DRAWLOGV','GSR_SPRITE_FOOTPRINT',
    'DRAKAN_FRAME_AUDIT','DRAKAN_FRAME_LIMIT','DRAKAN_VSYNC','DRAKAN_WORLD_RATE',
    'DRAKAN_AUDIO_DEVICE','DRAKAN_VOLUME','DRAKAN_HLE_LOG','DRAKAN_TRACE_FN','DRAKAN_TRACE_N','DRAKAN_PARRY_AUDIT',
    'DRAKAN_DLSS_NEURAL_DIR','DRAKAN_DLSS_VALIDATE',
    'GSR_PROFILE','GSR_WINSHOT','GSR_WINSHOT_AT','GSR_UPSCALE','GSR_HUDSCALE')) {
    $value = [Environment]::GetEnvironmentVariable($key,'Process')
    if ($null -ne $value) { $effective[$key] = $value }
}
foreach ($key in $values.Keys) { $effective[$key] = $values[$key] }
$previous = @{}
$process = $null
$executableHash = (Get-FileHash -LiteralPath "$BinDir/drakan.exe").Hash
@{ exe = "$BinDir/drakan.exe"; sha256 = $executableHash;
   script = $Script; script_sha256 = (Get-FileHash -LiteralPath $Script).Hash;
   input_ini_sha256 = (Get-FileHash -LiteralPath "$out/drakan_input.ini").Hash;
   environment = $effective } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath "$out/manifest.json"
$elapsed = [Diagnostics.Stopwatch]::StartNew()
try {
    foreach ($key in $values.Keys) {
        $previous[$key] = [Environment]::GetEnvironmentVariable($key,'Process')
        [Environment]::SetEnvironmentVariable($key,$values[$key],'Process')
    }
    $process = Start-Process -FilePath "$BinDir/drakan.exe" -ArgumentList ('"' + $Elf + '"') -WorkingDirectory $BinDir -WindowStyle Hidden -PassThru -RedirectStandardOutput "$out/stdout.log" -RedirectStandardError "$out/stderr.log"
    Write-Output "Combat diagnostic pid=$($process.Id) output=$out"
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds + 10)
    while (-not $process.HasExited -and (Get-Date) -lt $deadline) { Start-Sleep -Seconds 2; $process.Refresh() }
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit(); throw 'Combat diagnostic timed out' }
    if (-not (Select-String -LiteralPath "$out/input.log" -Pattern 'script quit' -Quiet)) {
        throw "Script did not finish; inspect $out/input.log"
    }
    if ($process.ExitCode -ne 0) { throw "Game exited with $($process.ExitCode)" }
    $afterHash = (Get-FileHash -LiteralPath "$BinDir/drakan.exe").Hash
    if ($afterHash -ne $executableHash) { throw 'Executable changed during diagnostic run' }
    @{ scriptQuit = $true; exitCode = $process.ExitCode; elapsedSeconds = $elapsed.Elapsed.TotalSeconds;
       sha256After = $afterHash } |
        ConvertTo-Json | Set-Content -LiteralPath "$out/completion.json"
    Write-Output "Completed: $out/input.log"
} finally {
    if ($process -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    foreach ($key in $previous.Keys) { [Environment]::SetEnvironmentVariable($key,$previous[$key],'Process') }
}
