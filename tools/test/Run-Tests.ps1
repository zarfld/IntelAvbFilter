# Intel AVB Filter Driver - Comprehensive Test Suite
# CANONICAL SCRIPT - Use wrappers for common scenarios
# Based on: test_driver.ps1 (comprehensive diagnostics) + run_tests.ps1 (test executor)
#
# Implements: #27 (REQ-NF-SCRIPTS-001: Consolidated Script Architecture)
# Reference: tools/test/test_driver.ps1 (THE reference diagnostic implementation)
#            tools/test/run_tests.ps1 (test execution logic)
#
# Wrappers:
#   - Quick-Test-Debug.bat   -> Run-Tests.ps1 -Configuration Debug -Quick
#   - Quick-Test-Release.bat -> Run-Tests.ps1 -Configuration Release -Quick
#   - Run-Full-Tests.bat     -> Run-Tests.ps1 -Full

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [Parameter(Mandatory=$false)]
    [switch]$Quick,
    
    [Parameter(Mandatory=$false)]
    [switch]$Full,
    
    [Parameter(Mandatory=$false)]
    [switch]$SkipHardwareCheck,
    
    [Parameter(Mandatory=$false)]
    [switch]$CollectLogs,
    
    [Parameter(Mandatory=$false)]
    [string]$TestExecutable,
    
    [Parameter(Mandatory=$false)]
    [switch]$HardwareOnly,
    
    [Parameter(Mandatory=$false)]
    [switch]$SecureBootCheck
)

# Require Administrator
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges. Please run as Administrator."
    exit 1
}

# ===========================
# Helper Functions
# ===========================
function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Failure {
    param([string]$Message)
    Write-Host "[FAIL] $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "[INFO] $Message" -ForegroundColor Yellow
}

# Helper function to run tests and track results
function Invoke-Test {
    param(
        [string]$TestName,
        [string]$Description = ""
    )
    
    $testPath = Join-Path $testExeDir $TestName
    if (-not (Test-Path $testPath)) {
        Write-Host "`n  => $TestName" -ForegroundColor Cyan
        if ($Description) {
            Write-Host "     $Description" -ForegroundColor Gray
        }
        Write-Failure "Test not found: $testPath"
        $script:totalTests++
        $script:failedTests++
        $script:testResults += [PSCustomObject]@{
            Name = $TestName
            Status = "NOT_FOUND"
            ExitCode = -1
        }
        return
    }
    
    Write-Host "`n  => $TestName" -ForegroundColor Cyan
    if ($Description) {
        Write-Host "     $Description" -ForegroundColor Gray
    }
    
    $script:totalTests++
    & $testPath
    $exitCode = $LASTEXITCODE
    
    if ($exitCode -eq 0 -or $null -eq $exitCode) {
        $script:passedTests++
        $script:testResults += [PSCustomObject]@{
            Name = $TestName
            Status = "PASSED"
            ExitCode = if ($exitCode) { $exitCode } else { 0 }
        }
    } else {
        Write-Host "  [WARN] Test exited with code $exitCode" -ForegroundColor Yellow
        $script:failedTests++
        $script:testResults += [PSCustomObject]@{
            Name = $TestName
            Status = "FAILED"
            ExitCode = $exitCode
        }
    }
}

# Banner
Write-Host @"
================================================================
  Intel AVB Filter Driver - Test Suite
  Configuration: $Configuration
================================================================
"@ -ForegroundColor Cyan

# Calculate paths
$scriptDir = $PSScriptRoot  # tools/test
$toolsDir = Split-Path $scriptDir -Parent  # tools
$repoRoot = Split-Path $toolsDir -Parent  # repository root
$testExeDir = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"

# Test execution tracking
$script:totalTests = 0
$script:passedTests = 0
$script:failedTests = 0
$script:testResults = @()

