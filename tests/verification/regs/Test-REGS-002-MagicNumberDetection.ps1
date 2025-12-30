# Test-REGS-002-MagicNumberDetection.ps1
# Verifies: #305 (TEST-REGS-002: Detect Magic Numbers in Register Access Code)
# Implements: #21 (REQ-NF-REGS-001: Eliminate Magic Numbers)

<#
.SYNOPSIS
    Detect magic number literals in register access code

.DESCRIPTION
    This test scans source code for hardcoded register offset literals.
    
    Target Patterns (register offsets):
    - 0x0B6xx - PTP register block (SYSTIML, SYSTIMH, TIMINCA, TSAUXC, etc.)
    - 0x00000 - CTRL register
    - 0x00008 - STATUS register
    - 0x054xx - MAC address registers
    - 0x086xx - TSN registers (I225 specific)
    
    Excluded Patterns (allowed):
    - 0xFFFFFFFF - Sentinel values
    - 0xAAAAxxxx - Debug markers
    - 0x0000000x - Zero with single digit (common bit masks)
    - Comments (// or /* */)
    - #define statements

.PARAMETER Verbose
    Show detailed violation information

.EXAMPLE
    .\Test-REGS-002-MagicNumberDetection.ps1
    
.EXAMPLE
    .\Test-REGS-002-MagicNumberDetection.ps1 -Verbose
#>

param(
    [switch]$Verbose
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$SourcePath = Join-Path $RepoRoot "src"

Write-Host "=== TEST-REGS-002: Magic Number Detection ===" -ForegroundColor Cyan
Write-Host "Scanning: $SourcePath" -ForegroundColor Gray
Write-Host ""

# Define patterns to detect
$Patterns = @(
    @{
        Name = "PTP Registers (0x0B6xx)"
        Regex = '0x0B6[0-9A-Fa-f]{2}\b'
        Description = "PTP register offsets (SYSTIML, SYSTIMH, TIMINCA, TSAUXC, etc.)"
    },
    @{
        Name = "CTRL Register (0x00000)"
        Regex = '0x00000(?!0)\b'
        Description = "Device Control Register (exclude 0x000000xx)"
    },
    @{
        Name = "STATUS Register (0x00008)"
        Regex = '0x00008\b'
        Description = "Device Status Register"
    },
    @{
        Name = "MAC Registers (0x054xx)"
        Regex = '0x054[0-9A-Fa-f]{2}\b'
        Description = "MAC address registers (RAL, RAH)"
    },
    @{
        Name = "TSN Registers (0x086xx)"
        Regex = '0x086[0-9A-Fa-f]{2}\b'
        Description = "Time-Sensitive Networking registers (I225 TAS)"
    }
)

# Exclusion patterns (allowed literals)
$ExclusionPatterns = @(
    '0xFFFFFFFF',  # Sentinel values
    '0xAAAA',      # Debug markers
    '0x0000000[0-9A-Fa-f]'  # Zero with single hex digit
)

$TotalViolations = 0
$ViolationsByFile = @{}

# Scan source files
$SourceFiles = Get-ChildItem -Path $SourcePath -Recurse -Include *.c,*.h -File

foreach ($File in $SourceFiles) {
    $RelativePath = $File.FullName.Substring($RepoRoot.Length + 1)
    $Content = Get-Content $File.FullName
    
    $FileViolations = @()
    
    for ($LineNum = 0; $LineNum -lt $Content.Count; $LineNum++) {
        $Line = $Content[$LineNum]
        
        # Skip comments
        if ($Line -match '^\s*//' -or $Line -match '^\s*/\*' -or $Line -match '\*/\s*$') {
            continue
        }
        
        # Skip #define statements (these define constants, not use magic numbers)
        if ($Line -match '^\s*#define\s+') {
            continue
        }
        
        foreach ($Pattern in $Patterns) {
            if ($Line -match $Pattern.Regex) {
                $Match = $Matches[0]
                
                # Check if it's an excluded pattern
                $IsExcluded = $false
                foreach ($Exclusion in $ExclusionPatterns) {
                    if ($Match -match $Exclusion) {
                        $IsExcluded = $true
                        break
                    }
                }
                
                if (-not $IsExcluded) {
                    $FileViolations += @{
                        LineNumber = $LineNum + 1
                        Line = $Line.Trim()
                        Pattern = $Pattern.Name
                        Value = $Match
                    }
                    $TotalViolations++
                }
            }
        }
    }
    
    if ($FileViolations.Count -gt 0) {
        $ViolationsByFile[$RelativePath] = $FileViolations
    }
}

# Report results
if ($TotalViolations -eq 0) {
    Write-Host "✅ No magic numbers detected!" -ForegroundColor Green
    Write-Host ""
    Write-Host "=== TEST-REGS-002 PASSED ===" -ForegroundColor Green
    exit 0
} else {
    Write-Host "❌ Found $TotalViolations magic number violations in $($ViolationsByFile.Count) files" -ForegroundColor Red
    Write-Host ""
    
    foreach ($FilePath in ($ViolationsByFile.Keys | Sort-Object)) {
        $Violations = $ViolationsByFile[$FilePath]
        Write-Host "📄 $FilePath ($($Violations.Count) violations)" -ForegroundColor Yellow
        
        if ($Verbose) {
            foreach ($Violation in $Violations) {
                Write-Host "   Line $($Violation.LineNumber): $($Violation.Value) [$($Violation.Pattern)]" -ForegroundColor Gray
                Write-Host "      $($Violation.Line)" -ForegroundColor DarkGray
            }
        } else {
            # Group by pattern
            $GroupedViolations = $Violations | Group-Object -Property Pattern
            foreach ($Group in $GroupedViolations) {
                $Lines = ($Group.Group | ForEach-Object { $_.LineNumber }) -join ', '
                Write-Host "   $($Group.Name): $($Group.Count) occurrences (lines: $Lines)" -ForegroundColor Gray
            }
        }
        Write-Host ""
    }
    
    Write-Host "=== Violation Summary ===" -ForegroundColor Cyan
    $PatternCounts = $ViolationsByFile.Values | ForEach-Object { $_ } | Group-Object -Property Pattern
    foreach ($Group in ($PatternCounts | Sort-Object -Property Count -Descending)) {
        Write-Host "  $($Group.Name): $($Group.Count) occurrences" -ForegroundColor Yellow
    }
    Write-Host ""
    
    Write-Host "💡 Recommendation: Replace magic numbers with named constants from intel-ethernet-regs headers" -ForegroundColor Cyan
    Write-Host ""
    Write-Host "=== TEST-REGS-002 FAILED ===" -ForegroundColor Red
    exit 1
}
