<#
.SYNOPSIS
    Build all test executables for Intel AVB Filter Driver

.DESCRIPTION
    Canonical test build script. Builds user-mode test tools using Visual Studio compiler.
    Replaces: build_i226_test.bat, Build-AllTests-Honest.ps1, Build-AllTests-TrulyHonest.ps1

.PARAMETER Configuration
    Build configuration (Debug or Release)

.PARAMETER TestName
    Optional: Build specific test only. If omitted, builds all tests.

.PARAMETER ShowDetails
    Show detailed build output

.EXAMPLE
    .\Build-Tests.ps1
    Build all tests in Debug configuration

.EXAMPLE
    .\Build-Tests.ps1 -Configuration Release -TestName avb_test_i226
    Build only avb_test_i226.exe in Release mode

.NOTES
    Requires: Visual Studio 2022 with C++ Build Tools
    Issue: #27 (REQ-NF-SCRIPTS-001: Canonical script consolidation)
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [Parameter(Mandatory=$false)]
    [string]$TestName,
    
    [Parameter(Mandatory=$false)]
    [switch]$ShowDetails,
    
    [Parameter(Mandatory=$false)]
    [switch]$BuildDiagnosticTools  # Moved from Run-Tests.ps1 -CompileDiagnostics (separation of concerns)
)

$ErrorActionPreference = "Stop"

# Calculate repository root from script location
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if ($ScriptDir -match '\\tools\\build$') {
    $RepoRoot = Split-Path (Split-Path $ScriptDir -Parent) -Parent
} else {
    $RepoRoot = $ScriptDir
}

Write-Host "Intel AVB Filter - Build Tests" -ForegroundColor Cyan
Write-Host "==============================" -ForegroundColor Cyan
Write-Host "Configuration: $Configuration" -ForegroundColor Yellow
if ($TestName) {
    Write-Host "Test: $TestName (single)" -ForegroundColor Yellow
} else {
    Write-Host "Test: ALL tests" -ForegroundColor Yellow
}
Write-Host ""

