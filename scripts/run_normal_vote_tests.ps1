param(
 [string]$NativeGcc='D:\Qt\Tools\mingw1310_64\bin\gcc.exe',
 [string]$Tclsh='D:\anaconda3\Library\bin\tclsh.exe',
 [string]$OutDir
)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$testRoot=Join-Path $root 'tests'
$srcRoot=Join-Path $root 'firmware\src'
if(!$OutDir){$OutDir=Join-Path $root 'build_out\normal_vote_tests'}
$OutDir=[IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$oldPath=$env:PATH
$env:PATH=(Split-Path -Parent $NativeGcc)+';'+$oldPath
try {
 $code=Get-Content -LiteralPath (Join-Path $srcRoot 'track_diag.c') -Raw
 if(!$code.Contains('candidates,kept,kept,1U);')){throw 'NVF1 candidate rule missing'}
 $legacy=$code.Replace('candidates,kept,kept,1U);','candidates,kept,raw+rejected+edge_rejected,1U);')
 [IO.File]::WriteAllText((Join-Path $OutDir 'legacy_track_diag_for_test.c'),$legacy,[Text.UTF8Encoding]::new($false))
 $flags=@('-std=c11','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-Wno-int-to-pointer-cast',"-I$testRoot","-I$srcRoot","-I$OutDir")
 $sources=@((Join-Path $testRoot 'test_normal_vote_pipeline.c'),(Join-Path $srcRoot 'tracks.c'),(Join-Path $srcRoot 'multiface.c'),(Join-Path $srcRoot 'sigmoid_exp_table.c'))
 $legacyExe=Join-Path $OutDir 'normal_vote_legacy_test.exe'
 & $NativeGcc @flags '-DLEGACY_NORMAL_VOTE' @sources -o $legacyExe -lm
 if($LASTEXITCODE -ne 0){throw 'Legacy regression compile failed'}
 $legacyOutput=& $legacyExe 2>&1
 if($LASTEXITCODE -eq 0){throw 'Legacy rule unexpectedly passed'}
 $legacyOutput | Write-Output
 Write-Output 'BASELINE_NORMAL_VOTE_FAILURE_REPRODUCED'
 $fixedExe=Join-Path $OutDir 'normal_vote_fixed_test.exe'
 & $NativeGcc @flags @sources -o $fixedExe -lm
 if($LASTEXITCODE -ne 0){throw 'NVF1 pipeline compile failed'}
 & $fixedExe
 if($LASTEXITCODE -ne 0){throw 'NVF1 regression failed'}
 foreach($test in @('test_arm5_flow','test_arm5_uart')){
  $exe=Join-Path $OutDir "$test.exe"
  & $NativeGcc @flags -ffreestanding -fno-builtin (Join-Path $testRoot "$test.c") -o $exe
  if($LASTEXITCODE -ne 0){throw "Compile failed: $test"}
  & $exe
  if($LASTEXITCODE -ne 0){throw "Regression failed: $test"}
  Write-Output "$test PASS"
 }
 foreach($test in @('test_loader_mock.tcl','test_normal_vote_status.tcl')){
  & $Tclsh (Join-Path $testRoot $test)
  if($LASTEXITCODE -ne 0){throw "Tcl mock failed: $test"}
 }
}finally{$env:PATH=$oldPath}
