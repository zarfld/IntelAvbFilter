<#
.SYNOPSIS
    Shared library for driver operations

.DESCRIPTION
    Common functions for driver installation, status checking, and management.
    Eliminates code duplication across Install-Driver.ps1, Uninstall-Driver.ps1, etc.

.NOTES
    Part of: Issue #27 (REQ-NF-SCRIPTS-001: Script Consolidation)
    Reduces code duplication (TC-SCRIPTS-002, TC-SCRIPTS-005)
#>

function Test-DriverInstalled {
    <#
    .SYNOPSIS
        Check if Intel AVB Filter Driver is installed
    
    .OUTPUTS
        Boolean - True if driver installed, False otherwise
    #>
    $service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
    return ($null -ne $service)
}

function Get-DriverInfPath {
    <#
    .SYNOPSIS
        Get path to driver INF file
    
    .PARAMETER Configuration
        Build configuration (Debug or Release)
    
    .OUTPUTS
        String - Full path to INF file
    #>
    param(
        [ValidateSet('Debug', 'Release')]
        [string]$Configuration = 'Release'
    )
    
    $basePath = "$PSScriptRoot\..\..\x64\$Configuration"
    $infPath = Join-Path $basePath "IntelAvbFilter.inf"
    
    if (-not (Test-Path $infPath)) {
        throw "INF not found: $infPath. Build driver first using Build-Driver.ps1"
    }
    
    return $infPath
}

function Uninstall-Driver {
    <#
    .SYNOPSIS
        Uninstall Intel AVB Filter Driver
    #>
    Write-Host "Uninstalling existing driver..." -ForegroundColor Yellow
    
    # Stop service
    sc.exe stop IntelAvbFilter | Out-Null
    
    # Delete service
    sc.exe delete IntelAvbFilter | Out-Null
    
    # Remove driver package
    pnputil.exe /delete-driver IntelAvbFilter.inf /uninstall /force | Out-Null
}

function Install-DriverViaPnPUtil {
    <#
    .SYNOPSIS
        Install driver using PnPUtil
    
    .PARAMETER InfPath
        Path to driver INF file
    #>
    param(
        [Parameter(Mandatory)]
        [string]$InfPath
    )
    
    Write-Host "Installing via PnPUtil..." -ForegroundColor Cyan
    pnputil.exe /add-driver $InfPath /install
    
    if ($LASTEXITCODE -ne 0) {
        throw "PnPUtil failed with exit code $LASTEXITCODE"
    }
}

function Install-DriverViaDevCon {
    <#
    .SYNOPSIS
        Install driver using DevCon
    
    .PARAMETER InfPath
        Path to driver INF file
    #>
    param(
        [Parameter(Mandatory)]
        [string]$InfPath
    )
    
    Write-Host "Installing via DevCon..." -ForegroundColor Cyan
    
    # Find DevCon.exe
    $devcon = Get-Command devcon.exe -ErrorAction SilentlyContinue
    if (-not $devcon) {
        throw "DevCon.exe not found. Install Windows Driver Kit (WDK)"
    }
    
    & devcon.exe install $InfPath
    
    if ($LASTEXITCODE -ne 0) {
        throw "DevCon failed with exit code $LASTEXITCODE"
    }
}

# Export functions
Export-ModuleMember -Function @(
    'Test-DriverInstalled',
    'Get-DriverInfPath',
    'Uninstall-Driver',
    'Install-DriverViaPnPUtil',
    'Install-DriverViaDevCon'
)
