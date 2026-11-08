param(
    [Parameter(Mandatory=$true)][string]$IsoPath,
    [string]$RepoRoot = (Join-Path $PSScriptRoot '../..'),
    [ValidateSet('native','windows','linux','web')][string]$TargetPlatform = 'native',
    # Applied when the build folder is configured; pass it again to switch an existing WorkDir.
    [string]$RuntimeSource,
    [string]$ConfigTemplate,
    [string]$WorkDir,
    [string]$Compiler,
    [string]$ToolWorkDir,
    # Playable folder, cleaned and refilled from the fresh build on every run (default <repo>/dist/DrakanPC).
    [string]$DistDirectory,
    # Explicit private mod pack source to install; existing mods survive ordinary rebuilds.
    [string]$ModDirectory,
    [switch]$NoDist,
    [string]$Python = 'python',
    # NVIDIA options. A DLSS SDK at <repo>/tools/dlss/dlss_sdk is picked up automatically; pass -DlssSdkDirectory for
    # another location or -NoNvidia to build without DLSS.
    [string]$DlssSdkDirectory,
    # Accept the NVIDIA RTX SDKs licence and download the public DLSS SDK from NVIDIA (fetch_dlss_sdk.py) when
    # <repo>/tools/dlss/dlss_sdk is missing. Never needed for a build without DLSS.
    [switch]$AcceptNvidiaDlssLicense,
    [switch]$NoNvidia,
    # Pre-fetched third-party sources for an offline build: raylib/ and the FFmpeg prebuilt zip. The builder kit ships
    # them in <repo>/deps, which is used automatically when present.
    [string]$DependencyDirectory,
    # Optional shared HD pack. Otherwise reuse local/textures or the existing Windows play folder.
    [string]$TextureDirectory,
    # Windows logical-processor bitmask (hex, e.g. 0xFFF); empty keeps the inherited affinity.
    [string]$CpuAffinity = '',
    # MSBuild projects built in parallel.
    [ValidateRange(1,16)][int]$Jobs = 2,
    # Compile the four VU1 AOT slices one after another instead of in parallel. Only needed with whole-block slices
    # (PS2X_VU1_AOT_CHUNK=0), which take up to ~17 GB each in cl; the default chunked slices take ~2 GB.
    [switch]$SerialVuSlices,
    # VU1 AOT blocks longer than this many pairs compile as out-of-line chunks of it (CMake PS2X_VU1_AOT_CHUNK). 8: ~2 GB
    # per slice in cl. 0 compiles whole blocks: up to ~17 GB and 10-20 min per slice, so it also implies -SerialVuSlices.
    # Measured: no difference in VU1 speed (docs/journal/2026-11-06/vu1-compile-cost.md).
    [ValidateRange(0, 64)][int]$VuChunk = 8,
    # Compile without warnings (CMake DRAKAN_COMPILER_WARNINGS=OFF) so the console shows progress and errors only.
    # Applied when the build folder is configured; later -Incremental runs keep the setting.
    [switch]$NoWarnings,
    # Player console: numbered steps, a progress bar with the elapsed time and a counter of compiled files instead of
    # the tools' own output, which goes to <WorkDir>-output.log (e.g. build/game-output.log).
    [switch]$Progress,
    # Compiler processes per project (cl /MP). The generated game code is 83 files of up to 3 MB in one project, so this is
    # what decides a full build's length: 1 compiles them one after another. Default: half the logical cores (of -CpuAffinity's, when set), at most 4 (the largest generated files use about 1.3 GB per compiler process; the VU1 AOT slices ~1.9 GB).
    # Total compiler load is about Jobs x CompilerProcesses; use 1 to keep the machine free for other work.
    [ValidateRange(1,32)][int]$CompilerProcesses = [Math]::Max(1, [Math]::Min(4, [int]([Environment]::ProcessorCount / 2))),
    [switch]$PrepareOnly,
    [switch]$GenerateOnly,
    [switch]$SourceOnly,
    # Reuse an existing WorkDir: skip ISO extraction, rerun the recompiler only when its inputs changed,
    # then let CMake/MSBuild rebuild just the changed sources and relink. -Regenerate forces the recompile.
    [switch]$Incremental,
    [switch]$Regenerate
)
$ErrorActionPreference = 'Stop'
$windowsHost = [OperatingSystem]::IsWindows()
. (Join-Path $PSScriptRoot 'Build-Affinity.ps1')
. (Join-Path $PSScriptRoot 'Build-Progress.ps1')
if ($CpuAffinity -and (-not $windowsHost -or $TargetPlatform -eq 'web')) {
    throw '-CpuAffinity supports native Windows builds only; Linux/WSL/browser builds keep their platform defaults.'
}
# Validate before extraction or any other build work.
if ($CpuAffinity) { Resolve-BuildAffinity $CpuAffinity | Out-Null }
# The default above counts every logical core; with a mask only the selected ones run the compilers.
if ($CpuAffinity -and -not $PSBoundParameters.ContainsKey('CompilerProcesses')) {
    $CompilerProcesses = [Math]::Max(1, [Math]::Min(4, [int][Math]::Floor((Get-BuildAffinityCount $CpuAffinity) / 2)))
}
if ($TargetPlatform -eq 'native') { $TargetPlatform = if ($windowsHost) { 'windows' } else { 'linux' } }
$webBuild = $TargetPlatform -eq 'web'
if ($webBuild -and $windowsHost) {
    foreach ($option in $PSBoundParameters.Keys) {
        if ($option -notin @('IsoPath','RepoRoot','TargetPlatform','WorkDir','Jobs','Incremental','TextureDirectory')) {
            throw "The Windows-to-WSL browser entry does not accept -$option. Pass advanced options to Build-LocalGame.ps1 inside WSL."
        }
    }
    if (-not (Get-Command wsl -ErrorAction SilentlyContinue)) { throw 'Browser compilation needs an x86-64 Linux toolchain. Install WSL Ubuntu, then rerun this command.' }
    $distributions = @(& wsl --list --quiet | ForEach-Object { ($_ -replace '\x00','').Trim() } | Where-Object { $_ })
    $distribution = $distributions | Where-Object { $_ -eq 'Ubuntu' } | Select-Object -First 1
    if (-not $distribution) { $distribution = $distributions | Where-Object { $_ -like 'Ubuntu-*' -or $_ -eq 'Debian' } | Select-Object -First 1 }
    $wslTargetArgs = if ($distribution) { @('--distribution',$distribution) } else { @() }
    $nativeRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
    $nativeIso = (Resolve-Path -LiteralPath $IsoPath).Path
    if (-not $WorkDir) { $WorkDir = Join-Path $nativeRoot 'build/final-game' }
    function Convert-WslPath([string]$Path) {
        $converted = & wsl @wslTargetArgs --exec wslpath -a -u ([IO.Path]::GetFullPath($Path))
        if ($LASTEXITCODE -ne 0) { throw 'Cannot translate the build path into WSL. Ensure an Ubuntu distribution is installed.' }
        return ($converted | Out-String).Trim()
    }
    $linuxRoot = Convert-WslPath $nativeRoot
    $linuxIso = Convert-WslPath $nativeIso
    $linuxWork = Convert-WslPath $WorkDir
    $wslArgs = $wslTargetArgs + @('--exec','bash',"$linuxRoot/scripts/buildtool/Build-Drakan.sh",$linuxIso,'--web','--work-dir',$linuxWork,'--jobs',"$Jobs")
    if ($TextureDirectory) { $wslArgs += @('--texture-dir',(Convert-WslPath $TextureDirectory)) }
    & wsl @wslArgs
    if ($LASTEXITCODE -ne 0) { throw 'The browser build in WSL failed; see the tool output above.' }
    return
}
if (-not $webBuild -and (($TargetPlatform -eq 'windows') -ne $windowsHost)) { throw 'Run native builds on the selected operating system (Linux may use WSL).' }
if (-not $windowsHost -and -not [OperatingSystem]::IsLinux()) { throw 'Native player builds support Windows and Linux.' }
if (-not $windowsHost -and -not $PSBoundParameters.ContainsKey('Python')) { $Python = 'python3' }
$platformSuffix = if ($webBuild) { '-web' } elseif ($windowsHost) { '' } else { '-linux' }
$gameName = if ($webBuild) { 'drakan.js' } elseif ($windowsHost) { 'drakan.exe' } else { 'drakan' }
$RepoRoot = (Resolve-Path -LiteralPath $RepoRoot).Path
$IsoPath = (Resolve-Path -LiteralPath $IsoPath).Path
if (-not $WorkDir) { $WorkDir = Join-Path $RepoRoot 'build/local-game' }
$WorkDir = [IO.Path]::GetFullPath($WorkDir)
# One writer per build tree; game instances do not take this lock.
New-Item -ItemType Directory -Force -Path (Split-Path $WorkDir) | Out-Null
try {
    $buildWriterLock = [IO.File]::Open("$WorkDir.build.lock", [IO.FileMode]::OpenOrCreate,
        [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
} catch {
    throw "Another build owns $WorkDir. Wait for it to finish before rebuilding this tree."
}
$affinityState = $null
try {
$affinityState = Enter-BuildAffinity $CpuAffinity

if ($Incremental) {
    if (-not (Test-Path -LiteralPath (Join-Path $WorkDir 'local-input.json') -PathType Leaf)) {
        throw "$WorkDir is not a prepared build directory; run a full build into it first (without -Incremental)."
    }
    # Trees from before the single CMake root (2026-10-22) cannot be reconfigured in place.
    foreach ($cache in "runtime-build$platformSuffix/CMakeCache.txt","compiler$(if ($windowsHost) { '' } else { '-linux' })/cmake/CMakeCache.txt") {
        $cache = Join-Path $WorkDir $cache
        if (-not (Test-Path -LiteralPath $cache -PathType Leaf)) { continue }
        $configuredRoot = (Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$').Matches.Groups[1].Value
        if ([IO.Path]::GetFullPath($configuredRoot) -ne [IO.Path]::GetFullPath($RepoRoot)) {
            throw "$cache was configured from $configuredRoot, not the repo root; do a full build into a fresh WorkDir."
        }
    }
} elseif (Test-Path -LiteralPath $WorkDir) {
    throw 'Choose a fresh private WorkDir (or pass -Incremental to rebuild an existing one); existing files are never removed.'
}
if (-not $ConfigTemplate) {
    $defaultConfig = Join-Path $RepoRoot 'configs/drakan.toml'
    if (Test-Path -LiteralPath $defaultConfig -PathType Leaf) { $ConfigTemplate = $defaultConfig }
}
if (-not $PrepareOnly) {
    if (-not $ConfigTemplate) { throw 'Missing configs/drakan.toml. Integrate the validated public metadata template or pass ConfigTemplate.' }
    if (-not $GenerateOnly -and -not (Test-Path -LiteralPath (Join-Path $RepoRoot 'scripts/prepare_local_runtime.py') -PathType Leaf)) {
        throw 'Missing scripts/prepare_local_runtime.py; integrate the local ELF-to-runtime generator.'
    }
}
if ($ConfigTemplate) { $ConfigTemplate = (Resolve-Path -LiteralPath $ConfigTemplate).Path }
if (-not $RuntimeSource) { $RuntimeSource = Join-Path $RepoRoot 'ps2recomp' }
$RuntimeSource = (Resolve-Path -LiteralPath $RuntimeSource).Path
if ($RuntimeSource -ne (Resolve-Path -LiteralPath (Join-Path $RepoRoot 'ps2recomp')).Path) {
    throw 'RuntimeSource must be the in-repo ps2recomp/ directory: the game is built from the single root CMakeLists.txt.'
}
$outputLog = Join-Path (Split-Path $WorkDir) "$(Split-Path $WorkDir -Leaf)-output.log"
New-Item -ItemType Directory -Force -Path (Split-Path $WorkDir) | Out-Null
if ($Progress) { Set-Content -LiteralPath $outputLog -Value "Build-LocalGame.ps1 $(Get-Date -Format s)" }
$stepCount = if ($PrepareOnly) { 1 } elseif ($GenerateOnly) { 2 } elseif ($SourceOnly) { 3 } else { 6 }
$step = 0
function Write-BuildStep([string]$Text) {
    $script:step++
    $script:stepTitle = "[$script:step/$stepCount] $Text"
    # Build-Drakan.ps1 reads this after a failure to decide whether a less parallel retry can help.
    $global:DrakanBuildStep = $Text
    # The next "Rebuild Drakan" reads this to say where an interrupted build stopped (Build-Progress.ps1).
    Write-BuildState $WorkDir @{ step = $script:step; steps = $stepCount; title = $Text; status = 'running' }
    if ($Progress) { Write-Host "`n$script:stepTitle" -ForegroundColor Cyan }
}
# Developer details: on the console normally, only in the output log with -Progress.
function Write-Detail([string]$Text) {
    if ($Progress) { Add-Content -LiteralPath $outputLog -Value $Text } else { Write-Host $Text }
}
# With -Progress the tool's output goes to the output log; the console shows a progress bar with the elapsed time and
# (for the compile, via -CompileTotal) one numbered line per compiled file. A failure prints the errors and the tail.
function Invoke-Logged([string]$Program, [string[]]$Arguments, [int]$CompileTotal = -1) {
    $info = [Diagnostics.ProcessStartInfo]::new((Get-Command $Program -CommandType Application | Select-Object -First 1).Source)
    foreach ($argument in $Arguments) { $info.ArgumentList.Add($argument) }
    $info.WorkingDirectory = (Get-Location).Path
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.Encoding]::UTF8
    $info.StandardErrorEncoding = [Text.Encoding]::UTF8
    $log = [IO.StreamWriter]::new($outputLog, $true, [Text.UTF8Encoding]::new($false))
    $log.WriteLine("`n> $Program $($Arguments -join ' ')")
    $tail = [Collections.Generic.Queue[string]]::new()
    $errors = [Collections.Generic.List[string]]::new()
    $compiled = 0
    $current = ''
    $started = Get-Date
    $tracker = $null
    if ($CompileTotal -ge 0) {
        $timesFile = Join-Path $WorkDir 'compile-times.json'
        $measured = @{}
        if (Test-Path -LiteralPath $timesFile -PathType Leaf) {
            try { $measured = Get-Content -LiteralPath $timesFile -Raw | ConvertFrom-Json -AsHashtable } catch { $measured = @{} }
        }
        if ($VuChunk -eq 0) {
            # Whole-block slices: about 10 minutes each on a fast PC unless this PC has measured them.
            foreach ($slice in 0..3) { if (-not $measured.ContainsKey("ps2_vu1_aot_$slice.cpp")) { $measured["ps2_vu1_aot_$slice.cpp"] = 600 } }
        }
        $tracker = New-CompileTracker $script:compilePlan ($Jobs * $CompilerProcesses) $measured
    }
    $title = $Host.UI.RawUI.WindowTitle
    $process = [Diagnostics.Process]::Start($info)
    try {
        $stderr = $process.StandardError.ReadToEndAsync()
        $pending = $null
        while ($true) {
            if (-not $pending) { $pending = $process.StandardOutput.ReadLineAsync() }
            if ($pending.Wait(1000)) {
                $line = $pending.Result
                $pending = $null
                if ($null -eq $line) { break }
                $log.WriteLine($line)
                $tail.Enqueue($line)
                if ($tail.Count -gt 40) { [void]$tail.Dequeue() }
                if ($line -match '\b(fatal )?error [A-Z]+\d+:|^CMake Error') {
                    $errors.Add($line.Trim())
                    if ($CompileTotal -ge 0) { Write-Host "  $($line.Trim())" -ForegroundColor Red }
                } elseif ($tracker -and $line -match '^\s+([\w.+-]+\.(c|cc|cpp|cxx))\s*$') {
                    # MSBuild (minimal verbosity) prints each source file as cl starts on it.
                    $compiled++
                    $current = $Matches[1]
                    Add-CompileStart $tracker $current (Get-Date)
                    $estimate = Get-CompileEstimate $current $tracker.Measured
                    $note = if ($estimate) {
                        "  (slow file: about $(Format-Duration $estimate))"
                    } else { '' }
                    Write-Host "  $(Format-CompileCounter $compiled $CompileTotal) $current$note"
                }
            }
            $now = Get-Date
            $elapsed = $now - $started
            $status = '{0:00}:{1:00}:{2:00} elapsed' -f [int][Math]::Floor($elapsed.TotalHours), $elapsed.Minutes, $elapsed.Seconds
            $percent = -1
            if ($tracker) {
                foreach ($job in (Update-CompileTracker $tracker $now)) {
                    if ($job.Elapsed.TotalSeconds -ge 60) {
                        Write-Host "         finished $($job.Name) after $(Format-Duration $job.Elapsed)" -ForegroundColor DarkGray
                    }
                }
                $oldest = Get-OldestCompile $tracker
                # A file that runs for minutes gets a line every 3 minutes, so the window never looks stuck (not every
                # console shows the progress bar, and the log keeps these lines).
                if ($oldest -and ($now - $oldest.Start).TotalMinutes -ge $oldest.NextNote) {
                    Write-Host "         still compiling $(Format-RunningCompile $oldest $now)" -ForegroundColor DarkGray
                    $oldest.NextNote += 3
                }
                $done = [Math]::Max($tracker.Finished, $compiled - $tracker.Running.Count)
                $status = "$done$(if ($CompileTotal -gt 0 -and $done -le $CompileTotal) { " of $CompileTotal" }) files done, $status"
                if ($CompileTotal -gt 0) { $percent = [Math]::Min(99, [int](100 * $done / $CompileTotal)) }
                $Host.UI.RawUI.WindowTitle = "Building Drakan: $(if ($percent -ge 0) { "$percent% " })($done files)"
                Write-Progress -Activity $script:stepTitle -Status $status -PercentComplete $percent
                # A second bar for the longest-running file, measured against its usual time when there is one.
                if ($oldest -and ($now - $oldest.Start).TotalSeconds -ge 20) {
                    $filePercent = if ($oldest.Estimate) {
                        [Math]::Min(99, [int](100 * ($now - $oldest.Start).TotalSeconds / $oldest.Estimate.TotalSeconds))
                    } else { -1 }
                    Write-Progress -Id 1 -ParentId 0 -Activity "Compiling $($oldest.Name)" -Status (Format-RunningCompile $oldest $now) -PercentComplete $filePercent
                } else {
                    Write-Progress -Id 1 -ParentId 0 -Activity 'Compiling' -Completed
                }
            } else {
                Write-Progress -Activity $script:stepTitle -Status $status -PercentComplete $percent
            }
        }
        $process.WaitForExit()
        $errorText = $stderr.Result
        if ($errorText) { $log.WriteLine($errorText) }
    } finally {
        $log.Dispose()
        if ($tracker -and $tracker.Durations.Count) {
            # Remember how long the slow files took on this PC: the next build's estimates use them.
            foreach ($name in $tracker.Durations.Keys) { if ($tracker.Durations[$name] -ge 60) { $measured[$name] = $tracker.Durations[$name] } }
            try { $measured | ConvertTo-Json | Set-Content -LiteralPath $timesFile } catch { Write-Verbose "compile-times.json: $_" }
        }
        if ($tracker) { Write-Progress -Id 1 -ParentId 0 -Activity 'Compiling' -Completed }
        Write-Progress -Activity $script:stepTitle -Completed
        $Host.UI.RawUI.WindowTitle = $title
    }
    if ($process.ExitCode) {
        Write-Host "`n$script:stepTitle failed. Last output:" -ForegroundColor Red
        $tail | ForEach-Object { Write-Host "  $_" }
        if ($errorText) { $errorText.TrimEnd() -split "`r?`n" | Select-Object -Last 20 | ForEach-Object { Write-Host "  $_" } }
        if ($errors.Count -and $CompileTotal -lt 0) {
            Write-Host 'Errors:' -ForegroundColor Red
            $errors | Select-Object -Unique -First 20 | ForEach-Object { Write-Host "  $_" -ForegroundColor Red }
        }
        Write-Host "Full output: $outputLog"
        throw "$(Split-Path $Program -Leaf) failed ($($process.ExitCode))"
    }
}
function Invoke-Checked([string]$Program, [string[]]$Arguments, [int]$CompileTotal = -1) {
    if ($Progress) { Invoke-Logged $Program $Arguments $CompileTotal; return }
    & $Program @Arguments
    if ($LASTEXITCODE) { throw "$Program failed ($LASTEXITCODE)" }
}
Write-BuildStep 'Reading the game from the ISO'
$prepare =@((Join-Path $PSScriptRoot 'prepare_iso.py'),'--iso',$IsoPath,'--output-directory',$WorkDir,'--repo-root',$RepoRoot)
if ($ConfigTemplate) { $prepare += @('--config-template',$ConfigTemplate) }
if ($Incremental) { $prepare += '--refresh-config' }
Invoke-Checked $Python $prepare
# The disc region prepare_iso.py detected (SYSTEM.CNF BOOT2): PAL SCES_500.06 or USA SCUS_971.28. WorkDirs prepared
# before regions existed have no "region" and are PAL. See docs/reference/regions.md.
$localInput = Get-Content -LiteralPath (Join-Path $WorkDir 'local-input.json') -Raw | ConvertFrom-Json
$gameRegion = if ($localInput.region) { [string]$localInput.region } else { 'pal' }
$regionProfile = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'regions.json') -Raw | ConvertFrom-Json).regions.$gameRegion
if (-not $regionProfile) { throw "Unknown disc region '$gameRegion' in $WorkDir/local-input.json" }
$bootElf = Join-Path $WorkDir $regionProfile.boot
Write-Detail "Disc: $($regionProfile.label)$(if ($regionProfile.status -ne 'validated') { " ($($regionProfile.status): docs/reference/regions.md)" })"
if ($PrepareOnly) { return }
# Everything the generated C++ depends on: the recompiler sources, its pinned deps, the config and metadata.
function Get-RecompileStamp {
    $inputs = @(Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'ps2recomp/ps2xRecomp') -Recurse -File)
    $inputs += foreach ($path in 'scripts/buildtool/toolchain.lock.json',$regionProfile.functions_csv,$regionProfile.jump_tables_csv) {
        Get-Item -LiteralPath (Join-Path $RepoRoot $path)
    }
    $inputs += Get-Item -LiteralPath (Join-Path $WorkDir 'config.toml')
    $lines = foreach ($file in ($inputs | Sort-Object FullName)) {
        "$([IO.Path]::GetRelativePath($RepoRoot, $file.FullName)) $((Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash)"
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes($lines -join "`n")
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
}
$stampPath = Join-Path $WorkDir 'recompile.stamp'
$generatedDir = Join-Path $WorkDir 'generated'
$skipRecompile = $false
if ($Incremental -and -not $Regenerate -and (Test-Path -LiteralPath $stampPath -PathType Leaf) -and
    (Test-Path -LiteralPath $generatedDir -PathType Container)) {
    $skipRecompile = (Get-Content -LiteralPath $stampPath -Raw).Trim() -eq (Get-RecompileStamp)
}
Write-BuildStep 'Translating the PS2 game code to C++'
if ($skipRecompile) {
    Write-Host 'Recompiler inputs unchanged; reusing generated sources.'
} else {
    if (-not $Compiler) {
        $adjacent = Join-Path $PSScriptRoot $(if ($windowsHost) { 'ps2_recomp.exe' } else { 'ps2_recomp' })
        if (Test-Path -LiteralPath $adjacent -PathType Leaf) { $Compiler = $adjacent }
        else {
            if (-not $ToolWorkDir) { $ToolWorkDir = Join-Path $WorkDir "compiler$(if ($windowsHost) { '' } else { '-linux' })" }
            $buildTool = Join-Path $PSScriptRoot 'build_tool.py'
            if (-not (Test-Path -LiteralPath $buildTool)) { throw 'Pass Compiler explicitly, or run from the complete source checkout.' }
            Invoke-Checked $Python @($buildTool,'--work-dir',$ToolWorkDir,'--jobs',"$Jobs")
            $Compiler = (Get-Content -LiteralPath (Join-Path $ToolWorkDir 'result.json') -Raw | ConvertFrom-Json).executable
        }
    }
    $Compiler = (Resolve-Path -LiteralPath $Compiler).Path
    # Without a stamp an interrupted translation is redone next time instead of trusting half-written sources.
    Remove-Item -LiteralPath $stampPath -Force -ErrorAction SilentlyContinue
    Push-Location $WorkDir
    try { Invoke-Checked $Compiler @('config.toml') } finally { Pop-Location }
    if (-not (Test-Path -LiteralPath $generatedDir -PathType Container)) { throw 'Recompiler did not create generated output.' }
    # The recompiler only rewrites translation units whose text changed, so MSBuild recompiles just those.
    Set-Content -LiteralPath $stampPath -Value (Get-RecompileStamp) -NoNewline
}
if ($GenerateOnly) { return }
Write-BuildStep 'Preparing the runtime files from the game'
Invoke-Checked $Python @((Join-Path $RepoRoot 'scripts/prepare_local_runtime.py'),
    '--elf',$bootElf,'--runtime-source',$RuntimeSource)
foreach ($required in 'ps2xRuntime/CMakeLists.txt','ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc','ps2xRuntime/src/res/drakan.ico') {
    if (-not (Test-Path -LiteralPath (Join-Path $RuntimeSource $required) -PathType Leaf)) {
        throw "The local runtime generator did not prepare $required"
    }
}
if ($SourceOnly) {
    Write-Host "Private sources prepared under $WorkDir. Generated-game compilation and launch were skipped."
    return
}
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
elseif ($windowsHost) {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Install VS 2022 Desktop development with C++ and CMake tools.' }
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    $cmake = Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
else { throw 'CMake must be installed and available on PATH.' }
$runtimeBuild = Join-Path $WorkDir "runtime-build$platformSuffix"
$gameOutput = Join-Path $WorkDir "bin$platformSuffix"
$dlssBuild = 'OFF'
if ($windowsHost -and -not $NoNvidia) {
    if (-not $DlssSdkDirectory -and $AcceptNvidiaDlssLicense) {
        Invoke-Checked $Python @((Join-Path $PSScriptRoot 'fetch_dlss_sdk.py'),'--accept-nvidia-license')
    }
    if (-not $DlssSdkDirectory) {
        $defaultSdk = Join-Path $RepoRoot 'tools/dlss/dlss_sdk'
        if (Test-Path -LiteralPath (Join-Path $defaultSdk 'include/nvsdk_ngx.h') -PathType Leaf) {
            $DlssSdkDirectory = $defaultSdk
        }
    }
    if ($DlssSdkDirectory) {
        $DlssSdkDirectory = (Resolve-Path -LiteralPath $DlssSdkDirectory).Path
        foreach ($required in 'include/nvsdk_ngx.h','lib/Windows_x86_64/x64/nvsdk_ngx_d.lib') {
            if (-not (Test-Path -LiteralPath (Join-Path $DlssSdkDirectory $required) -PathType Leaf)) {
                throw "The supplied DLSS SDK is missing $required"
            }
        }
        $dlssBuild = 'ON'
    }
}
Write-BuildStep 'Setting up the C++ build'
Write-Detail "NVIDIA options: DLSS=$dlssBuild$(if ($DlssSdkDirectory) { " (SDK $DlssSdkDirectory)" })  [-NoNvidia disables]"
# One CMake root: the `game` preset (CMakePresets.json) selects drakan.exe, release logging and no debug UI.
$preset = if ($webBuild) { 'web-game' } elseif ($windowsHost) { 'game' } else { 'linux-game' }
$argsList = @('-S',$RepoRoot,'--preset',$preset,'-B',$runtimeBuild,
    "-DPS2X_GENERATED_DIR=$generatedDir","-DPS2X_OUTPUT_DIR=$gameOutput","-DDRAKAN_GAME_REGION=$gameRegion")
if ($windowsHost) {
    $argsList += @("-DGSR_DLSS=$dlssBuild",
        "-DPS2X_VU1_AOT_SERIAL=$(if ($SerialVuSlices -or $VuChunk -eq 0) { 'ON' } else { 'OFF' })")
    if ($DlssSdkDirectory) { $argsList += "-DGSR_DLSS_SDK_DIR=$DlssSdkDirectory" }
}
if ($webBuild) {
    Invoke-Checked $Python @((Join-Path $PSScriptRoot 'prepare_web_deps.py'),'--repo-root',$RepoRoot,'--work-dir',$WorkDir,'--jobs',"$Jobs")
    $webDeps = Get-Content -LiteralPath (Join-Path $WorkDir 'web-deps/result.json') -Raw | ConvertFrom-Json
    $argsList += @("-DCMAKE_TOOLCHAIN_FILE=$($webDeps.toolchain)","-DPS2X_WEB_FFMPEG_DIR=$($webDeps.ffmpeg)",'-DPS2X_ENABLE_FFMPEG=ON')
}
if ($NoWarnings) { $argsList += '-DDRAKAN_COMPILER_WARNINGS=OFF' }
$argsList += "-DPS2X_VU1_AOT_CHUNK=$VuChunk"
Write-Detail "VU1 blocks: $(if ($VuChunk -eq 0) { 'whole (up to ~17 GB per slice, compiled one after another)' } else { "chunks of $VuChunk pairs" })"
if (-not $DependencyDirectory -and (Test-Path -LiteralPath (Join-Path $RepoRoot 'deps/raylib') -PathType Container)) {
    $DependencyDirectory = Join-Path $RepoRoot 'deps'
}
if ($DependencyDirectory) {
    $DependencyDirectory = (Resolve-Path -LiteralPath $DependencyDirectory).Path
    $ffmpegArchive = Get-ChildItem -LiteralPath $DependencyDirectory -Filter 'ffmpeg-*-lgpl-amd64-shared.zip' | Select-Object -First 1
    if (($windowsHost -and -not $ffmpegArchive) -or -not (Test-Path -LiteralPath (Join-Path $DependencyDirectory 'raylib/CMakeLists.txt'))) {
        throw "$DependencyDirectory must hold raylib/ and the FFmpeg prebuilt zip"
    }
    Write-Detail "Offline dependencies: $DependencyDirectory"
    $argsList += "-DFETCHCONTENT_SOURCE_DIR_RAYLIB=$((Join-Path $DependencyDirectory 'raylib').Replace('\', '/'))"
    if ($windowsHost) { $argsList += "-DFFMPEG_PREBUILT_URL=$($ffmpegArchive.FullName.Replace('\', '/'))" }
}
Invoke-Checked $cmake $argsList
# Sources the compile step counts against: the C/C++ files of ps2EntryRunner and the projects it references (raylib's
# examples are configured but not built), with their object files (Build-Progress.ps1). A rebuild counts what is still
# out of date: everything left after an interrupted build, only the edited sources after a fix (header edits add more).
$compileTotal = 0
$script:compilePlan = @()
$compileTitle = ''
if ($windowsHost) {
    $script:compilePlan = Get-CompilePlan $runtimeBuild
    $pendingCompiles = Get-PendingCompiles $script:compilePlan
    $compileTotal = $pendingCompiles.Count
    $compileTitle = if (-not $script:compilePlan.Count) { '' }
        elseif ($pendingCompiles.Count -eq $script:compilePlan.Count) { " ($compileTotal files, one to two hours on a first build)" }
        elseif ($pendingCompiles.Count) { " ($compileTotal of $($script:compilePlan.Count) files left)" }
        else { ' (checking for changed files)' }
}
Write-BuildStep "Compiling the game$compileTitle"
if ($windowsHost) {
    $savedCompilerOptions = [Environment]::GetEnvironmentVariable('_CL_','Process')
    try {
        [Environment]::SetEnvironmentVariable('_CL_',("$savedCompilerOptions /MP$CompilerProcesses").Trim(),'Process')
        Invoke-Checked $cmake @('--build',$runtimeBuild,'--config','Release','--target','ps2EntryRunner','--',"/m:$Jobs","/p:CL_MPCount=$CompilerProcesses") $compileTotal
    } finally { [Environment]::SetEnvironmentVariable('_CL_',$savedCompilerOptions,'Process') }
} else {
    Invoke-Checked $cmake @('--build',$runtimeBuild,'--target','ps2EntryRunner','--parallel',"$Jobs") $compileTotal
}
$gameExe = Join-Path $gameOutput $gameName
if (-not (Test-Path -LiteralPath $gameExe -PathType Leaf)) { throw "Build did not produce $gameExe" }
Write-BuildStep 'Setting up the play folder'
if ($gameRegion -eq 'pal') {
    # Private derivatives of the supplied disc, never tracked / downloaded. Cached in the existing WorkDir; copied
    # through the normal deployment below, so the multiplayer menu has live original meshes on its first launch.
    Write-Detail 'Extracting original character meshes and materials'
    Invoke-Checked $Python @((Join-Path $RepoRoot 'analysis/scripts/character_previews.py'),'--iso',$IsoPath,
        '--out',(Join-Path $gameOutput 'previews/chars'))
    Invoke-Checked $Python @('-I',(Join-Path $RepoRoot 'analysis/scripts/character_motion.py'),'--iso',$IsoPath,
        '--out',(Join-Path $gameOutput 'mods/drakan-character-motion.zip'))

}
    # The Choose-your-game cover of The Ancients' Gates: the title screen artwork of the supplied disc.
    Write-Detail 'Extracting the Drakan 2 cover picture'
    Invoke-Checked $Python @('-I',(Join-Path $RepoRoot 'analysis/scripts/title_cover.py'),'--iso',$IsoPath,
        '--out',(Join-Path $gameOutput 'previews/game-d2.png'))
if (-not $NoDist) {
    # A non-PAL build gets its own play folder so it never replaces the PAL one (dist/DrakanPC-usa).
    $regionSuffix = if ($gameRegion -eq 'pal') { '' } else { "-$gameRegion" }
    if (-not $DistDirectory) { $DistDirectory = Join-Path $RepoRoot "dist/DrakanPC$regionSuffix$platformSuffix" }
    $DistDirectory = [IO.Path]::GetFullPath($DistDirectory)
    if ($DistDirectory -eq $WorkDir -or $DistDirectory.StartsWith($WorkDir + [IO.Path]::DirectorySeparatorChar)) {
        throw 'DistDirectory must be outside WorkDir.'
    }
    if ($DistDirectory -eq [IO.Path]::GetPathRoot($DistDirectory) -or $DistDirectory -eq $RepoRoot) {
        throw 'DistDirectory cannot be a filesystem root or the source checkout.'
    }
    # Saves, user settings and hand-staged assets survive the clean; everything else is a build product or stale.
    $keep = 'mc0','mc1','mc0_*','mc1_*','saves','mods','textures','movies','subtitles','nr','ngx_data','*.ini','Play*.cmd','Play*.sh','PLAY.txt'
    New-Item -ItemType Directory -Force -Path $DistDirectory | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $DistDirectory -Force) {
        $name = $item.Name
        if ($keep | Where-Object { $name -like $_ }) { continue }
        $resolvedItem = [IO.Path]::GetFullPath($item.FullName)
        if (-not $resolvedItem.StartsWith($DistDirectory + [IO.Path]::DirectorySeparatorChar)) { throw 'Deployment cleanup escaped DistDirectory.' }
        Remove-Item -LiteralPath $resolvedItem -Recurse -Force
    }
    foreach ($item in Get-ChildItem -LiteralPath $gameOutput -Force) {
        $name = $item.Name
        # Never let the build folder's test saves, default settings or launchers replace the preserved ones;
        # the linker map and test-run logs stay in WorkDir/bin.
        if ($keep | Where-Object { $name -like $_ }) { continue }
        if ($name -like '*.map' -or $name -like '*.log') { continue }
        Copy-Item -LiteralPath $item.FullName -Destination $DistDirectory -Recurse -Force
    }
    if ($ModDirectory) {
        Invoke-Checked $Python @((Join-Path $PSScriptRoot 'install_private_mods.py'),
    # previews/ is kept across builds, so the generated cover is placed explicitly (a hand-made game-d2.png is replaced).
    $coverPicture = Join-Path $gameOutput 'previews/game-d2.png'
    if (Test-Path -LiteralPath $coverPicture -PathType Leaf) {
        New-Item -ItemType Directory -Force -Path (Join-Path $DistDirectory 'previews') | Out-Null
        Copy-Item -LiteralPath $coverPicture -Destination (Join-Path $DistDirectory 'previews/game-d2.png') -Force
    }
            '--source',$ModDirectory,'--target',(Join-Path $DistDirectory 'mods'))
    }
    if ($gameRegion -eq 'pal') {
        Invoke-Checked $Python @((Join-Path $PSScriptRoot 'install_private_mods.py'),
            '--source',(Join-Path $gameOutput 'mods/drakan-character-motion.zip'),
            '--target',(Join-Path $DistDirectory 'mods'),'--activate','drakan-character-motion')
    }
    if ($windowsHost) {
        # The DLSS runtime comes from the player's own NVIDIA SDK download (or an override in local/nvidia/dlss); it is
        # never part of the source tree. Without it the game simply runs without DLSS.
        if ($dlssBuild -eq 'ON') {
            $dlssRuntime = @((Join-Path $RepoRoot 'local/nvidia/dlss/nvngx_dlss.dll'),
                (Join-Path $DlssSdkDirectory 'lib/Windows_x86_64/rel/nvngx_dlss.dll')) |
                Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
            if ($dlssRuntime) { Copy-Item -LiteralPath $dlssRuntime -Destination $DistDirectory -Force }
        }
        # Launchers are tracked in scripts/launchers; a fresh play folder gets them, edited ones are kept.
        foreach ($launcher in Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'scripts/launchers') -Filter 'Play*.cmd') {
            $target = Join-Path $DistDirectory $launcher.Name
            if (-not (Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath $launcher.FullName -Destination $target }
        }
    } elseif (-not $webBuild) {
        $launcher = Join-Path $RepoRoot 'scripts/launchers/Play-Drakan.sh'
        $target = Join-Path $DistDirectory 'Play-Drakan.sh'
        if (-not (Test-Path -LiteralPath $target)) { Copy-Item -LiteralPath $launcher -Destination $target }
        Invoke-Checked 'chmod' @('+x',(Join-Path $DistDirectory $gameName),$target)
    }
    # Narration subtitles are game text, so they are not tracked: local/subtitles/*.srt is the persistent source
    # (gitignored) and is restored into every play folder. An already staged folder is only left alone with a warning.
    $subtitleSource = Join-Path $RepoRoot 'local/subtitles'
    $subtitleDest = Join-Path $DistDirectory 'subtitles'
    if (Test-Path -LiteralPath $subtitleSource -PathType Container) {
        New-Item -ItemType Directory -Force -Path $subtitleDest | Out-Null
        Copy-Item -Path (Join-Path $subtitleSource '*.srt') -Destination $subtitleDest -Force
    } elseif (-not (Test-Path -LiteralPath (Join-Path $subtitleDest '*.srt'))) {
        $message = "No narration subtitles: put intro_english.srt / outro_english.srt in $subtitleSource"
        if ($Progress) { Write-Detail $message } else { Write-Warning $message }
    }
    $textureSource = $TextureDirectory
    if (-not $textureSource) {
        foreach ($candidate in @((Join-Path $RepoRoot 'local/textures'), (Join-Path $RepoRoot 'dist/DrakanPC/textures'))) {
            if (Test-Path -LiteralPath $candidate -PathType Container) { $textureSource = $candidate; break }
        }
    }
    if ($textureSource) {
        $textureSource = (Resolve-Path -LiteralPath $textureSource).Path
        $textureDest = [IO.Path]::GetFullPath((Join-Path $DistDirectory 'textures'))
        if ($webBuild) {
            # Like the ISO, a local browser pack can be streamed directly from
            # its existing location; do not duplicate a multi-gigabyte pack.
            $webTextureSource = $textureSource
        } elseif ($textureSource -ne $textureDest) {
            Invoke-Checked $Python @((Join-Path $PSScriptRoot 'stage_texture_pack.py'),$textureSource,$textureDest)
            Write-Detail "Staged shared HD texture pack from $textureSource"
        }
    }
    $gameOutput = $DistDirectory
    $gameExe = Join-Path $DistDirectory $gameName
    Write-Detail "Cleaned and refreshed $DistDirectory"
    Write-Detail "Deployed $gameName SHA-256: $((Get-FileHash -LiteralPath $gameExe -Algorithm SHA256).Hash)"
}
if ($webBuild) {
    foreach ($asset in Get-ChildItem -LiteralPath (Join-Path $RepoRoot 'runtime/web') -File) {
        if ($asset.Name -in @('pre.js','glfw_proxy.js','raylib_proxy.h')) { continue }
        Copy-Item -LiteralPath $asset.FullName -Destination $gameOutput -Force
    }
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'serve_web.py') -Destination $gameOutput -Force
    foreach ($launcher in 'Play-Browser.sh','Play-Browser.cmd') {
        Copy-Item -LiteralPath (Join-Path $RepoRoot "scripts/launchers/$launcher") -Destination $gameOutput -Force
    }
    Invoke-Checked 'chmod' @('+x',(Join-Path $gameOutput 'Play-Browser.sh'))
    $webLaunch = @{ iso = $IsoPath }
    if ($webTextureSource) { $webLaunch.textures = $webTextureSource }
    if ($env:WSL_DISTRO_NAME -and (Get-Command wslpath -ErrorAction SilentlyContinue)) {
        $webLaunch.iso_windows = (& wslpath -w $IsoPath | Out-String).Trim()
        if ($webTextureSource) { $webLaunch.textures_windows = (& wslpath -w $webTextureSource | Out-String).Trim() }
        if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare the Windows browser launcher ISO path.' }
    }
    $webLaunch | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $gameOutput 'web-build.json')
    Write-Detail "Private browser build: $gameOutput"
    Write-Detail "Launch: $gameOutput/Play-Browser.sh (loopback only; ISO remains local)"
    Write-BuildState $WorkDir @{ status = 'done'; finished = (Get-Date).ToString('s'); error = $null }
    return
}
$launchArgs = @((Join-Path $PSScriptRoot 'prepare_launch.py'),'--repo-root',$RepoRoot,
    '--output-directory',$gameOutput,'--iso',$IsoPath,'--elf',$bootElf)
Invoke-Checked $Python $launchArgs
$quotedExe = $gameExe.Replace("'", "''")
$quotedOutput = $gameOutput.Replace("'", "''")
Write-Detail "Private executable: $gameExe"
Write-Detail "ISO playback path: $IsoPath"
Write-Detail "Launch from PowerShell: Start-Process -FilePath '$quotedExe' -WorkingDirectory '$quotedOutput'"
Write-Detail 'Local ISO/ELF paths and default input bindings are staged next to the executable; existing configs are preserved. No launch or upload was performed.'
Write-BuildState $WorkDir @{ status = 'done'; finished = (Get-Date).ToString('s'); error = $null; play = $DistDirectory }

} catch {
    Write-BuildState $WorkDir @{ status = 'failed'; error = "$_" }
    throw
} finally {
    try { Exit-BuildAffinity $affinityState } finally { $buildWriterLock.Dispose() }
}
