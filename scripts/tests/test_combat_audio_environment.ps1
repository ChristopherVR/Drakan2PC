# Asset-free regression: execute the wrapper's actual environment loops only.
# Inspect a synthetic child's C getenv, distinguishing absent from empty.
$ErrorActionPreference='Stop'
$wrapper=Join-Path (Split-Path -Parent $PSScriptRoot) 'harness/combat_audio_verify.ps1'
$tokens=$null;$errors=$null
$ast=[System.Management.Automation.Language.Parser]::ParseFile($wrapper,[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Wrapper syntax errors'}
$loops=$ast.FindAll({param($n) $n -is [System.Management.Automation.Language.ForEachStatementAst]},$true)
$apply=@($loops | Where-Object {$_.Condition.Extent.Text -eq '$values.Keys'})
$restore=@($loops | Where-Object {$_.Condition.Extent.Text -eq '$prior.Keys'})
if($apply.Count -ne 1 -or $restore.Count -ne 1){throw 'Expected one assignment and one restoration loop'}
$applyCode=[scriptblock]::Create($apply[0].Extent.Text)
$restoreCode=[scriptblock]::Create($restore[0].Extent.Text)
$probe=@'
import ctypes,json,sys
crt=ctypes.CDLL('ucrtbase')
crt.getenv.argtypes=[ctypes.c_char_p]
crt.getenv.restype=ctypes.c_char_p
value=crt.getenv(sys.argv[1].encode())
print(json.dumps({'exists':value is not None,'value':None if value is None else value.decode()}))
'@
function AssertChild([AllowNull()][object]$expected,[string]$phase){
 $result=(& python -c $probe $testKey) | ConvertFrom-Json
 if($LASTEXITCODE -ne 0){throw 'Synthetic child failed'}
 $exists=$null -ne $expected
 if($result.exists -ne $exists -or ($exists -and $result.value -cne $expected)){
  throw "$phase child getenv mismatch: expected exists=$exists value=<$expected>, got exists=$($result.exists) value=<$($result.value)>"
 }
}
$testKey='DRAKAN_COMBAT_SYNTHETIC_ENV_TEST'
$saved=[Environment]::GetEnvironmentVariable($testKey,'Process')
$count=0
try{
 foreach($original in @($null,'','original-value')){
  foreach($assignment in @($null,'','replacement-value')){
   if($null -eq $original){Remove-Item -LiteralPath "Env:$testKey" -ErrorAction SilentlyContinue}
   else{[Environment]::SetEnvironmentVariable($testKey,$original,'Process')}
   $prior=@{};$values=@{};$values[$testKey]=$assignment
   try{
    . $applyCode
    AssertChild $assignment 'assignment'
   }finally{
    . $restoreCode
   }
   AssertChild $original 'restoration'
   $count++
  }
 }
 Write-Output "PASS $count environment cases; child C getenv distinguishes absent/empty/nonempty on assignment and restoration"
}finally{
 if($null -eq $saved){Remove-Item -LiteralPath "Env:$testKey" -ErrorAction SilentlyContinue}
 else{[Environment]::SetEnvironmentVariable($testKey,$saved,'Process')}
}