# =============================================================================
# Build Diagnostic Tools (Moved from Run-Tests.ps1 -CompileDiagnostics)
# Separation of Concerns: BUILD logic belongs in Build-Tests.ps1
# =============================================================================
if ($BuildDiagnosticTools) {
    Write-Host "=> Compiling Diagnostic Tools" -ForegroundColor Cyan
    
    # Check for Visual Studio compiler
    $ClPath = (Get-Command cl.exe -ErrorAction SilentlyContinue)
    if (-not $ClPath) {
        Write-Host "   [INFO] Visual Studio compiler (cl.exe) not found in PATH" -ForegroundColor Yellow
        Write-Host "   Attempting to load Visual Studio environment..." -ForegroundColor Yellow
        
        # Try to load VS environment
        $VsPath = 'C:\Program Files\Microsoft Visual Studio\2022\Community'
        $DevShellDll = Join-Path $VsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
        
        if (Test-Path $DevShellDll) {
            try {
                Import-Module $DevShellDll -ErrorAction Stop
                Enter-VsDevShell -VsInstallPath $VsPath -SkipAutomaticLocation -ErrorAction Stop
                Write-Host "   [OK] Visual Studio environment loaded" -ForegroundColor Green
            } catch {
                Write-Host "   [FAIL] Failed to load Visual Studio environment: $_" -ForegroundColor Red
                exit 1
            }
        } else {
            Write-Host "   [FAIL] Visual Studio not found at: $VsPath" -ForegroundColor Red
            Write-Host "   Please install Visual Studio 2022 Community or update the path in this script." -ForegroundColor Yellow
            exit 1
        }
        
        # Verify cl.exe is now available
        $ClPath = (Get-Command cl.exe -ErrorAction SilentlyContinue)
        if (-not $ClPath) {
            Write-Host "   [FAIL] cl.exe still not found after loading VS environment" -ForegroundColor Red
            exit 1
        }
    }
    
    # Compile intel_avb_diagnostics.c
    $DiagSource = Join-Path $RepoRoot "tests\diagnostic\intel_avb_diagnostics.c"
    if (Test-Path $DiagSource) {
        Write-Host "   Building intel_avb_diagnostics.exe..." -ForegroundColor Yellow
        $Platform = "x64"
        $DiagOutputDir = Join-Path $RepoRoot "build\tools\avb_test\$Platform\$Configuration"
        if (-not (Test-Path $DiagOutputDir)) {
            New-Item -ItemType Directory -Path $DiagOutputDir -Force | Out-Null
        }
        $DiagExe = Join-Path $DiagOutputDir "intel_avb_diagnostics.exe"
        & cl.exe $DiagSource /Fe:$DiagExe /TC /link advapi32.lib setupapi.lib cfgmgr32.lib iphlpapi.lib
        if (Test-Path $DiagExe) {
            Write-Host "   [OK] intel_avb_diagnostics.exe compiled to $DiagOutputDir" -ForegroundColor Green
        } else {
            Write-Host "   [FAIL] intel_avb_diagnostics.exe compilation failed" -ForegroundColor Red
        }
    } else {
        Write-Host "   [SKIP] intel_avb_diagnostics.c not found at $DiagSource" -ForegroundColor Yellow
    }
    
    # Compile debug_device_interface.c
    $DevSource = Join-Path $RepoRoot "debug_device_interface.c"
    if (Test-Path $DevSource) {
        Write-Host "   Building debug_device_interface.exe..." -ForegroundColor Yellow
        $Platform = "x64"
        $DevOutputDir = Join-Path $RepoRoot "build\tools\avb_test\$Platform\$Configuration"
        if (-not (Test-Path $DevOutputDir)) {
            New-Item -ItemType Directory -Path $DevOutputDir -Force | Out-Null
        }
        $DevExe = Join-Path $DevOutputDir "device_interface_test.exe"
        & cl.exe $DevSource /Fe:$DevExe /link advapi32.lib
        if (Test-Path $DevExe) {
            Write-Host "   [OK] device_interface_test.exe compiled to $DevOutputDir" -ForegroundColor Green
        } else {
            Write-Host "   [FAIL] device_interface_test.exe compilation failed" -ForegroundColor Red
        }
    } else {
        Write-Host "   [SKIP] debug_device_interface.c not found at $DevSource" -ForegroundColor Yellow
    }
    
    # Check system capabilities
    Write-Host ""
    Write-Host "=> Checking System Capabilities" -ForegroundColor Cyan
    
    # Check Hyper-V
    Write-Host "   Checking Hyper-V status..." -ForegroundColor Yellow
    try {
        $HyperVFeature = dism /online /get-featureinfo /featurename:Microsoft-Hyper-V 2>&1 | Select-String "State : Enabled"
        if ($HyperVFeature) {
            Write-Host "   [OK] Hyper-V is enabled" -ForegroundColor Green
        } else {
            Write-Host "   [INFO] Hyper-V is not enabled" -ForegroundColor Yellow
        }
    } catch {
        Write-Host "   [INFO] Could not check Hyper-V status" -ForegroundColor Yellow
    }
    
    # Check for SignTool (WDK)
    $SignTool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($SignTool) {
        Write-Host "   [OK] SignTool found: $($SignTool.Source)" -ForegroundColor Green
    } else {
        Write-Host "   [INFO] SignTool (WDK) not found in PATH" -ForegroundColor Yellow
    }
    
    # Check for Inf2Cat (WDK)
    $Inf2Cat = Get-Command inf2cat.exe -ErrorAction SilentlyContinue
    if ($Inf2Cat) {
        Write-Host "   [OK] Inf2Cat found: $($Inf2Cat.Source)" -ForegroundColor Green
    } else {
        Write-Host "   [INFO] Inf2Cat (WDK) not found in PATH" -ForegroundColor Yellow
    }
    
    Write-Host ""
    Write-Host "Diagnostic tools build complete." -ForegroundColor Cyan
    Write-Host ""
}

# Change to repository root
Push-Location $RepoRoot

