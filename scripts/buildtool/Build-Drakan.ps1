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
    # Play folder: where drakan.exe and your saves go (default dist\DrakanPC next to this builder). Remembered in
    # build/build-options.json, so "Rebuild Drakan" keeps using it; 'default' goes back to dist\DrakanPC.
    [string]$DistDirectory,
    # Setup's update entry, using the remembered disc and the existing prepared build.
    [switch]$TransactionalUpdate,
    # Build without the optional NVIDIA features even when an SDK is present.
    [switch]$NoNvidia,
    # Accept the NVIDIA RTX SDKs licence and download NVIDIA's DLSS SDK (NVIDIA RTX GPUs only). Asked for when omitted
    # on a PC with an NVIDIA GPU.
    [switch]$EnableDlss,
    # Also build the browser version (needs WSL with Ubuntu); play it on the project's browser player page.
    [switch]$Web,
    # Drakan: Order of the Flame (Drakan 1, PC), optional: its install folder, disc folder (SETUP\DATA1.CAB), disc image
    # (.mdx / .iso) or the Drakan.zip that holds one. It is converted into a campaign mod pack before the build and
    # installed in the play folder (the title menu then offers "Drakan: Order of the Flame"). 'none' removes a remembered
    # choice. Asked for on the first interactive run; remembered in build/build-options.json.
    [string]$OotfSource,
    # Scene captions for Order of the Flame. The setup choice is remembered for later rebuilds.
    [ValidateSet('', 'bundled', 'generate')][string]$OotfSubtitles = '',
    # By default, reuse a local HD texture pack. 'on' also generates textures from the disc after the build (Vulkan).
    # 'auto' and the older 'off' choice both skip generation. The choice is remembered for rebuilds.
    [ValidateSet('', 'auto', 'on', 'off')][string]$HdTextures = '',
    # Use an existing folder of HD textures instead of searching the local and play folders.
    [string]$TextureDirectory,
    # Vulkan device number the upscaler uses (default: the first). -1 clears a remembered choice.
    [ValidateRange(-1, 15)][int]$HdGpu = -2,
    # Starting values of PC features, written into the play folder's ini files after the build (only the ones given;
    # a plain rebuild leaves the settings alone). All of them can be changed in the game's PC settings (F1) afterwards.
    # -MenuStyle modern (the PC main menu) | original (the game's own menus); -BowCombat ootf (Order of the Flame bow:
    # left click fires with auto-aim) | native (the PS2 bow); -DualWield on | off (experimental).
    [ValidateSet('', 'modern', 'original')][string]$MenuStyle = '',
    [ValidateSet('', 'on', 'off')][string]$IssueReporting = '',
    [ValidateSet('', 'ootf', 'native')][string]$BowCombat = '',
    [ValidateSet('', 'on', 'off')][string]$DualWield = '',
    # CPU cores the compilers may use (0 = automatic: half the logical cores, at most 4; never more than -CpuAffinity
    # selects). Memory still limits how many run at once. Remembered in build/build-options.json for later rebuilds.
    [ValidateRange(0, 256)][int]$Cores = -1,
    # Optional Windows logical-processor hex mask; 'default' clears a remembered mask.
    [string]$CpuAffinity,
    # 'Low' compiles at below-normal priority so the PC stays responsive; 'Normal' is the default. Remembered too.
    [ValidateSet('', 'Normal', 'Low')][string]$Priority = '',
    # How the VU1 microcode compiles. 'Chunked' (default): ~2 GB per compiler. 'Whole': the older whole-block code, up to
    # ~17 GB and 10-20 minutes for each of four files, compiled one after another; no measured speed difference in the
    # game. Remembered too.
    [ValidateSet('', 'Chunked', 'Whole')][string]$VuBlocks = '',
    # Start over instead of continuing an interrupted build (the play folder, saves and settings are kept).
    [switch]$Fresh,
    # Start the game when the build has finished.
    [switch]$LaunchWhenDone,
    # Only check the prerequisites and the ISO path, then stop (installs nothing).
    [switch]$CheckOnly,
    [ValidateSet('auto', 'en', 'de', 'fr', 'it', 'es')][string]$Language,
    # Use plain, motion-free console output.
    [switch]$Plain
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'powershell/Build-Affinity.ps1')
. (Join-Path $PSScriptRoot 'powershell/Build-Policy.ps1')
. (Join-Path $PSScriptRoot 'powershell/Build-Progress.ps1')
. (Join-Path $PSScriptRoot 'powershell/Build-Textures.ps1')
. (Join-Path $PSScriptRoot 'Build-Recovery.ps1')
. (Join-Path $PSScriptRoot 'powershell/Build-Job.ps1')
# Everything the build starts (cmake, MSBuild, the compilers, the game for the pictures) ends with this window.
[void](Enter-BuildJob)
$RepoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
if (-not $WorkDir) { $WorkDir = Join-Path $RepoRoot 'build/game' }
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
$problems = [Collections.Generic.List[string]]::new()
$buildToolsPage = 'https://visualstudio.microsoft.com/visual-cpp-build-tools/'
$webPlayerUrl = 'https://christophervr.github.io/Drakan2PC/'
$vsComponents = 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', 'Microsoft.VisualStudio.Component.VC.CMake.Project'
$sessionLog = Join-Path $RepoRoot 'build-log.txt'
$toolLog = "$WorkDir-output.log"
$optionsFile = Join-Path $RepoRoot 'build/build-options.json'
$saved = if (Test-Path -LiteralPath $optionsFile -PathType Leaf) {
    try { Get-Content -LiteralPath $optionsFile -Raw | ConvertFrom-Json } catch { $null }
}
$languagePreference = if ($Language) { $Language } else { $saved.language }
Initialize-BuildConsole $languagePreference -Plain:$Plain

