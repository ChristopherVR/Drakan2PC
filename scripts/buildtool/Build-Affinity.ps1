# Dot-sourced by the canonical builders. No native API imports or extra dependencies.
function Resolve-BuildAffinity([string]$Mask) {
    if (-not [OperatingSystem]::IsWindows()) { throw '-CpuAffinity is supported only on Windows.' }
    if ($Mask -notmatch '^(?:0[xX])?([0-9a-fA-F]{1,16})$') {
        throw '-CpuAffinity must be a nonzero hexadecimal mask of at most 16 digits (for example 0xFFF).'
    }
    $value = [Convert]::ToUInt64($Matches[1], 16)
    if ($value -eq 0) { throw '-CpuAffinity cannot be zero; omit it to keep the default affinity.' }
    $process = [Diagnostics.Process]::GetCurrentProcess()
    try {
        $available = [BitConverter]::ToUInt64([BitConverter]::GetBytes($process.ProcessorAffinity.ToInt64()), 0)
    } finally { $process.Dispose() }
    if (($value -band $available) -ne $value) {
        throw ('-CpuAffinity selects unavailable processors; the current process permits 0x{0:X}.' -f $available)
    }
    return [IntPtr]::new([BitConverter]::ToInt64([BitConverter]::GetBytes($value), 0))
}

function Enter-BuildAffinity([string]$Mask) {
    if (-not $Mask) { return $null }
    $affinity = Resolve-BuildAffinity $Mask
    $process = [Diagnostics.Process]::GetCurrentProcess()
    $state = @{ Process = $process; Affinity = $process.ProcessorAffinity;
        NodeReuse = [Environment]::GetEnvironmentVariable('MSBUILDDISABLENODEREUSE', 'Process') }
    try {
        # Set the parent before launching anything: CMake/MSBuild/cl and their children inherit it.
        # Reused MSBuild nodes could have been started with another affinity, so never use them here.
        $process.ProcessorAffinity = $affinity
        [Environment]::SetEnvironmentVariable('MSBUILDDISABLENODEREUSE', '1', 'Process')
    } catch {
        Exit-BuildAffinity $state
        throw
    }
    return $state
}

function Exit-BuildAffinity($State) {
    if ($null -eq $State) { return }
    try { $State.Process.ProcessorAffinity = $State.Affinity } finally {
        [Environment]::SetEnvironmentVariable('MSBUILDDISABLENODEREUSE', $State.NodeReuse, 'Process')
        $State.Process.Dispose()
    }
}
