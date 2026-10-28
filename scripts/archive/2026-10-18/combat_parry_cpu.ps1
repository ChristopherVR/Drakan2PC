param(
 [string]$DecompRoot=((Resolve-Path "$PSScriptRoot/../../..").Path),
 [string]$OutputDir='', [string[]]$RamFiles=@(), [switch]$CompileGlue
)
$ErrorActionPreference='Stop'
$DecompRoot=(Resolve-Path -LiteralPath $DecompRoot).Path
if(-not $OutputDir){$OutputDir="$DecompRoot/build/parry_20261018/cpu"}
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir=(Resolve-Path -LiteralPath $OutputDir).Path
$vswhere="${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $vs){throw 'MSVC not found'}
$header="$DecompRoot/ps2recomp/ps2xRuntime/src/lib"
$commands=@(('cl /nologo /std:c++20 /EHsc /W4 /I"'+$header+'" "'+$PSScriptRoot+'/combat_parry_cpu.cpp" /Fe:"'+$OutputDir+'/check.exe"'), 'if errorlevel 1 exit /b 1')
if($CompileGlue){
 # Syntax-check the actual integrating glue with its existing project's includes.
 # No existing build object/library/executable or shared runtime header is changed.
 [xml]$project=Get-Content -Raw "$DecompRoot/build/recomp_out/rt_o2/ps2xRuntime/ps2_runtime.vcxproj"
 $group=$project.Project.ItemDefinitionGroup | Where-Object {$_.Condition -like '*Release|x64*'} | Select-Object -First 1
 $includes=@($group.ClCompile.AdditionalIncludeDirectories.Split(';') | Where-Object {$_ -notlike '%*'} | ForEach-Object {'/I"'+$_+'"'})
 $defines=@('WIN32','_WINDOWS','NDEBUG','PS2_RUNTIME_LOGS=1','PS2X_HAS_FFMPEG=1','PS2X_HAVE_UI=1','PS2X_HAVE_SPU2=1','PS2X_HAVE_DRAKAN_INPUT=1','PS2X_HAVE_GSR=1' | ForEach-Object {'/D'+$_})
 $commands+=('cl /nologo /std:c++20 /EHsc /Zs '+($includes -join ' ')+' '+($defines -join ' ')+' "'+$header+'/drakan_input_glue.cpp"')
 $commands+='if errorlevel 1 exit /b 1'
}
$batch="@echo off`r`ncall `"$vs/VC/Auxiliary/Build/vcvars64.bat`" >nul`r`nif errorlevel 1 exit /b 1`r`n"+($commands -join "`r`n")+"`r`nexit /b 0`r`n"
Set-Content -LiteralPath "$OutputDir/build.cmd" -Value $batch -Encoding ascii
$resolved=@($RamFiles | ForEach-Object {(Resolve-Path -LiteralPath $_).Path})
Push-Location $OutputDir
try{
 & $env:ComSpec /d /c "`"$OutputDir/build.cmd`""
 if($LASTEXITCODE){throw 'Combat observer compile failed'}
 & "$OutputDir/check.exe" @resolved
 if($LASTEXITCODE){throw 'Combat observer CPU checks failed'}
}finally{Pop-Location}
