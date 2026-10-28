param([Parameter(Mandatory=$true)][string]$BridgeHeaderDir,[Parameter(Mandatory=$true)][string]$OutputDir)
$ErrorActionPreference='Stop'
$BridgeHeaderDir=(Resolve-Path -LiteralPath $BridgeHeaderDir).Path
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir=(Resolve-Path -LiteralPath $OutputDir).Path
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC not found'}
$cmd=@"
@echo off
call "$vs/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /MP1 /I"$BridgeHeaderDir" "$PSScriptRoot/combat_quick_bridge_check.cpp" /Fe:"$OutputDir/check.exe"
exit /b %errorlevel%
"@
Set-Content -LiteralPath "$OutputDir/build.cmd" -Value $cmd -Encoding ascii
Push-Location $OutputDir
try {
    & $env:ComSpec /d /c "`"$OutputDir/build.cmd`""
    if($LASTEXITCODE){throw 'Bridge diagnostic build failed'}
    & "$OutputDir/check.exe"
    if($LASTEXITCODE){throw 'Bridge diagnostic failed'}
} finally { Pop-Location }