# ===========================
# Feature: Secure Boot Check (from test_secure_boot_compatible.bat)
# ===========================
if ($SecureBootCheck) {
    Write-Step "Checking Secure Boot Status"
    
    try {
        $secureBootEnabled = Confirm-SecureBootUEFI
        if ($secureBootEnabled) {
            Write-Info "Secure Boot is ENABLED - test signing may be blocked"
            Write-Host "  Alternative testing methods:" -ForegroundColor Yellow
            Write-Host "    1. Disable Secure Boot in BIOS/UEFI (recommended for development)" -ForegroundColor Gray
            Write-Host "    2. Use EV code signing certificate" -ForegroundColor Gray
            Write-Host "    3. Test in VM without Secure Boot" -ForegroundColor Gray
        } else {
            Write-Success "Secure Boot is disabled or not supported"
        }
    } catch {
        Write-Info "Could not determine Secure Boot status (may not be supported)"
    }
    
    # Check for test certificate
    $certPath = Join-Path $repoRoot "build\x64\$Configuration\IntelAvbFilter.cer"
    if (Test-Path $certPath) {
        Write-Host "`n  Installing test certificate to Trusted Root..." -ForegroundColor Gray
        try {
            certutil -addstore root $certPath | Out-Null
            Write-Success "Test certificate installed"
        } catch {
            Write-Info "Certificate installation failed (may require manual installation)"
        }
    }
}

# ===========================
# NOTE: -CompileDiagnostics MOVED to Build-Tests.ps1 -BuildDiagnosticTools
# Separation of Concerns: BUILD logic belongs in Build-Tests.ps1, not Run-Tests.ps1
# To compile diagnostic tools, use: .\tools\build\Build-Tests.ps1 -BuildDiagnosticTools
# ===========================

# ===========================
# Feature: Hardware-Only Mode (from test_hardware_only.bat)
# ===========================
if ($HardwareOnly) {
    Write-Step "Compiling Hardware-Only Test Application"
    
    # Check for Visual Studio compiler
    $clPath = Get-Command cl.exe -ErrorAction SilentlyContinue
    if (-not $clPath) {
        Write-Failure "Visual Studio compiler (cl.exe) not found in PATH"
        Write-Info "Run this from 'Developer Command Prompt for VS' or add VS tools to PATH"
        exit 1
    }
    
    # Compile with HARDWARE_ONLY=1 flag
    $testSource = Join-Path $repoRoot "tests\device_specific\i219\avb_test_i219.c"
    if (Test-Path $testSource) {
        Write-Host "  Compiling with /DHARDWARE_ONLY=1 flag..." -ForegroundColor Gray
        $hwOutput = Join-Path $repoRoot "avb_test_hardware_only.exe"
        & cl.exe $testSource /DHARDWARE_ONLY=1 /Fe:$hwOutput 2>&1 | Out-Null
        if (Test-Path $hwOutput) {
            Write-Success "Hardware-Only test application compiled: avb_test_hardware_only.exe"
            Write-Host "  NO SIMULATION - Real hardware or explicit failure" -ForegroundColor Yellow
            
            # Run the hardware-only test
            Write-Host "`n  Running Hardware-Only test..." -ForegroundColor Cyan
            & $hwOutput
        } else {
            Write-Failure "Compilation failed"
        }
    } else {
        Write-Failure "Test source not found: $testSource"
    }
}

# ===========================
# Test 1: Check Driver Service Status
# ===========================
Write-Step "Checking Driver Service Status"

$service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
if ($service) {
    Write-Success "Service found: $($service.Status)"
    
    if ($service.Status -ne 'Running') {
        Write-Info "Service is not running. Driver may not be loaded."
    }
} else {
    Write-Failure "Service 'IntelAvbFilter' not found. Driver may not be installed."
    Write-Info "Install driver first: .\tools\setup\Install-Driver.ps1 -Configuration $Configuration -InstallDriver"
}

