param([string]$VitisRoot='E:\vivado2023\Vitis\2023.1',[string]$OutDir)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
if(!$OutDir){$OutDir=Join-Path $sourceRoot 'build_out\firmware'}
$OutDir=[IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$bspWork=Join-Path $OutDir 'bsp'
if(Test-Path -LiteralPath $bspWork){throw 'BSP work directory already exists. Choose a fresh OutDir'}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'firmware\bsp') -Destination $bspWork -Recurse
$bspCpu=Join-Path $bspWork 'ps7_cortexa9_0'
New-Item -ItemType Directory -Path (Join-Path $bspCpu 'lib') -Force | Out-Null
$toolBin=Join-Path $VitisRoot 'gnu\aarch32\nt\gcc-arm-none-eabi\bin'
$savedPath=$env:PATH;$savedRdi=$env:RDI_PLATFORM
try {
 $env:PATH=$toolBin+';'+(Join-Path $VitisRoot 'gnuwin\bin')+';'+$savedPath
 $env:RDI_PLATFORM='win64'
 Push-Location $bspWork
 try { & (Join-Path $VitisRoot 'gnuwin\bin\make.exe') all; if($LASTEXITCODE -ne 0){throw 'BSP source build failed'} }finally{Pop-Location}
 $gcc=Join-Path $toolBin 'arm-none-eabi-gcc.exe'
 $flags=@('-mcpu=cortex-a9','-mfpu=vfpv3','-mfloat-abi=hard','-DPROFILE_TASK_DETAIL=0','-DXPAR_AXI4_LITE_BOX_V1_0_0_BASEADDR=XPAR_MULTIBOX_OVERLAY_0_BASEADDR')
 $objects=@()
 foreach($src in Get-ChildItem -LiteralPath (Join-Path $sourceRoot 'firmware\src') -Filter '*.c'){
  $obj=Join-Path $OutDir ($src.BaseName+'.o')
  & $gcc @flags -Wall -Wextra -Werror -O2 -g3 -c "-I$bspCpu/include" $src.FullName -o $obj
  if($LASTEXITCODE -ne 0){throw "C compile failed: $($src.Name)"};$objects+=$obj
 }
 $elf=Join-Path $OutDir 'yolo_metal_defect_3class.elf'
 & $gcc @flags '-Wl,-build-id=none' "-specs=$sourceRoot/firmware/Xilinx.spec" "-Wl,-T,$sourceRoot/firmware/src/lscript.ld" "-Wl,-Map,$OutDir/firmware.map" "-L$bspCpu/lib" -o $elf @objects '-Wl,--start-group,-lxilffs,-lxil,-lm,-lgcc,-lc,--end-group'
 if($LASTEXITCODE -ne 0){throw 'Firmware link failed'}
 $nm=Join-Path $VitisRoot 'llvm-clang\win64\llvm\bin\llvm-nm.exe'
 $symbols=@{};& $nm -n $elf | ForEach-Object {if($_ -match '^([0-9a-fA-F]+)\s+\S\s+(\S+)$'){$symbols[$Matches[2]]=$Matches[1]}}
 if($LASTEXITCODE -ne 0){throw 'Symbol export failed'}
 $lines=@('# Symbols from this newly compiled ELF')
 foreach($name in @('diag_frame_begin','layers_config','arm_sort_state','arm_sort_result','arm_sort_samples','disc_request','disc_status','disc_sequence','disc_result')){
  if(!$symbols.ContainsKey($name)){throw "Required symbol missing: $name"}
  $lines+=('set build_symbol({0}) 0x{1}' -f $name,$symbols[$name])
 }
 $lines+=('set build_elf_sha256 {0}' -f (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash)
 [IO.File]::WriteAllLines((Join-Path $OutDir 'symbol_addresses.tcl'),$lines,[Text.UTF8Encoding]::new($false))
 Write-Output 'SOURCE_ONLY_FIRMWARE_BUILD_PASS'
}finally{$env:PATH=$savedPath;$env:RDI_PLATFORM=$savedRdi}
