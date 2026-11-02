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
    [string]$TestArgs = "",

    [Parameter(Mandatory=$false)]
    [switch]$HardwareOnly,
    
    [Parameter(Mandatory=$false)]
    [switch]$SecureBootCheck
)

$ErrorActionPreference = 'Stop'

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

# ===========================
# Lifecycle Metrics Snapshot -- SSOT: tools/lib/Lib-AvbLifecycle.ps1
# ===========================
. (Join-Path $PSScriptRoot '..\lib\Lib-AvbLifecycle.ps1')

# ===========================
# Authoritative dispatch (TEST-HILPATH-001, Q decision 1 2026-10-29)
# ===========================
# This script never starts a test program itself.  Every test runs through tools\test\Run-Tests-CI.ps1: machine lock,
# build provenance, HIL safety class, #328 hazard holds, exact-target fan-out, adapter baseline, post-test state check
# and results document.  An entry the runner registry does not know fails closed (inventory gap, TEST-COMPLETE).
$runCi = Join-Path $PSScriptRoot 'Run-Tests-CI.ps1'
$script:DispatchRuns = @()

# Same normalization as Run-Tests-CI.ps1 ConvertTo-AvbHilEntryKey: 'x.exe args' -> 'x args'; ps1: entries unchanged.
function ConvertTo-AvbEntryKey([string]$Entry) {
    if ($Entry.StartsWith('ps1:')) { return $Entry }
    $p = $Entry.Trim() -split '\s+', 2
    $b = $p[0] -replace '\.exe$', ''
    if ($p.Count -gt 1) { return "$b $($p[1])" }
    return $b
}

# Runner registry (parsed from Run-Tests-CI.ps1, never executed): suite -> entries in its list, its declared coverage
# gaps and (HardwareMutation) its attended-only table.
function Get-AvbRunnerRegistry {
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($runCi, [ref]$null, [ref]$null)
    $reg = [ordered]@{}
    foreach ($s in 'HardwareUnit', 'HardwareIntegration', 'HardwareMutation', 'HardwareMandatory', 'Unit', 'Integration') {
        $names = @("${s}Tests", "${s}CoverageGaps"); if ($s -eq 'HardwareMutation') { $names += 'HardwareMutationAttendedOnly' }
        $entries = @()
        foreach ($n in $names) {
            $a = $ast.FindAll({ param($x) $x -is [System.Management.Automation.Language.AssignmentStatementAst] -and $x.Left.Extent.Text -eq "`$$n" }, $true) | Select-Object -First 1
            if (-not $a) { continue }
            $h = $a.Right.Find({ param($x) $x -is [System.Management.Automation.Language.HashtableAst] }, $true)
            if ($h) { $entries += @($h.KeyValuePairs | ForEach-Object { $_.Item1.Value }) }
            else { $entries += @($a.Right.FindAll({ param($x) $x -is [System.Management.Automation.Language.StringConstantExpressionAst] }, $true) | ForEach-Object { $_.Value }) }
        }
        $reg[$s] = @($entries | ForEach-Object { ConvertTo-AvbEntryKey $_ } | Sort-Object -Unique)
    }
    return $reg
}

