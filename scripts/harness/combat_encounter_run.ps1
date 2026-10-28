param(
 [Parameter(Mandatory=$true)][string]$DecompRoot,
 [Parameter(Mandatory=$true)][string]$Script,
 [string]$BinDir='', [string]$SaveRoot='',
 [Parameter(Mandatory=$true)][string]$Name,
 [int]$TimeoutSeconds=120,
 [ValidateSet('off','dlaa','quality','balanced','performance')][string]$DlssMode='off',
 [switch]$NeuralRendering,
 [string]$InputConfig='',
 [ValidateRange(0,600)][int]$AuditEvery=15,
 [switch]$EnableAudio,
 [switch]$AudioDump,
 [ValidateRange(0,5)][int]$SoundLog=0,
 [switch]$NoCapture
)
$ErrorActionPreference='Stop'
$DecompRoot=(Resolve-Path -LiteralPath $DecompRoot).Path
if($Name -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid evidence name'}
if(-not $BinDir){$BinDir="$DecompRoot/build/recomp_combat/encounter/bin"}
$BinDir=(Resolve-Path -LiteralPath $BinDir).Path
$Script=(Resolve-Path -LiteralPath $Script).Path
if(-not $SaveRoot){$SaveRoot="$DecompRoot/iso"}
$SaveRoot=(Resolve-Path -LiteralPath $SaveRoot).Path
$cards="$DecompRoot/build/recomp_combat/encounter/cards/$Name"
if(Test-Path -LiteralPath $cards){throw 'Evidence cards already exist'}
function HashCards([string]$root){
 @(Get-ChildItem "$root/mc0","$root/mc1" -File -Recurse | Sort-Object FullName | Get-FileHash | Select-Object Path,Hash) | ConvertTo-Json
}
$originalBefore=HashCards "$DecompRoot/iso"
$sourceBefore=HashCards $SaveRoot
New-Item -ItemType Directory -Path $cards | Out-Null
Copy-Item -LiteralPath "$DecompRoot/iso/SCES_500.06" -Destination "$cards/SCES_500.06"
foreach($card in 'mc0','mc1'){Copy-Item -LiteralPath "$SaveRoot/$card" -Destination "$cards/$card" -Recurse}
$privateBefore=HashCards $cards
$out=Join-Path (Split-Path -Parent $BinDir) "t/$Name"
$values=@{DRAKAN_ISO=(Join-Path (Split-Path -Parent $DecompRoot) "Drakan - The Ancients' Gates.iso");DRAKAN_ELF="$cards/SCES_500.06";DRAKAN_DLSS=$DlssMode;DRAKAN_DLSS_NEURAL=([int]$NeuralRendering.IsPresent).ToString();GSR_WINSHOT="$out/render";GSR_WINSHOT_AT='1900,2200,2500,2800,3100,3400,3700,4000';PS2X_WATCHDOG='5';DRAKAN_SND_DBG='1'}
if($NoCapture){$values.GSR_WINSHOT='';$values.GSR_WINSHOT_AT=''}
$prior=@{}
try{
 foreach($k in $values.Keys){$prior[$k]=[Environment]::GetEnvironmentVariable($k,'Process');[Environment]::SetEnvironmentVariable($k,$values[$k],'Process')}
 & "$PSScriptRoot/combat_diagnostic_run.ps1" -BinDir $BinDir -Elf "$cards/SCES_500.06" -Script $Script -Name $Name -TimeoutSeconds $TimeoutSeconds -InputConfig $InputConfig -AuditEvery $AuditEvery -EnableAudio:$EnableAudio -AudioDump:$AudioDump -SoundLog $SoundLog
}finally{
 $saveMismatch=$false
 try{
  if(Test-Path -LiteralPath $out){
  Copy-Item -LiteralPath "$BinDir/drakan.log" -Destination "$out/runtime.log" -ErrorAction SilentlyContinue
  $originalBefore | Set-Content "$out/original-saves-before.json"
  $originalAfter=HashCards "$DecompRoot/iso"; $originalAfter | Set-Content "$out/original-saves-after.json"
  $sourceAfter=HashCards $SaveRoot
  $privateAfter=HashCards $cards
  $privateBefore | Set-Content "$out/private-saves-before.json"
  $privateAfter | Set-Content "$out/private-saves-after.json"
  $saveMismatch=($originalBefore -cne $originalAfter) -or ($sourceBefore -cne $sourceAfter) -or ($privateBefore -cne $privateAfter)
  @{source=$SaveRoot;original_saves_unchanged=($originalBefore -ceq $originalAfter);source_saves_unchanged=($sourceBefore -ceq $sourceAfter);private_saves_unchanged=($privateBefore -ceq $privateAfter);environment=$values} | ConvertTo-Json -Depth 4 | Set-Content "$out/encounter-manifest.json"
  }
 }finally{
  foreach($k in $prior.Keys){[Environment]::SetEnvironmentVariable($k,$prior[$k],'Process')}
 }
 if($saveMismatch){throw "Save hash mismatch; inspect $out/encounter-manifest.json (launch environment restored)"}
}
