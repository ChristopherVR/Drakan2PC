# Incremental build of a private generated-game tree (the build/recomp_* dev layout):
#   <BuildDir>/rc          recompiler build tree   (only with -Gen; root CMakeLists.txt, `recompiler` preset)
#   <BuildDir>/config.toml recompiler config        (only with -Gen)
#   <BuildDir>/gen         recompiler output        (PS2X_GENERATED_DIR)
#   <BuildDir>/rt          runtime build tree       (root CMakeLists.txt, `game` preset; builds ps2EntryRunner)
#   <BuildDir>/bin         drakan.exe output
# Missing rc/ and rt/ trees are configured on first use. Trees created before the single CMake root (configured from
# ps2recomp/) cannot be reused: build into a fresh -BuildDir.
# Replaces the former combat_build.ps1 / combat_build.sh / perf_build.ps1 (same build, different flags).
#
#   pwsh scripts/harness/build_game.ps1                          # build/recomp_perf, 2 jobs x 2 compiler procs
#   pwsh scripts/harness/build_game.ps1 -BuildDir build/recomp_combat
#   pwsh scripts/harness/build_game.ps1 -Gen -Jobs 0 -CompilerJobs 0   # regenerate, then build unbounded (old perf_build.ps1)
#
# -Jobs / -CompilerJobs of 0 leave MSBuild /m and cl /MP unbounded.
param(
    [string]$BuildDir = '',
    [switch]$Gen,
    [switch]$NoBuild,
    [ValidateRange(0,64)][int]$Jobs = 2,
    [ValidateRange(0,64)][int]$CompilerJobs = 2,
    [string]$Target = 'ps2EntryRunner',
    [string]$LogName = 'combat-build.log'
)
$ErrorActionPreference = 'Stop'
$RepoRoot = (Resolve-Path -LiteralPath "$PSScriptRoot/../..").Path
if (-not $BuildDir) { $BuildDir = "$RepoRoot/build/recomp_perf" }
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmakeCommand) { $cmake = $cmakeCommand.Source }
else {
    $vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
    $vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Visual Studio C++ tools not found' }
    $cmake = "$vs/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
}
function Initialize-Tree([string]$Tree, [string[]]$Arguments) {
    $cache = "$Tree/CMakeCache.txt"
    if (Test-Path -LiteralPath $cache) {
        $cachedHome = (Select-String -Path $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$').Matches[0].Groups[1].Value
        if ((Resolve-Path -LiteralPath $cachedHome).Path -ne $RepoRoot) {
            throw "$Tree was configured from '$cachedHome', not the repository root; use a fresh -BuildDir."
        }
        return
    }
    & $cmake -S $RepoRoot @Arguments
    if ($LASTEXITCODE) { throw "configure failed for $Tree" }
}
$msbuildArgs = @(if ($Jobs -gt 0) { "/m:$Jobs" } else { '/m' })
if ($CompilerJobs -gt 0) { $msbuildArgs += "/p:CL_MPCount=$CompilerJobs" }
$msbuildArgs += '/v:m'

$sw = [Diagnostics.Stopwatch]::StartNew()
if ($Gen) {
    Initialize-Tree "$BuildDir/rc" @('--preset','recompiler','-B',"$BuildDir/rc")
    & $cmake --build "$BuildDir/rc" --config Release --target ps2_recomp -- /m /v:m 2>&1 | Select-String 'error' | Select-Object -First 10
    "recompiler built $($sw.Elapsed)"
    & "$BuildDir/rc/bin/Release/ps2_recomp.exe" "$BuildDir/config.toml" *> "$BuildDir/recomp.log"
    if ($LASTEXITCODE) { Get-Content "$BuildDir/recomp.log" -Tail 20; throw 'recompile failed' }
    "generated $($sw.Elapsed)"
}
if ($NoBuild) { return }
$generated = if (Test-Path -LiteralPath "$BuildDir/gen") { "$BuildDir/gen" } else { "$BuildDir/generated" }
Initialize-Tree "$BuildDir/rt" @('--preset','game','-B',"$BuildDir/rt","-DPS2X_GENERATED_DIR=$generated","-DPS2X_OUTPUT_DIR=$BuildDir/bin")

$log = "$BuildDir/$LogName"
$oldCompilerOptions = [Environment]::GetEnvironmentVariable('_CL_','Process')
try {
    if ($CompilerJobs -gt 0) {
        # MSBuild /m alone does not constrain the generated project's /MP children.
        # _CL_ is appended to cl arguments, so this overrides an unbounded /MP.
        [Environment]::SetEnvironmentVariable('_CL_',("$oldCompilerOptions /MP$CompilerJobs").Trim(),'Process')
    }
    & $cmake --build "$BuildDir/rt" --config Release --target $Target -- @msbuildArgs *> $log
    if ($LASTEXITCODE) {
        Select-String -Path $log -Pattern ' error ' | Select-Object -First 15
        Get-Content $log -Tail 30
        throw "Build failed ($LASTEXITCODE); log: $log"
    }
} finally { [Environment]::SetEnvironmentVariable('_CL_',$oldCompilerOptions,'Process') }
"built $($sw.Elapsed)"
Get-Item "$BuildDir/bin/drakan.exe" | Select-Object FullName,Length,LastWriteTime
