param([string]$OutputDir = "$PSScriptRoot/../../../../build/recomp_stab/object_motion_tests")
$ErrorActionPreference = 'Stop'
$inc = [IO.Path]::GetFullPath("$PSScriptRoot/../include")
$out = [IO.Path]::GetFullPath($OutputDir)
New-Item -ItemType Directory -Force $out | Out-Null
$vc = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Auxiliary/Build/vcvars64.bat'
# Intentionally no outer GS include path: public memory/worker headers must not
# depend on the ObjectMotionScope packet, tracker or capture implementation.
$command = "call `"$vc`" >nul && cd /d `"$out`" && cl /nologo /std:c++20 /EHsc /O2 /W4 /showIncludes /I`"$inc`" /Fe:object_motion_headers.exe `"$PSScriptRoot/object_motion_header_tests.cpp`" > header_compile.log 2>&1"
cmd /c $command
if ($LASTEXITCODE) { Get-Content "$out/header_compile.log" -Tail 20; throw 'Public header compile failed' }
$unwanted = Select-String -Path "$out/header_compile.log" -SimpleMatch -Pattern 'object_motion_tracker.h','object_motion_capture.h','object_motion_scope.h','scene_guide_camera.h'
if($unwanted) { $unwanted; throw 'Public header still imports capture implementation or packet' }
& "$out/object_motion_headers.exe"
if($LASTEXITCODE) { throw 'Incomplete Job ownership check failed' }
Write-Output 'PASS: incomplete Job ownership; public headers exclude tracker/capture/packet dependencies'
