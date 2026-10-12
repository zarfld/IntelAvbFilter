# .\tools\archive\deprecated\FullSuite.ps1

$Configuration = "Debug"

.\tools\build\Build-Driver.ps1 -Configuration $Configuration    
.\tools\build\Build-Tests.ps1 -Configuration $Configuration    
 

$testExeDir = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"

$FullSuite = Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue

  
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration $Configuration -Action Reinstall -CaptureDbgView 
  
foreach ($t in $FullSuite) { 
 .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
} 