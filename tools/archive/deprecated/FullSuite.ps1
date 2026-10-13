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

# tools\test\Run-Tests-CI.ps1
$CISuite = @("Unit", "Integration", "All", "HardwareUnit", "HardwareIntegration",
                 "HardwareMandatory", "HardwareMutation")


foreach ($Suite in $CISuite) { 
 .\tools\test\Run-Tests-CI.ps1 -Suite $Suite -CaptureDbgView -Configuration $Configuration 
} 

$CIFailed = @("test_scripts_consolidate.exe","test_ioctl_abi.exe", "test_tx_timestamp_provenance.exe","test_build_tests_syspaths.ps1")
foreach ($t in $CIFailed) { 
 .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
} 



#$FullSuite = Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue
#foreach ($t in $FullSuite) { 
# .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration $Configuration 
#} 