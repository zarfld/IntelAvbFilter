param(
    [Parameter(Mandatory=$true)][string]$BuildCmd
)
$ErrorActionPreference = 'Stop'
Write-Host "[vs_compile] Resolving Visual Studio environment..."
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if(!(Test-Path $vswhere)) { Write-Error 'vswhere.exe not found (install VS Build Tools)'; exit 1 }
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs) { Write-Error 'Visual C++ toolset not found'; exit 1 }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
if(!(Test-Path $vcvars)) { Write-Error 'vcvars64.bat not found'; exit 1 }
Write-Host "[vs_compile] Using VC vars: $vcvars"
$cmd = "call `"$vcvars`" && $BuildCmd"
Write-Host "[vs_compile] Executing: $BuildCmd"
cmd /c $cmd
$ec = $LASTEXITCODE
if($ec -ne 0){ Write-Error "[vs_compile] Command failed with exit code $ec" }
exit $ec
