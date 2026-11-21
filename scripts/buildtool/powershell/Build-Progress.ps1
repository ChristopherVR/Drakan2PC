# Dot-sourced by Build-LocalGame.ps1 (compile progress) and Build-Drakan.ps1 (resume review). Kept apart from the
# scripts so scripts/tests can run it against synthetic build folders without a compiler.

$script:BuildMessageFile = Join-Path (Split-Path -Parent $PSScriptRoot) 'build-messages.json'
$script:BuildMessageCatalog = $null
$script:BuildLanguage = 'en'
$script:PlainBuildConsole = $false
$script:BuildPulse = '|/-\'

function Initialize-BuildConsole([string]$Language, [switch]$Plain) {
    if (-not $Language -or $Language -eq 'auto') { $Language = [CultureInfo]::CurrentUICulture.TwoLetterISOLanguageName }
    $Language = $Language.ToLowerInvariant()
    $script:BuildLanguage = if ($Language -in 'en', 'de', 'fr', 'it', 'es') { $Language } else { 'en' }
    $script:PlainBuildConsole = [bool]$Plain
}

function Get-BuildText([string]$Key, [object[]]$Values) {
    if (-not $script:BuildMessageCatalog) {
        $script:BuildMessageCatalog = Get-Content -LiteralPath $script:BuildMessageFile -Raw -Encoding utf8 | ConvertFrom-Json -AsHashtable
    }
    $messages = $script:BuildMessageCatalog[$Key]
    if (-not $messages) { throw "No builder console message for '$Key'." }
    $text = $messages[$script:BuildLanguage] ?? $messages['en']
    if ($Values.Count) { return $text -f $Values }
    return $text
}

# The six steps of a full Build-LocalGame.ps1 run, in order. Build-Drakan.ps1's review and the retry policy match on
# these titles (the compile step's title gets a file count appended).
$script:DrakanBuildSteps = @(
    'Reading the game from the ISO'
    'Translating the PS2 game code to C++'
    'Preparing the runtime files from the game'
    'Setting up the C++ build'
    'Compiling the game'
    'Setting up the play folder'
)

function Test-BuildConsoleColor {
    return (-not $script:PlainBuildConsole -and (Test-BuildTerminal) -and -not $env:NO_COLOR)
}

function Test-BuildTerminal {
    return ($Host.Name -eq 'ConsoleHost' -and -not [Console]::IsOutputRedirected -and [Console]::WindowWidth -gt 1)
}

function Write-BuildAccent([string]$Text, [ConsoleColor]$Color, [switch]$Live) {
    $useColor = Test-BuildConsoleColor
    if ($useColor) {
        $previous = [Console]::ForegroundColor
        [Console]::ForegroundColor = $Color
    }
    try {
        if ($Live) { [Console]::Write($Text) }
        else { Write-Host $Text }
    } finally {
        if ($useColor) { [Console]::ForegroundColor = $previous }
    }
}

function Write-BuildHeading([string]$Title, [ConsoleColor]$Color = 'Cyan') {
    Write-Host ''
    Write-BuildAccent $Title $Color
}

function Write-BuildValue([string]$Label, [string]$Value) {
    $line = '  {0,-19} {1}' -f $Label, $Value
    if (-not [Console]::IsOutputRedirected -and $Host.Name -eq 'ConsoleHost' -and $line.Length -ge $Host.UI.RawUI.WindowSize.Width) {
        Write-Host "  $Label"
        Write-Host "    $Value"
    } else {
        Write-Host $line
    }
}

# "45 s", "4 min", "1 h 05 min".
function Format-Duration([TimeSpan]$Span) {
    if ($Span.TotalSeconds -lt 60) { return "$([int][Math]::Floor($Span.TotalSeconds)) s" }
    if ($Span.TotalMinutes -lt 60) { return "$([int][Math]::Floor($Span.TotalMinutes)) min" }
    return '{0} h {1:00} min' -f [int][Math]::Floor($Span.TotalHours), $Span.Minutes
}

