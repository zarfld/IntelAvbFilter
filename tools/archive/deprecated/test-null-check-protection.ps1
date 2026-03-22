# Test IntelAvbFilter NULL Check Protection
# Rapidly toggles network adapter to trigger race condition
# Should NOT crash if NULL checks are in place

param(
    [int]$Cycles = 50,
    [string]$AdapterName = "Ethernet 2"
)

Write-Host "================================================================"
Write-Host "  Intel AVB Filter Driver - NULL Check Test"
Write-Host "================================================================"
Write-Host ""
Write-Host "This test rapidly disables/enables the network adapter to"
Write-Host "trigger the race condition that caused bugcheck 0xD1."
Write-Host ""
Write-Host "With NULL checks in place, you should see debug messages"
Write-Host "instead of a crash."
Write-Host ""
Write-Host "Monitor DebugView for messages like:"
Write-Host "  'FilterSendNetBufferLists: NULL FilterModuleContext!'"
Write-Host "  'FilterReceiveNetBufferLists: NULL FilterModuleContext!'"
Write-Host ""
Write-Host "Test Configuration:" -ForegroundColor Cyan
Write-Host "  Adapter: $AdapterName"
Write-Host "  Cycles: $Cycles"
Write-Host "  Delay: 100ms between state changes"
Write-Host ""

# Verify adapter exists
$adapter = Get-NetAdapter -Name $AdapterName -ErrorAction SilentlyContinue
if (-not $adapter) {
    Write-Host "[ERROR] Network adapter '$AdapterName' not found!" -ForegroundColor Red
    Write-Host "Available adapters:" -ForegroundColor Yellow
    Get-NetAdapter | Format-Table Name, Status, InterfaceDescription
    exit 1
}

Write-Host "[INFO] Found adapter: $($adapter.InterfaceDescription)" -ForegroundColor Green
Write-Host ""

$confirm = Read-Host "Ready to start test? (y/N)"
if ($confirm -ne 'y' -and $confirm -ne 'Y') {
    Write-Host "Test cancelled."
    exit 0
}

Write-Host ""
Write-Host "Starting test..." -ForegroundColor Yellow
Write-Host "Press Ctrl+C to stop early"
Write-Host ""

$startTime = Get-Date
$errors = 0

try {
    for ($i = 1; $i -le $Cycles; $i++) {
        Write-Progress -Activity "Testing NULL Check Protection" -Status "Cycle $i of $Cycles" -PercentComplete (($i / $Cycles) * 100)
        
        try {
            # Disable adapter
            Disable-NetAdapter -Name $AdapterName -Confirm:$false -ErrorAction Stop
            Start-Sleep -Milliseconds 100
            
            # Enable adapter
            Enable-NetAdapter -Name $AdapterName -Confirm:$false -ErrorAction Stop
            Start-Sleep -Milliseconds 100
            
            Write-Host "Cycle $i/$Cycles completed" -ForegroundColor Gray
        }
        catch {
            Write-Host "Cycle $i failed: $($_.Exception.Message)" -ForegroundColor Red
            $errors++
        }
    }
}
catch {
    Write-Host "`n[ERROR] Test interrupted: $($_.Exception.Message)" -ForegroundColor Red
}
finally {
    Write-Progress -Activity "Testing NULL Check Protection" -Completed
}

$endTime = Get-Date
$duration = $endTime - $startTime

Write-Host ""
Write-Host "================================================================"
Write-Host "  Test Complete"
Write-Host "================================================================"
Write-Host "  Cycles Completed: $Cycles"
Write-Host "  Errors: $errors"
Write-Host "  Duration: $($duration.TotalSeconds.ToString('F1')) seconds"
Write-Host "  System State: " -NoNewline

# Check if system is still responsive
$driverCheck = Get-WmiObject Win32_SystemDriver | Where-Object {$_.Name -eq "IntelAvbFilter"}
if ($driverCheck -and $driverCheck.State -eq "Running") {
    Write-Host "✅ Driver still running - NO CRASH!" -ForegroundColor Green
} else {
    Write-Host "⚠️ Driver state unknown" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Next Steps:" -ForegroundColor Cyan
Write-Host "  1. Check DebugView for 'NULL FilterModuleContext' messages"
Write-Host "  2. Check Event Viewer for any bugcheck logs"
Write-Host "  3. If no crash occurred, the fix is working!"
Write-Host ""
