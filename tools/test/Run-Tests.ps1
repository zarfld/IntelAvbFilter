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
    [string]$TestExecutable
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
try {
    $handle = [System.IO.File]::Open($devicePath, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $handle.Close()
    Write-Success "Device node accessible: $devicePath"
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
    
} elseif ($Full) {
    # Quick tests only
    Write-Step "Running Quick Verification Tests"
    
    $quickTests = @(
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
    
    # Phase 0: Architecture Compliance Validation
    Write-Host "`n=== PHASE 0: Architecture Compliance Validation ===" -ForegroundColor Green
    Write-Host "Purpose: Verify core architectural requirements are met" -ForegroundColor Gray
    
    $phase0Tests = @(
        @{Name="avb_capability_validation_test.exe"; Desc="Verify realistic hardware capability reporting (no false advertising)"},
        @{Name="avb_device_separation_test.exe"; Desc="Verify clean device separation architecture compliance"}
    )
    foreach ($test in $phase0Tests) {
        $testPath = Join-Path $testExeDir $test.Name
        if (Test-Path $testPath) {
            Write-Host "`n  => $($test.Name)" -ForegroundColor Magenta
            Write-Host "     $($test.Desc)" -ForegroundColor Gray
            & $testPath
        }
    }
    
    # Phase 1: Basic Hardware Diagnostics
    Write-Host "`n=== PHASE 1: Basic Hardware Diagnostics ===" -ForegroundColor Green
    Write-Host "Purpose: Comprehensive hardware analysis and troubleshooting" -ForegroundColor Gray
    
    $phase1Tests = @(
        @{Name="avb_diagnostic_test.exe"; Desc="Comprehensive hardware diagnostics"},
        @{Name="avb_hw_state_test.exe"; Desc="Hardware state transitions and management"}
    )
    foreach ($test in $phase1Tests) {
        $testPath = Join-Path $testExeDir $test.Name
        if (Test-Path $testPath) {
            Write-Host "`n  => $($test.Name)" -ForegroundColor Cyan
            & $testPath
        }
    }
    
    # Phase 2: TSN IOCTL Handler Verification
    Write-Host "`n=== PHASE 2: TSN IOCTL Handler Verification (Critical Fix Validation) ===" -ForegroundColor Green
    Write-Host "Purpose: Verify TAS/FP/PTM IOCTLs no longer return ERROR_INVALID_FUNCTION" -ForegroundColor Gray
    
    $phase2Tests = @(
        @{Name="test_tsn_ioctl_handlers.exe"; Desc="TSN IOCTL handler validation"},
        @{Name="tsn_hardware_activation_validation.exe"; Desc="TSN features activate at hardware level"}
    )
    foreach ($test in $phase2Tests) {
        $testPath = Join-Path $testExeDir $test.Name
        if (Test-Path $testPath) {
            Write-Host "`n  => $($test.Name)" -ForegroundColor Yellow
            & $testPath
        }
    }
    
    # Phase 3: Multi-Adapter Hardware Testing
    Write-Host "`n=== PHASE 3: Multi-Adapter Hardware Testing ===" -ForegroundColor Green
    
    $testPath = Join-Path $testExeDir "avb_multi_adapter_test.exe"
    if (Test-Path $testPath) {
        Write-Host "`n  => avb_multi_adapter_test.exe" -ForegroundColor Yellow
        & $testPath
    }
    
    # Phase 4: I226 Advanced Feature Testing
    Write-Host "`n=== PHASE 4: I226 Advanced Feature Testing ===" -ForegroundColor Green
    
    $phase4Tests = @("avb_i226_test.exe", "avb_i226_advanced_test.exe")
    foreach ($testName in $phase4Tests) {
        $testPath = Join-Path $testExeDir $testName
        if (Test-Path $testPath) {
            Write-Host "`n  => $testName" -ForegroundColor Magenta
            & $testPath
        }
    }
    
    # Phase 5: I210 Basic Testing
    Write-Host "`n=== PHASE 5: I210 Basic Testing ===" -ForegroundColor Green
    
    $testPath = Join-Path $testExeDir "avb_test_i210.exe"
    if (Test-Path $testPath) {
        Write-Host "`n  => avb_test_i210.exe" -ForegroundColor Blue
        & $testPath
    }
    
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
        $testPath = Join-Path $testExeDir $testName
        if (Test-Path $testPath) {
            Write-Host "`n  => $testName" -ForegroundColor Cyan
            & $testPath
        }
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

Write-Host "`n================================================================" -ForegroundColor Cyan
Write-Host "  Test Suite Complete!" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Cyan
