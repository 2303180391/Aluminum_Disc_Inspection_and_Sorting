param([string]$Vivado='E:\vivado2023\Vivado\2023.1\bin\vivado.bat',[string]$OutDir,[switch]$CreateOnly,[switch]$GenerateOnly)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
if(!$OutDir){$OutDir=Join-Path $sourceRoot 'build_out\hardware'}
$OutDir=[IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$buildModeArg=if($CreateOnly){'--create'}elseif($GenerateOnly){'--generate'}else{'--build'}
Push-Location $OutDir
try {
 & $Vivado -mode batch -source (Join-Path $sourceRoot 'hardware\recreate_project.tcl') -tclargs $OutDir $buildModeArg
 if($LASTEXITCODE -ne 0){throw 'Hardware reconstruction failed. Check vivado.log'}
}finally{Pop-Location}
