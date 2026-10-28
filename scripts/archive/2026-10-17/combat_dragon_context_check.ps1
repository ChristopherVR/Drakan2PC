param([Parameter(Mandatory=$true)][string]$HeaderDir,[Parameter(Mandatory=$true)][string]$SourceRoot,[Parameter(Mandatory=$true)][string]$OutputDir,[string[]]$RamFiles=@())
$ErrorActionPreference='Stop'
$HeaderDir=(Resolve-Path -LiteralPath $HeaderDir).Path
$SourceRoot=(Resolve-Path -LiteralPath $SourceRoot).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir=(Resolve-Path -LiteralPath $OutputDir).Path
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC not found'}
$sources='Config','FrameInput','PadSynth','Script','DrakanInput','MenuMouse','HostInput' | ForEach-Object {'"'+"$SourceRoot/runtime/input/src/$_.cpp"+'"'}
$cmd=@"
@echo off
call "$vs/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /MP1 /I"$HeaderDir" /I"$SourceRoot/runtime/input/include" "$PSScriptRoot/combat_dragon_context_check.cpp" $($sources -join ' ') /Fe:"$OutputDir/check.exe" /link user32.lib gdi32.lib
exit /b %errorlevel%
"@
Set-Content -LiteralPath "$OutputDir/build.cmd" -Value $cmd -Encoding ascii
$resolved=@($RamFiles | ForEach-Object {(Resolve-Path -LiteralPath $_).Path})
Push-Location $OutputDir
try {
 & $env:ComSpec /d /c "`"$OutputDir/build.cmd`""
 if($LASTEXITCODE){throw 'Dragon context diagnostic build failed'}
 & "$OutputDir/check.exe" @resolved
 if($LASTEXITCODE){throw 'Dragon context diagnostic failed'}
} finally {Pop-Location}