# Test definitions
# Type: "cl" = cl.exe build, "nmake" = nmake build with .mak file
$AllTests = @(
    # AVB Integration Tests (cl.exe)
    @{
        Name = "avb_test_i226"
        Type = "cl"
        Source = "tests/integration/avb/avb_test_um.c"
        Output = "avb_test_i226.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "I226 AVB Test Tool (User-Mode)"
    },
    @{
        Name = "avb_test_i210"
        Type = "cl"
        Source = "tests/integration/avb/avb_test_um.c"
        Output = "avb_test_i210_um.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "I210 AVB Test Tool (User-Mode)"
    },
    # AVB Integration Tests (nmake)
    @{
        Name = "avb_test_project"
        Type = "nmake"
        Makefile = "tests/integration/avb/avb_test.mak"
        Description = "AVB Test (nmake project)"
    },
    @{
        Name = "avb_capability_validation"
        Type = "nmake"
        Makefile = "tests/integration/avb/avb_capability_validation.mak"
        Description = "AVB Capability Validation (nmake)"
    },
    
    # PTP Integration Tests (cl.exe)
    @{
        Name = "ptp_clock_control_test"
        Type = "cl"
        Source = "tests/integration/ptp/ptp_clock_control_test.c"
        Output = "ptp_clock_control_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "PTP Clock Control Test"
    },
    @{
        Name = "ptp_clock_control_production"
        Type = "cl"
        Source = "tests/integration/ptp/ptp_clock_control_production_test.c"
        Output = "ptp_clock_control_production_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "PTP Clock Production Test"
    },
    
    # TSN Integration Tests (cl.exe)
    @{
        Name = "tsauxc_toggle_test"
        Type = "cl"
        Source = "tests/integration/tsn/tsauxc_toggle_test.c"
        Output = "tsauxc_toggle_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "TSAUXC Register Toggle Test"
    },
    
    # Verification Tests - Register Constants (compile-time C_ASSERT)
    @{
        Name = "test_register_constants"
        Type = "cl"
        Source = "tests/verification/regs/test_register_constants.c"
        Output = "test_register_constants.obj"
        CompileOnly = $true
        Includes = "-I intel-ethernet-regs/gen -I C:\PROGRA~2\WI3CF2~1\10\Include\100226~1.0\km"
        CompilerFlags = "/c /kernel /WX /D_AMD64_ /DAMD64"  # Kernel mode requires architecture defines
        Description = "Verification: Register Constants (TEST-REGS-003)"
    },
    
    # TSN Integration Tests (nmake)
    @{
        Name = "tsn_ioctl_test"
        Type = "nmake"
        Makefile = "tests/integration/tsn/tsn_ioctl_test.mak"
        Description = "TSN IOCTL Test (nmake)"
    },
    @{
        Name = "tsn_hardware_activation"
        Type = "nmake"
        Makefile = "tests/integration/tsn/tsn_hardware_activation_validation.mak"
        Description = "TSN Hardware Activation (nmake)"
    },
    
    # Multi-Adapter Tests (nmake)
    @{
        Name = "avb_multi_adapter"
        Type = "nmake"
        Makefile = "tests/integration/multi_adapter/avb_multi_adapter.mak"
        Description = "Multi-Adapter Test (nmake)"
    },
    @{
        Name = "avb_device_separation"
        Type = "nmake"
        Makefile = "tests/integration/multi_adapter/avb_device_separation_validation.mak"
        Description = "Device Separation Test (nmake)"
    },
    
    # E2E Tests (cl.exe)
    @{
        Name = "comprehensive_ioctl_test"
        Type = "cl"
        Source = "tests/e2e/comprehensive_ioctl_test.c"
        Output = "comprehensive_ioctl_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Comprehensive IOCTL End-to-End Test"
    },
    
    # Diagnostic Tests (cl.exe)
    @{
        Name = "quick_diagnostics"
        Type = "cl"
        Source = "tests/diagnostic/quick_diagnostics.c"
        Output = "quick_diagnostics.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Libs = "advapi32.lib"
        Description = "Quick Diagnostic Tool"
    },
    @{
        Name = "intel_avb_diagnostics"
        Type = "cl"
        Source = "tests/diagnostic/intel_avb_diagnostics.c"
        Output = "intel_avb_diagnostics.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Intel AVB Comprehensive Diagnostics"
    },
    @{
        Name = "device_open_test"
        Type = "cl"
        Source = "tests/diagnostic/device_open_test.c"
        Output = "device_open_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Device Open Diagnostic"
    },
    # Diagnostic Tests (nmake)
    @{
        Name = "avb_diagnostic"
        Type = "nmake"
        Makefile = "tests/diagnostic/avb_diagnostic.mak"
        Description = "AVB Diagnostic (nmake)"
    },
    @{
        Name = "avb_hw_state_test"
        Type = "nmake"
        Makefile = "tests/diagnostic/avb_hw_state_test.mak"
        Description = "HW State Test (nmake)"
    },
    @{
        Name = "hardware_investigation"
        Type = "nmake"
        Makefile = "tests/diagnostic/hardware_investigation.mak"
        Description = "Hardware Investigation (nmake)"
    },
    @{
        Name = "critical_prerequisites"
        Type = "nmake"
        Makefile = "tests/diagnostic/critical_prerequisites_investigation.mak"
        Description = "Prerequisites Investigation (nmake)"
    },
    @{
        Name = "enhanced_tas_investigation"
        Type = "nmake"
        Makefile = "tests/diagnostic/enhanced_tas_investigation.mak"
        Description = "TAS Investigation (nmake)"
    },
    
    # Device-Specific Tests (cl.exe)
    @{
        Name = "avb_test_i210_device"
        Type = "cl"
        Source = "tests/device_specific/i210/avb_test_i210_um.c"
        Output = "avb_test_i210.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "I210 Device-Specific Test"
    },
    @{
        Name = "avb_test_i219"
        Type = "cl"
        Source = "tests/device_specific/i219/avb_test_i219.c"
        Output = "avb_test_i219.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "I219 Device-Specific Test"
    },
    # Device-Specific Tests (nmake)
    @{
        Name = "avb_i226_test"
        Type = "nmake"
        Makefile = "tests/device_specific/i226/avb_i226_test.mak"
        Description = "i226 AVB Test (nmake)"
    },
    @{
        Name = "avb_i226_advanced"
        Type = "nmake"
        Makefile = "tests/device_specific/i226/avb_i226_advanced.mak"
        Description = "i226 Advanced Test (nmake)"
    },
    @{
        Name = "chatgpt5_i226_tas"
        Type = "nmake"
        Makefile = "tests/device_specific/i226/chatgpt5_i226_tas_validation.mak"
        Description = "i226 TAS Validation (nmake)"
    },
    @{
        Name = "corrected_i226_tas"
        Type = "nmake"
        Makefile = "tests/device_specific/i226/corrected_i226_tas_test.mak"
        Description = "i226 TAS Test (nmake)"
    },
    
    # Unit Tests - IOCTL (cl.exe)
    @{
        Name = "test_ioctl_simple"
        Type = "cl"
        Source = "tests/unit/ioctl/test_ioctl_simple.c"
        Output = "test_ioctl_simple.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: Simple IOCTL Test"
    },
    @{
        Name = "test_ioctl_routing"
        Type = "cl"
        Source = "tests/unit/ioctl/test_ioctl_routing.c"
        Output = "test_ioctl_routing.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Libs = "advapi32.lib"
        Description = "Unit: IOCTL Routing Test"
    },
    @{
        Name = "test_ioctl_trace"
        Type = "cl"
        Source = "tests/unit/ioctl/test_ioctl_trace.c"
        Output = "test_ioctl_trace.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: IOCTL Trace Test"
    },
    @{
        Name = "test_minimal_ioctl"
        Type = "cl"
        Source = "tests/unit/ioctl/test_minimal_ioctl.c"
        Output = "test_minimal_ioctl.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: Minimal IOCTL Test"
    },
    # DISABLED: test_tsn_ioctl_handlers - KM placeholder, needs UM implementation
    # TODO: Create standalone UM version or use test_tsn_ioctl_handlers_um
    @{
        Name = "test_tsn_ioctl_handlers"
        Type = "cl"
        Source = "tests/unit/ioctl/test_tsn_ioctl_handlers.c"
        Output = "test_tsn_ioctl_handlers.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: TSN IOCTL Handlers Test"
        Disabled = $true  # KM placeholder only, use test_tsn_ioctl_handlers_um instead
    },
    
    # Unit Tests - Hardware (cl.exe)
    # DISABLED: test_hw_state - uses outdated AVB_DEVICE_INFO_REQUEST fields
    # TODO: Update to use device_info string parsing instead of direct struct fields
    @{
        Name = "test_hw_state"
        Type = "cl"
        Source = "tests/unit/hardware/test_hw_state.c"
        Output = "test_hw_state.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: Hardware State Test"
        Disabled = $true  # Needs update: AVB_DEVICE_INFO_REQUEST changed
    },
    @{
        Name = "test_clock_config"
        Type = "cl"
        Source = "tests/unit/hardware/test_clock_config.c"
        Output = "test_clock_config.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: Clock Config Test"
    },
    @{
        Name = "test_clock_working"
        Type = "cl"
        Source = "tests/unit/hardware/test_clock_working.c"
        Output = "test_clock_working.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: Clock Working Test"
    },
    @{
        Name = "test_direct_clock"
        Type = "cl"
        Source = "tests/unit/hardware/test_direct_clock.c"
        Output = "test_direct_clock.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: Direct Clock Access Test"
    },
    
    # Unit Tests - Hardware Abstraction Layer (HAL) (cl.exe)
    @{
        Name = "test_hal_unit"
        Type = "cl"
        Source = "tests/unit/hal/test_hal_unit.c"
        Output = "test_hal_unit.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: HAL Unit Tests (TEST-PORTABILITY-HAL-001, Issue #308)"
    },
    @{
        Name = "test_hal_errors"
        Type = "cl"
        Source = "tests/unit/hal/test_hal_errors.c"
        Output = "test_hal_errors.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: HAL Error Scenario Tests (TEST-PORTABILITY-HAL-002, Issue #309)"
    },
    @{
        Name = "test_hal_performance"
        Type = "cl"
        Source = "tests/unit/hal/test_hal_performance.c"
        Output = "test_hal_performance.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Unit: HAL Performance Metrics Tests (TEST-PORTABILITY-HAL-003, Issue #310)"
    },
    
    # Integration Tests - PTP (additional, cl.exe)
    @{
        Name = "hw_timestamping_control"
        Type = "cl"
        Source = "tests/integration/ptp/hw_timestamping_control_test.c"
        Output = "hw_timestamping_control_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: HW Timestamping Control"
    },
    @{
        Name = "rx_timestamping"
        Type = "cl"
        Source = "tests/integration/ptp/rx_timestamping_test.c"
        Output = "rx_timestamping_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: RX Timestamping"
    },
    # DISABLED: ptp_bringup_fixed - empty file (WIP)
    # TODO: Implement test or remove file
    @{
        Name = "ptp_bringup_fixed"
        Type = "cl"
        Source = "tests/integration/ptp/ptp_bringup_fixed.c"
        Output = "ptp_bringup_fixed.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: PTP Bringup Fixed"
        Disabled = $true  # Empty file, needs implementation
    },
    
    # Integration Tests - TSN (additional, cl.exe)
    # DISABLED: target_time_test - uses systim_high/systim_low (now systim u64)
    # TODO: Update to use AVB_CLOCK_CONFIG.systim (u64) instead
    @{
        Name = "target_time_test"
        Type = "cl"
        Source = "tests/integration/tsn/target_time_test.c"
        Output = "target_time_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: Target Time Test"
        Disabled = $true  # Needs update: AVB_CLOCK_CONFIG.systim is now u64
    },
    @{
        Name = "ssot_register_validation"
        Type = "cl"
        Source = "tests/integration/tsn/ssot_register_validation_test.c"
        Output = "ssot_register_validation_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: SSOT Register Validation"
    },
    @{
        Name = "test_tsn_ioctl_handlers_um"
        Type = "cl"
        Source = "tests/integration/tsn/test_tsn_ioctl_handlers_um.c"
        Output = "test_tsn_ioctl_handlers_um.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: TSN IOCTL Handlers (User-Mode)"
    },
    
    # Integration Tests - AVB (additional variants, cl.exe)
    # DISABLED: avb_test_actual - Kernel-mode placeholder (includes precomp.h)
    # TODO: This file is kernel-mode only (see "Kernel-mode placeholder" comment in source)
    @{
        Name = "avb_test_actual"
        Type = "cl"
        Source = "tests/integration/avb/avb_test_actual.c"
        Output = "avb_test_actual.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: AVB Test (Actual)"
        Disabled = $true  # Kernel-mode placeholder (precomp.h)
    },
    # DISABLED: avb_test_app - Kernel-mode placeholder (precomp.h)
    # TODO: This is KM-only build placeholder
    @{
        Name = "avb_test_app"
        Type = "cl"
        Source = "tests/integration/avb/avb_test_app.c"
        Output = "avb_test_app.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: AVB Test App"
        Disabled = $true  # Kernel-mode placeholder (precomp.h)
    },
    # DISABLED: avb_test_standalone - Kernel-mode placeholder (precomp.h)
    # TODO: This is KM-only build placeholder
    @{
        Name = "avb_test_standalone"
        Type = "cl"
        Source = "tests/integration/avb/avb_test_standalone.c"
        Output = "avb_test_standalone.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: AVB Test (Standalone)"
        Disabled = $true  # Kernel-mode placeholder (precomp.h)
    },
    # DISABLED: avb_integration_tests - Kernel-mode placeholder (precomp.h)
    # TODO: This is KM-only build placeholder
    @{
        Name = "avb_integration_tests"
        Type = "cl"
        Source = "tests/integration/AvbIntegrationTests.c"
        Output = "AvbIntegrationTests.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: AVB Integration Test Suite"
        Disabled = $true  # Kernel-mode placeholder (precomp.h)
    },
    
    # Integration Tests - Multi-Adapter (cl.exe)
    @{
        Name = "test_all_adapters"
        Type = "cl"
        Source = "tests/integration/multi_adapter/test_all_adapters.c"
        Output = "test_all_adapters.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: Test All Adapters"
    },
    @{
        Name = "test_multidev_adapter_enum"
        Type = "cl"
        Source = "tests/integration/multi_adapter/test_multidev_adapter_enum.c"
        Output = "test_multidev_adapter_enum.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: Multi-Adapter Enumeration (Verifies #15 REQ-F-MULTIDEV-001)"
    },
    @{
        Name = "test_device_register_access"
        Type = "cl"
        Source = "tests/integration/device_abstraction/test_device_register_access.c"
        Output = "test_device_register_access.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: Device Register Access via Abstraction Layer (Verifies #40 REQ-F-DEVICE-ABS-003)"
    },
    @{
        Name = "test_ndis_send_path"
        Type = "cl"
        Source = "tests/integration/ndis_send/test_ndis_send_path.c"
        Output = "test_ndis_send_path.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: NDIS FilterSend Packet Processing (Verifies #42 REQ-F-NDIS-SEND-001, #291 TEST-NDIS-SEND-PATH-001)"
    },
    @{
        Name = "test_ndis_receive_path"
        Type = "cl"
        Source = "tests/integration/ndis_receive/test_ndis_receive_path.c"
        Output = "test_ndis_receive_path.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: NDIS FilterReceive Packet Processing (Verifies #43 REQ-F-NDIS-RECEIVE-001, #290 TEST-NDIS-RECEIVE-PATH-001)"
    },
    @{
        Name = "test_tx_timestamp_retrieval"
        Type = "cl"
        Source = "tests/integration/tx_timestamp/test_tx_timestamp_retrieval.c"
        Output = "test_tx_timestamp_retrieval.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: TX Timestamp Retrieval IOCTL (Verifies #35 REQ-F-IOCTL-TS-001)"
    },
    @{
        Name = "test_hw_state_machine"
        Type = "cl"
        Source = "tests/integration/hw_state/test_hw_state_machine.c"
        Output = "test_hw_state_machine.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: Hardware State Machine IOCTL (Verifies #18 REQ-F-HWCTX-001)"
    },
    @{
        Name = "test_lazy_initialization"
        Type = "cl"
        Source = "tests/integration/lazy_init/test_lazy_initialization.c"
        Output = "test_lazy_initialization.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Integration: Lazy Initialization (Verifies #16 REQ-F-LAZY-INIT-001)"
    },
    @{
        Name = "test_registry_diagnostics"
        Type = "cl"
        Source = "tests/integration/registry_diag/test_registry_diagnostics.c"
        Output = "test_registry_diagnostics.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Libs = "advapi32.lib"
        Description = "Integration: Registry Diagnostics (Verifies #17 REQ-NF-DIAG-REG-001)"
    },
    
    # Diagnostic Tests (additional, cl.exe)
    @{
        Name = "diagnose_ptp"
        Type = "cl"
        Source = "tests/diagnostic/diagnose_ptp.c"
        Output = "diagnose_ptp.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Diagnostic: PTP Diagnostics"
    },
    @{
        Name = "test_extended_diag"
        Type = "cl"
        Source = "tests/diagnostic/test_extended_diag.c"
        Output = "test_extended_diag.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Diagnostic: Extended Diagnostics"
    },
    @{
        Name = "avb_diagnostic_test_um"
        Type = "cl"
        Source = "tests/diagnostic/avb_diagnostic_test_um.c"
        Output = "avb_diagnostic_test_um.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Diagnostic: AVB Diagnostic (User-Mode)"
    },
    @{
        Name = "avb_hw_state_test_um"
        Type = "cl"
        Source = "tests/diagnostic/avb_hw_state_test_um.c"
        Output = "avb_hw_state_test_um.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "Diagnostic: HW State Test (User-Mode)"
    },
    
    # E2E Tests (additional, cl.exe)
    @{
        Name = "avb_comprehensive_test"
        Type = "cl"
        Source = "tests/e2e/avb_comprehensive_test.c"
        Output = "avb_comprehensive_test.exe"
        Includes = "-I include -I external/intel_avb/lib -I intel-ethernet-regs/gen"
        Description = "E2E: AVB Comprehensive Test"
    },
    
    # TAEF Tests (nmake)
    @{
        Name = "taef_tests"
        Type = "nmake"
        Makefile = "tests/taef/Makefile.mak"
        Description = "TAEF Test Suite (nmake)"
    },
    
    # Integration Suite (nmake)
    @{
        Name = "integration_suite"
        Type = "nmake"
        Makefile = "tests/integration/Makefile.mak"
        Description = "Integration Test Suite (nmake)"
    }
)

