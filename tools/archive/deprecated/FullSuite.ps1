# .\tools\archive\deprecated\FullSuite.ps1 *>&1 | Tee-Object -FilePath ".\logs\FullSuite_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"

$Configuration = "Debug"

.\tools\build\Build-Driver.ps1 -Configuration $Configuration    
.\tools\build\Build-Tests.ps1 -Configuration $Configuration    
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration $Configuration -Action Reinstall -CaptureDbgView 

$scriptDir = $PSScriptRoot  #  .\tools\archive\deprecated\
$ArchiveDir = Split-Path $scriptDir -Parent  # .\tools\archive\
$toolsDir = Split-Path $ArchiveDir -Parent  # .\tools\
$repoRoot = Split-Path $toolsDir -Parent  # repository root
$testExeDir = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"



  





$FullSuite = Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue
foreach ($t in $FullSuite) { 
 .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
} 