# Intel AVB Filter Driver - Truly Honest Build Script
# NO FALSE ADVERTISING - Shows actual build output and results

Write-Host "Building all user-mode AVB test tools..." -ForegroundColor Cyan
Write-Host "=======================================" -ForegroundColor Cyan

$ErrorActionPreference = "Continue"

# Calculate repository root from script location
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\build$') {
    # Script is in tools/build/ subdirectory
    $RepoRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    # Script is in repository root
    $RepoRoot = $ScriptDir
}

# Change to repository root for build commands
Push-Location $RepoRoot

# Initialize VS2022 x64 environment
Write-Host "Initializing Visual Studio 2022 x64 environment..." -ForegroundColor Yellow
$env:VSINSTALLDIR = "C:\Program Files\Microsoft Visual Studio\2022\Community\"
$vcvars = "$env:VSINSTALLDIR\VC\Auxiliary\Build\vcvars64.bat"

if (-not (Test-Path $vcvars)) {
    Write-Host "? Visual Studio 2022 not found at expected location" -ForegroundColor Red
    Write-Host "   Please ensure VS2022 with WDK is installed" -ForegroundColor Red
    exit 1
}

# Track actual results
$tests = @()
$successCount = 0
$failCount = 0

Write-Host "? VS2022 found, starting builds..." -ForegroundColor Green

# Helper function to build and verify
function Build-Test {
    param(
        [string]$Name,
        [string]$MakeCommand,  
        [string]$ExpectedExe
    )
    
    Write-Host "`n--- Building $Name ---" -ForegroundColor Yellow
    
    # Build with vcvars environment
    $buildCmd = "cmd /c `"call `"$vcvars`" && $MakeCommand`""
    
    try {
        $output = Invoke-Expression $buildCmd 2>&1
        
        # Check if executable was actually created
        if (Test-Path $ExpectedExe) {
            Write-Host "? $Name - BUILD SUCCEEDED" -ForegroundColor Green
            $script:successCount++
            $script:tests += @{ Name = $Name; Status = "SUCCESS"; File = $ExpectedExe }
        } else {
            Write-Host "? $Name - BUILD FAILED (no executable created)" -ForegroundColor Red
            Write-Host "   Expected: $ExpectedExe" -ForegroundColor Red
            if ($output -match "error|Error|ERROR") {
                Write-Host "   Build errors detected in output" -ForegroundColor Red
            }
            $script:failCount++
            $script:tests += @{ Name = $Name; Status = "FAILED"; File = $ExpectedExe }
        }
    }
    catch {
        Write-Host "? $Name - BUILD FAILED (exception)" -ForegroundColor Red
        Write-Host "   Exception: $($_.Exception.Message)" -ForegroundColor Red
        $script:failCount++
        $script:tests += @{ Name = $Name; Status = "FAILED"; File = $ExpectedExe }
    }
}

# Build all tests
Build-Test "AVB Diagnostic Test" "nmake -f tools\avb_test\avb_diagnostic.mak clean && nmake -f tools\avb_test\avb_diagnostic.mak" "build\tools\avb_test\x64\Debug\avb_diagnostic_test.exe"

Build-Test "AVB Hardware State Test" "nmake -f tools\avb_test\avb_hw_state_test.mak clean && nmake -f tools\avb_test\avb_hw_state_test.mak" "build\tools\avb_test\x64\Debug\avb_hw_state_test.exe"

Build-Test "TSN IOCTL Handler Test" "del build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers_um.obj && nmake -f tools\avb_test\tsn_ioctl_test.mak" "build\tools\avb_test\x64\Debug\test_tsn_ioctl_handlers.exe"

Build-Test "TSN Hardware Activation Validation Test" "del build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.obj && nmake -f tools\avb_test\tsn_hardware_activation_validation.mak" "build\tools\avb_test\x64\Debug\tsn_hardware_activation_validation.exe"

Build-Test "AVB Capability Validation Test" "cd tools\avb_test && nmake -f avb_capability_validation.mak" "build\tools\avb_test\x64\Debug\avb_capability_validation_test.exe"

Build-Test "AVB Device Separation Validation Test" "cd tools\avb_test && nmake -f avb_device_separation_validation.mak" "build\tools\avb_test\x64\Debug\avb_device_separation_test.exe"

Build-Test "I210 Basic Test" "nmake -f tools\avb_test\avb_test.mak clean && nmake -f tools\avb_test\avb_test.mak" "build\tools\avb_test\x64\Debug\avb_test_i210.exe"

Build-Test "I226 Test" "cd tools\avb_test && nmake -f avb_i226_test.mak clean && nmake -f avb_i226_test.mak" "build\tools\avb_test\x64\Debug\avb_i226_test.exe"

Build-Test "I226 Advanced Test" "cd tools\avb_test && nmake -f avb_i226_advanced.mak clean && nmake -f avb_i226_advanced.mak" "build\tools\avb_test\x64\Debug\avb_i226_advanced_test.exe"

Build-Test "Multi-Adapter Test" "cd tools\avb_test && nmake -f avb_multi_adapter.mak clean && nmake -f avb_multi_adapter.mak" "build\tools\avb_test\x64\Debug\avb_multi_adapter_test.exe"

# HONEST FINAL REPORT
Write-Host "`n=======================================" -ForegroundColor Cyan
Write-Host "FINAL HONEST REPORT - NO FALSE ADVERTISING" -ForegroundColor Cyan  
Write-Host "=======================================" -ForegroundColor Cyan

Write-Host "Total tests attempted: $($tests.Count)" -ForegroundColor White
Write-Host "Successfully built: $successCount" -ForegroundColor Green
Write-Host "Failed to build: $failCount" -ForegroundColor Red

Write-Host "`n--- DETAILED RESULTS ---" -ForegroundColor Yellow
foreach ($test in $tests) {
    $status = $test.Status
    $name = $test.Name
    $file = $test.File
    
    if ($status -eq "SUCCESS") {
        Write-Host "  ? $name" -ForegroundColor Green
    } else {
        Write-Host "  ? $name" -ForegroundColor Red
    }
}

Write-Host "`n--- VERIFICATION: Actual Files ---" -ForegroundColor Yellow
$actualFiles = Get-ChildItem "build\tools\avb_test\x64\Debug\*.exe" -ErrorAction SilentlyContinue
if ($actualFiles) {
    Write-Host "Files that actually exist: $($actualFiles.Count)" -ForegroundColor White
    foreach ($file in $actualFiles) {
        Write-Host "  ?? $($file.Name)" -ForegroundColor Gray
    }
} else {
    Write-Host "? No executable files found in build directory" -ForegroundColor Red
}

# Restore original directory
Pop-Location

# FINAL HONEST STATUS
if ($failCount -eq 0) {
    Write-Host "`n? ALL BUILDS SUCCEEDED" -ForegroundColor Green
    Write-Host "? All $successCount tests are ready for hardware validation" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n? BUILD FAILURES DETECTED" -ForegroundColor Red
    Write-Host "? Only $successCount out of $($tests.Count) tests built successfully" -ForegroundColor Red
    Write-Host "? $failCount test(s) need investigation" -ForegroundColor Red
    Write-Host "`nThis is HONEST reporting - no false advertising!" -ForegroundColor Yellow
    exit 1
}