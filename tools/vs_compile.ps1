param(
    [Parameter(Mandatory=$true)][string]$BuildCmd
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'lib\NativeExit.ps1')   # exit codes via $global:LASTEXITCODE (immune to caller shadowing)
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

# Create temp PowerShell script to execute build command in VS environment.
# Unique names per invocation: a fixed %TEMP%\vc.ps1 / vc.cmd let concurrent or stale compiles overwrite each other.
$tempId = [System.IO.Path]::GetRandomFileName().Replace('.', '')
$tempScript = Join-Path $env:TEMP "vc_$tempId.ps1"
$tempCmd = Join-Path $env:TEMP "vc_$tempId.cmd"

# Write build command to separate file to avoid embedding it in script
$BuildCmd | Out-File -FilePath $tempCmd -Encoding ASCII -NoNewline

# Script launches VS environment and executes command from file
$devShellInit = ""
if (-not $env:VSCMD_VER) {
    $devShellInit = "& '$shortPath' -Arch amd64 -SkipAutomaticLocation | Out-Null`r`n"
}

# The child reads $global:LASTEXITCODE (reset first, so an earlier command's code is never reused); a command that
# runs no native program leaves no exit code, which is a failure, never success.
@"
$devShellInit`$cmd = Get-Content '$tempCmd' -Raw
`$global:LASTEXITCODE = `$null
Invoke-Expression `$cmd
`$ec = `$global:LASTEXITCODE
Remove-Item '$tempCmd' -ErrorAction SilentlyContinue
if (`$null -eq `$ec) { Write-Host '[vs_compile] no exit code observed (the command ran no native program)'; exit 1 }
exit `$ec
"@ | Out-File -FilePath $tempScript -Encoding ASCII

# Execute directly in current PowerShell session (not via nested powershell.exe).  A PowerShell error in the child is
# a failure with no exit code, never success.
Reset-AvbExitCode
try { & $tempScript }
catch { Write-Host "[vs_compile] PowerShell error: $($_.Exception.Message)" -ForegroundColor Red; Reset-AvbExitCode }
$ec = Get-AvbExitCode

# Cleanup temp files (keep the script if the build failed, for debugging)
Remove-Item $tempCmd -ErrorAction SilentlyContinue
if (Test-AvbExitSuccess $ec) { Remove-Item $tempScript -ErrorAction SilentlyContinue; exit 0 }
Write-Host "[vs_compile] Temp script preserved for debugging: $tempScript"
Write-Host "[vs_compile] Command failed with exit code $(Format-AvbExitCode $ec)" -ForegroundColor Red
exit (Get-AvbFailureExitCode $ec)
