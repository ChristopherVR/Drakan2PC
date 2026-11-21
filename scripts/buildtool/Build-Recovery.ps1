# The journal directory is a sibling of the play folder so publishing and restoring use same-volume renames.
# The work directory can be on a different volume and is never renamed.
function Get-BuildPlayUpdate([string]$Play, [string]$GameName = 'drakan.exe') {
    $journal = "$Play.drakan-rebuild"
    [pscustomobject]@{
        Play = $Play
        GameName = $GameName
        Journal = $journal
        Owner = Join-Path $journal 'play-path.txt'
        Stage = Join-Path $journal 'stage'
        Previous = Join-Path $journal 'previous'
    }
}

function Enter-BuildPlayLock([string]$Play) {
    # A custom play folder can be targeted by two work directories. Both deployments must take this lock.
    $lockPath = if ([OperatingSystem]::IsWindows()) { $Play.ToUpperInvariant() } else { $Play }
    $bytes = [Text.Encoding]::UTF8.GetBytes($lockPath)
    $key = [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
    $lock = [Threading.Mutex]::new($false, "DrakanPC-play-$key")
    try {
        try { $acquired = $lock.WaitOne(0) }
        catch [Threading.AbandonedMutexException] { $acquired = $true }
        if (-not $acquired) { throw "Another rebuild is using the play folder: $Play" }
        $lock
    } catch {
        $lock.Dispose()
        throw
    }
}

function Assert-BuildPlayUpdateDirectory([string]$Path) {
    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Automatic rebuild needs an ordinary folder without links: $Path"
    }
}

function Assert-BuildPlayUpdateTree([string]$Path) {
    Assert-BuildPlayUpdateDirectory $Path
    if (Get-ChildItem -LiteralPath $Path -Recurse -Force -Attributes ReparsePoint | Select-Object -First 1) {
        throw "Automatic rebuild needs an ordinary folder without links: $Path"
    }
}

function Restore-BuildPlayUpdate($Update) {
    if (-not (Get-Item -LiteralPath $Update.Journal -Force -ErrorAction SilentlyContinue)) { return }
    Assert-BuildPlayUpdateDirectory $Update.Journal
    $owner = Get-Item -LiteralPath $Update.Owner -Force -ErrorAction SilentlyContinue
    if (-not $owner -and -not (Get-ChildItem -LiteralPath $Update.Journal -Force | Select-Object -First 1)) {
        [IO.Directory]::Delete($Update.Journal)
        return
    }
    if ($owner -isnot [IO.FileInfo] -or ($owner.Attributes -band [IO.FileAttributes]::ReparsePoint) -or
        [IO.File]::ReadAllText($Update.Owner) -ne $Update.Play) {
        throw "The rebuild folder does not belong to this installation: $($Update.Journal)"
    }
    if ([IO.Directory]::Exists($Update.Previous)) {
        Assert-BuildPlayUpdateDirectory $Update.Previous
        if (-not [IO.Directory]::Exists($Update.Play)) {
            [IO.Directory]::Move($Update.Previous, $Update.Play)
            Write-Host "Restored the previous playable build: $($Update.Play)"
        } elseif ([IO.Directory]::Exists($Update.Stage) -or
            -not [IO.File]::Exists((Join-Path $Update.Play $Update.GameName))) {
            throw "The previous playable build is available at $($Update.Previous). Both builds were kept."
        }
    }
    # Recover first. Keep the owner file until every known staging directory has been removed, so an interrupted
    # cleanup can be retried. A previous folder beside a published game is only a leftover staging copy.
    foreach ($path in $Update.Stage, $Update.Previous) {
        if (Get-Item -LiteralPath $path -Force -ErrorAction SilentlyContinue) {
            Assert-BuildPlayUpdateTree $path
            [IO.Directory]::Delete($path, $true)
        }
    }
    if (Get-ChildItem -LiteralPath $Update.Journal -Force | Where-Object { $_.Name -ne $owner.Name }) {
        throw "The rebuild folder contains unexpected files: $($Update.Journal)"
    }
    [IO.File]::Delete($Update.Owner)
    [IO.Directory]::Delete($Update.Journal)
}

function Start-BuildPlayUpdate($Update, [string[]]$Keep) {
    Assert-BuildPlayUpdateTree $Update.Play
    # Block a second game launch during the snapshot. A running game also prevents this lock from being taken.
    $exe = Join-Path $Update.Play $Update.GameName
    try { $guard = [IO.File]::Open($exe, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None) }
    catch { throw "Close the game before rebuilding. Could not lock $exe. $($_.Exception.Message)" }
    try {
        if (Get-Item -LiteralPath $Update.Journal -Force -ErrorAction SilentlyContinue) {
            throw "An earlier rebuild is still present: $($Update.Journal)"
        }
        # An interruption before this move can leave only an unreferenced owner file, not a half-written journal.
        $creating = "$($Update.Journal).$([Guid]::NewGuid().ToString('N'))"
        [void][IO.Directory]::CreateDirectory($creating)
        $owner = Join-Path $creating ([IO.Path]::GetFileName($Update.Owner))
        try {
            [IO.File]::WriteAllText($owner, $Update.Play)
            [IO.Directory]::Move($creating, $Update.Journal)
        } finally {
            if ([IO.Directory]::Exists($creating)) {
                [IO.File]::Delete($owner)
                [IO.Directory]::Delete($creating)
            }
        }
        [void][IO.Directory]::CreateDirectory($Update.Stage)
        foreach ($item in Get-ChildItem -LiteralPath $Update.Play -Force) {
            $name = $item.Name
            if ($Keep | Where-Object { $name -like $_ }) {
                Copy-Item -LiteralPath $item.FullName -Destination $Update.Stage -Recurse -Force
            }
        }
        $guard
    } catch {
        $guard.Dispose()
        throw
    }
}

function Publish-BuildPlayUpdate($Update, [IO.FileStream]$Guard) {
    Assert-BuildPlayUpdateTree $Update.Stage
    foreach ($name in @($Update.GameName, 'drakan.ini', 'DrakanPC-release.txt')) {
        $file = Get-Item -LiteralPath (Join-Path $Update.Stage $name)
        if ($file -isnot [IO.FileInfo] -or $file.Length -eq 0) {
            throw "The staged rebuild is missing $name."
        }
    }
    # Windows cannot rename a directory while a child has an open handle. Release the launch guard immediately
    # before the rename. If the game reopened in between, Windows refuses the move before the play folder changes.
    if ([OperatingSystem]::IsWindows()) { $Guard.Dispose() }
    try {
        [IO.Directory]::Move($Update.Play, $Update.Previous)
        [IO.Directory]::Move($Update.Stage, $Update.Play)
    } finally { $Guard.Dispose() }
    try { Restore-BuildPlayUpdate $Update }
    catch { Write-Warning "The game was updated. The previous files are still at $($Update.Previous)." }
}
