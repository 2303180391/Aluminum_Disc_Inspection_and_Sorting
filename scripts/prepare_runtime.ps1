param([string]$BuildRoot,[string]$OutDir)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
if(!$BuildRoot){$BuildRoot=Join-Path $sourceRoot 'build_out'}
if(!$OutDir){$OutDir=Join-Path $BuildRoot 'runtime'}
if(Test-Path -LiteralPath $OutDir){throw 'Runtime already exists. Choose a new OutDir'}
$bit=Join-Path $BuildRoot 'hardware\design_1_wrapper_arm5.bit'
$xsa=Join-Path $BuildRoot 'hardware\design_1_wrapper_arm5.xsa'
$elf=Join-Path $BuildRoot 'firmware\yolo_metal_defect_3class.elf'
$map=Join-Path $BuildRoot 'firmware\symbol_addresses.tcl'
foreach($required in @($bit,$xsa,$elf,$map)){if(!(Test-Path -LiteralPath $required)){throw "Build output missing: $required"}}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'runtime') -Destination $OutDir -Recurse
Copy-Item -LiteralPath (Join-Path $sourceRoot 'model\sd_parameters') -Destination (Join-Path $OutDir 'sd_bin_board_v2') -Recurse
New-Item -ItemType Directory -Path (Join-Path $OutDir 'hardware'),(Join-Path $OutDir 'build_rebuilt') -Force | Out-Null
Copy-Item -LiteralPath $bit -Destination (Join-Path $OutDir 'hardware\design_1_wrapper_arm5.bit')
Copy-Item -LiteralPath $elf -Destination (Join-Path $OutDir 'build_rebuilt\yolo_metal_defect_3class.elf')
Copy-Item -LiteralPath $map -Destination (Join-Path $OutDir 'build_rebuilt\symbol_addresses.tcl')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($xsa)
try {
 $entry=@($zip.Entries | Where-Object {$_.FullName -match '(^|/)ps7_init.tcl$'})
 if($entry.Count -ne 1){throw 'XSA does not contain a unique ps7_init.tcl'}
 [IO.Compression.ZipFileExtensions]::ExtractToFile($entry[0],(Join-Path $OutDir 'hardware\ps7_init.tcl'))
}finally{$zip.Dispose()}
$manifest=Get-Content -LiteralPath (Join-Path $OutDir 'release_profile.tcl') -Raw
$manifest+="`nset arm5_release_hashes {`n"
foreach($rel in @('hardware/design_1_wrapper_arm5.bit','build_rebuilt/yolo_metal_defect_3class.elf','hardware/ps7_init.tcl')){
 $manifest+=(' {{{0}}} {1}' -f $rel,(Get-FileHash -LiteralPath (Join-Path $OutDir $rel) -Algorithm SHA256).Hash)+"`n"
}
$manifest+="}`n"
[IO.File]::WriteAllText((Join-Path $OutDir 'release_manifest.tcl'),$manifest,[Text.UTF8Encoding]::new($false))
Write-Output "SOURCE_ONLY_RUNTIME_PREPARED $OutDir; no board connected"