function Show-BuilderRelease {
    $marker = Join-Path $RepoRoot 'DrakanPC-release.txt'
    if (-not (Test-Path -LiteralPath $marker -PathType Leaf)) { return }
    $component = '(?:0|[1-9][0-9]{0,9})'
    $tagPattern = "\Av$component\.$component\.$component\z"
    try { $installed = (Get-Content -LiteralPath $marker -Raw -Encoding ascii).Trim() }
    catch { return }
    if ($installed -cnotmatch $tagPattern) { return }

    $releases = 'https://github.com/ChristopherVR/Drakan2PC/releases'
    $tagPrefix = "$releases/tag/"
    $handler = [Net.Http.HttpClientHandler]::new()
    $handler.AllowAutoRedirect = $false
    $client = [Net.Http.HttpClient]::new($handler)
    $client.Timeout = [TimeSpan]::FromSeconds(3)
    $request = [Net.Http.HttpRequestMessage]::new([Net.Http.HttpMethod]::Head, "$releases/latest")
    $request.Headers.UserAgent.ParseAdd('DrakanPC')
    $response = $null
    $latest = $null
    try {
        $response = $client.SendAsync($request, [Net.Http.HttpCompletionOption]::ResponseHeadersRead).GetAwaiter().GetResult()
        $location = $response.Headers.Location
        if ($response.StatusCode -eq [Net.HttpStatusCode]::Found -and
            $location -and $location.OriginalString.StartsWith($tagPrefix, [StringComparison]::Ordinal)) {
            $candidate = $location.OriginalString.Substring($tagPrefix.Length)
            if ($candidate -cmatch $tagPattern) { $latest = $candidate }
        }
    } catch {
        # The release server is optional when building from a local disc.
    } finally {
        if ($response) { $response.Dispose() }
        $request.Dispose()
        $client.Dispose()
    }

    $state = 'offline'
    if ($latest) {
        [long[]]$localVersion = $installed.Substring(1).Split('.')
        [long[]]$publishedVersion = $latest.Substring(1).Split('.')
        $state = 'current'
        for ($i = 0; $i -lt $localVersion.Length; $i++) {
            if ($localVersion[$i] -lt $publishedVersion[$i]) { $state = 'behind'; break }
            if ($localVersion[$i] -gt $publishedVersion[$i]) { $state = 'ahead'; break }
        }
    }
    Write-BuildValue (Get-BuildText 'builder_release') (Get-BuildText "builder_release_$state" @(
        $installed, $latest, "$tagPrefix$latest"))
}

# Optional extras (scripts/buildtool/extras.py): what they cost on this PC, printed before anything long starts.
function Get-GpuNames {
    @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue | ForEach-Object { $_.Name } | Where-Object { $_ }) -join '|'
}

function Format-ExtrasDuration([double]$Low, [double]$High) {
    $first = Format-Duration ([TimeSpan]::FromMinutes($Low))
    $last = Format-Duration ([TimeSpan]::FromMinutes($High))
    if ($first -eq $last) { return $first }
    return Get-BuildText 'estimate_range' @($first, $last)
}

function Show-ExtrasEstimate([string]$Python, [string]$Extras, [bool]$Ootf, [string]$Source, [bool]$Hd) {
    $estimateArgs = @($Extras, 'estimate', '--json', '--timings', (Join-Path $RepoRoot 'build/extras-timings.json'), '--gpu-names', (Get-GpuNames))
    if ($Ootf) { $estimateArgs += '--ootf' }
    if ($OotfSubtitles -eq 'generate') { $estimateArgs += '--generate-subtitles' }
    if ($Source) { $estimateArgs += @('--source', $Source) }
    if ($Hd) { $estimateArgs += '--hd' }
    $output = & $Python @estimateArgs
    if ($LASTEXITCODE -ne 0) { return }
    $estimate = $output | ConvertFrom-Json
    Write-BuildHeading (Get-BuildText 'estimate_title')
    if ($Hd) {
        $gpuName = $estimate.gpu.name
        if (-not $gpuName) { $gpuName = Get-BuildText 'estimate_gpu_missing' }
        Write-BuildValue (Get-BuildText 'estimate_gpu') $gpuName
    }
    foreach ($item in $estimate.steps) {
        $key = 'estimate_' + $item.key.Replace('-', '_')
        $duration = Format-ExtrasDuration $item.low $item.high
        if ($item.measured) { $duration = Get-BuildText 'estimate_measured' @($duration) }
        Write-BuildValue (Get-BuildText $key) $duration
    }
    Write-BuildValue (Get-BuildText 'estimate_total') (Format-ExtrasDuration $estimate.low $estimate.high)
    if ($estimate.diskGb) { Write-BuildValue (Get-BuildText 'estimate_disk') "$($estimate.diskGb) GB" }
    if ($Hd -and $estimate.gpu.tier -eq 'none') { Write-Warning (Get-BuildText 'estimate_gpu_incompatible') }
    elseif ($Hd) { Write-Host ('  ' + (Get-BuildText 'estimate_gpu_approximate')) }
}

