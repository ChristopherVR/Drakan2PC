param([string]$OutputDir = (Join-Path $env:TEMP ('drakan-viewport-policy-' + [guid]::NewGuid().ToString('N'))))
$ErrorActionPreference = 'Stop'
$out = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force -Path $out | Out-Null
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
$command = "call `"$vc`" >nul && cd /d `"$out`" && cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /Fe:inventory_viewport_policy_tests.exe `"$PSScriptRoot/inventory_viewport_policy_tests.cpp`" > compile.log 2>&1"
cmd /c $command
if ($LASTEXITCODE) { Get-Content "$out/compile.log" -Tail 20; throw 'Inventory viewport CPU test compile failed' }
& "$out/inventory_viewport_policy_tests.exe"
exit $LASTEXITCODE
