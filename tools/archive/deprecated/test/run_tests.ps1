# Intel AVB Filter Driver - Comprehensive Test Suite
# Execute all validation tests for multi-adapter Intel AVB hardware

Write-Host "Intel AVB Filter Driver - Test Suite Execution" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan

# Calculate repository root from script location
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\test$') {
    # Script is in tools/test/ subdirectory
    $RepoRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    # Script is in repository root
    $RepoRoot = $ScriptDir
}

Write-Host "`n? Phase 0: Architecture Compliance Validation" -ForegroundColor Green
Write-Host "Purpose: Verify core architectural requirements are met" -ForegroundColor Gray

Write-Host "`n  ?? Capability Validation Test" -ForegroundColor Magenta
Write-Host "  Purpose: Verify realistic hardware capability reporting (no false advertising)" -ForegroundColor Gray
& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_capability_validation_test.exe"

Write-Host "`n  ?? Device Separation Validation Test" -ForegroundColor Magenta  
Write-Host "  Purpose: Verify clean device separation architecture compliance" -ForegroundColor Gray
& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_device_separation_test.exe"

Write-Host "`n? Phase 1: Basic Hardware Diagnostics" -ForegroundColor Green
Write-Host "Purpose: Comprehensive hardware analysis and troubleshooting" -ForegroundColor Gray
& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_diagnostic_test.exe"

Write-Host "`n? Hardware State Management Test" -ForegroundColor Green  
Write-Host "Purpose: Test hardware state transitions and management" -ForegroundColor Gray
& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_hw_state_test.exe"

Write-Host "`n? Phase 2: TSN IOCTL Handler Verification (Critical Fix Validation)" -ForegroundColor Green
Write-Host "Purpose: Verify TAS/FP/PTM IOCTLs no longer return ERROR_INVALID_FUNCTION" -ForegroundColor Gray
& "$RepoRoot\build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe"

Write-Host "`n? TSN Hardware Activation Validation" -ForegroundColor Green
Write-Host "Purpose: Verify TSN features actually activate at hardware level" -ForegroundColor Gray
& "$RepoRoot\build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.exe"

Write-Host "`n? Phase 3: Multi-Adapter Hardware Testing" -ForegroundColor Yellow
& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe"

Write-Host "`n? Phase 4: I226 Advanced Feature Testing" -ForegroundColor Magenta
& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_i226_test.exe"

& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_i226_advanced_test.exe"

Write-Host "`n? Phase 5: I210 Basic Testing" -ForegroundColor Blue
& "$RepoRoot\build\tools\avb_test\x64\Debug\avb_test_i210.exe"

Write-Host "`n? Phase 6: Specialized Investigation Tests" -ForegroundColor Cyan
Write-Host "Purpose: Deep hardware investigation and TSN validation" -ForegroundColor Gray

Write-Host "`n  ?? ChatGPT5 I226 TAS Validation" -ForegroundColor DarkCyan
& "$RepoRoot\build\tools\avb_test\x64\Debug\chatgpt5_i226_tas_validation.exe"

Write-Host "`n  ?? Corrected I226 TAS Test" -ForegroundColor DarkCyan
& "$RepoRoot\build\tools\avb_test\x64\Debug\corrected_i226_tas_test.exe"

Write-Host "`n  ?? Critical Prerequisites Investigation" -ForegroundColor DarkCyan
& "$RepoRoot\build\tools\avb_test\x64\Debug\critical_prerequisites_investigation.exe"

Write-Host "`n  ?? Enhanced TAS Investigation" -ForegroundColor DarkCyan
& "$RepoRoot\build\tools\avb_test\x64\Debug\enhanced_tas_investigation.exe"

Write-Host "`n  ?? Hardware Investigation Tool" -ForegroundColor DarkCyan
& "$RepoRoot\build\tools\avb_test\x64\Debug\hardware_investigation_tool.exe"

Write-Host "`n? Test Suite Complete" -ForegroundColor Green
Write-Host "Key Success Indicators:" -ForegroundColor Gray
Write-Host "  - Architecture compliance tests pass" -ForegroundColor Gray
Write-Host "  - No false capability advertising" -ForegroundColor Gray  
Write-Host "  - Clean device separation maintained" -ForegroundColor Gray
Write-Host "  - TSN IOCTL handlers no longer return Error 1" -ForegroundColor Gray
Write-Host "  - Hardware activation validation identifies issues" -ForegroundColor Gray
Write-Host "Expected: TAS/FP/PTM IOCTLs return success or hardware-specific errors (not Error 1)" -ForegroundColor Gray

Write-Host "`n?? External Library Tests" -ForegroundColor Cyan
if (Test-Path "$RepoRoot\external\intel_avb\lib\run_tests.ps1") {
    Push-Location "$RepoRoot\external\intel_avb\lib"
    & "$RepoRoot\external\intel_avb\lib\run_tests.ps1"
    Pop-Location
} else {
    Write-Host "External library tests not found - skipping" -ForegroundColor Yellow
}