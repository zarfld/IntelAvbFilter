# Intel AVB Filter Driver - Comprehensive Test Script
# Run this script as Administrator after driver installation

param(
    [Parameter(Mandatory=$false)]
    [switch]$SkipHardwareCheck,
    
    [Parameter(Mandatory=$false)]
    [switch]$RunAllTests,
    
    [Parameter(Mandatory=$false)]
    [switch]$CollectLogs
)

# Require Administrator
if (-NOT ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "This script requires Administrator privileges"
    exit 1
}

function Write-Step {
    param([string]$Message)
    Write-Host "`n==> $Message" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "? $Message" -ForegroundColor Green
}

function Write-Failure {
    param([string]$Message)
    Write-Host "? $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "? $Message" -ForegroundColor Yellow
}

# Banner
Write-Host @"
??????????????????????????????????????????????????????????????????
?                                                                ?
?      Intel AVB Filter Driver - Comprehensive Test Suite       ?
?                                                                ?
??????????????????????????????????????????????????????????????????

"@ -ForegroundColor Cyan

# Test 1: Check Driver Service Status
Write-Step "Test 1: Checking Driver Service Status"

try {
    $service = Get-Service -Name "IntelAvbFilter" -ErrorAction Stop
    
    if ($service.Status -eq 'Running') {
        Write-Success "Driver service is RUNNING"
        Write-Host "  Display Name: $($service.DisplayName)" -ForegroundColor Gray
        Write-Host "  Start Type: $($service.StartType)" -ForegroundColor Gray
    } else {
        Write-Failure "Driver service is $($service.Status)"
        Write-Info "This may be normal if no Intel hardware is detected"
    }
} catch {
    Write-Failure "Driver service not found"
    Write-Host "  Error: $($_.Exception.Message)" -ForegroundColor Red
    Write-Info "Driver may not be installed correctly"
}

# Test 2: Enumerate Intel Network Adapters
Write-Step "Test 2: Enumerating Intel Network Adapters"

$intelAdapters = Get-NetAdapter | Where-Object {$_.InterfaceDescription -like "*Intel*"}

