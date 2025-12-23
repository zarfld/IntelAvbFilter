param(
    [Parameter(Mandatory=$true)][string]$BuildCmd
)
$ErrorActionPreference = 'Stop'
Write-Host "[vs_compile] Resolving Visual Studio environment..."
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(!(Test-Path $vswhere)) { Write-Host '[vs_compile] ERROR: vswhere.exe not found (install VS Build Tools)' -ForegroundColor Red; exit 1 }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs) { Write-Host '[vs_compile] ERROR: Visual C++ toolset not found' -ForegroundColor Red; exit 1 }

# Use Launch-VsDevShell.ps1 to avoid cmd.exe line length limit
$launchVsDevShell = Join-Path $vs 'Common7/Tools/Launch-VsDevShell.ps1'
if(!(Test-Path $launchVsDevShell)) { Write-Host '[vs_compile] ERROR: Launch-VsDevShell.ps1 not found' -ForegroundColor Red; exit 1 }

# Get 8.3 short path for Launch-VsDevShell.ps1 to reduce command line length
$shortPath = & cmd /c "for %A in (`"$launchVsDevShell`") do @echo %~sA" 2>&1 | Select-Object -Last 1
if(!(Test-Path $shortPath)) { $shortPath = $launchVsDevShell }

Write-Host "[vs_compile] Executing: $BuildCmd"

# Create temp PowerShell script to execute build command in VS environment
# Use ultra-short name to minimize command line length
$tempScript = Join-Path $env:TEMP 'vc.ps1'
$tempCmd = Join-Path $env:TEMP 'vc.cmd'

# Write build command to separate file to avoid embedding it in script
$BuildCmd | Out-File -FilePath $tempCmd -Encoding ASCII -NoNewline

# Script launches VS environment and executes command from file
$devShellInit = ""
if (-not $env:VSCMD_VER) {
    $devShellInit = "& '$shortPath' -Arch amd64 -SkipAutomaticLocation | Out-Null`r`n"
}

@"
$devShellInit`$cmd = Get-Content '$tempCmd' -Raw
Invoke-Expression `$cmd
`$ec = `$LASTEXITCODE
Remove-Item '$tempCmd' -ErrorAction SilentlyContinue
exit `$ec
"@ | Out-File -FilePath $tempScript -Encoding ASCII

# Execute directly in current PowerShell session (not via nested powershell.exe)
& $tempScript
$ec = $LASTEXITCODE

# Cleanup temp script (keep it if build failed for debugging)
if($ec -eq 0){ Remove-Item $tempScript -ErrorAction SilentlyContinue }
else { Write-Host "[vs_compile] Temp script preserved for debugging: $tempScript" }

if($ec -ne 0){ Write-Host "[vs_compile] Command failed with exit code $ec" -ForegroundColor Red }
exit $ec
