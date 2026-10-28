param(
    [string]$SourceRoot = ((Resolve-Path "$PSScriptRoot/../..").Path),
    [string]$OutputDir = "$env:TEMP/drakan-inventory-mouse-check"
)
$ErrorActionPreference = 'Stop'
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ tools not found' }
$sources = 'Config','FrameInput','PadSynth','Script','DrakanInput','MenuMouse','HostInput' |
    ForEach-Object { '"' + "$SourceRoot/runtime/input/src/$_.cpp" + '"' }
$exe = "$OutputDir/combat-inventory-mouse-check.exe"
$build = @"
@echo off
call "$vs/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I"$SourceRoot/runtime/input/include" "$PSScriptRoot/combat_inventory_mouse_check.cpp" $($sources -join ' ') /Fe:"$exe" /link user32.lib gdi32.lib
exit /b %errorlevel%
"@
$cmdFile = "$OutputDir/build-combat-diagnostic.cmd"
Set-Content -LiteralPath $cmdFile -Value $build -Encoding ascii
Push-Location $OutputDir
try {
    & $env:ComSpec /d /c "`"$cmdFile`""
    if ($LASTEXITCODE) { throw "Diagnostic compile failed: $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE) { throw "Inventory mouse check failed: $LASTEXITCODE" }
} finally { Pop-Location }