# Program name as typed (Build-Tests Output, e.g. avb_test_i210_um.exe, or a test script name) -> runner entry + suite.
# Suite preference: HardwareUnit, HardwareIntegration, HardwareMutation, HardwareMandatory, Unit, Integration.
function Resolve-AvbRunnerEntry {
    param([string]$Name, [string]$Arguments, $Registry)
    $n = $Name.Trim()
    if ($n -match '(?i)\.ps1$' -and -not $n.StartsWith('ps1:')) {
        $f = Get-ChildItem -Path (Join-Path $repoRoot 'tests') -Filter ([System.IO.Path]::GetFileName($n)) -Recurse -File -ErrorAction SilentlyContinue | Select-Object -First 1
        $n = if ($f) { 'ps1:' + $f.FullName.Substring($repoRoot.Length + 1).Replace('\', '/') } else { "ps1:$n" }
    } elseif (-not $n.StartsWith('ps1:')) {
        $base = $n -replace '(?i)\.exe$', ''
        $name = $null
        foreach ($l in (Get-Content -LiteralPath (Join-Path $repoRoot 'tools\build\Build-Tests.ps1'))) {
            if ($l -match '^\s*Name\s*=\s*"([^"]+)"') { $name = $Matches[1] }
            elseif ($name -and $l -match '^\s*Output\s*=\s*"([^"]+)"' -and [System.IO.Path]::GetFileNameWithoutExtension($Matches[1]) -eq $base) { $base = $name; break }
        }
        $n = $base
    }
    $key = ConvertTo-AvbEntryKey $(if ($Arguments) { "$n $Arguments" } else { $n })
    foreach ($s in $Registry.Keys) { if (@($Registry[$s]) -contains $key) { return @{ Entry = $key; Suite = $s } } }
    return @{ Entry = $key; Suite = $null }
}

# One Run-Tests-CI.ps1 invocation (in-process, like LocalSuites): results document + logs under $ReportDir; earlier
# suites of this run are prior evidence (-PriorResultsDir), so a halt carries forward as in hil.yml.
function Invoke-AvbDispatch {
    param([string]$Label, [string]$Suite, [string[]]$Entries = @(), [string]$ReportDir)
    $logs = Join-Path $ReportDir $Suite
    New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $json = Join-Path $ReportDir ("results-{0}-{1}-{2}.json" -f $Suite, $Configuration, (Get-Date -Format 'HHmmss'))
    $ciArgs = @{ Suite = $Suite; Configuration = $Configuration; LogsDir = $logs; ResultsJson = $json; PriorResultsDir = $ReportDir }
    if (@($Entries).Count -gt 0) { $ciArgs.Tests = @($Entries) }
    Write-Host ("`n  => Run-Tests-CI.ps1 -Suite {0}{1}" -f $Suite, $(if (@($Entries).Count) { ' -Tests ' + (@($Entries) -join ', ') } else { '' })) -ForegroundColor Cyan
    $global:LASTEXITCODE = $null
    & $runCi @ciArgs
    $ec = $global:LASTEXITCODE
    if ($null -eq $ec) { $ec = 1 }   # no exit code = failure
    $script:totalTests++
    $st = if ($ec -eq 0) { 'PASSED' } else { 'FAILED' }
    if ($ec -eq 0) { $script:passedTests++ } else { $script:failedTests++ }
    $script:testResults += [PSCustomObject]@{ Name = "$Label [Run-Tests-CI $Suite]"; Status = $st; ExitCode = $ec }
    $script:DispatchRuns += [PSCustomObject]@{ Label = $Label; Suite = $Suite; Exit = $ec; Results = $json }
}

# Hardware-independent static verification scripts (no test program; CI runs them as workflow steps too).
function Invoke-AvbStaticVerification([string[]]$Ids) {
    $map = [ordered]@{
        'TEST-SSOT-001' = @('tests\verification\ssot\Test-SSOT-001-NoDuplicates.ps1', $false)
        'TEST-SSOT-003' = @('tests\verification\ssot\Test-SSOT-003-IncludePattern.ps1', $false)
        'TEST-SSOT-004' = @('tests\verification\ssot\Test-SSOT-004-Completeness.ps1', $false)
        'TEST-REGS-001' = @('tests\verification\regs\Test-REGS-001-BuildVerification.ps1', $true)
        'TEST-REGS-003' = @('tests\verification\regs\Test-REGS-003-RegisterConstantVerification.ps1', $true)
    }
    if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory -Path $logsDir -Force | Out-Null }
    foreach ($id in $Ids) {
        $script:totalTests++
        $verificationScript = Join-Path $repoRoot $map[$id][0]
        if (-not (Test-Path -LiteralPath $verificationScript)) {
            $script:failedTests++; Write-Failure "$id script missing: $verificationScript"
            $script:testResults += [PSCustomObject]@{ Name = $id; Status = 'FAILED'; ExitCode = 'N/A' }; continue
        }
        Write-Host "`n    => $id" -ForegroundColor Yellow
        $logFile = Join-Path $logsDir "${id}_$(Get-Date -Format 'yyyyMMdd_HHmmss').log"
        $global:LASTEXITCODE = $null
        if ($map[$id][1]) { & $verificationScript -Configuration $Configuration 2>&1 | Tee-Object -FilePath $logFile }
        else { & $verificationScript 2>&1 | Tee-Object -FilePath $logFile }
        $ec = $global:LASTEXITCODE
        if ($ec -eq 0) { $script:passedTests++; Write-Success "$id PASSED"; $script:testResults += [PSCustomObject]@{ Name = $id; Status = 'PASSED'; ExitCode = 0 } }
        else { $script:failedTests++; Write-Failure "$id FAILED (exit $ec)"; $script:testResults += [PSCustomObject]@{ Name = $id; Status = 'FAILED'; ExitCode = $ec } }
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
$logsDir = Join-Path $repoRoot "logs"
$testExeDir = Join-Path $repoRoot "build\tools\avb_test\x64\$Configuration"

# Test execution tracking
$script:totalTests = 0
$script:passedTests = 0
$script:failedTests = 0
$script:skippedTests = 0   # initialized here; do not rely on implicit $null
$script:testResults = @()

# ===========================
# Precondition Gate -- test signing, Secure Boot, driver, hardware
# ===========================
$precondScript = Join-Path $toolsDir 'setup\Test-Preconditions.ps1'
if (Test-Path $precondScript) {
    Write-Step "Checking test preconditions"
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $precondScript -ForTest
    $precondExit = $LASTEXITCODE
    if ($precondExit -eq 1) {
        Write-Host "[PRECONDITIONS] Critical checks failed. Fix issues above before running tests." -ForegroundColor Red
        exit 1
    }
    # Exit code 2 (warnings only) -- continue with caution
} else {
    Write-Host "[WARN] Test-Preconditions.ps1 not found -- skipping precondition checks." -ForegroundColor Yellow
}

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
    # Legacy: compiled tests\device_specific\i219\avb_test_i219.c with /DHARDWARE_ONLY=1 into the repository root and
    # started it directly.  The source never references HARDWARE_ONLY (checked 2026-10-29): the program is the
    # registered entry avb_test_i219 (Mutating:Target, one run per supported adapter), run in Test 5 through
    # Run-Tests-CI.ps1 before the selected mode.
    Write-Step "Hardware-Only test requested: registered entry avb_test_i219 (runs in Test 5 through Run-Tests-CI.ps1)"
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
                            "15BB" { "Intel I219-LM Gigabit Network Connection" }
                            "15BC" { "Intel I219-V Gigabit Network Connection" }
                            "15BD" { "Intel I219-LM Gigabit Network Connection" }
                            "15BE" { "Intel I219-V Gigabit Network Connection" }
                            "15F3" { "Intel I219-LM Gigabit Network Connection" }
                            "15F4" { "Intel I219-V Gigabit Network Connection" }
                            "125B" { "Intel I226-V 2.5GbE Network Connection" }
                            "125C" { "Intel I226-LM 2.5GbE Network Connection" }
                            "1521" { "Intel I350 Gigabit Network Connection" }
                            default { "Unknown Intel Device (0x$did)" }
                        }
                        Write-Host "    Device: $deviceName" -ForegroundColor Cyan
                        
                        # Check if supported
                        $supported = $did -in @("1533", "153A", "15BB", "15BC", "15BD", "15BE", "15F3", "15F4", "125B", "125C", "1521")
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
# Test 5: Execute Tests -- through tools\test\Run-Tests-CI.ps1 only
# ===========================
#   -TestExecutable X [-TestArgs A]  the registered runner entry 'X A' in the suite that lists or declares it; an
#                                    unregistered program fails closed (inventory gap, TEST-COMPLETE) and never runs
#   -Quick                           PARTIAL / SMOKE run: the legacy quick list as registered entries, per suite in
#                                    the HIL order; its success never satisfies HIL coverage
#   -Full, or no mode switch         FULL run (the legacy default ran every test): static SSOT/REGS verification, then
#                                    Unit, HardwareUnit, HardwareIntegration, HardwareMutation, HardwareMandatory (last,
#                                    as in hil.yml and LocalSuites), chained through -PriorResultsDir
# Not done here any more: the legacy EventLog subscription refresh before *event_log* tests (EventLog restart, which can
# detach the filter, then an IntelAvbFilter service start) - an ungated service change outside the machine lock and
# without restoration.  test_event_log is gate-BLOCKED (Mutating); its setup is an open requirement in $HilTestSafety.
$ReportDir = Join-Path $logsDir ("RunTests_{0}_{1}" -f $Configuration, (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $ReportDir -Force | Out-Null
$runnerRegistry = Get-AvbRunnerRegistry
$suiteOrder = @('Unit', 'HardwareUnit', 'HardwareIntegration', 'HardwareMutation', 'HardwareMandatory')
Write-Info "Dispatcher: $runCi   Report: $ReportDir"

function Add-AvbUnregistered([string]$Entry) {
    Write-Failure "[BLOCKED] '$Entry' is not a registered runner entry (no suite list, coverage gap or attended-only table in tools\test\Run-Tests-CI.ps1) - inventory gap, not executed"
    $script:totalTests++; $script:failedTests++
    $script:testResults += [PSCustomObject]@{ Name = $Entry; Status = 'BLOCKED(UNREGISTERED)'; ExitCode = 'N/A' }
}

if ($HardwareOnly) { Invoke-AvbDispatch -Label 'hardware-only' -Suite 'HardwareIntegration' -Entries @('avb_test_i219') -ReportDir $ReportDir }

if ($TestExecutable) {
    Write-Step "Running Specific Test: $TestExecutable $TestArgs (Run-Tests-CI.ps1)"
    $r = Resolve-AvbRunnerEntry -Name $TestExecutable -Arguments $TestArgs -Registry $runnerRegistry
    if ($r.Suite) { Invoke-AvbDispatch -Label $r.Entry -Suite $r.Suite -Entries @($r.Entry) -ReportDir $ReportDir }
    else { Add-AvbUnregistered $r.Entry }
} elseif ($Quick) {
    Write-Step "Running Quick Verification Tests -- PARTIAL / SMOKE RUN (never satisfies HIL coverage)"
    Invoke-AvbStaticVerification -Ids @('TEST-SSOT-001')
    # The legacy quick list (program names as built), resolved to registered entries.
    $quickPrograms = @('test_multidev_adapter_enum.exe', 'test_device_register_access.exe', 'test_ndis_send_path.exe',
                       'test_ndis_receive_path.exe', 'test_tx_timestamp_retrieval.exe', 'test_hw_state_machine.exe',
                       'test_lazy_initialization.exe', 'test_registry_diagnostics.exe', 'ptp_clock_control_test.exe',
                       'ptp_clock_control_production_test.exe', 'avb_capability_validation_test.exe', 'avb_diagnostic_test.exe',
                       'Test-DebugLevelVerbosity.ps1')
    $bySuite = [ordered]@{}
    foreach ($q in $quickPrograms) {
        $r = Resolve-AvbRunnerEntry -Name $q -Arguments '' -Registry $runnerRegistry
        if (-not $r.Suite) { Add-AvbUnregistered $r.Entry; continue }
        if (-not $bySuite.Contains($r.Suite)) { $bySuite[$r.Suite] = @() }
        $bySuite[$r.Suite] += $r.Entry
    }
    foreach ($s in $suiteOrder) { if ($bySuite.Contains($s)) { Invoke-AvbDispatch -Label 'quick (partial)' -Suite $s -Entries $bySuite[$s] -ReportDir $ReportDir } }
    Write-Host "`n  [PARTIAL] Quick run only - HIL coverage requires the full run (-Full: Unit + all four hardware suites)" -ForegroundColor Yellow
} else {
    Write-Step "Running Full Test Suite: static verification + Unit + HardwareUnit, HardwareIntegration, HardwareMutation, HardwareMandatory"
    Invoke-AvbStaticVerification -Ids @('TEST-SSOT-001', 'TEST-SSOT-003', 'TEST-SSOT-004', 'TEST-REGS-001', 'TEST-REGS-003')
    foreach ($s in $suiteOrder) { Invoke-AvbDispatch -Label 'full' -Suite $s -ReportDir $ReportDir }
}
foreach ($d in $script:DispatchRuns) { Write-Info ("Run-Tests-CI {0} [{1}] exit {2}: {3}" -f $d.Suite, $d.Label, $d.Exit, $d.Results) }

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
        Write-Host "  OK Passed:          $($script:passedTests)" -ForegroundColor Green
    }
    if ($script:failedTests -gt 0) {
        Write-Host "  X Failed:          $($script:failedTests)" -ForegroundColor Red
    }
    if ($script:skippedTests -gt 0) {
        Write-Host "  o Skipped:         $($script:skippedTests)" -ForegroundColor Yellow
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
# Interactive Test Prompt -- removed 2026-10-29 (TEST-HILPATH-001): it ran every *.exe of the test directory
# directly.  Running this script without a mode switch now IS the full run (all registered suites, gated).
# ===========================

Write-Host "For detailed kernel debug output, use DebugView:" -ForegroundColor Cyan
Write-Host "  https://docs.microsoft.com/en-us/sysinternals/downloads/debugview" -ForegroundColor White
Write-Host ""

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Test Suite Complete!" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Cyan

# ===========================
# Propagate exit code
# Exit codes:
#   0 = PASS      -- all tests passed (or no tests were run as a deliberate no-op)
#   1 = FAIL      -- at least one test failed, blocked, or reported CLEANUP_FAILED
#   2 = SKIP_ONLY -- tests ran but all produced SKIP_NO_TESTS (no hardware/device)
# ===========================
if ($script:failedTests -gt 0) {
    exit 1
} elseif ($script:passedTests -eq 0 -and $script:skippedTests -gt 0) {
    exit 2
} else {
    exit 0
}