function Format-BuildProgressFrame([int]$Frame, [int]$Percent, [string]$Status, [int]$Width) {
    $pulse = $script:BuildPulse[$Frame % $script:BuildPulse.Length]
    $meter = if ($Percent -ge 0) { '{0,3}%' -f $Percent } else { '' }
    $prefix = "  $pulse "
    $room = $Width - $prefix.Length - 3
    if ($meter) { $room -= $meter.Length + 2 }
    $barWidth = [Math]::Min(20, $room - $Status.Length)
    if ($barWidth -ge 8) {
        if ($Percent -ge 0) {
            $filled = [int][Math]::Floor($barWidth * $Percent / 100)
            $bar = ('#' * $filled) + ('-' * ($barWidth - $filled))
        } else {
            $lead = $Frame % ($barWidth - 2)
            $bar = ('-' * $lead) + '###' + ('-' * ($barWidth - $lead - 3))
        }
        $prefix += "[$bar] "
    }
    if ($meter) { $prefix += "$meter  " }
    $line = $prefix + $Status
    if ($line.Length -gt $Width) {
        return $line.Substring(0, [Math]::Max(0, $Width - 3)) + ('.' * [Math]::Min(3, $Width))
    }
    return $line
}

function Write-BuildProgressFrame([int]$Frame, [int]$Percent, [string]$Status) {
    $width = [Math]::Max(1, [Console]::WindowWidth - 1)
    $line = Format-BuildProgressFrame $Frame $Percent $Status $width
    Write-BuildAccent ("`r" + $line.PadRight($width)) Cyan -Live
}

function Clear-BuildProgressFrame {
    [Console]::Write("`r" + (' ' * [Math]::Max(1, [Console]::WindowWidth - 1)) + "`r")
}

# The C/C++ sources of ps2EntryRunner.vcxproj and the projects it references (raylib's examples are configured but
# not built), each with the object file MSBuild writes for it. CMake's Visual Studio generator puts objects in the
# project's IntDir (<target>.dir\Release\) unless an item names its own ObjectFileName (same-named sources).
# Project files write paths with backslashes; .NET on Linux/macOS does not treat those as separators.
function Resolve-ProjectPath([string]$Path, [string]$BaseDir) {
    $native = $Path -replace '[\\/]', [string][IO.Path]::DirectorySeparatorChar
    return [IO.Path]::GetFullPath($native, $BaseDir + [IO.Path]::DirectorySeparatorChar)
}

