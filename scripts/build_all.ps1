param([string]$Vivado='E:\vivado2023\Vivado\2023.1\bin\vivado.bat',[string]$VitisRoot='E:\vivado2023\Vitis\2023.1',[string]$BuildRoot)
$ErrorActionPreference='Stop'
if(!$BuildRoot){$BuildRoot=Join-Path (Split-Path $PSScriptRoot -Parent) 'build_out'}
& (Join-Path $PSScriptRoot 'build_hardware.ps1') -Vivado $Vivado -OutDir (Join-Path $BuildRoot 'hardware')
& (Join-Path $PSScriptRoot 'build_firmware.ps1') -VitisRoot $VitisRoot -OutDir (Join-Path $BuildRoot 'firmware')
& (Join-Path $PSScriptRoot 'build_gui.ps1') -OutDir (Join-Path $BuildRoot 'pc')
& (Join-Path $PSScriptRoot 'prepare_runtime.ps1') -BuildRoot $BuildRoot
Write-Output 'SOURCE_ONLY_ALL_BUILT; hardware validation still required'