if ($intelAdapters) {
    Write-Success "Found $($intelAdapters.Count) Intel network adapter(s)"
    
    foreach ($adapter in $intelAdapters) {
        Write-Host "`n  Adapter: $($adapter.InterfaceDescription)" -ForegroundColor White
        Write-Host "    Name: $($adapter.Name)" -ForegroundColor Gray
        Write-Host "    Status: $($adapter.Status)" -ForegroundColor Gray
        Write-Host "    MAC: $($adapter.MacAddress)" -ForegroundColor Gray
        Write-Host "    Speed: $($adapter.LinkSpeed)" -ForegroundColor Gray
        
        # Get hardware ID
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
                    $supported = $did -in @("1533", "153A", "15F3", "125B", "125C", "1521", "1522", "1523", "1524")
                    if ($supported) {
                        Write-Success "    ? This device is SUPPORTED by the driver"
                    } else {
                        Write-Info "    ? This device may not be fully supported"
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

# Test 3: Check Device Node
Write-Step "Test 3: Checking Device Node Creation"

try {
    $deviceExists = [System.IO.File]::Exists("\\.\IntelAvbFilter")
    
    if ($deviceExists) {
        Write-Success "Device node \\.\IntelAvbFilter exists"
        Write-Info "This indicates the driver has created its device interface"
        
        # Try to open the device
        try {
            $handle = [System.IO.File]::Open("\\.\IntelAvbFilter", 
                [System.IO.FileMode]::Open, 
                [System.IO.FileAccess]::ReadWrite, 
                [System.IO.FileShare]::ReadWrite)
            
            if ($handle) {
                Write-Success "Successfully opened device node"
                $handle.Close()
            }
        } catch {
            Write-Failure "Could not open device node: $($_.Exception.Message)"
        }
    } else {
        Write-Failure "Device node \\.\IntelAvbFilter does NOT exist"
        Write-Info "This is normal if:"
        Write-Info "  • No supported Intel hardware is present"
        Write-Info "  • Intel hardware is present but driver didn't load"
        Write-Info "  • Driver service is stopped"
    }
} catch {
    Write-Failure "Error checking device node: $($_.Exception.Message)"
}

# Test 4: Check Test Application Availability
Write-Step "Test 4: Checking Test Applications"

$testDir = "$PSScriptRoot\tools\avb_test\x64\Debug"
if (-not (Test-Path $testDir)) {
    $testDir = "$PSScriptRoot\x64\Debug"
}

$testApps = @(
    "avb_test.exe",
    "avb_multi_adapter_test.exe",
    "test_tsn_ioctl_handlers.exe",
    "avb_test_i210.exe",
    "avb_i226_advanced_test.exe"
)

$availableTests = @()

if (Test-Path $testDir) {
    Write-Success "Test directory found: $testDir"
    
    foreach ($app in $testApps) {
        $appPath = Join-Path $testDir $app
        if (Test-Path $appPath) {
            Write-Success "  ? $app"
            $availableTests += $appPath
        } else {
            Write-Info "  ? $app (not built)"
        }
    }
} else {
    Write-Failure "Test directory not found"
    Write-Info "You may need to build the solution first"
    Write-Host "  Command: msbuild IntelAvbFilter.sln /p:Configuration=Debug /p:Platform=x64" -ForegroundColor Gray
}

# Test 5: Run Basic Device Test
if ($availableTests.Count -gt 0) {
    Write-Step "Test 5: Running Basic Device Test"
    
    $basicTest = $availableTests | Where-Object {$_ -like "*avb_test.exe"}
    if ($basicTest) {
        Write-Host "  Running: $basicTest" -ForegroundColor Gray
        Write-Host ""
        
        $output = & $basicTest 2>&1 | Out-String
        Write-Host $output
        
        if ($output -like "*Device opened successfully*") {
            Write-Success "Basic device test PASSED"
        } elseif ($output -like "*Open*failed*2*") {
            Write-Info "Device node not accessible (Error 2 - FILE_NOT_FOUND)"
            Write-Info "This is expected if no Intel hardware is detected"
        } else {
            Write-Failure "Basic device test had unexpected result"
        }
    }
}

# Test 6: Run TSN IOCTL Handler Test (if available and RunAllTests)
if ($RunAllTests -and $availableTests.Count -gt 0) {
    Write-Step "Test 6: Running TSN IOCTL Handler Test"
    
    $tsnTest = $availableTests | Where-Object {$_ -like "*test_tsn_ioctl_handlers.exe"}
    if ($tsnTest) {
        Write-Host "  Running: $tsnTest" -ForegroundColor Gray
        Write-Host ""
        
        & $tsnTest
    } else {
        Write-Info "TSN IOCTL handler test not available"
    }
}

# Test 7: Check Event Viewer Logs
if ($CollectLogs) {
    Write-Step "Test 7: Collecting Event Viewer Logs"
    
    try {
        $events = Get-EventLog -LogName System -Source "*Intel*" -Newest 20 -ErrorAction SilentlyContinue
        
        if ($events) {
            Write-Success "Found $($events.Count) recent Intel-related events"
            
            foreach ($event in $events) {
                $color = switch ($event.EntryType) {
                    "Error" { "Red" }
                    "Warning" { "Yellow" }
                    default { "Gray" }
                }
                
                Write-Host "  [$($event.TimeGenerated)] $($event.EntryType): $($event.Message.Split("`n")[0])" -ForegroundColor $color
            }
        } else {
            Write-Info "No recent Intel-related events in System log"
        }
    } catch {
        Write-Info "Could not retrieve event logs"
    }
}

# Summary
Write-Host "`n" -NoNewline
Write-Host "????????????????????????????????????????????????????????????????" -ForegroundColor Cyan
Write-Host "                         TEST SUMMARY" -ForegroundColor Cyan
Write-Host "????????????????????????????????????????????????????????????????" -ForegroundColor Cyan

# Determine overall status
$hasService = (Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue) -ne $null
$hasIntelHw = $intelAdapters.Count -gt 0
$hasDeviceNode = try { [System.IO.File]::Exists("\\.\IntelAvbFilter") } catch { $false }
$hasTests = $availableTests.Count -gt 0

Write-Host "`nDriver Status:" -ForegroundColor Yellow
if ($hasService) {
    Write-Host "  ? Driver service installed" -ForegroundColor Green
} else {
    Write-Host "  ? Driver service not found" -ForegroundColor Red
}

Write-Host "`nHardware Status:" -ForegroundColor Yellow
if ($hasIntelHw) {
    Write-Host "  ? Intel network adapter(s) detected: $($intelAdapters.Count)" -ForegroundColor Green
} else {
    Write-Host "  ? No Intel network adapters detected" -ForegroundColor Red
}

Write-Host "`nDevice Interface Status:" -ForegroundColor Yellow
if ($hasDeviceNode) {
    Write-Host "  ? Device node created and accessible" -ForegroundColor Green
} else {
    Write-Host "  ? Device node not created" -ForegroundColor Red
    if (-not $hasIntelHw) {
        Write-Host "    (Expected - no Intel hardware)" -ForegroundColor Gray
    }
}

Write-Host "`nTest Tools Status:" -ForegroundColor Yellow
if ($hasTests) {
    Write-Host "  ? Test applications available: $($availableTests.Count)" -ForegroundColor Green
} else {
    Write-Host "  ? Test applications not built" -ForegroundColor Yellow
}

Write-Host "`nOverall Assessment:" -ForegroundColor Yellow
if ($hasService -and $hasIntelHw -and $hasDeviceNode) {
    Write-Host "  ?? DRIVER IS FULLY OPERATIONAL!" -ForegroundColor Green
    Write-Host "     The driver is installed, Intel hardware is detected," -ForegroundColor Green
    Write-Host "     and the device interface is accessible." -ForegroundColor Green
    Write-Host "`n  Next steps:" -ForegroundColor Cyan
    Write-Host "    • Run test applications to verify functionality" -ForegroundColor White
    Write-Host "    • Use DebugView to monitor kernel debug output" -ForegroundColor White
    Write-Host "    • Test AVB/TSN features with your application" -ForegroundColor White
} elseif ($hasService -and -not $hasIntelHw) {
    Write-Host "  ? DRIVER INSTALLED BUT NO INTEL HARDWARE" -ForegroundColor Yellow
    Write-Host "     The driver is installed correctly but requires" -ForegroundColor Yellow
    Write-Host "     Intel Ethernet hardware to function." -ForegroundColor Yellow
} elseif (-not $hasService) {
    Write-Host "  ? DRIVER NOT PROPERLY INSTALLED" -ForegroundColor Red
    Write-Host "     The driver service is not installed or registered." -ForegroundColor Red
    Write-Host "`n  Try:" -ForegroundColor Cyan
    Write-Host "    .\setup_driver.ps1 -InstallDriver" -ForegroundColor White
} else {
    Write-Host "  ? PARTIAL INSTALLATION" -ForegroundColor Yellow
    Write-Host "     The driver is installed but not fully functional." -ForegroundColor Yellow
}

Write-Host "`n"

# Offer to run more tests
if ($hasDeviceNode -and $availableTests.Count -gt 0 -and -not $RunAllTests) {
    $response = Read-Host "Would you like to run all available tests? (y/n)"
    if ($response -eq 'y' -or $response -eq 'Y') {
        Write-Host "`nRunning all available tests..." -ForegroundColor Cyan
        foreach ($test in $availableTests) {
            Write-Host "`n--- Running: $(Split-Path $test -Leaf) ---" -ForegroundColor Cyan
            & $test
            Write-Host ""
        }
    }
}

Write-Host "For detailed kernel debug output, use DebugView:" -ForegroundColor Cyan
Write-Host "  https://docs.microsoft.com/en-us/sysinternals/downloads/debugview" -ForegroundColor White
Write-Host ""
