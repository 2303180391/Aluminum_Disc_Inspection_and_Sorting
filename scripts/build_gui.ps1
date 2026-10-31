param([string]$OutDir)
$ErrorActionPreference='Stop'
$sourceRoot=Split-Path $PSScriptRoot -Parent
if(!$OutDir){$OutDir=Join-Path $sourceRoot 'build_out\pc'}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$csc='C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if(!(Test-Path -LiteralPath $csc)){$csc='C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe'}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'pc\tests\演示回放_非实测.log') -Destination (Join-Path $OutDir '演示回放_非实测.log') -Force
foreach($entry in @(@('ParserTests','MonitorModel'),@('ControlTests','ControlModel'))){
 $test=Join-Path $OutDir ($entry[0]+'.exe')
 & $csc /nologo /codepage:65001 /target:exe /warn:4 /warnaserror "/out:$test" (Join-Path $sourceRoot ('pc\src\'+$entry[1]+'.cs')) (Join-Path $sourceRoot ('pc\tests\'+$entry[0]+'.cs'))
 if($LASTEXITCODE -ne 0){throw 'GUI test compile failed'}
 & $test;if($LASTEXITCODE -ne 0){throw 'GUI tests failed'}
}
$app=Join-Path $OutDir 'MetalDefectMonitor.exe'
& $csc /nologo /codepage:65001 /target:winexe /platform:anycpu /warn:4 /warnaserror /optimize+ ("/win32manifest:"+(Join-Path $sourceRoot 'pc\src\app.manifest')) /r:System.dll /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll "/out:$app" (Join-Path $sourceRoot 'pc\src\MonitorModel.cs') (Join-Path $sourceRoot 'pc\src\ControlModel.cs') (Join-Path $sourceRoot 'pc\src\App.cs')
if($LASTEXITCODE -ne 0){throw 'GUI build failed'}
Copy-Item -LiteralPath (Join-Path $sourceRoot 'pc\MetalDefectMonitor.exe.config') -Destination ($app+'.config') -Force
Write-Output 'SOURCE_ONLY_GUI_BUILD_PASS; application not launched'
