# IntelAvbFilter manual uninstall / cleanup
#
# Run from an elevated PowerShell session.
#
# Removes:
#   1. IntelAvbFilter kernel-driver service
#   2. IntelAvbFilter driver package from the Driver Store
#   3. IntelAvbFilter NetCfg / NDIS filter component
#   4. ETW provider manifest
#   5. Leftover .sys / .sys.old from "rename-locked-file" installs
#   6. Verifies that no adapter bindings remain
#
# Important identifiers:
#   Service name:       IntelAvbFilter
#   NetCfg ComponentID: MS_IntelAvbFilter
#   INF original name:  intelavbfilter.inf
#   ETW manifest:       IntelAvbFilter.man  (in System32\drivers)
#
# Do NOT hard-code the oemXX.inf number. Windows assigns this dynamically.

$ErrorActionPreference = "Stop"

$ServiceName = "IntelAvbFilter"
$ComponentId = "MS_IntelAvbFilter"
$OriginalInf = "intelavbfilter.inf"

Write-Host "=== IntelAvbFilter manual uninstall ==="
Write-Host ""

# ---------------------------------------------------------------------------
# 1. Check current service state
# ---------------------------------------------------------------------------

Write-Host "[1/6] Checking kernel-driver service..."

sc.exe query $ServiceName

# The driver may already be stopped. Try to stop it, but do not treat
# ERROR_SERVICE_NOT_ACTIVE / service-not-found as fatal.
Write-Host ""
Write-Host "Attempting to stop service..."

sc.exe stop $ServiceName 2>$null

# ---------------------------------------------------------------------------
# 2. Find the IntelAvbFilter Driver Store package
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "[2/6] Looking for Driver Store package..."

# pnputil output groups driver information in blocks.
# Find the block whose Original Name is intelavbfilter.inf and extract
# the corresponding Published Name (for example: oem3.inf).

$driverOutput = pnputil.exe /enum-drivers

$lines = $driverOutput -split "`r?`n"

$PublishedInf = $null

for ($i = 0; $i -lt $lines.Count; $i++) {

    if ($lines[$i] -match "Original Name:\s+$([regex]::Escape($OriginalInf))") {

        # Search backwards inside this driver block for "Published Name".
        for ($j = $i; $j -ge [Math]::Max(0, $i - 10); $j--) {

            if ($lines[$j] -match "Published Name:\s+(oem\d+\.inf)") {
                $PublishedInf = $Matches[1]
                break
            }
        }

        if ($PublishedInf) {
            break
        }
    }
}

if ($PublishedInf) {

    Write-Host "Found IntelAvbFilter package: $PublishedInf"

    # -----------------------------------------------------------------------
    # 3. Remove package from Driver Store
    # -----------------------------------------------------------------------

    Write-Host ""
    Write-Host "[3/6] Removing Driver Store package..."

    pnputil.exe /delete-driver $PublishedInf /uninstall /force

}
else {

    Write-Host ""
    Write-Host "[3/6] IntelAvbFilter INF is not present in Driver Store."
}

# ---------------------------------------------------------------------------
# 4. Remove service registration if it still exists
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "[4/6] Checking/removing service registration..."

sc.exe query $ServiceName 2>$null

if ($LASTEXITCODE -eq 0) {

    Write-Host "Service still exists. Deleting it..."

    sc.exe delete $ServiceName
}
else {

    Write-Host "Service is already absent."
}

# ---------------------------------------------------------------------------
# 4b. Unregister ETW provider manifest
#     Our install script runs: wevtutil im IntelAvbFilter.man
#     The corresponding uninstall is: wevtutil um
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "[4b/6] Unregistering ETW provider manifest..."

$manPath = "$env:SystemRoot\System32\drivers\IntelAvbFilter.man"
if (Test-Path $manPath) {
    wevtutil.exe um $manPath 2>$null
    Write-Host "  ETW manifest unregistered ($manPath)"
} else {
    Write-Host '  ETW manifest not present - skipping.'
}

# ---------------------------------------------------------------------------
# 4c. Remove leftover .sys / .sys.old / .sys.new / .sys.bak.* / .man files
#     Older installs used rename-to-.sys.old; current installs use retry-copy (no .sys.old).
#     Remove all variants to ensure a clean slate for the next install.
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "[4c/6] Cleaning up driver binary remnants in System32\drivers..."

foreach ($ext in @(".sys", ".sys.old", ".sys.new", ".man")) {
    $path = "$env:SystemRoot\System32\drivers\IntelAvbFilter$ext"
    if (Test-Path $path) {
        try {
            Remove-Item -Force $path
            Write-Host "  Removed $path"
        } catch {
            Write-Warning "  Could not remove $path : $_"
            Write-Host "  (Will be removed after reboot)"
        }
    }
}
# Remove any timestamped .sys.bak.* files left by previous installs
Get-Item "$env:SystemRoot\System32\drivers\IntelAvbFilter.sys.bak.*" -ErrorAction SilentlyContinue |
    ForEach-Object {
        try   { Remove-Item $_.FullName -Force; Write-Host "  Removed $($_.FullName)" }
        catch { Write-Warning "  Could not remove $($_.FullName) : $_" }
    }

# ---------------------------------------------------------------------------
# 5. Remove the actual NDIS / NetCfg component
#
# Note:
#   IntelAvbFilter     = kernel service name
#   MS_IntelAvbFilter = NetCfg component ID
#
# These are NOT interchangeable.
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "[5/6] Checking NetCfg component..."

netcfg.exe -q $ComponentId

if ($LASTEXITCODE -eq 0) {

    Write-Host ""
    Write-Host "Removing NetCfg component $ComponentId..."

    netcfg.exe -u $ComponentId
}
else {

    Write-Host "NetCfg component is already absent."
}

# ---------------------------------------------------------------------------
# 6. Verification
# ---------------------------------------------------------------------------

Write-Host ""
Write-Host "[6/6] Verification"
Write-Host "------------------------------------------------------------"

Write-Host ""
Write-Host "Service:"
sc.exe query $ServiceName 2>$null

if ($LASTEXITCODE -ne 0) {
    Write-Host "  OK - service not installed."
}

Write-Host ""
Write-Host "Driver Store:"

$remainingDriver = pnputil.exe /enum-drivers |
    Select-String -Pattern "intelavbfilter" -CaseSensitive:$false

if ($remainingDriver) {

    Write-Warning "IntelAvbFilter still appears in Driver Store:"
    $remainingDriver
}
else {

    Write-Host "  OK - IntelAvbFilter package not found."
}

Write-Host ""
Write-Host "NetCfg component:"

netcfg.exe -q $ComponentId

Write-Host ""
Write-Host "Network adapter bindings:"

$bindings = Get-NetAdapterBinding |
    Where-Object {
        $_.ComponentID -eq $ComponentId
    }

if ($bindings) {

    Write-Warning "IntelAvbFilter bindings still exist:"
    $bindings | Format-Table Name, DisplayName, ComponentID, Enabled -AutoSize
}
else {

    Write-Host "  OK - no IntelAvbFilter adapter bindings found."
}

Write-Host ""
Write-Host "=== Cleanup complete ==="
Write-Host ""
Write-Host "For a clean HIL/debugging baseline, reboot before reinstalling:"
Write-Host '    shutdown /r /t 0'