# Filter tests if specific test requested
if ($TestName) {
    $TestsToBuild = @($AllTests | Where-Object { $_.Name -eq $TestName })
    if (-not $TestsToBuild) {
        Write-Host "ERROR: Test '$TestName' not found" -ForegroundColor Red
        Write-Host ""
        Write-Host "Available tests:" -ForegroundColor Yellow
        $AllTests | ForEach-Object { Write-Host "  - $($_.Name)" -ForegroundColor Gray }
        Pop-Location
        exit 1
    }
} else {
    $TestsToBuild = @($AllTests)
}

# Build tracking
$SucceededTests = @()
$SkippedTests = @()
$FailedTests = @()

# Find VS environment once and initialize a VS dev shell in THIS process.
# This avoids calling vcvars64.bat later (which can fail with cmd.exe "The input line is too long" once PATH is large).
$VsDevShellInitialized = $false
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

Write-Host "Locating Visual Studio environment..." -ForegroundColor Yellow
if (Test-Path $vswhere) {
    $vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vs) {
        $launchVsDevShell = Join-Path $vs 'Common7\Tools\Launch-VsDevShell.ps1'
        if (Test-Path $launchVsDevShell) {
            & $launchVsDevShell -Arch amd64 -SkipAutomaticLocation | Out-Null
            $VsDevShellInitialized = $true
            Write-Host "  VS environment found" -ForegroundColor Green
        }
    }
}

