<#
.SYNOPSIS
Guided build for people who have their own Drakan ISO: checks (and where possible installs) what the build needs,
asks for the ISO and runs Build-LocalGame.ps1 with settings that fit this machine. DrakanPC-Setup.exe and the builder
kit's Build-Drakan.cmd start this script.

.DESCRIPTION
The setup ships PowerShell 7, Python, the prebuilt recompiler and the third-party sources (tools/, deps/), so the only
thing installed system-wide is Microsoft's free C++ Build Tools, downloaded from Microsoft on the first run. The first
build takes one to two hours; later runs reuse build/game. The playable folder is dist/DrakanPC. Nothing is uploaded.
#>
param(
    # Your PAL SCES-500.06 or USA SCUS-971.28 ISO. Asked for (and remembered in build/iso-path.txt) when omitted.
    [string]$IsoPath,
    [string]$WorkDir,
    [string]$DistDirectory,
    # Build without the optional NVIDIA features even when an SDK is present.
    [switch]$NoNvidia,
    # Accept the NVIDIA RTX SDKs licence and download NVIDIA's DLSS SDK (NVIDIA RTX GPUs only). Asked for when omitted
    # on a PC with an NVIDIA GPU.
    [switch]$EnableDlss,
    # Also build the browser version (needs WSL with Ubuntu); play it on the project's browser player page.
    [switch]$Web,
    # CPU cores the compilers may use (0 = automatic: half the logical cores, at most 4). Memory still limits how many
    # run at once. Remembered in build/build-options.json for later rebuilds.
    [ValidateRange(0, 256)][int]$Cores = -1,
    # 'Low' compiles at below-normal priority so the PC stays responsive; 'Normal' is the default. Remembered too.
    [ValidateSet('', 'Normal', 'Low')][string]$Priority = '',
    # Only check the prerequisites and the ISO path, then stop (installs nothing).
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (-not $WorkDir) { $WorkDir = Join-Path $RepoRoot 'build/game' }
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
$problems = [Collections.Generic.List[string]]::new()
$buildToolsUrl = 'https://aka.ms/vs/17/release/vs_BuildTools.exe'
$webPlayerUrl = 'https://christophervr.github.io/Drakan2PC/'
$vsComponents = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', 'Microsoft.VisualStudio.Component.VC.CMake.Project'

function Write-Step([string]$Text) { Write-Host "`n== $Text" -ForegroundColor Cyan }

function Find-VisualStudio {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $found = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires @vsComponents -property installationPath
    if ($found) { return @($found)[0] }
    return $null
}

function Install-BuildTools {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $existing = if (Test-Path -LiteralPath $vswhere) {
        & $vswhere -products 'Microsoft.VisualStudio.Product.BuildTools' -version '[17.0,18.0)' -property installationPath
    }
    $bootstrapper = Join-Path ([IO.Path]::GetTempPath()) 'drakan_vs_BuildTools.exe'
    Write-Host "Downloading the Visual Studio 2022 Build Tools installer from Microsoft ..."
    Invoke-WebRequest -Uri $buildToolsUrl -OutFile $bootstrapper -UseBasicParsing
    $signature = Get-AuthenticodeSignature -LiteralPath $bootstrapper
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') {
        throw "The downloaded Build Tools installer is not signed by Microsoft ($($signature.Status)); not running it."
    }
    $arguments = @()
    if ($existing) { $arguments += @('modify', '--installPath', "`"$(@($existing)[0])`"") }
    $arguments += @('--passive', '--wait', '--norestart', '--nocache', '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended')
    foreach ($component in $vsComponents) { $arguments += @('--add', $component) }
    Write-Host 'Installing the C++ Build Tools (about 3 GB). Windows asks for permission; the installer shows its progress.'
    $process = Start-Process -FilePath $bootstrapper -ArgumentList $arguments -Wait -PassThru
    Remove-Item -LiteralPath $bootstrapper -ErrorAction SilentlyContinue
    switch ($process.ExitCode) {
        0 { }
        3010 { Write-Warning 'The Build Tools ask for a restart. The build continues; restart Windows afterwards if it fails.' }
        1602 { throw 'The Build Tools installation was cancelled.' }
        default { throw "The Build Tools installer failed (exit code $($process.ExitCode))." }
    }
}

Start-Transcript -LiteralPath (Join-Path $RepoRoot 'build-log.txt') -Force | Out-Null
try {
    Write-Step 'Checking what the build needs'
    if ($env:OS -ne 'Windows_NT') { throw 'The game build is Windows-only.' }
    if ($PSVersionTable.PSVersion.Major -lt 7) { $problems.Add('PowerShell 7 or newer (winget install Microsoft.PowerShell)') }

    # Python 3.11+: the bundled copy first, then the first interpreter on PATH that is new enough.
    $python = $null
    $bundledPython = Join-Path $RepoRoot 'tools/python/python.exe'
    if (Test-Path -LiteralPath $bundledPython -PathType Leaf) { $python = $bundledPython }
    else {
        foreach ($candidate in @(@('py', '-3'), @('python'), @('python3'))) {
            if (-not (Get-Command $candidate[0] -ErrorAction SilentlyContinue)) { continue }
            $probe = @($candidate | Select-Object -Skip 1) + @('-c', 'import sys; print(sys.version_info >= (3, 11)); print(sys.executable)')
            $lines = @(& $candidate[0] @probe 2>$null)
            if ($LASTEXITCODE -eq 0 -and $lines.Count -ge 2 -and $lines[0] -eq 'True') { $python = $lines[1]; break }
        }
    }
    if ($python) { Write-Host "Python:  $python" } else { $problems.Add('Python 3.11 or newer on PATH (https://www.python.org/downloads/)') }

    # Git is only needed to fetch sources the kit does not already carry.
    $offline = (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'ps2_recomp.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $RepoRoot 'deps/raylib') -PathType Container)
    if ($offline) { Write-Host 'Sources: bundled (no downloads needed besides the Build Tools)' }
    elseif (Get-Command git -ErrorAction SilentlyContinue) { Write-Host "Git:     $((Get-Command git).Source)" }
    else { $problems.Add('Git on PATH (winget install Git.Git); the build fetches pinned dependencies with it') }

    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($WorkDir))
    $freeGb = [Math]::Round($drive.AvailableFreeSpace / 1GB, 1)
    $vs = Find-VisualStudio
    $neededGb = if ($vs) { 4 } else { 8 }
    Write-Host "Disk:    $freeGb GB free on $($drive.Name)"
    if (-not (Test-Path -LiteralPath $WorkDir) -and $freeGb -lt $neededGb) {
        $problems.Add("At least $neededGb GB free on $($drive.Name) (the build uses about 2 GB$(if (-not $vs) { ', the C++ Build Tools about 3 GB' }))")
    }
    if ($RepoRoot.Length -gt 80) {
        Write-Warning "The install folder path is long ($($RepoRoot.Length) characters). If the build fails with path errors, reinstall somewhere short such as C:\DrakanPC."
    }

    # Compiler memory: up to ~6 GB per process on the large generated files, ~17 GB for each of the four VU1 AOT slices.
    # The slices always compile one after another (-SerialVuSlices); a second MSBuild project only runs beside them when
    # there is room for a slice plus a full set of generated-code compilers.
    # The CPU choice (installer page or -Cores / -Priority) is remembered so "Rebuild Drakan" keeps it.
    $optionsFile = Join-Path $RepoRoot 'build/build-options.json'
    $saved = if (Test-Path -LiteralPath $optionsFile -PathType Leaf) {
        try { Get-Content -LiteralPath $optionsFile -Raw | ConvertFrom-Json } catch { $null }
    }
    if ($Cores -lt 0) { $Cores = if ($saved -and $null -ne $saved.cores) { [int]$saved.cores } else { 0 } }
    if (-not $Priority) { $Priority = if ($saved -and $saved.priority) { [string]$saved.priority } else { 'Normal' } }
    New-Item -ItemType Directory -Force -Path (Split-Path $optionsFile) | Out-Null
    @{ cores = $Cores; priority = $Priority } | ConvertTo-Json | Set-Content -LiteralPath $optionsFile

    $logicalCores = [Environment]::ProcessorCount
    $memoryGb = [Math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
    $memoryCap = [Math]::Max(1, [int][Math]::Floor($memoryGb / 6))
    if ($Cores -gt 0) {
        # Total compiler load is about projects x processes: two projects only when there are cores for both.
        $budget = [Math]::Min($Cores, $logicalCores)
        $jobs = if ($budget -ge 4 -and $memoryGb -ge 17 + 6 * [int][Math]::Floor($budget / 2) + 6) { 2 } else { 1 }
        $compilerProcesses = [Math]::Max(1, [Math]::Min([int][Math]::Floor($budget / $jobs), $memoryCap))
        Write-Host "CPU:     $budget of $logicalCores logical cores (your choice)"
    } else {
        $compilerProcesses = [Math]::Max(1, [Math]::Min([Math]::Min(4, [int]($logicalCores / 2)), $memoryCap))
        $jobs = if ($memoryGb -ge 17 + 6 * $compilerProcesses + 6) { 2 } else { 1 }
        Write-Host "CPU:     automatic ($logicalCores logical cores)"
    }
    Write-Host "Memory:  $memoryGb GB -> $jobs project(s) x $compilerProcesses compiler process(es)"
    if ($Priority -eq 'Low') {
        # Compilers started from here inherit the priority class; MSBuild nodes left over from an earlier build would not.
        (Get-Process -Id $PID).PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal
        $env:MSBUILDDISABLENODEREUSE = '1'
        Write-Host 'Priority: low (the PC stays responsive; the build takes longer while other programs are busy)'
    }
    if ($memoryGb -lt 24) {
        Write-Warning ('Less than 24 GB of memory: a few files need about 17 GB while compiling, so Windows will use its page file ' +
            'and the build will be slow. Keep the page file enabled (the Windows default) and close other programs.')
    }

    if ($problems.Count) {
        Write-Host "`nInstall the following, then run this again:" -ForegroundColor Yellow
        $problems | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
        exit 1
    }
    if ($vs) { Write-Host "C++:     $vs" }
    elseif ($CheckOnly) { Write-Host 'C++:     not installed yet; the build installs Microsoft''s C++ Build Tools on its first run' }
    else {
        Write-Step 'Installing Microsoft C++ Build Tools (first run only)'
        Install-BuildTools
        $vs = Find-VisualStudio
        if (-not $vs) { throw 'The C++ Build Tools were installed but cannot be found; restart Windows and run the build again.' }
        Write-Host "C++:     $vs"
    }
    if ($vs) {
        # Build with this instance (it has CMake), even when another Visual Studio is installed.
        $env:CMAKE_GENERATOR_INSTANCE = $vs
        $env:PATH = "$(Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin');$env:PATH"
    }

    Write-Step 'Choosing the ISO'
    $rememberedIso = Join-Path $RepoRoot 'build/iso-path.txt'
    if (-not $IsoPath -and (Test-Path -LiteralPath $rememberedIso -PathType Leaf)) {
        $previous = (Get-Content -LiteralPath $rememberedIso -Raw).Trim()
        if (Test-Path -LiteralPath $previous -PathType Leaf) {
            $answer = Read-Host "Use $previous ? [Y/n]"
            if ($answer -notmatch '^\s*n') { $IsoPath = $previous }
        }
    }
    if (-not $IsoPath) {
        try {
            Add-Type -AssemblyName System.Windows.Forms
            $dialog = [Windows.Forms.OpenFileDialog]::new()
            $dialog.Title = 'Select your Drakan: The Ancients'' Gates ISO (PAL SCES-500.06 or USA SCUS-971.28)'
            $dialog.Filter = 'Disc images (*.iso)|*.iso|All files (*.*)|*.*'
            if ($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) { $IsoPath = $dialog.FileName }
        } catch {
            Write-Verbose "File dialog unavailable: $_"
        }
    }
    if (-not $IsoPath) { $IsoPath = (Read-Host 'Path to your ISO (you can drag the file into this window)').Trim().Trim('"') }
    if (-not $IsoPath -or -not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) { throw "ISO not found: $IsoPath" }
    $IsoPath = (Resolve-Path -LiteralPath $IsoPath).Path
    Write-Host "ISO:     $IsoPath"
    New-Item -ItemType Directory -Force -Path (Split-Path $rememberedIso) | Out-Null
    Set-Content -LiteralPath $rememberedIso -Value $IsoPath -NoNewline
    if ($CheckOnly) { Write-Host "`nReady to build."; return }

    # NVIDIA DLSS is opt-in: the SDK is downloaded from NVIDIA only after its licence is accepted.
    $dlssSdk = Join-Path $RepoRoot 'tools/dlss/dlss_sdk/include/nvsdk_ngx.h'
    if (-not $NoNvidia -and -not $EnableDlss -and -not (Test-Path -LiteralPath $dlssSdk -PathType Leaf)) {
        $nvidiaGpu = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
            Where-Object { $_.PNPDeviceID -match 'VEN_10DE' }).Count -gt 0
        if ($nvidiaGpu -and [Environment]::UserInteractive) {
            $lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dlss.lock.json') -Raw | ConvertFrom-Json
            Write-Host "`nOptional: NVIDIA DLSS for your NVIDIA GPU. The SDK is downloaded from NVIDIA under the NVIDIA RTX SDKs licence:"
            Write-Host "  https://github.com/$($lock.repository)/blob/$($lock.tag)/LICENSE.txt"
            $answer = Read-Host 'Accept the licence and enable DLSS? [y/N]'
            if ($answer -match '^\s*y') { $EnableDlss = $true }
        }
    }

    $buildArgs = @{
        IsoPath = $IsoPath; WorkDir = $WorkDir; Python = $python
        Jobs = $jobs; CompilerProcesses = $compilerProcesses; SerialVuSlices = $true; Progress = $true
    }
    # The installer's shortcuts open dist\DrakanPC, whichever region the ISO is (Build-LocalGame.ps1 alone would put a
    # USA build into dist\DrakanPC-usa next to a PAL one).
    if (-not $DistDirectory) { $DistDirectory = Join-Path $RepoRoot 'dist/DrakanPC' }
    $buildArgs.DistDirectory = $DistDirectory
    if ($NoNvidia) { $buildArgs.NoNvidia = $true }
    elseif ($EnableDlss) { $buildArgs.AcceptNvidiaDlssLicense = $true }
    $prepared = Join-Path $WorkDir 'local-input.json'
    # PAL and USA discs are both accepted (docs/reference/regions.md). A build folder holds one region: when the ISO is
    # the other one, start that folder over (it only holds generated files; saves and settings live in the play folder).
    if (Test-Path -LiteralPath $prepared -PathType Leaf) {
        $isoRegion = (& $python (Join-Path $PSScriptRoot 'prepare_iso.py') --detect --iso $IsoPath | Out-String).Trim()
        if ($LASTEXITCODE) { throw 'This ISO is not a supported Drakan: The Ancients'' Gates disc (PAL SCES-500.06 or USA SCUS-971.28).' }
        $builtRegion = (Get-Content -LiteralPath $prepared -Raw | ConvertFrom-Json).region
        if (-not $builtRegion) { $builtRegion = 'pal' }
        if ($isoRegion -ne $builtRegion) {
            Write-Step "The ISO is the $($isoRegion.ToUpper()) release; the previous build was $($builtRegion.ToUpper()). Building again from scratch."
            Remove-Item -LiteralPath $WorkDir -Recurse -Force
        }
    }
    # Compiler warnings are for developers and bury the progress. Only a not yet configured folder gets this: flipping it
    # on a configured one would recompile everything, including the 10-minute VU1 slices.
    if (-not (Test-Path -LiteralPath (Join-Path $WorkDir 'runtime-build/CMakeCache.txt') -PathType Leaf)) {
        $buildArgs.NoWarnings = $true
    }
    if (Test-Path -LiteralPath $prepared -PathType Leaf) {
        Write-Step "Rebuilding (only what changed)"
        $buildArgs.Incremental = $true
    } else {
        # A folder without local-input.json stopped before the ISO was read; it holds nothing worth keeping.
        if (Test-Path -LiteralPath $WorkDir) { Remove-Item -LiteralPath $WorkDir -Recurse -Force }
        Write-Step 'Building the game: this takes one to two hours. Some files take 10 minutes each; the window is not stuck.'
    }
    $started = Get-Date
    try {
        & (Join-Path $PSScriptRoot 'Build-LocalGame.ps1') @buildArgs
    } catch {
        # Most failures past the ISO check are the compiler running out of memory; retry once, one file at a time.
        if (-not (Test-Path -LiteralPath $prepared -PathType Leaf)) { throw }
        Write-Warning "The build stopped: $_"
        Write-Step 'Trying again with less parallel compiling'
        $buildArgs.Incremental = $true
        $buildArgs.Jobs = 1
        $buildArgs.CompilerProcesses = [Math]::Max(1, [int][Math]::Floor($compilerProcesses / 2))
        & (Join-Path $PSScriptRoot 'Build-LocalGame.ps1') @buildArgs
    }
    if ($Web) {
        Write-Step 'Building the browser version (WSL)'
        try {
            & (Join-Path $PSScriptRoot 'Build-LocalGame.ps1') -IsoPath $IsoPath -TargetPlatform web -WorkDir $WorkDir -Jobs $jobs
            Write-Host "Browser version: $(Join-Path $RepoRoot 'dist/DrakanPC-web')"
            Write-Host "Play it at $webPlayerUrl (choose that folder and your ISO), or with Play-Browser.cmd in that folder."
        } catch {
            Write-Warning "The browser version was not built: $_"
            Write-Warning 'It needs WSL with Ubuntu (wsl --install -d Ubuntu). The Windows game is ready either way.'
        }
    }
    $elapsed = (Get-Date) - $started
    $play = if ($DistDirectory) { [IO.Path]::GetFullPath($DistDirectory) } else { Join-Path $RepoRoot 'dist/DrakanPC' }
    Write-Step "Done in $([int]$elapsed.TotalMinutes) min"
    Write-Host "Play folder: $play"
    Write-Host 'Start the game with the Drakan shortcut (or Play.cmd in that folder). F1 opens the PC settings.'
    Write-Host 'The game reads your ISO while it runs, so do not move or rename it.'
} finally {
    Stop-Transcript | Out-Null
}
