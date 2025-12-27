<#
.SYNOPSIS
  Legacy build script kept for compatibility.

.DESCRIPTION
  This script is intentionally a thin wrapper around the canonical build script:
    tools/build/Build-Tests.ps1

  It exists so older docs/entrypoints continue to work after test source reorg.
  Canonical behavior (test enumeration, env setup, output conventions) lives in
  Build-Tests.ps1.

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

Write-Host "Build-AllTests-Honest.ps1 -> Build-Tests.ps1" -ForegroundColor DarkGray

& $canonical -Configuration $Configuration
exit $LASTEXITCODE