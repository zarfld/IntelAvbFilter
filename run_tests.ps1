# Intel AVB Filter Driver - Comprehensive Test Suite
# Execute all validation tests for multi-adapter Intel AVB hardware

Write-Host "Intel AVB Filter Driver - Test Suite Execution" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan

Write-Host "`n?? Phase 1: Basic Hardware Diagnostics" -ForegroundColor Green
Write-Host "Purpose: Comprehensive hardware analysis and troubleshooting" -ForegroundColor Gray
.\build\tools\avb_test\x64\Debug\avb_diagnostic_test.exe

Write-Host "`n?? Hardware State Management Test" -ForegroundColor Green  
Write-Host "Purpose: Test hardware state transitions and management" -ForegroundColor Gray
.\build\tools\avb_test\x64\Debug\avb_hw_state_test.exe

Write-Host "`n?? Phase 2: TSN IOCTL Handler Verification (NEW - Critical Fix Validation)" -ForegroundColor Green
Write-Host "Purpose: Verify TAS/FP/PTM IOCTLs no longer return ERROR_INVALID_FUNCTION" -ForegroundColor Gray
.\build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe

Write-Host "`n?? Phase 3: Multi-Adapter Hardware Testing" -ForegroundColor Yellow
.\build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe

Write-Host "`n?? Phase 4: I226 Advanced Feature Testing" -ForegroundColor Magenta
.\build\tools\avb_test\x64\Debug\avb_i226_test.exe
# Available I226 test options:
# .\build\tools\avb_test\x64\Debug\avb_i226_test.exe info      - Show I226 device information
# .\build\tools\avb_test\x64\Debug\avb_i226_test.exe ptp      - Test I226 PTP timing verification
# .\build\tools\avb_test\x64\Debug\avb_i226_test.exe tsn      - Test I226 TSN register access
# .\build\tools\avb_test\x64\Debug\avb_i226_test.exe advanced - Test I226 advanced TSN features
# .\build\tools\avb_test\x64\Debug\avb_i226_test.exe all      - Run all tests (default)

.\build\tools\avb_test\x64\Debug\avb_i226_advanced_test.exe

Write-Host "`n? Phase 5: I210 PTP Testing (Known Issues)" -ForegroundColor Red
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-unlock
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-bringup
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-probe
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe selftest
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe snapshot
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe snapshot-ssot
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-enable-ssot
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ptp-probe
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe info
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe caps
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ts-get
.\build\tools\avb_test\x64\Debug\avb_test_i210.exe ts-set-now

Write-Host "`n?? TSN Hardware Activation Validation (Enhanced Implementation Verification)" -ForegroundColor Green
Write-Host "Purpose: Verify TSN features actually activate at hardware level (not just IOCTL success)" -ForegroundColor Gray
.\build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.exe

Write-Host "`n? Test Suite Complete" -ForegroundColor Green
Write-Host "Key Success Indicator: TSN IOCTL handlers should no longer return Error 1" -ForegroundColor Gray
Write-Host "Expected: TAS/FP/PTM IOCTLs return success or hardware-specific errors (not Error 1)" -ForegroundColor Gray
.\build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe
cd .\external\intel_avb\lib
.\run_tests.ps1
cd ../../../