# ===========================
# Test 2: Enumerate Intel Network Adapters (Detailed)
# ===========================
if (-not $SkipHardwareCheck) {
    Write-Step "Enumerating Intel Network Adapters"
    
    $intelAdapters = Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*Intel*"}
    
    if ($intelAdapters) {
        Write-Success "Found $($intelAdapters.Count) Intel network adapter(s)"
        
        foreach ($adapter in $intelAdapters) {
            Write-Host "`n  Adapter: $($adapter.InterfaceDescription)" -ForegroundColor White
            Write-Host "    Name: $($adapter.Name)" -ForegroundColor Gray
            Write-Host "    Status: $($adapter.Status)" -ForegroundColor Gray
            Write-Host "    MAC: $($adapter.MacAddress)" -ForegroundColor Gray
            Write-Host "    Speed: $($adapter.LinkSpeed)" -ForegroundColor Gray
            
            # Get hardware ID and identify device
            try {
                $device = Get-PnpDevice -InstanceId $adapter.PnPDeviceID -ErrorAction SilentlyContinue
                if ($device -and $device.HardwareID) {
                    Write-Host "    Hardware ID: $($device.HardwareID[0])" -ForegroundColor Gray
                    
                    # Parse VID/DID
                    if ($device.HardwareID[0] -match 'VEN_([0-9A-F]{4})&DEV_([0-9A-F]{4})') {
                        $vid = $matches[1]
                        $did = $matches[2]
                        Write-Host "    VID: 0x$vid, DID: 0x$did" -ForegroundColor Gray
                        
                        # Identify device type
                        $deviceName = switch ($did) {
                            "1533" { "Intel I210 Gigabit Network Connection" }
                            "153A" { "Intel I217-LM Gigabit Network Connection" }
                            "15F3" { "Intel I219-LM Gigabit Network Connection" }
                            "125B" { "Intel I226-V 2.5GbE Network Connection" }
                            "125C" { "Intel I226-LM 2.5GbE Network Connection" }
                            "1521" { "Intel I350 Gigabit Network Connection" }
                            default { "Unknown Intel Device (0x$did)" }
                        }
                        Write-Host "    Device: $deviceName" -ForegroundColor Cyan
                        
                        # Check if supported
                        $supported = $did -in @("1533", "153A", "15F3", "125B", "125C", "1521")
                        if ($supported) {
                            Write-Success "    [SUPPORTED] This device is supported by the driver"
                        } else {
                            Write-Info "    [UNSUPPORTED] This device may not be fully supported"
                        }
                    }
                }
            } catch {
                Write-Host "    Could not retrieve hardware details" -ForegroundColor DarkGray
            }
        }
    } else {
        Write-Failure "No Intel network adapters found"
        Write-Info "The driver requires Intel Ethernet hardware to function"
    }
}

# ===========================
# Test 3: Check Device Node Access
# ===========================
Write-Step "Testing Device Node Access"

$devicePath = "\\.\IntelAvbFilter"

# Use Win32 CreateFile API (device nodes don't work with FileStream)
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class DeviceAccess {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    public static extern IntPtr CreateFile(
        string lpFileName,
        uint dwDesiredAccess,
        uint dwShareMode,
        IntPtr lpSecurityAttributes,
        uint dwCreationDisposition,
        uint dwFlagsAndAttributes,
        IntPtr hTemplateFile);
        
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr hObject);
    
    public const uint GENERIC_READ = 0x80000000;
    public const uint GENERIC_WRITE = 0x40000000;
    public const uint OPEN_EXISTING = 3;
    public const uint FILE_ATTRIBUTE_NORMAL = 0x80;
    public static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);
}
"@

try {
    $handle = [DeviceAccess]::CreateFile(
        $devicePath,
        [DeviceAccess]::GENERIC_READ -bor [DeviceAccess]::GENERIC_WRITE,
        0,
        [IntPtr]::Zero,
        [DeviceAccess]::OPEN_EXISTING,
        [DeviceAccess]::FILE_ATTRIBUTE_NORMAL,
        [IntPtr]::Zero
    )
    
    if ($handle -ne [DeviceAccess]::INVALID_HANDLE_VALUE) {
        [DeviceAccess]::CloseHandle($handle) | Out-Null
        Write-Success "Device node accessible: $devicePath"
    } else {
        $lastError = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
        Write-Failure "Cannot access device node: $devicePath"
        Write-Info "Win32 Error Code: $lastError"
        Write-Info "Driver may not be loaded or device not created"
    }
} catch {
    Write-Failure "Cannot access device node: $devicePath"
    Write-Info "Error: $($_.Exception.Message)"
    Write-Info "Driver may not be loaded or installed correctly"
}

