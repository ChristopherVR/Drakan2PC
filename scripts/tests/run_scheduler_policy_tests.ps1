param(
    [string]$OutputDir = (Join-Path ([System.IO.Path]::GetTempPath()) ('drakan-scheduler-policy-' + [guid]::NewGuid().ToString('N'))),
    [string]$Python = 'python'
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

& $Python "$PSScriptRoot/generate_scheduler_policy_fixture.py" --output "$OutputDir/scheduler_policy_fixture.h"
if ($LASTEXITCODE) { throw "Scheduler fixture generation failed: $LASTEXITCODE" }

$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'MSVC C++ tools not found' }
$exe = "$OutputDir/scheduler_policy_test.exe"
$build = @"
@echo off
call "$vs/VC/Auxiliary/Build/vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 /I"$OutputDir" "$PSScriptRoot/scheduler_policy_test.cpp" /Fe:"$exe"
exit /b %errorlevel%
"@
$cmdFile = "$OutputDir/build-scheduler-policy-test.cmd"
Set-Content -LiteralPath $cmdFile -Value $build -Encoding ascii
Push-Location $OutputDir
try {
    & $env:ComSpec /d /c "`"$cmdFile`""
    if ($LASTEXITCODE) { throw "Scheduler policy test compile failed: $LASTEXITCODE" }
    & $exe
    if ($LASTEXITCODE) { throw "Scheduler policy tests failed: $LASTEXITCODE" }
} finally { Pop-Location }
