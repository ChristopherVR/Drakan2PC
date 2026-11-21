function Test-BuildTextureDirectory([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $false }
    $replacement = [IO.Directory]::EnumerateFiles((Resolve-Path -LiteralPath $Path).ProviderPath) |
        Where-Object { [IO.Path]::GetFileName($_) -match '^[0-9a-f]{16}\.(png|dds)$' -and [IO.FileInfo]::new($_).Length -gt 0 } |
        Select-Object -First 1
    return [bool]$replacement
}

function Find-BuildTextureDirectory([string]$RepoRoot, [string]$PlayFolder, [string]$ExplicitDirectory) {
    if ($ExplicitDirectory -and -not (Test-Path -LiteralPath $ExplicitDirectory -PathType Container)) {
        throw (Get-BuildText 'hd_missing' @($ExplicitDirectory))
    }
    $candidates = if ($ExplicitDirectory) { $ExplicitDirectory } else {
        Join-Path $RepoRoot 'local/textures'
        if ($PlayFolder) { Join-Path $PlayFolder 'textures' }
        Join-Path $RepoRoot 'dist/DrakanPC/textures'
    }
    foreach ($candidate in $candidates | Select-Object -Unique) {
        if (Test-BuildTextureDirectory $candidate) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    if ($ExplicitDirectory) { throw (Get-BuildText 'hd_empty' @($ExplicitDirectory)) }
}
