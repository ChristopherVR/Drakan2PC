# Requires an exclusive GPU handoff. Uses only fresh copies of existing cards.
param(
 [Parameter(Mandatory=$true)][string]$BinDir,
 [Parameter(Mandatory=$true)][string]$Name,
 [string]$DecompRoot=((Resolve-Path "$PSScriptRoot/../..").Path),
 [ValidateSet('neutral','held','tap')][string[]]$Cases=@('tap'),
 [ValidateRange(0,23)][int]$Phase=0,
 [ValidateRange(60,120)][int]$TimeoutSeconds=90
)
$ErrorActionPreference='Stop'
if($Name -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid evidence name'}
if(Get-Process -Name drakan -ErrorAction SilentlyContinue){throw 'Game already running'}
$DecompRoot=(Resolve-Path -LiteralPath $DecompRoot).Path
$BinDir=(Resolve-Path -LiteralPath $BinDir).Path
$root=Join-Path (Split-Path -Parent $BinDir) "recipes/$Name"
if(Test-Path -LiteralPath $root){throw 'Recipe batch already exists'}
New-Item -ItemType Directory -Path $root | Out-Null
$values=@{DRAKAN_PARRY_AUDIT='1';DRAKAN_TRACE_FN=$null;DRAKAN_TRACE_N=$null;
 DRAKAN_SND_DBG='0';DRAKAN_DLSS='off';DRAKAN_DLSS_NEURAL='0';GSR_UPSCALE='off';
 PS2X_GS_SCALE='2';GSR_PROFILE='0';DRAKAN_HLE_LOG='100000';DRAKAN_NR_TEST_SEQ=$null;
 DRAKAN_DLSS_LOG=$null;DRAKAN_DLSS_DEBUG=$null}
$prior=@{}
try{
 foreach($key in $values.Keys){
  $prior[$key]=[Environment]::GetEnvironmentVariable($key,'Process')
  if($null -eq $values[$key]){Remove-Item -LiteralPath "Env:$key" -ErrorAction SilentlyContinue}
  else{[Environment]::SetEnvironmentVariable($key,$values[$key],'Process')}
 }
 foreach($case in $Cases){
  $recipe="$root/$case.txt"
  & python "$PSScriptRoot/combat_parry_recipe.py" $case $recipe --phase $Phase
  if($LASTEXITCODE){throw 'Recipe generation failed'}
  & "$PSScriptRoot/combat_encounter_run.ps1" -DecompRoot $DecompRoot -BinDir $BinDir -Name "$Name-$case" -Script $recipe -SaveRoot "$DecompRoot/build/recomp_combat/encounter/fixture-hostile675" -TimeoutSeconds $TimeoutSeconds -DlssMode off -AuditEvery 1 -NoCapture
  $out=Join-Path (Split-Path -Parent $BinDir) "t/$Name-$case"
  & python "$PSScriptRoot/combat_parry_report.py" $out | Set-Content -LiteralPath "$out/parry-report.json" -Encoding UTF8
  if($LASTEXITCODE){throw "Invalid parry evidence: $out"}
 }
}finally{
 foreach($key in $prior.Keys){
  if($null -eq $prior[$key]){Remove-Item -LiteralPath "Env:$key" -ErrorAction SilentlyContinue}
  else{[Environment]::SetEnvironmentVariable($key,$prior[$key],'Process')}
 }
}