function Find-VisualStudio {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { return $null }
    $found = & $vswhere -latest -products '*' -version '[17.0,18.0)' -requires @vsComponents -property installationPath
    if ($found) { return @($found)[0] }
    return $null
}

function Install-BuildTools {
    # Installed through winget, or the Visual Studio Installer already on the PC for a modify: the builder never
    # downloads and starts an installer itself (that pattern makes antivirus heuristics flag the kit as a downloader).
    $vsArguments = @('--passive', '--wait', '--norestart', '--add', 'Microsoft.VisualStudio.Workload.VCTools', '--includeRecommended')
    foreach ($component in $vsComponents) { $vsArguments += @('--add', $component) }
    $installerDir = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer"
    $existing = if (Test-Path -LiteralPath "$installerDir/vswhere.exe") {
        & "$installerDir/vswhere.exe" -products 'Microsoft.VisualStudio.Product.BuildTools' -version '[17.0,18.0)' -property installationPath
    }
    Write-Host (Get-BuildText 'cpp_prompt')
    if ($existing -and (Test-Path -LiteralPath "$installerDir/setup.exe")) {
        $arguments = @('modify', '--installPath', "`"$(@($existing)[0])`"") + $vsArguments
        $code = (Start-Process -FilePath "$installerDir/setup.exe" -ArgumentList $arguments -Wait -PassThru).ExitCode
    } elseif (Get-Command winget -ErrorAction SilentlyContinue) {
        & winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --source winget `
            --accept-package-agreements --accept-source-agreements --override ($vsArguments -join ' ')
        $code = $LASTEXITCODE
    } else {
        throw (Get-BuildText 'cpp_winget' @($buildToolsPage))
    }
    switch ($code) {
        0 { }
        3010 { Write-Warning (Get-BuildText 'cpp_restart') }
        # winget: APPINSTALLER_CLI_ERROR_INSTALL_REBOOT_REQUIRED_TO_FINISH / _FOR_INSTALL
        { $_ -in -1978334967, -1978334966 } { Write-Warning (Get-BuildText 'cpp_restart') }
        1602 { throw (Get-BuildText 'cpp_cancel') }
        default { throw (Get-BuildText 'cpp_fail' @($code)) }
    }
}
Start-Transcript -LiteralPath $sessionLog -Force | Out-Null
$previousRendering = if ($PSStyle) { $PSStyle.OutputRendering }
try {
    if ($PSStyle -and -not (Test-BuildConsoleColor)) { $PSStyle.OutputRendering = 'PlainText' }
    Write-BuildHeading 'Drakan PC | The Ancients'' Gates'
    if ($CheckOnly) { Write-Host ('  ' + (Get-BuildText 'check_mode')) }
    else { Write-Host ('  ' + (Get-BuildText 'build_mode')) }
    Write-BuildHeading (Get-BuildText 'system_check')
    if ($env:OS -ne 'Windows_NT') { throw (Get-BuildText 'windows_only') }
    if ($TransactionalUpdate -and (-not $DistDirectory -or $DistDirectory -eq 'default' -or $Fresh -or $CheckOnly)) {
        throw 'Automatic rebuild needs the registered play folder and the existing prepared build.'
    }

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
    if (-not $python) { $problems.Add((Get-BuildText 'python_required')) }
    if (-not $TransactionalUpdate) { Show-BuilderRelease }

    # Node.js 22.18+ or 23.6+ (TypeScript type stripping on by default) makes the multiplayer character motions in the
    # last step: the bundled copy first, then one on PATH. character_motion.py finds it on PATH. No TypeScript flag is
    # passed (Node 26 removed --experimental-transform-types), so the editor code keeps to erasable syntax.
    $bundledNode = Join-Path $RepoRoot 'tools/node'
    if (Test-Path -LiteralPath (Join-Path $bundledNode 'node.exe') -PathType Leaf) { $env:PATH = "$bundledNode;$env:PATH" }
    $node = Get-Command node -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $nodeVersion = if ($node) { (& $node.Source -p 'process.versions.node' 2>$null | Out-String).Trim() }
    $nodeOk = $nodeVersion -and [version]$nodeVersion -ge [version]'22.18' -and
        -not ([version]$nodeVersion -ge [version]'23.0' -and [version]$nodeVersion -lt [version]'23.6')
    if (-not $nodeOk) { $problems.Add((Get-BuildText 'node_required')) }

    # Git is only needed to fetch sources the kit does not already carry.
    $offline = (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'ps2_recomp.exe') -PathType Leaf) -and
        (Test-Path -LiteralPath (Join-Path $RepoRoot 'deps/raylib') -PathType Container)
    if (-not $offline -and -not (Get-Command git -ErrorAction SilentlyContinue)) {
        $problems.Add((Get-BuildText 'git_required'))
    }

    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($WorkDir))
    $freeGb = [Math]::Round($drive.AvailableFreeSpace / 1GB, 1)
    $vs = Find-VisualStudio
    $neededGb = if ($vs) { 4 } else { 8 }
    if (-not (Test-Path -LiteralPath $WorkDir) -and $freeGb -lt $neededGb) {
        $problems.Add((Get-BuildText 'disk_required' @($neededGb, $drive.Name)))
    }
    if ($RepoRoot.Length -gt 80) {
        Write-Warning (Get-BuildText 'path_long' @($RepoRoot.Length))
    }
    if ($problems.Count) {
        Write-BuildHeading (Get-BuildText 'requirements') Yellow
        $problems | ForEach-Object { Write-Host "  - $_" }
        Write-BuildValue (Get-BuildText 'session_log') $sessionLog
        exit 1
    }
    $pythonVersion = (& $python --version | Out-String).Trim()
    Write-BuildValue (Get-BuildText 'tools') "$pythonVersion, Node.js $nodeVersion"
    Write-BuildValue (Get-BuildText 'sources') $(if ($offline) { (Get-BuildText 'bundled') } else { (Get-BuildText 'fetched') })
    $cppStatus = if ($vs) { (Get-BuildText 'ready') } else { (Get-BuildText 'cpp_first') }
    Write-BuildValue 'C++ Build Tools' $cppStatus
    Write-BuildValue (Get-BuildText 'build_disk') (Get-BuildText 'disk_free' @($freeGb, $drive.Name))
    Write-Verbose "Python: $python | Node.js: $($node.Source) | Visual Studio: $vs"

    # Compiler memory: up to about 2 GB per process (1.3 GB on the largest generated files, 1.9 GB on a VU1 AOT slice);
    # a second MSBuild project only runs when there is room for both sets. Sizing: Get-BuildParallelism (Build-Policy.ps1).
    # The CPU choice (installer page or -Cores / -Priority) is remembered so "Rebuild Drakan" keeps it.
    if ($Cores -lt 0) { $Cores = if ($saved -and $null -ne $saved.cores) { [int]$saved.cores } else { 0 } }
    if (-not $Priority) { $Priority = if ($saved -and $saved.priority) { [string]$saved.priority } else { 'Normal' } }
    if (-not $VuBlocks) { $VuBlocks = if ($saved -and $saved.vuBlocks) { [string]$saved.vuBlocks } else { 'Chunked' } }
    $wholeVu = $VuBlocks -eq 'Whole'
    if (-not $PSBoundParameters.ContainsKey('CpuAffinity')) {
        $CpuAffinity = if ($saved -and $saved.cpuAffinity) { [string]$saved.cpuAffinity } else { '' }
    }
    if ($CpuAffinity -eq 'default') { $CpuAffinity = '' }
    if (-not $DistDirectory -and $saved -and $saved.playFolder) { $DistDirectory = [string]$saved.playFolder }
    if ($DistDirectory -eq 'default') { $DistDirectory = '' }
    # The play folder (drakan.exe, saves, settings) is asked for once on the first interactive run; the setup passes
    # -DistDirectory from its own folder page. Only an answered question is remembered, so -CheckOnly does not skip it.
    $playKnown = $PSBoundParameters.ContainsKey('DistDirectory') -or [bool]($saved -and $saved.PSObject.Properties['playFolder'])
    Write-BuildHeading (Get-BuildText 'game_files')
    if (-not $playKnown -and [Environment]::UserInteractive -and -not $CheckOnly) {
        $defaultPlay = Join-Path $RepoRoot 'dist\DrakanPC'
        Write-Host ('  ' + (Get-BuildText 'choose_play'))
        Write-BuildValue (Get-BuildText 'default') $defaultPlay
        $answer = (Read-Host (Get-BuildText 'play_prompt')).Trim().Trim('"')
        if ($answer) {
            # A bare drive (D:\) gets its own folder rather than filling the drive root.
            $DistDirectory = if ([IO.Path]::GetFullPath($answer) -eq [IO.Path]::GetPathRoot([IO.Path]::GetFullPath($answer))) {
                Join-Path $answer 'DrakanPC'
            } else { $answer }
        }
        $playKnown = $true
    }
    # Optional extras. A remembered key (even an empty one) means the question was answered before.
    $extrasKnown = [bool]($saved -and $saved.PSObject.Properties['ootfSource'])
    if (-not $PSBoundParameters.ContainsKey('OotfSource') -and $extrasKnown) { $OotfSource = [string]$saved.ootfSource }
    if ($OotfSource -eq 'none') { $OotfSource = '' }
    if (-not $OotfSubtitles) { $OotfSubtitles = if ($saved -and $saved.ootfSubtitles -eq 'generate') { 'generate' } else { 'bundled' } }
    if (-not $HdTextures) { $HdTextures = if ($saved -and $saved.hdTextures) { 'on' } else { 'auto' } }
    if ($HdGpu -eq -2) { $HdGpu = if ($saved -and $null -ne $saved.hdGpu) { [int]$saved.hdGpu } else { -1 } }
    if ($DistDirectory) {
        $DistDirectory = [IO.Path]::GetFullPath($DistDirectory)
        $inside = { param($Path, $Parent) $Path -eq $Parent -or $Path.StartsWith($Parent.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) }
        if ($DistDirectory -eq [IO.Path]::GetPathRoot($DistDirectory) -or $DistDirectory -eq $RepoRoot -or
            (& $inside $DistDirectory $WorkDir) -or (& $inside $DistDirectory (Join-Path $RepoRoot 'build'))) {
            throw (Get-BuildText 'play_invalid' @($DistDirectory))
        }
    }
    $playShown = if ($DistDirectory) { $DistDirectory } else { Join-Path $RepoRoot 'dist\DrakanPC' }
    $playShown = [IO.Path]::TrimEndingDirectorySeparator($playShown)
    $playDrive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($playShown))
    if (-not $playDrive.IsReady) {
        throw (Get-BuildText 'play_unavailable' @($playShown))
    }
    if (-not $CheckOnly) {
        $update = Get-BuildPlayUpdate $playShown
        if (Get-Item -LiteralPath $update.Journal -Force -ErrorAction SilentlyContinue) {
            $playLock = Enter-BuildPlayLock $playShown
            try { Restore-BuildPlayUpdate $update }
            finally {
                try { $playLock.ReleaseMutex() } finally { $playLock.Dispose() }
            }
        }
    }
    Write-BuildValue (Get-BuildText 'play_folder') $playShown
    if ($playDrive.Name -ne $drive.Name) {
        Write-BuildValue (Get-BuildText 'play_disk') (Get-BuildText 'disk_free' @([Math]::Round($playDrive.AvailableFreeSpace / 1GB, 1), $playDrive.Name))
    }
    if ($CpuAffinity) { Resolve-BuildAffinity $CpuAffinity | Out-Null }
    $textureSource = Find-BuildTextureDirectory $RepoRoot $playShown $TextureDirectory
    New-Item -ItemType Directory -Force -Path (Split-Path $optionsFile) | Out-Null
    $saveOptions = {
        $options = @{ cores = $Cores; priority = $Priority; cpuAffinity = $CpuAffinity; vuBlocks = $VuBlocks; hdGpu = $HdGpu }
        if ($playKnown) { $options.playFolder = $DistDirectory }
        # Only answered questions are remembered, so a run that never got to ask still asks the next time.
        if ($extrasKnown) { $options.ootfSource = $OotfSource; $options.ootfSubtitles = $OotfSubtitles }
        $options.hdTextures = ($HdTextures -eq 'on')
        if ($languagePreference) { $options.language = $languagePreference }
        $options | ConvertTo-Json | Set-Content -LiteralPath $optionsFile
    }
    if ($extrasKnown -or $PSBoundParameters.ContainsKey('OotfSource')) { $extrasKnown = $true }
    if (-not $TransactionalUpdate) { & $saveOptions }

    $logicalCores = [Environment]::ProcessorCount
    # The compilers only run on the mask's processors, so size the build for those, not for the whole CPU.
    $usableCores = Get-BuildAffinityCount $CpuAffinity $logicalCores
    $memoryGb = [Math]::Floor((Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory / 1GB)
    $sizing = Get-BuildParallelism -Cores $Cores -LogicalCores $logicalCores -MemoryGb $memoryGb -UsableCores $usableCores -WholeVuBlocks:$wholeVu
    $jobs = $sizing.Jobs
    $compilerProcesses = $sizing.CompilerProcesses
    if ($Priority -eq 'Low') {
        # Compilers started from here inherit the priority class; MSBuild nodes left over from an earlier build would not.
        (Get-Process -Id $PID).PriorityClass = [Diagnostics.ProcessPriorityClass]::BelowNormal
        $env:MSBUILDDISABLENODEREUSE = '1'
    }
    if ($wholeVu -and $memoryGb -lt 24) {
        Write-Warning (Get-BuildText 'ram_whole')
    } elseif ($memoryGb -lt 8) {
        Write-Warning (Get-BuildText 'ram_low')
    }

    $rememberedIso = Join-Path $RepoRoot 'build/iso-path.txt'
    if (-not $IsoPath -and (Test-Path -LiteralPath $rememberedIso -PathType Leaf)) {
        $previous = (Get-Content -LiteralPath $rememberedIso -Raw).Trim()
        if (Test-Path -LiteralPath $previous -PathType Leaf) {
            Write-BuildValue (Get-BuildText 'previous_disc') $previous
            if ($TransactionalUpdate -or (Read-Host (Get-BuildText 'use_disc')) -notmatch '^\s*n') {
                $IsoPath = $previous
            }
        }
    }
    if ($TransactionalUpdate -and -not $IsoPath) {
        throw 'The saved disc image could not be found. Open Rebuild Drakan to choose the ISO again.'
    }
    if (-not $IsoPath) {
        try {
            Add-Type -AssemblyName System.Windows.Forms
            $dialog = [Windows.Forms.OpenFileDialog]::new()
            $dialog.Title = Get-BuildText 'select_iso'
            $dialog.Filter = (Get-BuildText 'iso_filter')
            if ($dialog.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) { $IsoPath = $dialog.FileName }
        } catch {
            Write-Verbose "File dialog unavailable: $_"
        }
    }
    if (-not $IsoPath) { $IsoPath = (Read-Host (Get-BuildText 'iso_prompt')).Trim().Trim('"') }
    if (-not $IsoPath -or -not (Test-Path -LiteralPath $IsoPath -PathType Leaf)) { throw (Get-BuildText 'iso_missing' @($IsoPath)) }
    $IsoPath = (Resolve-Path -LiteralPath $IsoPath).Path
    Write-BuildValue (Get-BuildText 'disc') $IsoPath
    New-Item -ItemType Directory -Force -Path (Split-Path $rememberedIso) | Out-Null
    if (-not $TransactionalUpdate) { Set-Content -LiteralPath $rememberedIso -Value $IsoPath -NoNewline }

    # NVIDIA DLSS is opt-in: the SDK is downloaded from NVIDIA only after its licence is accepted.
    $dlssSdk = Join-Path $RepoRoot 'tools/dlss/dlss_sdk/include/nvsdk_ngx.h'
    if (-not ($CheckOnly -or $TransactionalUpdate -or $NoNvidia -or $EnableDlss) -and
        -not (Test-Path -LiteralPath $dlssSdk -PathType Leaf)) {
        $nvidiaGpu = @(Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
            Where-Object { $_.PNPDeviceID -match 'VEN_10DE' }).Count -gt 0
        if ($nvidiaGpu -and [Environment]::UserInteractive) {
            $lock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dlss.lock.json') -Raw | ConvertFrom-Json
            Write-BuildHeading (Get-BuildText 'dlss_optional')
            Write-Host ('  ' + (Get-BuildText 'dlss_terms'))
            Write-BuildValue (Get-BuildText 'licence') "https://github.com/$($lock.repository)/blob/$($lock.tag)/LICENSE.txt"
            $answer = Read-Host (Get-BuildText 'dlss_prompt')
            if ($answer.Trim().ToLowerInvariant() -in (Get-BuildText 'yes').Split(',')) { $EnableDlss = $true }
        }
    }

    # Ask for the optional campaign once, then remember the answer.
    $extras = Join-Path $PSScriptRoot 'extras.py'
    $interactive = [Environment]::UserInteractive -and -not ($CheckOnly -or $TransactionalUpdate)
    $extraNotes = [Collections.Generic.List[string]]::new()
    if (-not $extrasKnown -and -not $OotfSource -and $interactive) {
        Write-BuildHeading (Get-BuildText 'ootf_optional')
        Write-Host ('  ' + (Get-BuildText 'ootf_intro'))
        Write-Host ('  ' + (Get-BuildText 'ootf_formats'))
        $answer = (Read-Host (Get-BuildText 'ootf_prompt')).Trim().Trim('"')
        if ($answer) {
            & $python $extras check-source --deep $answer
            if ($LASTEXITCODE) { Write-Warning (Get-BuildText 'ootf_bad') } else { $OotfSource = $answer }
        }
        $extrasKnown = $true
    }
    if (-not $TransactionalUpdate) { & $saveOptions }
    if ($OotfSource) {
        # Checked before anything long starts. A source that does not work (moved disc, wrong file, damaged zip) only leaves
        # Order of the Flame out of this run; it never stops the game build, and the remembered choice is kept.
        Write-BuildHeading (Get-BuildText 'ootf_check')
        & $python $extras check-source --deep $OotfSource
        if ($LASTEXITCODE) {
            Write-Warning (Get-BuildText 'ootf_bad')
            $extraNotes.Add((Get-BuildText 'ootf_failed'))
            $OotfSource = ''
        }
    }
    $buildArgs = @{
        IsoPath = $IsoPath; WorkDir = $WorkDir; Python = $python
        CpuAffinity = $CpuAffinity; Jobs = $jobs; CompilerProcesses = $compilerProcesses; Progress = $true
        Language = $script:BuildLanguage; Plain = $Plain.IsPresent
        VuChunk = $(if ($wholeVu) { 0 } else { 8 })
    }
    # The installer's shortcuts open dist\DrakanPC, whichever region the ISO is (Build-LocalGame.ps1 alone would put a
    # USA build into dist\DrakanPC-usa next to a PAL one).
    $DistDirectory = $playShown
    $buildArgs.DistDirectory = $DistDirectory
    if ($textureSource) { $buildArgs.TextureDirectory = $textureSource }
    if ($TransactionalUpdate) { $buildArgs.TransactionalUpdate = $true }
    if ($NoNvidia) { $buildArgs.NoNvidia = $true }
    elseif ($EnableDlss) { $buildArgs.AcceptNvidiaDlssLicense = $true }
    $prepared = Join-Path $WorkDir 'local-input.json'
    # PAL and USA discs are both accepted (docs/reference/regions.md). A build folder holds one region: when the ISO is
    # the other one, start that folder over (it only holds generated files; saves and settings live in the play folder).
    if (Test-Path -LiteralPath $prepared -PathType Leaf) {
        $isoRegion = (& $python (Join-Path $PSScriptRoot 'prepare_iso.py') --detect --iso $IsoPath | Out-String).Trim()
        if ($LASTEXITCODE) { throw (Get-BuildText 'iso_invalid') }
        $builtRegion = (Get-Content -LiteralPath $prepared -Raw | ConvertFrom-Json).region
        if (-not $builtRegion) { $builtRegion = 'pal' }
        if ($isoRegion -ne $builtRegion) {
            Write-BuildHeading (Get-BuildText 'region_changed') Yellow
            Write-Host ('  ' + (Get-BuildText 'region_rebuild' @($builtRegion.ToUpper(), $isoRegion.ToUpper())))
            $Fresh = $true
        }
    }
    # Where did the last build get to? Each step's output is checked on disk, so a build that was closed, crashed or ran
    # out of memory continues where it stopped: finished steps are only re-checked and compiled files are kept.
    if ((Test-Path -LiteralPath $prepared -PathType Leaf) -and -not $Fresh) {
        $state = Read-BuildState $WorkDir
        $review = Get-BuildReview $WorkDir (Join-Path $RepoRoot 'ps2recomp') (Join-Path $WorkDir 'bin/drakan.exe')
        $resumeAt = @($review | Where-Object { -not $_.Ok } | Select-Object -First 1)
        Write-BuildHeading (Get-BuildText 'previous_build')
        if ($state) {
            $when = try { ([datetime]$state.updated).ToString('yyyy-MM-dd HH:mm') } catch { $state.updated }
            $stageTitle = if ($state.step -in (1..$DrakanBuildSteps.Count)) { Get-BuildText "step_$($state.step)" } else { $state.title }
            switch ($state.status) {
                'done' { Write-BuildValue (Get-BuildText 'finished') $when }
                'failed' {
                    Write-BuildValue (Get-BuildText 'last_failure') "$stageTitle ($when)"
                    Write-Host "  $($state.error)"
                }
                default { Write-BuildValue (Get-BuildText 'interrupted') "$stageTitle ($when)" }
            }
        }
        if ($resumeAt -and $resumeAt[0].Step -eq 1) {
            Write-BuildValue (Get-BuildText 'start_over') (Get-BuildText 'incomplete_iso')
            $Fresh = $true
        } elseif ($resumeAt -or ($state -and $state.status -ne 'done')) {
            $from = if ($resumeAt) { $resumeAt[0] } else { [pscustomobject]@{ Step = 6; Title = $DrakanBuildSteps[5] } }
            Write-BuildValue (Get-BuildText 'resume_at') "[$($from.Step)/$($DrakanBuildSteps.Count)] $(Get-BuildText "step_$($from.Step)")"
            if ($from.Detail) { Write-BuildValue (Get-BuildText 'on_disk') $from.Detail }
            if ($interactive) {
                $answer = Read-Host (Get-BuildText 'resume_prompt')
                if ($answer -match '^\s*s') { $Fresh = $true }
            }
        }
    }
    $usingCache = (Test-Path -LiteralPath $prepared -PathType Leaf) -and -not $Fresh
    if ($TransactionalUpdate -and -not $usingCache) {
        throw 'The previous build needs repair. Open Rebuild Drakan to review it before trying the update again.'
    }
    Write-BuildHeading (Get-BuildText 'plan')
    Write-BuildValue (Get-BuildText 'windows_game') $(if ($usingCache) { (Get-BuildText 'reuse_build') } else { (Get-BuildText 'full_build') })
    $affinityText = if ($CpuAffinity) { Get-BuildText 'cpu_mask' @($CpuAffinity) } else { '' }
    $workers = $jobs * $compilerProcesses
    $workerLabel = if ($workers -eq 1) { (Get-BuildText 'worker') } else { (Get-BuildText 'workers') }
    $priorityText = Get-BuildText "priority_$($Priority.ToLowerInvariant())"
    Write-BuildValue (Get-BuildText 'compiler') (Get-BuildText 'compiler_plan' @($workers, $workerLabel, $memoryGb, $priorityText, $affinityText))
    if ($wholeVu) { Write-BuildValue (Get-BuildText 'vu_mode') (Get-BuildText 'vu_whole') }
    if ($OotfSource) { Write-BuildValue 'Order of the Flame' $OotfSource }
    if ($Web) { Write-BuildValue (Get-BuildText 'browser_build') (Get-BuildText 'browser_after') }
    $texturePlan = if ($HdTextures -eq 'on') { (Get-BuildText 'hd_generate') }
        elseif ($textureSource) { Get-BuildText 'hd_reuse' @($textureSource) }
        else { (Get-BuildText 'hd_original_plan') }
    Write-BuildValue (Get-BuildText 'hd_label') $texturePlan
    Write-BuildValue (Get-BuildText 'session_log') $sessionLog
    Write-BuildValue (Get-BuildText 'tool_output') $toolLog
    if ($CheckOnly) {
        Write-BuildHeading (Get-BuildText 'ready_build') Green
        Write-Host ('  ' + (Get-BuildText 'iso_found'))
        return
    }
    $generateHd = $HdTextures -eq 'on' -and -not $TransactionalUpdate
    if ($OotfSource -or $generateHd) {
        Show-ExtrasEstimate $python $extras ([bool]$OotfSource) $OotfSource $generateHd
    }
    if (-not $vs) {
        if ($TransactionalUpdate) { throw (Get-BuildText 'cpp_not_found') }
        Write-BuildHeading (Get-BuildText 'cpp_install')
        Install-BuildTools
        $vs = Find-VisualStudio
        if (-not $vs) { throw (Get-BuildText 'cpp_not_found') }
    }
    # Use the CMake that belongs to the selected Visual Studio instance.
    $env:CMAKE_GENERATOR_INSTANCE = $vs
    $env:PATH = "$(Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin');$env:PATH"
    # Builds up to v0.1.11 saved into the build folder (next to the ELF) instead of the play folder. Before the build
    # folder is deleted, those saves are copied into the play folder (migrate_saves.py, issue #6); when that fails the
    # folder is not deleted.
    $rescueSaves = {
        & $python -I (Join-Path $PSScriptRoot 'migrate_saves.py') --play $DistDirectory --work $WorkDir --before-delete
        if ($LASTEXITCODE) {
            throw (Get-BuildText 'rescue_failed' @($WorkDir, $DistDirectory))
        }
    }
    if ($Fresh -and (Test-Path -LiteralPath $WorkDir)) {
        Write-BuildHeading (Get-BuildText 'fresh_start')
        & $rescueSaves
        Remove-Item -LiteralPath $WorkDir -Recurse -Force
    }
    # Compiler warnings are for developers and bury the progress. Only a not yet configured folder gets this: flipping it
    # on a configured one would recompile everything, including the 10-minute VU1 slices.
    if (-not (Test-Path -LiteralPath (Join-Path $WorkDir 'runtime-build/CMakeCache.txt') -PathType Leaf)) {
        $buildArgs.NoWarnings = $true
    }
    if ($usingCache) {
        $buildArgs.Incremental = $true
    } else {
        # A folder without local-input.json stopped before the ISO was read; it holds nothing worth keeping.
        if (Test-Path -LiteralPath $WorkDir) { & $rescueSaves; Remove-Item -LiteralPath $WorkDir -Recurse -Force }
    }
    $started = Get-Date
    $ootfPack = $null
    if ($OotfSource) {
        # Before the game build: the converter needs only the ISO's level template, and the build then installs the pack
        # (-ModDirectory) and pictures its maps. A failure here does not stop the game build.
        Write-BuildHeading (Get-BuildText 'ootf_prepare')
        $ootfArgs = @($extras, 'ootf', '--iso', $IsoPath, '--source', $OotfSource, '--play', $DistDirectory, '--work', (Join-Path $RepoRoot 'build/ootf'))
        if ($OotfSubtitles -eq 'generate') { $ootfArgs += '--generate-subtitles' }
        if ($HdTextures -eq 'on') { $ootfArgs += '--hd' }
        & $python @ootfArgs
        if ($LASTEXITCODE -eq 0) {
            $ootfMods = Join-Path $RepoRoot 'build/ootf/mods'
            $ootfPack = Join-Path $ootfMods 'ootf-levels.zip'
            $buildArgs.ModDirectory = $ootfMods
        } else {
            Write-Warning (Get-BuildText 'ootf_failed')
            $extraNotes.Add((Get-BuildText 'ootf_failed'))
        }
    }
    Write-BuildHeading (Get-BuildText 'windows_start')
    Invoke-BuildWithRetry (Join-Path $PSScriptRoot 'Build-LocalGame.ps1') $buildArgs $prepared (Get-BuildText 'retry')
    if ($Web) {
        Write-BuildHeading (Get-BuildText 'browser_start')
        try {
            & (Join-Path $PSScriptRoot 'Build-LocalGame.ps1') -IsoPath $IsoPath -TargetPlatform web -WorkDir $WorkDir -Jobs $jobs -TextureDirectory $textureSource
            Write-BuildValue (Get-BuildText 'browser_folder') (Join-Path $RepoRoot 'dist/DrakanPC-web')
            Write-Host ('  ' + (Get-BuildText 'browser_play' @($webPlayerUrl)))
        } catch {
            Write-Warning (Get-BuildText 'browser_failed')
            Write-Host "  $_"
        }
    }
    $play = $DistDirectory
    if ($generateHd) {
        Write-BuildHeading (Get-BuildText 'hd_start')
        Write-Host ('  ' + (Get-BuildText 'hd_resume'))
        $hdArgs = @($extras, 'hd', '--iso', $IsoPath, '--play', $play, '--work', (Join-Path $RepoRoot 'build/hdpack'))
        if ($ootfPack) { $hdArgs += @('--ootf-pack', $ootfPack) }
        if ($HdGpu -ge 0) { $hdArgs += @('--gpu-index', $HdGpu) }
        & $python @hdArgs
        if ($LASTEXITCODE -ne 0) {
            Write-Warning (Get-BuildText 'hd_failed')
            $extraNotes.Add((Get-BuildText 'hd_failed'))
        }
    }
    if ($MenuStyle -or $BowCombat -or $DualWield -or $IssueReporting) {
        Write-BuildHeading (Get-BuildText 'applying_settings')
        $defaultArgs = @((Join-Path $PSScriptRoot 'apply_defaults.py'), '--play', $play)
        if ($MenuStyle) { $defaultArgs += @('--menus', $MenuStyle) }
        if ($IssueReporting) { $defaultArgs += @('--issue-reporting', $IssueReporting) }
        if ($BowCombat) { $defaultArgs += @('--bow', $BowCombat) }
        if ($DualWield) { $defaultArgs += @('--dual-wield', $DualWield) }
        & $python @defaultArgs
        if ($LASTEXITCODE -ne 0) { $extraNotes.Add((Get-BuildText 'settings_failed')) }
    }
    $elapsed = (Get-Date) - $started
    $installedTextures = Test-BuildTextureDirectory (Join-Path $play 'textures')
    Write-BuildHeading (Get-BuildText 'play_ready') Green
    Write-BuildValue (Get-BuildText 'elapsed') (Format-Duration $elapsed)
    Write-BuildValue (Get-BuildText 'play_folder') $play
    Write-BuildValue (Get-BuildText 'start_game') (Get-BuildText 'launch_tip')
    Write-BuildValue (Get-BuildText 'disc') (Get-BuildText 'keep_disc')
    Write-BuildValue (Get-BuildText 'hd_label') $(if ($installedTextures) { (Get-BuildText 'hd_ready') } else { (Get-BuildText 'hd_original') })
    if ($ootfPack) { Write-BuildValue 'Order of the Flame' (Get-BuildText 'ootf_ready') }
    if ($extraNotes.Count) {
        Write-BuildHeading (Get-BuildText 'extra_notes') Yellow
        foreach ($note in $extraNotes) { Write-Host "  - $note" }
    }
    if ($LaunchWhenDone -and (Test-Path -LiteralPath (Join-Path $play 'drakan.exe') -PathType Leaf)) {
        Disable-BuildJobKill # the game must outlive this window
        Start-Process -FilePath (Join-Path $play 'drakan.exe') -WorkingDirectory $play
    }
} catch {
    Write-BuildHeading (Get-BuildText 'stopped') Red
    Write-Error -Message $_.Exception.Message -ErrorAction Continue
    Write-BuildValue (Get-BuildText 'session_log') $sessionLog
    exit 1
} finally {
    if ($PSStyle) { $PSStyle.OutputRendering = $previousRendering }
    Stop-Transcript | Out-Null
}
