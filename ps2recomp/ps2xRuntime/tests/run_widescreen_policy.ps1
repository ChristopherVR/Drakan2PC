param([string]$OutputDir = "$PSScriptRoot/../../../../build/recomp_stab/widescreen_policy_tests")
$ErrorActionPreference = 'Stop'
$out = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $out | Out-Null
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
$command = "call `"$vc`" >nul && cd /d `"$out`" && cl /nologo /std:c++20 /EHsc /O2 /W4 /Fe:widescreen_policy_tests.exe `"$PSScriptRoot/widescreen_policy_tests.cpp`" > compile.log 2>&1"
cmd /c $command
if ($LASTEXITCODE) { Get-Content "$out/compile.log" -Tail 20; throw 'Widescreen policy test compile failed' }
& "$out/widescreen_policy_tests.exe"
exit $LASTEXITCODE