if (-not $VsDevShellInitialized) {
    Write-Host "  ERROR: VS environment not found (cl/nmake builds will fail)." -ForegroundColor Red
    Write-Host "  Hint: Install Visual Studio Build Tools (Desktop development with C++)." -ForegroundColor DarkYellow
    Write-Host ""
} else {
    Write-Host ""
}

# Set up output directory (matching Build-Driver.ps1 structure)
$Platform = "x64"
$OutputDir = Join-Path $RepoRoot "build\tools\avb_test\$Platform\$Configuration"
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
    Write-Host "Created output directory: $OutputDir" -ForegroundColor Gray
    Write-Host ""
}

# Build each test
foreach ($Test in $TestsToBuild) {
    Write-Host "Building: $($Test.Name)" -ForegroundColor Yellow
    Write-Host "  Description: $($Test.Description)" -ForegroundColor Gray
    
    # Skip disabled tests
    if ($Test.Disabled) {
        Write-Host "  SKIPPED: Test is disabled (needs repair)" -ForegroundColor DarkYellow
        $SkippedTests += $Test.Name
        Write-Host ""
        continue
    }
    
    if ($Test.Type -eq "cl") {
        # cl.exe build
        Write-Host "  Type: cl.exe" -ForegroundColor Gray
        Write-Host "  Source: $($Test.Source)" -ForegroundColor Gray
        
        # Set full output path
        $OutputPath = Join-Path $OutputDir $Test.Output
        Write-Host "  Output: $OutputPath" -ForegroundColor Gray
        Write-Host ""
        
        # Verify source file exists
        if (-not (Test-Path $Test.Source)) {
            Write-Host "  ERROR: Source file not found: $($Test.Source)" -ForegroundColor Red
            $FailedTests += $Test.Name
            Write-Host ""
            continue
        }
        
        # Build command with full output path
        # Check if compile-only (produces .obj) or executable (produces .exe)
        if ($Test.CompileOnly) {
            # Compile-only: use /Fo for object file output
            $CompilerFlags = if ($Test.CompilerFlags) { $Test.CompilerFlags } else { "/c" }
            $BuildCmd = "cl /nologo /W4 /Zi $CompilerFlags $($Test.Includes) $($Test.Source) /Fo:`"$OutputPath`""
        } else {
            # Executable: use /Fe for executable output
            $BuildCmd = "cl /nologo /W4 /Zi $($Test.Includes) $($Test.Source) /Fe:`"$OutputPath`""
            if ($Test.Libs) {
                $BuildCmd += " /link $($Test.Libs)"
            }
        }
        
        if ($ShowDetails) {
            Write-Host "  Command: $BuildCmd" -ForegroundColor Gray
            Write-Host ""
        }
        
        # Use vs_compile.ps1 to set up environment
        $BuildScript = Join-Path $RepoRoot "tools\vs_compile.ps1"
        if (-not (Test-Path $BuildScript)) {
            Write-Host "  ERROR: vs_compile.ps1 not found" -ForegroundColor Red
            $FailedTests += $Test.Name
            Write-Host ""
            continue
        }
        
        # Execute build
        $result = & $BuildScript -BuildCmd $BuildCmd 2>&1
        
        if ($LASTEXITCODE -eq 0 -and (Test-Path $OutputPath)) {
            Write-Host "  SUCCESS: $OutputPath created" -ForegroundColor Green
            $SucceededTests += $Test.Name
        } else {
            Write-Host "  FAILED: Compiler returned error code $LASTEXITCODE" -ForegroundColor Red
            $FailedTests += $Test.Name
        }
        
    } elseif ($Test.Type -eq "nmake") {
        # nmake build
        Write-Host "  Type: nmake" -ForegroundColor Gray
        Write-Host "  Makefile: $($Test.Makefile)" -ForegroundColor Gray
        Write-Host ""
        
        if (-not $VsDevShellInitialized) {
            Write-Host "  SKIPPED: VS dev shell not initialized" -ForegroundColor Yellow
            $SkippedTests += $Test.Name
            Write-Host ""
            continue
        }
        
        # Verify makefile exists
        $MakeFileAbs = Join-Path $RepoRoot $Test.Makefile
        if (-not (Test-Path $MakeFileAbs)) {
            Write-Host "  ERROR: Makefile not found: $MakeFileAbs" -ForegroundColor Red
            $FailedTests += $Test.Name
            Write-Host ""
            continue
        }
        
        # Get absolute directory and filename
        $MakeDir = Split-Path $MakeFileAbs -Parent
        $MakeFile = Split-Path $MakeFileAbs -Leaf
        
        if ($ShowDetails) {
            Write-Host "  Working Directory: $MakeDir" -ForegroundColor Gray
            Write-Host "  Makefile: $MakeFile" -ForegroundColor Gray
            Write-Host ""
        }
        
        # Execute nmake with Visual Studio Developer environment
        Push-Location $MakeDir
        try {
            $isTaef = ($Test.Name -eq 'taef_tests')

            if ($isTaef) {
                if (-not $env:TAEF_INCLUDE -or $env:TAEF_INCLUDE.Trim().Length -eq 0) {
                    $taefInc = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Testing\Development\inc'
                    if (Test-Path (Join-Path $taefInc 'WexTestClass.h')) {
                        $env:TAEF_INCLUDE = $taefInc
                    }
                }
                if (-not $env:TAEF_LIB -or $env:TAEF_LIB.Trim().Length -eq 0) {
                    $taefLib = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Testing\Development\lib\x64'
                    if (Test-Path (Join-Path $taefLib 'Wex.Common.lib')) {
                        $env:TAEF_LIB = $taefLib
                    }
                }
            }

            $prevErrorPref = $ErrorActionPreference
            $ErrorActionPreference = 'SilentlyContinue'
            $output = @()
            try {
                $output = @(nmake /f $MakeFile 2>&1)
                $buildExitCode = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $prevErrorPref
            }

            if ($ShowDetails -and $output.Count -gt 0) {
                $output | ForEach-Object { Write-Host "    $_" -ForegroundColor Gray }
            }
            
            if ($buildExitCode -eq 0) {
                Write-Host "  SUCCESS: nmake build completed" -ForegroundColor Green
                $SucceededTests += $Test.Name
            } else {
                Write-Host "  FAILED: nmake returned error code $buildExitCode" -ForegroundColor Red
                if (-not $ShowDetails -and $output -and $output.Count -gt 0) {
                    Write-Host "  nmake output (tail):" -ForegroundColor DarkYellow
                    $tailStart = [Math]::Max(0, $output.Count - 30)
                    $output[$tailStart..($output.Count - 1)] | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkYellow }
                }
                $FailedTests += $Test.Name
            }
        } finally {
            Pop-Location
        }
        
    } else {
        Write-Host "  ERROR: Unknown build type: $($Test.Type)" -ForegroundColor Red
        $FailedTests += $Test.Name
    }
    
    Write-Host ""
}

