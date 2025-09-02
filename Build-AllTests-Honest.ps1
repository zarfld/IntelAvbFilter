# Intel AVB Filter Driver - Honest Build Script
# NO FALSE ADVERTISING - Reports actual build results

Write-Host "Building all user-mode AVB test tools..." -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan

$ErrorActionPreference = "Continue"
$VerbosePreference = "Continue"

# Build tracking
$TotalTests = 10
$SuccessfulBuilds = @()
$FailedBuilds = @()

# Helper function to run build with vcvars
function Build-TestWithVcvars {
    param(
        [string]$Name,
        [string]$Command
    )
    
    Write-Host "`n=======================================================" -ForegroundColor Yellow
    Write-Host "Building $Name..." -ForegroundColor Yellow
    Write-Host "=======================================================" -ForegroundColor Yellow
    
    $vcvarsCmd = '"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"'
    $fullCmd = "cmd /c `"$vcvarsCmd && $Command`""
    
    try {
        $result = Invoke-Expression $fullCmd
        
        if ($LASTEXITCODE -eq 0) {
            Write-Host "? $Name SUCCESS" -ForegroundColor Green
            $script:SuccessfulBuilds += $Name
            return $true
        } else {
            Write-Host "? $Name FAILED (Exit Code: $LASTEXITCODE)" -ForegroundColor Red
            $script:FailedBuilds += $Name
            return $false
        }
    }
    catch {
        Write-Host "? $Name FAILED (Exception: $($_.Exception.Message))" -ForegroundColor Red
        $script:FailedBuilds += $Name
        return $false
    }
}

# Build all tests
Build-TestWithVcvars "AVB Diagnostic Test" "nmake -f tools\avb_test\avb_diagnostic.mak clean && nmake -f tools\avb_test\avb_diagnostic.mak"

Build-TestWithVcvars "AVB Hardware State Test" "nmake -f tools\avb_test\avb_hw_state_test.mak clean && nmake -f tools\avb_test\avb_hw_state_test.mak"

Build-TestWithVcvars "TSN IOCTL Handler Test" "del build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers_um.obj && nmake -f tools\avb_test\tsn_ioctl_test.mak"

Build-TestWithVcvars "TSN Hardware Activation Validation Test" "del build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.obj && nmake -f tools\avb_test\tsn_hardware_activation_validation.mak"

Build-TestWithVcvars "AVB Capability Validation Test" "cd tools\avb_test && del ..\..\build\tools\avb_test\x64\Debug\avb_capability_validation_test_um.obj && nmake -f avb_capability_validation.mak"

Build-TestWithVcvars "AVB Device Separation Validation Test" "cd tools\avb_test && del ..\..\build\tools\avb_test\x64\Debug\avb_device_separation_test_um.obj && nmake -f avb_device_separation_validation.mak"

Build-TestWithVcvars "I210 Basic Test" "nmake -f tools\avb_test\avb_test.mak clean && nmake -f tools\avb_test\avb_test.mak"

Build-TestWithVcvars "I226 Test" "cd tools\avb_test && nmake -f avb_i226_test.mak clean && nmake -f avb_i226_test.mak"

Build-TestWithVcvars "I226 Advanced Test" "cd tools\avb_test && nmake -f avb_i226_advanced.mak clean && nmake -f avb_i226_advanced.mak"

Build-TestWithVcvars "Multi-Adapter Test" "cd tools\avb_test && nmake -f avb_multi_adapter.mak clean && nmake -f avb_multi_adapter.mak"

# HONEST REPORTING - Check what actually exists
Write-Host "`n=======================================================" -ForegroundColor Cyan
Write-Host "HONEST BUILD SUMMARY - NO FALSE ADVERTISING" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan

Write-Host "Build attempts: $TotalTests" -ForegroundColor White
Write-Host "Successful builds: $($SuccessfulBuilds.Count)" -ForegroundColor Green
Write-Host "Failed builds: $($FailedBuilds.Count)" -ForegroundColor Red

if ($FailedBuilds.Count -gt 0) {
    Write-Host "`nFailed builds:" -ForegroundColor Red
    foreach ($failed in $FailedBuilds) {
        Write-Host "  ? $failed" -ForegroundColor Red
    }
}

Write-Host "`n--- VERIFICATION: Checking actual files ---" -ForegroundColor Yellow

$TestFiles = @{
    "AVB Diagnostic Test" = "build\tools\avb_test\x64\Debug\avb_diagnostic_test.exe"
    "AVB Hardware State Test" = "build\tools\avb_test\x64\Debug\avb_hw_state_test.exe"  
    "TSN IOCTL Handler Test" = "build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe"
    "TSN Hardware Activation Validation Test" = "build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.exe"
    "AVB Capability Validation Test" = "build\tools\avb_test\x64\Debug\avb_capability_validation_test.exe"
    "AVB Device Separation Validation Test" = "build\tools\avb_test\x64\Debug\avb_device_separation_test.exe"
    "I210 Basic Test" = "build\tools\avb_test\x64\Debug\avb_test_i210.exe"
    "I226 Test" = "build\tools\avb_test\x64\Debug\avb_i226_test.exe"
    "I226 Advanced Test" = "build\tools\avb_test\x64\Debug\avb_i226_advanced_test.exe"
    "Multi-Adapter Test" = "build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe"
}

$ActuallyExist = 0
$ActuallyMissing = 0

foreach ($test in $TestFiles.GetEnumerator()) {
    if (Test-Path $test.Value) {
        Write-Host "  ? $($test.Key) - EXISTS" -ForegroundColor Green
        $ActuallyExist++
    } else {
        Write-Host "  ? $($test.Key) - MISSING" -ForegroundColor Red
        $ActuallyMissing++
    }
}

# FINAL HONEST STATUS
Write-Host "`n=======================================================" -ForegroundColor Cyan
Write-Host "FINAL STATUS - ABSOLUTE HONESTY" -ForegroundColor Cyan
Write-Host "=======================================================" -ForegroundColor Cyan

Write-Host "Files that actually exist: $ActuallyExist/$TotalTests" -ForegroundColor White

if ($ActuallyMissing -eq 0) {
    Write-Host "? ALL TESTS BUILT SUCCESSFULLY" -ForegroundColor Green
    Write-Host "? All tests are ready for hardware validation." -ForegroundColor Green
    exit 0
} else {
    Write-Host "? BUILD FAILURES DETECTED" -ForegroundColor Red
    Write-Host "? $ActuallyExist tests exist, $ActuallyMissing are missing" -ForegroundColor Red
    Write-Host "? Some tests failed to build - investigate errors above" -ForegroundColor Red
    Write-Host "`nThis is HONEST reporting - no false advertising!" -ForegroundColor Yellow
    exit 1
}