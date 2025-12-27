<#
.SYNOPSIS
  Legacy "TrulyHonest" build script kept for compatibility.

.DESCRIPTION
  Thin wrapper around the canonical build script:
    tools/build/Build-Tests.ps1

  This variant always enables detailed output.

  Issue: #27
#>

param(
    [Parameter(Mandatory = $false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$canonical = Join-Path $scriptDir "Build-Tests.ps1"

Write-Host "Build-AllTests-TrulyHonest.ps1 -> Build-Tests.ps1" -ForegroundColor DarkGray

& $canonical -Configuration $Configuration -ShowDetails
exit $LASTEXITCODE