# ===========================
# Test 4: Locate Test Executables
# ===========================
Write-Step "Locating Test Executables"

if (-not (Test-Path $testExeDir)) {
    Write-Failure "Test directory not found: $testExeDir"
    Write-Info "Build tests first: .\tools\build\Build-Driver.ps1 -Configuration $Configuration -BuildTests"
    exit 1
}

$availableTests = Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue
if (-not $availableTests) {
    Write-Failure "No test executables found in: $testExeDir"
    exit 1
}

Write-Success "Found $($availableTests.Count) test executable(s)"
foreach ($test in $availableTests) {
    Write-Host "  [OK] $($test.Name) ($($test.Length) bytes)" -ForegroundColor Green
}

# ===========================
# Test 5: Execute Tests
# ===========================
if ($TestExecutable) {
    # Run specific test
    Write-Step "Running Specific Test: $TestExecutable"
    
    $testPath = Join-Path $testExeDir $TestExecutable
    if (-not (Test-Path $testPath)) {
        Write-Failure "Test executable not found: $testPath"
        exit 1
    }
    
    Write-Host "Executing: $testPath" -ForegroundColor Gray
    Write-Host ""
    & $testPath
    
} elseif ($Quick) {
    # Quick tests only
    Write-Step "Running Quick Verification Tests"
    
    # SSOT Quick Check (critical for code quality)
    Write-Host "`n[Quick SSOT Check] Verifying Single Source of Truth Compliance..." -ForegroundColor Cyan
    $ssotTestDir = Join-Path $repoRoot "tests\verification\ssot"
    $ssotTest1 = Join-Path $ssotTestDir "Test-SSOT-001-NoDuplicates.ps1"
    
    if (Test-Path $ssotTest1) {
        $script:totalTests++
        & $ssotTest1
        if ($LASTEXITCODE -eq 0) {
            $script:passedTests++
            Write-Host "[OK] SSOT validation passed - no duplicate IOCTLs" -ForegroundColor Green
        } else {
            $script:failedTests++
            Write-Host "[FAIL] SSOT validation failed - duplicate IOCTLs detected!" -ForegroundColor Red
            Write-Host "       Run full SSOT tests: .\tests\verification\ssot\Run-All-SSOT-Tests.ps1" -ForegroundColor Yellow
        }
    } else {
        Write-Info "SSOT tests not found (install from tests/verification/ssot/)"
    }
    Write-Host ""
    
    $quickTests = @(
        "test_multidev_adapter_enum.exe",
        "test_device_register_access.exe",
        "test_ndis_send_path.exe",
        "test_ndis_receive_path.exe",
        "test_tx_timestamp_retrieval.exe",
        "test_hw_state_machine.exe",
        "test_lazy_initialization.exe",
        "test_registry_diagnostics.exe",
        "ptp_clock_control_test.exe",
        "ptp_clock_control_production_test.exe",
        "avb_capability_validation_test.exe",
        "avb_diagnostic_test.exe"
    )
    
    foreach ($testName in $quickTests) {
        $testPath = Join-Path $testExeDir $testName
        if (Test-Path $testPath) {
            Write-Host "`n===============================================" -ForegroundColor Cyan
            Write-Host "Running: $testName" -ForegroundColor Yellow
            Write-Host "===============================================" -ForegroundColor Cyan
            & $testPath
        } else {
            Write-Info "Skipping $testName (not built)"
        }
    }
    
} elseif ($Full) {
    # Full comprehensive test suite (phased execution from run_tests.ps1)
    Write-Step "Running Full Comprehensive Test Suite"
    
    # Phase -1: Standards Compliance & Code Quality Verification
    Write-Host "`n=== PHASE -1: Standards Compliance & Code Quality Verification ===" -ForegroundColor Green
    Write-Host "Purpose: Verify ISO/IEC/IEEE standards compliance and architecture decisions" -ForegroundColor Gray
    
    # SSOT Verification Tests (Issue #24, ADR #123, TEST #301-303)
    Write-Host "`n  [SSOT Tests] Single Source of Truth Verification" -ForegroundColor Cyan
    Write-Host "  Verifies: Issue #24 (REQ-NF-SSOT-001), ADR #123 (ADR-SSOT-001)" -ForegroundColor Gray
    
    $ssotTestDir = Join-Path $repoRoot "tests\verification\ssot"
    
    # TEST-SSOT-001: No Duplicate IOCTL Definitions
    $ssotTest1 = Join-Path $ssotTestDir "Test-SSOT-001-NoDuplicates.ps1"
    if (Test-Path $ssotTest1) {
        Write-Host "`n    => TEST-SSOT-001: Verify No Duplicate IOCTL Definitions (#301)" -ForegroundColor Yellow
        $script:totalTests++
        & $ssotTest1
        if ($LASTEXITCODE -eq 0) {
            $script:passedTests++
            Write-Success "TEST-SSOT-001 PASSED"
        } else {
            $script:failedTests++
            Write-Failure "TEST-SSOT-001 FAILED - SSOT violations detected!"
        }
    } else {
        Write-Info "TEST-SSOT-001 script not found (skipping)"
    }
    
    # TEST-SSOT-003: All Files Use SSOT Header Include
    $ssotTest3 = Join-Path $ssotTestDir "Test-SSOT-003-IncludePattern.ps1"
    if (Test-Path $ssotTest3) {
        Write-Host "`n    => TEST-SSOT-003: Verify All Files Use SSOT Header (#302)" -ForegroundColor Yellow
        $script:totalTests++
        & $ssotTest3
        if ($LASTEXITCODE -eq 0) {
            $script:passedTests++
            Write-Success "TEST-SSOT-003 PASSED"
        } else {
            $script:failedTests++
            Write-Failure "TEST-SSOT-003 FAILED - Include pattern violations detected!"
        }
    } else {
        Write-Info "TEST-SSOT-003 script not found (skipping)"
    }
    
    # TEST-SSOT-004: SSOT Header Completeness
    $ssotTest4 = Join-Path $ssotTestDir "Test-SSOT-004-Completeness.ps1"
    if (Test-Path $ssotTest4) {
        Write-Host "`n    => TEST-SSOT-004: Verify SSOT Header Completeness (#303)" -ForegroundColor Yellow
        $script:totalTests++
        & $ssotTest4
        if ($LASTEXITCODE -eq 0) {
            $script:passedTests++
            Write-Success "TEST-SSOT-004 PASSED"
        } else {
            $script:failedTests++
            Write-Failure "TEST-SSOT-004 FAILED - SSOT header incomplete!"
        }
    } else {
        Write-Info "TEST-SSOT-004 script not found (skipping)"
    }
    
    Write-Host "`n  Note: TEST-SSOT-002 (CI Workflow Validation) requires manual negative testing" -ForegroundColor Gray
    Write-Host "        See tests/verification/ssot/README.md for procedure" -ForegroundColor Gray
    
    # Phase 0: Architecture Compliance Validation
    Write-Host "`n=== PHASE 0: Architecture Compliance Validation ===" -ForegroundColor Green
    Write-Host "Purpose: Verify core architectural requirements are met" -ForegroundColor Gray
    
    $phase0Tests = @(
        @{Name="test_multidev_adapter_enum.exe"; Desc="Verify Multi-Adapter Enumeration (#15 REQ-F-MULTIDEV-001)"},
        @{Name="test_device_register_access.exe"; Desc="Verify device abstraction layer register access (#40 REQ-F-DEVICE-ABS-003)"},
        @{Name="test_ndis_send_path.exe"; Desc="Verify NDIS FilterSend packet processing (#42 REQ-F-NDIS-SEND-001)"},
        @{Name="test_ndis_receive_path.exe"; Desc="Verify NDIS FilterReceive packet processing (#43 REQ-F-NDIS-RECEIVE-001)"},
        @{Name="test_tx_timestamp_retrieval.exe"; Desc="Verify TX Timestamp Retrieval IOCTL (#35 REQ-F-IOCTL-TS-001)"},
        @{Name="test_hw_state_machine.exe"; Desc="Verify Hardware State Machine IOCTL (#18 REQ-F-HWCTX-001)"},
        @{Name="test_lazy_initialization.exe"; Desc="Verify Lazy Initialization (#16 REQ-F-LAZY-INIT-001)"},
        @{Name="test_registry_diagnostics.exe"; Desc="Verify Registry Diagnostics (#17 REQ-NF-DIAG-REG-001)"},
        @{Name="ptp_clock_control_test.exe"; Desc="Verify PTP Clock Control including GET_CLOCK_CONFIG (#4)"},
        @{Name="ptp_clock_control_production_test.exe"; Desc="Verify PTP Clock Production IOCTLs (#4 P0 CRITICAL)"},
        @{Name="avb_capability_validation_test.exe"; Desc="Verify realistic hardware capability reporting (no false advertising)"},
        @{Name="avb_device_separation_test.exe"; Desc="Verify clean device separation architecture compliance"}
    )
    foreach ($test in $phase0Tests) {
        Invoke-Test -TestName $test.Name -Description $test.Desc
    }
    
    # Phase 1: Basic Hardware Diagnostics
    Write-Host "`n=== PHASE 1: Basic Hardware Diagnostics ===" -ForegroundColor Green
    Write-Host "Purpose: Comprehensive hardware analysis and troubleshooting" -ForegroundColor Gray
    
    $phase1Tests = @(
        @{Name="avb_diagnostic_test.exe"; Desc="Comprehensive hardware diagnostics"},
        @{Name="avb_hw_state_test.exe"; Desc="Hardware state transitions and management"}
    )
    foreach ($test in $phase1Tests) {
        Invoke-Test -TestName $test.Name -Description $test.Desc
    }
    
    # Phase 2: TSN IOCTL Handler Verification
    Write-Host "`n=== PHASE 2: TSN IOCTL Handler Verification (Critical Fix Validation) ===" -ForegroundColor Green
    Write-Host "Purpose: Verify TAS/FP/PTM IOCTLs no longer return ERROR_INVALID_FUNCTION" -ForegroundColor Gray
    
    $phase2Tests = @(
        @{Name="test_tsn_ioctl_handlers.exe"; Desc="TSN IOCTL handler validation"},
        @{Name="tsn_hardware_activation_validation.exe"; Desc="TSN features activate at hardware level"}
    )
    foreach ($test in $phase2Tests) {
        Invoke-Test -TestName $test.Name -Description $test.Desc
    }
    
    # Phase 3: Multi-Adapter Hardware Testing
    Write-Host "`n=== PHASE 3: Multi-Adapter Hardware Testing ===" -ForegroundColor Green
    
    Invoke-Test -TestName "avb_multi_adapter_test.exe"
    
    # Phase 4: I226 Advanced Feature Testing
    Write-Host "`n=== PHASE 4: I226 Advanced Feature Testing ===" -ForegroundColor Green
    
    $phase4Tests = @("avb_i226_test.exe", "avb_i226_advanced_test.exe")
    foreach ($testName in $phase4Tests) {
        Invoke-Test -TestName $testName
    }
    
    # Phase 5: I210 Basic Testing
    Write-Host "`n=== PHASE 5: I210 Basic Testing ===" -ForegroundColor Green
    
    Invoke-Test -TestName "avb_test_i210.exe"
    
    # Phase 6: Specialized Investigation Tests
    Write-Host "`n=== PHASE 6: Specialized Investigation Tests ===" -ForegroundColor Green
    Write-Host "Purpose: Deep hardware investigation and TSN validation" -ForegroundColor Gray
    
    $phase6Tests = @(
        "chatgpt5_i226_tas_validation.exe",
        "corrected_i226_tas_test.exe",
        "critical_prerequisites_investigation.exe",
        "enhanced_tas_investigation.exe",
        "hardware_investigation_tool.exe"
    )
    foreach ($testName in $phase6Tests) {
        Invoke-Test -TestName $testName
    }
    
} else {
    # Default: Run ALL available tests
    Write-Step "Running All Available Tests"
    Write-Info "Found $($availableTests.Count) test(s) - executing all"
    Write-Host ""
    
    foreach ($test in $availableTests) {
        Write-Host "`n===============================================" -ForegroundColor Cyan
        Write-Host "Running: $($test.Name)" -ForegroundColor Yellow
        Write-Host "===============================================" -ForegroundColor Cyan
        
        $testPath = $test.FullName
        & $testPath
    }
}

