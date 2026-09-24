# Reproduction + regression sequence for issue #328:
#   Core repro: test_ptp_phc_stability.exe twice back-to-back.
#   First run's UT-CORR-007/008 leaves the DPC TX-timestamp timer running;
#   second run's UT-CORR-009 driver-reload then blocks in FilterPause/AvbStopTimers
#   (was: 5-hour hang in StartServiceA).
#   Lifecycle tests show driver binding/unbinding state between cycles.
#
# USAGE: must be run from an ELEVATED PowerShell session.
#   Right-click PowerShell -> "Run as administrator", then:
#   Set-Location <repo>; .\tools\test\issue_328repro.ps1

# Abort early if not elevated — DbgView capture requires the same privilege level.
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Run this script from an elevated (Administrator) PowerShell session."
    exit 1
}

.\tools\build\Build-Driver.ps1 -Configuration Debug
.\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall

# After UT-CORR-009 the service is STOPPED (NDIS double-rebind cycle).
# Reinstall restores it before the next run.
function Ensure-DriverRunning {
    $state = (sc.exe query IntelAvbFilter | Select-String "STATE").ToString()
    if ($state -notmatch "4  RUNNING") {
        Write-Host "[issue_328repro] Service not Running - reinstalling to restore binding..."
        .\tools\setup\Install-Driver-Elevated.ps1 -Configuration Debug -Action Reinstall
    }
}

$TstSequ = @(
    # Cycle 1: baseline lifecycle status, then first phc_stability run
    "test_control_device_lifecycle.exe",
    "test_lifecycle_coverage.exe",
    "test_ptp_phc_stability.exe",   # Run 1: leaves DPC timer running after UT-CORR-007/008

    # Cycle 2: lifecycle after reload, then second phc_stability — was: 5-hour hang here
    "test_control_device_lifecycle.exe",
    "test_lifecycle_coverage.exe",
    "test_ptp_phc_stability.exe",   # Run 2: UT-CORR-009 hits FilterPause while timer active

    # Cycle 3: idempotency / lifecycle after second reload
    "test_control_device_lifecycle.exe",
    "test_lifecycle_coverage.exe"
)

foreach ($t in $TstSequ) {
  #  Ensure-DriverRunning   - no workaround to enable driver in red-test!!!
    .\tools\test\Run-Tests-Elevated.ps1 -TestName $t -CaptureDbgView -Configuration Debug
}