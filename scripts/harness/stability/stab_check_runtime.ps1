param()
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path "$PSScriptRoot/../../..").Path
$src = "$root/ps2recomp"
$out = "$root/build/recomp_stab/runtime_check"
New-Item -ItemType Directory -Force $out | Out-Null
# Compile the actual current function, with a 64-byte fake guest window and canaries.
# Pointer resolution is mocked here; full address aliasing is exercised by difftest.
$libc = Get-Content "$src/ps2xRuntime/src/lib/Kernel/Stubs/LibC.cpp" -Raw
$start = $libc.IndexOf('    void strncat(')
$end = $libc.IndexOf('    void strchr(', $start)
if ($start -lt 0 -or $end -lt 0) { throw 'Cannot extract current strncat implementation' }
Set-Content -LiteralPath "$out/stab_string_impl.h" -Value $libc.Substring($start, $end - $start) -Encoding utf8
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vsPath = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Visual Studio C++ tools not found' }
$vc = "$vsPath/VC/Auxiliary/Build/vcvars64.bat"
$cmd = "call `"$vc`" >nul && cd /d `"$out`" && cl /nologo /std:c++20 /EHsc /O2 /Zc:preprocessor- /I`"$out`" /I`"$src/ps2xRuntime/include`" /I`"$src/ps2xIOP/include`" /I`"$src/ps2xRuntime/src/lib/Kernel`" /Fe:runtime_safety.exe `"$src/ps2xRecomp/difftest/runtime_safety.cpp`" > cl.log 2>&1"
cmd /c $cmd
if ($LASTEXITCODE) { Get-Content "$out/cl.log" -Tail 30; throw 'Runtime safety check build failed' }
& "$out/runtime_safety.exe"
exit $LASTEXITCODE