# ===========================
# Optional: Collect Logs
# ===========================
if ($CollectLogs) {
    Write-Step "Collecting Event Logs"
    
    $logDir = Join-Path $repoRoot "logs"
    if (-not (Test-Path $logDir)) {
        New-Item -ItemType Directory -Path $logDir -Force | Out-Null
    }
    
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $logFile = Join-Path $logDir "test_logs_$timestamp.txt"
    
    Write-Host "Saving logs to: $logFile" -ForegroundColor Gray
    
    # Get System event log entries
    Get-EventLog -LogName System -Source "IntelAvbFilter" -Newest 50 -ErrorAction SilentlyContinue | 
        Format-List TimeGenerated, EntryType, Message | 
        Out-File $logFile
    
    Write-Success "Logs saved to: $logFile"
}

# ===========================
# Test Summary
# ===========================
Write-Host "`n" -NoNewline
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "                       TEST SUMMARY" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan

# Determine overall status
$hasService = (Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue) -ne $null
$intelAdapters = Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*Intel*"} -ErrorAction SilentlyContinue
$hasIntelHw = $intelAdapters.Count -gt 0

# Check device node using same Win32 API as the test above
$hasDeviceNode = $false
try {
    $handle = [DeviceAccess]::CreateFile(
        "\\.\IntelAvbFilter",
        [DeviceAccess]::GENERIC_READ -bor [DeviceAccess]::GENERIC_WRITE,
        0,
        [IntPtr]::Zero,
        [DeviceAccess]::OPEN_EXISTING,
        [DeviceAccess]::FILE_ATTRIBUTE_NORMAL,
        [IntPtr]::Zero
    )
    
    if ($handle -ne [DeviceAccess]::INVALID_HANDLE_VALUE) {
        [DeviceAccess]::CloseHandle($handle) | Out-Null
        $hasDeviceNode = $true
    }
} catch {
    $hasDeviceNode = $false
}

