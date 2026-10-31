param([string]$VivadoBin='E:\vivado2023\Vivado\2023.1\bin',[string]$OutDir)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
if(!$OutDir){$OutDir=Join-Path $sourceRoot 'build_out\rtl_tests'}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$arm=Join-Path $sourceRoot 'hardware\tree\arm5'
$rtl=Join-Path $sourceRoot 'hardware\tree\rtl'
$tests=Join-Path $sourceRoot 'tests'
Push-Location $OutDir
try{
 & (Join-Path $VivadoBin 'xvlog.bat') --sv (Join-Path $arm 'arm5_bridge.sv') (Join-Path $arm 'arm5_controller.v') (Join-Path $arm 'arm_xy_interpolator.v') (Join-Path $arm 'servo_pwm.v') (Join-Path $arm 'servo_pwm_5ch.v') (Join-Path $rtl 'multibox_regs.sv') (Join-Path $tests 'tb_arm5_bridge.sv') (Join-Path $tests 'tb_pwm_fractional.sv') (Join-Path $tests 'tb_arm5_controller.sv') (Join-Path $tests 'tb_xy_interpolator.sv') (Join-Path $tests 'tb_arm5_regs.sv')
 if($LASTEXITCODE -ne 0){throw 'RTL compile failed'}
 foreach($test in @('tb_arm5_controller','tb_arm5_bridge','tb_pwm_fractional','tb_xy_interpolator','tb_arm5_regs')){
  & (Join-Path $VivadoBin 'xelab.bat') $test -mt off -s ('source_'+$test)
  if($LASTEXITCODE -ne 0){throw "RTL elaboration failed: $test"}
  $result=& (Join-Path $VivadoBin 'xsim.bat') ('source_'+$test) -R 2>&1
  $result | Write-Output
  if($LASTEXITCODE -ne 0 -or "$result" -match 'Fatal:|Error:'){throw "RTL simulation failed: $test"}
 }
 Write-Output 'SOURCE_ONLY_RTL_TESTS_PASS tests=5'
}finally{Pop-Location}