function Get-CompilePlan([string]$RuntimeBuild, [string]$Configuration = 'Release') {
    $plan = [Collections.Generic.List[object]]::new()
    $root = Get-ChildItem -LiteralPath $RuntimeBuild -Filter 'ps2EntryRunner.vcxproj' -Recurse -File -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $root) { return , $plan }
    $projects = [Collections.Generic.Queue[string]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $projects.Enqueue($root.FullName)
    while ($projects.Count) {
        $project = $projects.Dequeue()
        if (-not $project -or -not $seen.Add($project) -or -not (Test-Path -LiteralPath $project -PathType Leaf)) { continue }
        $projectDir = Split-Path $project
        $xml = [xml](Get-Content -LiteralPath $project -Raw)
        $intDir = $null
        foreach ($node in $xml.GetElementsByTagName('IntDir')) {
            if ($node.GetAttribute('Condition') -match "'$Configuration\|") { $intDir = $node.InnerText; break }
        }
        if (-not $intDir) { $intDir = "$([IO.Path]::GetFileNameWithoutExtension($project)).dir\$Configuration\" }
        $intDir = Resolve-ProjectPath $intDir $projectDir
        foreach ($item in $xml.GetElementsByTagName('ClCompile')) {
            $source = $item.GetAttribute('Include')
            if (-not $source) { continue }
            $source = Resolve-ProjectPath $source $projectDir
            $object = $null
            foreach ($child in $item.GetElementsByTagName('ObjectFileName')) {
                $condition = $child.GetAttribute('Condition')
                if (-not $condition -or $condition -match "'$Configuration\|") { $object = $child.InnerText; break }
            }
            if ($object -and $object -notmatch '[\\/]$') {
                $object = Resolve-ProjectPath $object.Replace('$(IntDir)', $intDir) $projectDir
            } else {
                $object = Join-Path $intDir "$([IO.Path]::GetFileNameWithoutExtension($source)).obj"
            }
            $plan.Add([pscustomobject]@{ Source = $source; Name = Split-Path $source -Leaf; Object = $object })
        }
        foreach ($reference in $xml.GetElementsByTagName('ProjectReference')) {
            $projects.Enqueue((Resolve-ProjectPath $reference.GetAttribute('Include') $projectDir))
        }
    }
    return , $plan
}

# Sources whose object is missing or older than the source: what an interrupted build still has to compile. Header
# changes are not seen, so this is a lower bound for an ordinary rebuild, and exact for a resumed one.
function Get-PendingCompiles($Plan) {
    $pending = [Collections.Generic.List[object]]::new()
    foreach ($entry in $Plan) {
        $object = Get-Item -LiteralPath $entry.Object -ErrorAction SilentlyContinue
        $source = Get-Item -LiteralPath $entry.Source -ErrorAction SilentlyContinue
        # An empty object is one cl had only started on when the build stopped.
        if (-not $object -or $object.Length -eq 0 -or ($source -and $source.LastWriteTimeUtc -gt $object.LastWriteTimeUtc)) {
            $pending.Add($entry)
        }
    }
    return , $pending
}

# Count nonempty objects created or updated during this invocation, even when MSBuild emits no source names.
function New-CompileOutputWatch($Plan) {
    $pending = [Collections.Generic.Dictionary[string, long]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in @($Plan)) {
        $file = [IO.FileInfo]::new($entry.Object)
        $stamp = if ($file.Exists -and $file.Length) { $file.LastWriteTimeUtc.Ticks } else { 0 }
        [void]$pending.TryAdd($file.FullName, $stamp)
    }
    return [pscustomobject]@{ Pending = $pending; Finished = 0 }
}

function Update-CompileOutputWatch($Watch) {
    $completed = @(foreach ($entry in $Watch.Pending.GetEnumerator()) {
        $file = [IO.FileInfo]::new($entry.Key)
        if ($file.Exists -and $file.Length -gt 0 -and $file.LastWriteTimeUtc.Ticks -gt $entry.Value) { $entry.Key }
    })
    foreach ($path in $completed) { [void]$Watch.Pending.Remove($path) }
    $Watch.Finished += $completed.Count
}

# Files that are known to take long, with how long they took on a fast PC. Overridden by the times this PC measured
# (<WorkDir>/compile-times.json), so the estimate fits the machine from the second build on.
function Get-CompileEstimate([string]$Name, [hashtable]$Measured) {
    if ($Measured -and $Measured.ContainsKey($Name)) { return [TimeSpan]::FromSeconds([double]$Measured[$Name]) }
    if ($Name -like 'ps2_vu1_aot_*') { return [TimeSpan]::FromMinutes(6) }
    return $null
}

# Tracks which files are compiling. MSBuild (minimal verbosity) prints a source's name when cl starts on it and
# nothing when it finishes; a file counts as finished once its object file is written after it started. Without a
# plan (or for a name it does not know) the oldest files are taken as finished once more than $Slots are running.
function New-CompileTracker($Plan, [int]$Slots, [hashtable]$Measured) {
    $objects = @{}
    foreach ($entry in @($Plan)) {
        if (-not $objects.ContainsKey($entry.Name)) { $objects[$entry.Name] = [Collections.Generic.List[string]]::new() }
        $objects[$entry.Name].Add($entry.Object)
    }
    return [pscustomobject]@{
        Objects = $objects; Slots = [Math]::Max(1, $Slots); Measured = $(if ($Measured) { $Measured } else { @{} })
        Running = [Collections.Generic.List[object]]::new(); Durations = @{}; Started = 0; Finished = 0
    }
}

function Add-CompileStart($Tracker, [string]$Name, [datetime]$Now) {
    $Tracker.Started++
    $Tracker.Running.Add([pscustomobject]@{
        Name = $Name; Start = $Now; Objects = $Tracker.Objects[$Name]
        Estimate = Get-CompileEstimate $Name $Tracker.Measured
    })
}

# MSBuild can print a bare name, a full path or a project-number prefix such as 4>.
function Get-CompileSourceName([string]$Line) {
    $name = ($Line.Trim() -replace '^\d+>\s*', '').Trim('"')
    if ($name.Contains('"')) { return }
    if ($name -match '^(?:[a-z]:[\\/]|[\\/])') { $name = ($name -split '[\\/]')[-1] }
    if ($name -match '^[\w.+-]+\.(?:c|cc|cpp|cxx)$') { return $name }
}

# Moves finished files out of Running and records how long each took.
function Update-CompileTracker($Tracker, [datetime]$Now) {
    $done = [Collections.Generic.List[object]]::new()
    foreach ($job in @($Tracker.Running)) {
        if (-not $job.Objects) { continue }
        foreach ($object in $job.Objects) {
            $item = Get-Item -LiteralPath $object -ErrorAction SilentlyContinue
            # cl creates an empty object a few seconds after it starts and fills it in at the end. A second of slack
            # for file-system timestamp granularity.
            if ($item -and $item.Length -gt 0 -and $item.LastWriteTime -ge $job.Start.AddSeconds(-1)) { $done.Add($job); break }
        }
    }
    # Fallback for files without a known object: cl cannot run more than $Slots at once.
    $unknown = @($Tracker.Running | Where-Object { -not $_.Objects -and $_ -notin $done })
    $excess = $Tracker.Running.Count - $done.Count - $Tracker.Slots
    for ($i = 0; $i -lt [Math]::Min($excess, $unknown.Count); $i++) { $done.Add($unknown[$i]) }
    foreach ($job in $done) {
        [void]$Tracker.Running.Remove($job)
        $Tracker.Finished++
        $job | Add-Member -NotePropertyName Elapsed -NotePropertyValue ($Now - $job.Start) -Force
        $Tracker.Durations[$job.Name] = [int]($Now - $job.Start).TotalSeconds
    }
    return , $done
}

# The longest-running file whose object file is known, or $null. A file the plan does not know cannot be seen finishing
# (it is only retired once more than $Slots files run), so it would stay "oldest" for the rest of the build.
function Get-OldestCompile($Tracker) {
    $known = @($Tracker.Running | Where-Object { $_.Objects })
    if (-not $known.Count) { return $null }
    return ($known | Sort-Object Start | Select-Object -First 1)
}

# The progress line's text: "262/380 files, 00:04:09 | ps2_vu1_aot_2.cpp 4 min (about 6 min)". No file name repeats, no brackets.
function Format-ProgressStatus([int]$Done, [int]$Total, [string]$Elapsed, $Oldest, [datetime]$Now) {
    $text = if ($Total -gt 0 -and $Done -le $Total) { Get-BuildText 'progress_known' @($Done, $Total, $Elapsed) }
        else { Get-BuildText 'progress_unknown' @($Done, $Elapsed) }
    if ($Oldest) {
        $text += " | $($Oldest.Name) $(Format-Duration ($Now - $Oldest.Start))"
        if ($Oldest.Estimate) { $text += Get-BuildText 'progress_estimate' @((Format-Duration $Oldest.Estimate)) }
    }
    return $text
}

# Build state: Build-LocalGame.ps1 records each step it starts and how the run ended in <WorkDir>/build-state.json, so
# the next "Rebuild Drakan" can say where the last build stopped.
function Write-BuildState([string]$WorkDir, [hashtable]$Fields) {
    if (-not (Test-Path -LiteralPath $WorkDir -PathType Container)) { return }
    $path = Join-Path $WorkDir 'build-state.json'
    $state = @{}
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        try { (Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable).GetEnumerator() | ForEach-Object { $state[$_.Key] = $_.Value } } catch { }
    }
    foreach ($key in $Fields.Keys) { $state[$key] = $Fields[$key] }
    $state.updated = (Get-Date).ToString('s')
    $state | ConvertTo-Json | Set-Content -LiteralPath $path
}