$hasTests = (Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue).Count -gt 0

# ===========================
# Infrastructure Status
# ===========================
Write-Host "`nInfrastructure Status:" -ForegroundColor Yellow
if ($hasService) {
    $svc = Get-Service -Name "IntelAvbFilter"
    Write-Host "  [OK] Driver service: $($svc.Status)" -ForegroundColor Green
} else {
    Write-Host "  [FAIL] Driver service not found" -ForegroundColor Red
}

if ($hasIntelHw) {
    Write-Host "  [OK] Intel adapters: $($intelAdapters.Count) detected" -ForegroundColor Green
} else {
    Write-Host "  [FAIL] No Intel network adapters detected" -ForegroundColor Red
}

if ($hasDeviceNode) {
    Write-Host "  [OK] Device node: Accessible" -ForegroundColor Green
} else {
    Write-Host "  [FAIL] Device node: Not created" -ForegroundColor Red
}

if ($hasTests) {
    $testCount = (Get-ChildItem -Path $testExeDir -Filter "*.exe" -ErrorAction SilentlyContinue).Count
    Write-Host "  [OK] Test executables: $testCount available" -ForegroundColor Green
} else {
    Write-Host "  [WARN] Test executables: Not built" -ForegroundColor Yellow
}

# ===========================
# Test Execution Results
# ===========================
if ($script:totalTests -gt 0) {
    Write-Host "`nTest Execution Results:" -ForegroundColor Yellow
    Write-Host "  Total Tests Run:    $($script:totalTests)" -ForegroundColor Cyan
    
    if ($script:passedTests -gt 0) {
        Write-Host "  ✓ Passed:          $($script:passedTests)" -ForegroundColor Green
    }
    if ($script:failedTests -gt 0) {
        Write-Host "  ✗ Failed:          $($script:failedTests)" -ForegroundColor Red
    }
    if ($script:skippedTests -gt 0) {
        Write-Host "  ○ Skipped:         $($script:skippedTests)" -ForegroundColor Yellow
    }
    
    # Calculate success rate
    $successRate = [math]::Round(($script:passedTests / $script:totalTests) * 100, 1)
    Write-Host "  Success Rate:       $successRate%" -ForegroundColor $(if ($successRate -eq 100) { "Green" } elseif ($successRate -ge 80) { "Yellow" } else { "Red" })
    
    # Show failed tests details if any
    if ($script:failedTests -gt 0) {
        Write-Host "`n  Failed Tests:" -ForegroundColor Red
        $script:testResults | Where-Object { $_.Status -ne "PASSED" } | ForEach-Object {
            Write-Host "    - $($_.Name) (Exit code: $($_.ExitCode))" -ForegroundColor Red
        }
    }
}