# Summary
$SuccessCount = $SucceededTests.Count
$SkipCount = $SkippedTests.Count
$FailCount = $FailedTests.Count

Write-Host "==============================" -ForegroundColor Cyan
Write-Host "Build Summary" -ForegroundColor Cyan
Write-Host "==============================" -ForegroundColor Cyan
Write-Host "Total Tests: $($TestsToBuild.Count)" -ForegroundColor White
Write-Host "Successful: $SuccessCount" -ForegroundColor Green
Write-Host "Skipped: $SkipCount (disabled, need repair)" -ForegroundColor DarkYellow
Write-Host "Failed: $FailCount" -ForegroundColor $(if ($FailCount -gt 0) { "Red" } else { "Green" })

$accounted = $SuccessCount + $SkipCount + $FailCount
$unaccounted = $TestsToBuild.Count - $accounted
if ($unaccounted -ne 0) {
    Write-Host "" 
    Write-Host "WARNING: Count mismatch (unaccounted: $unaccounted)" -ForegroundColor Yellow
    $accountedSet = @{}
    $SucceededTests | ForEach-Object { $accountedSet[$_] = $true }
    $SkippedTests | ForEach-Object { $accountedSet[$_] = $true }
    $FailedTests | ForEach-Object { $accountedSet[$_] = $true }
    $unaccountedNames = $TestsToBuild | Where-Object { -not $accountedSet.ContainsKey($_.Name) } | ForEach-Object { $_.Name }
    if ($unaccountedNames) {
        Write-Host "Unaccounted Tests:" -ForegroundColor Yellow
        $unaccountedNames | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
    }
}

if ($FailCount -gt 0) {
    Write-Host ""
    Write-Host "Failed Tests:" -ForegroundColor Red
    $FailedTests | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
}

Write-Host ""

# Return to original directory
Pop-Location

# Exit with error code if any builds failed
if ($FailCount -gt 0) {
    exit 1
}

exit 0
