param([string]$OutputDir = "$PSScriptRoot/../../../../build/recomp_stab/object_motion_tests", [string]$RamBegin='', [string]$RamEnd='')
$ErrorActionPreference = 'Stop'
$inc = [IO.Path]::GetFullPath("$PSScriptRoot/../include")
$gsInc = [IO.Path]::GetFullPath("$PSScriptRoot/../../../../runtime/gs/include")
$out = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $out | Out-Null
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
$command = "call `"$vc`" >nul && cd /d `"$out`" && cl /nologo /std:c++20 /EHsc /O2 /W4 /I`"$inc`" /I`"$gsInc`" /Fe:object_motion_tests.exe `"$PSScriptRoot/object_motion_scope_tests.cpp`" > compile.log 2>&1"
cmd /c $command
if ($LASTEXITCODE) { Get-Content "$out/compile.log" -Tail 30; throw 'Object motion test compile failed' }
if($RamBegin -and $RamEnd) { & "$out/object_motion_tests.exe" $RamBegin $RamEnd }
else { & "$out/object_motion_tests.exe" }
exit $LASTEXITCODE