# ===========================
# Overall Assessment
# ===========================
Write-Host "`nOverall Assessment:" -ForegroundColor Yellow

# Determine overall status
$infraOK = $hasService -and $hasIntelHw -and $hasDeviceNode
$testsOK = $script:totalTests -eq 0 -or ($script:failedTests -eq 0 -and $script:totalTests -gt 0)

if ($infraOK -and $testsOK) {
    Write-Host "  => ALL SYSTEMS OPERATIONAL!" -ForegroundColor Green
    if ($script:totalTests -gt 0) {
        Write-Host "     Infrastructure validated, $($script:totalTests) tests passed." -ForegroundColor Green
    } else {
        Write-Host "     Infrastructure validated, ready for testing." -ForegroundColor Green
    }
} elseif ($infraOK -and -not $testsOK) {
    Write-Host "  => INFRASTRUCTURE OK, BUT TESTS FAILED" -ForegroundColor Yellow
    Write-Host "     Driver installed, but $($script:failedTests) test(s) failed." -ForegroundColor Yellow
    Write-Host "     Review test output above for details." -ForegroundColor Yellow
} elseif (-not $infraOK -and $testsOK) {
    if ($hasService -and -not $hasIntelHw) {
        Write-Host "  => DRIVER INSTALLED BUT NO INTEL HARDWARE" -ForegroundColor Yellow
        Write-Host "     The driver requires Intel Ethernet hardware." -ForegroundColor Yellow
    } elseif (-not $hasService) {
        Write-Host "  => DRIVER NOT PROPERLY INSTALLED" -ForegroundColor Red
        Write-Host "     The driver service is not installed or registered." -ForegroundColor Red
        Write-Host "`n  Try:" -ForegroundColor Cyan
        Write-Host "    .\tools\setup\Install-Driver.ps1 -Configuration $Configuration -InstallDriver" -ForegroundColor White
    } else {
        Write-Host "  => PARTIAL INSTALLATION" -ForegroundColor Yellow
        Write-Host "     The driver is installed but not fully functional." -ForegroundColor Yellow
    }
} else {
    Write-Host "  => INFRASTRUCTURE AND TESTS FAILED" -ForegroundColor Red
    Write-Host "     Both infrastructure and test execution have issues." -ForegroundColor Red
}

Write-Host "`n"

# ===========================
# Interactive Test Prompt
# ===========================
if ($hasDeviceNode -and $hasTests -and -not $Quick -and -not $Full -and -not $TestExecutable) {
    $response = Read-Host "Would you like to run all available tests? (y/n)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        Write-Host "`nRunning all available tests..." -ForegroundColor Cyan
        $allTests = Get-ChildItem -Path $testExeDir -Filter "*.exe"
        foreach ($test in $allTests) {
            Write-Host "`n--- Running: $($test.Name) ---" -ForegroundColor Cyan
            & $test.FullName
            Write-Host ""
        }
    }
}

Write-Host "For detailed kernel debug output, use DebugView:" -ForegroundColor Cyan
Write-Host "  https://docs.microsoft.com/en-us/sysinternals/downloads/debugview" -ForegroundColor White
Write-Host ""

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Test Suite Complete!" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Cyan