function Read-BuildState([string]$WorkDir) {
    $path = Join-Path $WorkDir 'build-state.json'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { return $null }
    try { return Get-Content -LiteralPath $path -Raw | ConvertFrom-Json } catch { return $null }
}

# What each step leaves behind, checked on disk: one entry per step with .Ok and a short .Detail.
function Get-BuildReview([string]$WorkDir, [string]$RuntimeSource, [string]$GameExe) {
    $review = [Collections.Generic.List[object]]::new()
    function Add-Check([int]$Step, [bool]$Ok, [string]$Detail) {
        $review.Add([pscustomobject]@{ Step = $Step; Title = $script:DrakanBuildSteps[$Step - 1]; Ok = $Ok; Detail = $Detail })
    }
    $inputFile = Join-Path $WorkDir 'local-input.json'
    $config = Join-Path $WorkDir 'config.toml'
    $boot = $null
    if (Test-Path -LiteralPath $inputFile -PathType Leaf) {
        try { $boot = (Get-Content -LiteralPath $inputFile -Raw | ConvertFrom-Json).boot } catch { $boot = $null }
        if (-not $boot) { $boot = 'SCES_500.06' }
    }
    $okIso = $boot -and (Test-Path -LiteralPath (Join-Path $WorkDir $boot) -PathType Leaf) -and
        (Test-Path -LiteralPath $config -PathType Leaf)
    Add-Check 1 $okIso $(if ($okIso) { Get-BuildText 'review_iso' @($boot) }
        else { Get-BuildText 'review_iso_missing' })

    $generated = Join-Path $WorkDir 'generated'
    $sources = @(Get-ChildItem -LiteralPath $generated -Filter '*.cpp' -File -ErrorAction SilentlyContinue).Count
    $stamp = Test-Path -LiteralPath (Join-Path $WorkDir 'recompile.stamp') -PathType Leaf
    Add-Check 2 ($sources -gt 0 -and $stamp) $(if ($sources -and $stamp) { Get-BuildText 'review_cpp' @($sources) }
        elseif ($sources) { Get-BuildText 'review_cpp_partial' @($sources) } else { Get-BuildText 'review_cpp_missing' })

    $vuImage = if ($RuntimeSource) { Join-Path $RuntimeSource 'ps2xRuntime/src/lib/vu/aot/ps2_vu1_image.inc' }
    $okVu = $vuImage -and (Test-Path -LiteralPath $vuImage -PathType Leaf)
    Add-Check 3 $okVu $(if ($okVu) { Get-BuildText 'review_vu' } else { Get-BuildText 'review_vu_missing' })

    $runtimeBuild = Join-Path $WorkDir 'runtime-build'
    $okCmake = Test-Path -LiteralPath (Join-Path $runtimeBuild 'CMakeCache.txt') -PathType Leaf
    Add-Check 4 $okCmake $(if ($okCmake) { Get-BuildText 'review_build_ready' } else { Get-BuildText 'review_build_missing' })

    $plan = if ($okCmake) { Get-CompilePlan $runtimeBuild } else { @() }
    $pending = if ($plan.Count) { Get-PendingCompiles $plan } else { @() }
    $exe = $GameExe -and (Test-Path -LiteralPath $GameExe -PathType Leaf)
    $detail = if (-not $plan.Count) { Get-BuildText 'review_compile_missing' }
        else { Get-BuildText 'review_compiled' @(($plan.Count - $pending.Count), $plan.Count) }
    if ($exe -and $plan.Count) { $detail += Get-BuildText 'review_linked' }
    $okCompile = [bool]($plan.Count -and -not $pending.Count -and $exe)
    $review.Add([pscustomobject]@{ Step = 5; Title = $script:DrakanBuildSteps[4]; Ok = $okCompile; Detail = $detail
        Compiled = $plan.Count - $pending.Count; Total = $plan.Count })
    return , $review
}
