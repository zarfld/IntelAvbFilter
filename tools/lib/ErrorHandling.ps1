<#
.SYNOPSIS
    Shared library for error handling and logging

.DESCRIPTION
    Common error handling patterns and logging utilities.
    Provides consistent error handling across all scripts.

.NOTES
    Part of: Issue #27 (REQ-NF-SCRIPTS-001: Script Consolidation)
    Reduces code duplication (TC-SCRIPTS-002, TC-SCRIPTS-005)
#>

function Invoke-WithErrorHandling {
    <#
    .SYNOPSIS
        Execute script block with standardized error handling
    
    .PARAMETER ScriptBlock
        Code to execute
    
    .PARAMETER ErrorMessage
        Custom error message prefix
    #>
    param(
        [Parameter(Mandatory)]
        [ScriptBlock]$ScriptBlock,
        
        [string]$ErrorMessage = "Operation failed"
    )
    
    try {
        & $ScriptBlock
    }
    catch {
        Write-Error "$ErrorMessage`: $_"
        throw
    }
}

function Test-AdminPrivileges {
    <#
    .SYNOPSIS
        Check if script is running with administrator privileges
    
    .OUTPUTS
        Boolean - True if running as admin, False otherwise
    #>
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Assert-AdminPrivileges {
    <#
    .SYNOPSIS
        Throw error if not running as administrator
    
    .PARAMETER Message
        Custom error message
    #>
    param(
        [string]$Message = "This script requires administrator privileges. Run as Administrator."
    )
    
    if (-not (Test-AdminPrivileges)) {
        throw $Message
    }
}

function Write-ColorOutput {
    <#
    .SYNOPSIS
        Write colored output with consistent formatting
    
    .PARAMETER Message
        Message to display
    
    .PARAMETER Type
        Message type (Info, Success, Warning, Error)
    #>
    param(
        [Parameter(Mandatory)]
        [string]$Message,
        
        [ValidateSet('Info', 'Success', 'Warning', 'Error')]
        [string]$Type = 'Info'
    )
    
    $color = switch ($Type) {
        'Info'    { 'Cyan' }
        'Success' { 'Green' }
        'Warning' { 'Yellow' }
        'Error'   { 'Red' }
    }
    
    $prefix = switch ($Type) {
        'Info'    { '[INFO]' }
        'Success' { '[✓]' }
        'Warning' { '[!]' }
        'Error'   { '[✗]' }
    }
    
    Write-Host "$prefix $Message" -ForegroundColor $color
}

# Export functions
Export-ModuleMember -Function @(
    'Invoke-WithErrorHandling',
    'Test-AdminPrivileges',
    'Assert-AdminPrivileges',
    'Write-ColorOutput'
)
