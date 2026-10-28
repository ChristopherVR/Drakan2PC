# Bounded verification on a private copy of a specified candidate.
# Requires the existing hostile675 and bow-item fixtures, plus privately
# extracted SDBs. Coordinate GPU ownership before invoking. No build/deploy.
param(
 [Parameter(Mandatory=$true)][string]$BinDir,
 [Parameter(Mandatory=$true)][string]$Name,
 [string]$Executable='',
 [string]$DecompRoot=(Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
 [string]$SoundRoot='',
 [string]$ExpectedSha256='',
 [Alias('Case')][ValidateSet('all','melee','bow-mkb')][string]$Selection='all',
 [ValidateSet('off','dlaa','quality','balanced','performance')][string]$DlssMode='off',
 [switch]$NeuralRendering,
 [ValidateRange(60,180)][int]$TimeoutSeconds=140
)
$ErrorActionPreference='Stop'
if($Name -notmatch '^[A-Za-z0-9_-]+$'){throw 'Invalid evidence name'}
if(Get-Process -Name drakan -ErrorAction SilentlyContinue){throw 'Game already running; coordinate exclusive GPU use first'}
$DecompRoot=(Resolve-Path -LiteralPath $DecompRoot).Path
$BinDir=(Resolve-Path -LiteralPath $BinDir).Path
if(-not $Executable){$Executable="$BinDir/drakan.exe"}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if(-not $SoundRoot){$SoundRoot="$DecompRoot/build/research/sdb"}
$SoundRoot=(Resolve-Path -LiteralPath $SoundRoot).Path
$sourceHash=(Get-FileHash -LiteralPath $Executable).Hash
if($ExpectedSha256 -and $sourceHash -ne $ExpectedSha256){throw 'Source executable hash mismatch'}
if($NeuralRendering){
 foreach($file in 'sl.interposer.dll','sl.common.dll','sl.dlss_nr.dll','nvngx_dlssnr.dll'){
  if(-not (Test-Path -LiteralPath "$BinDir/nr/$file" -PathType Leaf)){throw "Requested NR bundle missing: $BinDir/nr/$file"}
 }
}
$root="$DecompRoot/build/combat_audio_verification/$Name"
if(Test-Path -LiteralPath $root){throw 'Verification directory already exists'}
foreach($suffix in 'melee','bow-mkb'){
 if(($Selection -eq 'all' -or $Selection -eq $suffix) -and (Test-Path -LiteralPath "$DecompRoot/build/recomp_combat/encounter/cards/$Name-$suffix")){throw 'Private cards already exist'}
}
New-Item -ItemType Directory -Path "$root/bin" | Out-Null
Copy-Item -LiteralPath $Executable -Destination "$root/bin/drakan.exe"
Get-ChildItem -LiteralPath $BinDir -File | Where-Object {$_.Extension -in '.dll','.ini'} | Copy-Item -Destination "$root/bin"
if($NeuralRendering){Copy-Item -LiteralPath "$BinDir/nr" -Destination "$root/bin/nr" -Recurse}
if((Get-FileHash -LiteralPath "$root/bin/drakan.exe").Hash -ne $sourceHash){throw 'Copied executable hash mismatch'}
$bundleHashes=if($NeuralRendering){@(Get-ChildItem -LiteralPath "$root/bin/nr" -Recurse -File | Get-FileHash | Select-Object Path,Hash)}else{@()}
@{source_executable=$Executable;sha256=$sourceHash;dependency_bin=$BinDir;nr_bundle=$bundleHashes} | ConvertTo-Json -Depth 4 | Set-Content "$root/copy-manifest.json" -Encoding UTF8
$prior=@{}
$values=@{DRAKAN_HLE_LOG='100000';DRAKAN_TRACE_FN='314578';DRAKAN_TRACE_N='200';GSR_PROFILE='0';PS2X_GS_SCALE='4';PS2X_GS_PACING='2';GSR_UPSCALE=$DlssMode;DRAKAN_DLSS_LOG=$null;DRAKAN_DLSS_DEBUG=$null;GSR_UPSCALE_TIMING=$null;DRAKAN_NR_TEST_SEQ=$null;DRAKAN_DLSS_NEURAL_DIR="$root/bin/nr";DRAKAN_DLSS_VALIDATE=$(if($NeuralRendering){'1'}else{$null})}
function Report([string]$script,[string[]]$arguments,[string]$destination){
 $scriptPath="$PSScriptRoot/$script"
 & python $scriptPath @arguments | Set-Content -LiteralPath $destination -Encoding UTF8
 if($LASTEXITCODE -ne 0){throw "Evidence check failed: $script; inspect $destination"}
}
try{
 foreach($k in $values.Keys){
  $prior[$k]=[Environment]::GetEnvironmentVariable($k,'Process')
  if($null -eq $values[$k]){Remove-Item -LiteralPath "Env:$k" -ErrorAction SilentlyContinue}
  else{[Environment]::SetEnvironmentVariable($k,$values[$k],'Process')}
 }
 $cases=@(@(
  @{suffix='melee';fixture='fixture-hostile675';recipe='combat_encounter_melee.txt';names='Swish|Rynn Attack'},
  @{suffix='bow-mkb';fixture='fixture-bow-item';recipe='combat_movement_followup.txt';names='^Arrow launch$'}
 ) | Where-Object {$Selection -eq 'all' -or $_.suffix -eq $Selection})
 $results=@()
 foreach($case in $cases){
  $run="$Name-$($case.suffix)"
  & "$PSScriptRoot/combat_encounter_run.ps1" -DecompRoot $DecompRoot -BinDir "$root/bin" -SaveRoot "$DecompRoot/build/recomp_combat/encounter/$($case.fixture)" -Script "$PSScriptRoot/recipes/$($case.recipe)" -Name $run -TimeoutSeconds $TimeoutSeconds -DlssMode $DlssMode -NeuralRendering:$NeuralRendering -EnableAudio -AudioDump -SoundLog 3 -AuditEvery 1 -NoCapture
  $out="$root/t/$run"
  $evaluationEvidence=$null
  if($NeuralRendering){
   # Existing one-shot validation emits only after a successful NR evaluation.
   # In combined mode the native path reaches NR only after successful SR.
   # This avoids enabling per-frame verbose DLSS logs during the audio run.
   $validation=Select-String -LiteralPath "$out/stderr.log" -Pattern '\[dlss-nr-validation\] (\d+)x(\d+) changedRGB=(\d+)/(\d+) nonFiniteRGB=(\d+)' | Select-Object -First 1
   if(-not $validation){throw "No positive NR evaluation validation: $run"}
   $groups=$validation.Matches[0].Groups
   if([long]$groups[3].Value -le 0 -or [long]$groups[5].Value -ne 0){throw "NR output validation failed: $run"}
   $evaluationEvidence=@{nr_success_observed=$true;sr_success_inferred_from_combined_path=($DlssMode -ne 'off');validation=$validation.Line}
   $evaluationEvidence | ConvertTo-Json | Set-Content "$out/evaluation-evidence.json" -Encoding UTF8
  }
  Report 'combat_trace_summary.py' @("$out/input.log") "$out/input-summary.json"
  $inputEvidence=Get-Content -Raw -LiteralPath "$out/input-summary.json" | ConvertFrom-Json
  $expectedCircle=if($case.suffix -eq 'melee'){1}else{0}
  if($inputEvidence.cached_pad_edges.square.down.Count -ne 5 -or $inputEvidence.cached_pad_edges.square.up.Count -ne 5 -or $inputEvidence.cached_pad_edges.circle.down.Count -ne $expectedCircle -or $inputEvidence.cached_pad_edges.circle.up.Count -ne $expectedCircle){throw "Pad delivery differs from retained 77AF recipe: $run"}
  Report 'combat_audio_report.py' @($out,'--sound-root',$SoundRoot,'--names',$case.names) "$out/audio-report.json"
  Report 'combat_ram_report.py' @($out) "$out/ram-report.json"
  $audio=Get-Content -Raw -LiteralPath "$out/audio-report.json" | ConvertFrom-Json
  $saves=Get-Content -Raw -LiteralPath "$out/encounter-manifest.json" | ConvertFrom-Json
  if(-not ($saves.original_saves_unchanged -and $saves.source_saves_unchanged -and $saves.private_saves_unchanged)){throw 'Save hash changed'}
  $outputVoices=@($audio.selected_voices | Where-Object {($_.max_volume_word_lr[0] -gt 0 -or $_.max_volume_word_lr[1] -gt 0) -and $_.mix_after.rms -gt 0})
  if(-not $audio.wasapi_output_observed -or $outputVoices.Count -eq 0){throw "Missing named attack output evidence: $run"}
  if($case.suffix -eq 'melee'){
   $ram=Get-Content -Raw -LiteralPath "$out/ram-report.json" | ConvertFrom-Json
   $start=$ram | Where-Object {$_.ram -eq 'ram_target.ram'}
   $end=$ram | Where-Object {$_.ram -eq 'ram_end.ram'}
   $before=$start.npcs | Where-Object {$_.slot -eq $start.target_slot4da}
   $after=$end.npcs | Where-Object {$_.slot -eq $start.target_slot4da}
   if(-not $start.rynn_available -or -not $end.rynn_available -or $start.equipped_channels.ED0.resource_class -ne 0x1000e -or -not $before -or -not $after -or $before.handle -ne $after.handle -or $after.hp -ge $before.hp){throw 'Native equipped dagger enemy damage not established'}
  }else{
   Report 'combat_inventory_mouse_ram_check.py' @($out) "$out/inventory-check.json"
   Report 'combat_movement_followup_check.py' @($out) "$out/movement-check.json"
  }
  $results+=@{run=$run;output=$out;named_output_voices=$outputVoices.Count;evaluations=$evaluationEvidence;saves=$saves;completion=(Get-Content -Raw -LiteralPath "$out/completion.json" | ConvertFrom-Json)}
 }
 @{sha256=$sourceHash;dlss=$DlssMode;neural=$NeuralRendering.IsPresent;results=$results;limit='Functional evidence only. No subjective parity, successful parry or performance claim.'} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath "$root/verification.json" -Encoding UTF8
 Write-Output "Verification evidence: $root/verification.json"
}finally{
 foreach($k in $prior.Keys){
  if($null -eq $prior[$k]){Remove-Item -LiteralPath "Env:$k" -ErrorAction SilentlyContinue}
  else{[Environment]::SetEnvironmentVariable($k,$prior[$k],'Process')}
 }
}
