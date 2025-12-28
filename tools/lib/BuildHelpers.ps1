<#
.SYNOPSIS
    Shared library for build operations

.DESCRIPTION
    Common functions for finding MSBuild, Visual Studio, and build tools.
    Eliminates code duplication across Build-Driver.ps1, Build-Tests.ps1, etc.

.NOTES
    Part of: Issue #27 (REQ-NF-SCRIPTS-001: Script Consolidation)
    Reduces code duplication (TC-SCRIPTS-002, TC-SCRIPTS-005)
#>

function Find-MSBuild {
    <#
    .SYNOPSIS
        Find MSBuild.exe using vswhere
    
    .OUTPUTS
        String - Full path to MSBuild.exe
    #>
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2019 or later."
    }
    
    $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
    
    if (-not $vsPath) {
        throw "Visual Studio not found. Install Visual Studio 2019 or later."
    }
    
    $msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
    
    if (-not (Test-Path $msbuild)) {
        throw "MSBuild not found at: $msbuild"
    }
    
    return $msbuild
}

function Find-VCVarsAll {
    <#
    .SYNOPSIS
        Find vcvarsall.bat for Visual Studio environment setup
    
    .OUTPUTS
        String - Full path to vcvarsall.bat
    #>
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found. Install Visual Studio 2019 or later."
    }
    
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    
    if (-not $vsPath) {
        throw "Visual Studio C++ tools not found. Install Visual Studio with C++ workload."
    }
    
    $vcvarsall = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
    
    if (-not (Test-Path $vcvarsall)) {
        throw "vcvarsall.bat not found at: $vcvarsall"
    }
    
    return $vcvarsall
}

function Invoke-MSBuild {
    <#
    .SYNOPSIS
        Invoke MSBuild with standard parameters
    
    .PARAMETER SolutionPath
        Path to solution or project file
    
    .PARAMETER Configuration
        Build configuration (Debug or Release)
    
    .PARAMETER Platform
        Target platform (x64 or x86)
    
    .PARAMETER Rebuild
        Force clean rebuild
    #>
    param(
        [Parameter(Mandatory)]
        [string]$SolutionPath,
        
        [ValidateSet('Debug', 'Release')]
        [string]$Configuration = 'Release',
        
        [ValidateSet('x64', 'x86')]
        [string]$Platform = 'x64',
        
        [switch]$Rebuild
    )
    
    $msbuild = Find-MSBuild
    
    $buildArgs = @(
        $SolutionPath,
        "/t:$(if ($Rebuild) { 'Clean;Build' } else { 'Build' })",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/m"  # Parallel build
    )
    
    Write-Host "Building with MSBuild: $Configuration|$Platform" -ForegroundColor Cyan
    
    & $msbuild @buildArgs
    
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild failed with exit code $LASTEXITCODE"
    }
}

# Export functions
Export-ModuleMember -Function @(
    'Find-MSBuild',
    'Find-VCVarsAll',
    'Invoke-MSBuild'
)
