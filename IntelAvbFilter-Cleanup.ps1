<#
  IntelAvbFilter-Cleanup.ps1
  Zweck:
    - Deaktiviert das Filter-Binding (optional auf allen physischen Adaptern)
    - Entfernt ALLE vorhandenen oem*.inf Pakete mit Original Name "intelavbfilter.inf"
    - (Optional) Aktiviert Testsigning und importiert Test-Zertifikat
    - Installiert die angegebene INF neu
    - Reaktiviert das Binding
    - Prüft anschließend, ob der Ndi-Registry-Schlüssel erzeugt wurde

  Verwendung (als Administrator):
    .\IntelAvbFilter-Cleanup.ps1 -InfPath .\x64\Debug\IntelAvbFilter.inf -AllAdapters -Verbose

  Optional:
    -EnableTestSigning -CertPath .\IntelAvbFilterTest.cer

#>
[CmdletBinding(SupportsShouldProcess=$true)]
param(
  [string]$AdapterName = "Ethernet 2",
  [string]$ComponentId = "ms_intelavbfilter",
  [Parameter(Mandatory=$true)]
  [string]$InfPath,
  [switch]$EnableTestSigning,
  [string]$CertPath,
  [switch]$AllAdapters,
  [int]$PnPutilTimeoutSec = 180,          # erhöhtes Default
  [switch]$SkipDriverEnum                 # optional: /enum-drivers überspringen
)

#region Helper Functions
function Assert-Admin {
  if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    throw "Bitte PowerShell als Administrator starten."
  }
}

function Invoke-PnpUtil {
  param(
    [string[]]$Args,
    [int]$TimeoutSec = 180
  )
  if(-not $Args -or $Args.Count -eq 0){
    throw "Invoke-PnpUtil ohne Argumente aufgerufen (Programmfehler)"
  }
  Write-Verbose ("[pnputil] start: {0}" -f ($Args -join ' '))
  $psi = [System.Diagnostics.ProcessStartInfo]::new()
  $psi.FileName               = 'pnputil.exe'
  $psi.Arguments              = ($Args -join ' ')
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError  = $true
  $psi.UseShellExecute        = $false
  $psi.CreateNoWindow         = $true
  $p = [System.Diagnostics.Process]::Start($psi)
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $spinner = '|/-\\'
  $i = 0
  while(-not $p.HasExited){
    if($sw.Elapsed.TotalSeconds -ge $TimeoutSec){
      try { $p.Kill() | Out-Null } catch {}
      throw "Timeout (${TimeoutSec}s) bei pnputil $($Args -join ' ')"
    }
    if($sw.Elapsed.TotalSeconds -ge 2){
      Write-Host -NoNewline ("`r[pnputil] läuft {0:N1}s {1}" -f $sw.Elapsed.TotalSeconds, $spinner[$i%$spinner.Length])
      $i++
    }
    Start-Sleep -Milliseconds 200
  }
  $sw.Stop()
  Write-Host ("`r[pnputil] fertig in {0:N1}s          " -f $sw.Elapsed.TotalSeconds)
  [PSCustomObject]@{
    ExitCode = $p.ExitCode
    StdOut   = $p.StandardOutput.ReadToEnd()
    StdErr   = $p.StandardError.ReadToEnd()
    Command  = $psi.Arguments
    Duration = $sw.Elapsed
  }
}

function Get-OemInfsByOriginalName {
  param([string]$OriginalNameRegex)
  $rPub = '^(Published Name|Veröffentlichter Name)\s*:\s*(\S+)' 
  $rOrg = '^(Original Name|Originalname)\s*:\s*(.+)$'
  $pubName = $null
  $list = [System.Collections.Generic.List[string]]::new()
  $res = Invoke-PnpUtil @('/enum-drivers') -TimeoutSec $PnPutilTimeoutSec
  foreach($line in ($res.StdOut -split "`r?`n")) {
    if($line -match $rPub){ $pubName = $Matches[2]; continue }
    if($line -match $rOrg){
      $org = $Matches[2].Trim()
      if($pubName -and ($org -imatch $OriginalNameRegex)){ $list.Add($pubName) }
      $pubName = $null
    }
  }
  $list | Sort-Object -Unique
}

function Disable-LwfBinding {
  param([string]$Adapter,[string]$Cid)
  try {
    Write-Host "[-] Binding deaktivieren: $Adapter ($Cid)" -ForegroundColor DarkYellow
    if(Get-NetAdapter -Name $Adapter -ErrorAction SilentlyContinue){
      Disable-NetAdapterBinding -Name $Adapter -ComponentID $Cid -ErrorAction SilentlyContinue | Out-Null
    }
  } catch { Write-Warning "Disable-Binding fehlgeschlagen fuer ${Adapter}: $_" }
}
function Enable-LwfBinding {
  param([string]$Adapter,[string]$Cid)
  try {
    Write-Host "[+] Binding aktivieren:   $Adapter ($Cid)" -ForegroundColor Green
    if(Get-NetAdapter -Name $Adapter -ErrorAction SilentlyContinue){
      Enable-NetAdapterBinding -Name $Adapter -ComponentID $Cid -ErrorAction SilentlyContinue | Out-Null
    }
  } catch { Write-Warning "Enable-Binding fehlgeschlagen fuer ${Adapter}: $_" }
}

function Remove-OemInf {
  param([string]$OemInf)
  Write-Host "[-] Entferne Paket: $OemInf" -ForegroundColor DarkYellow
  $res = Invoke-PnpUtil @('/delete-driver', $OemInf, '/uninstall', '/force') -TimeoutSec $PnPutilTimeoutSec
  if($res.ExitCode -ne 0){
    Write-Warning "Fehler beim Entfernen $OemInf (ExitCode $($res.ExitCode))\n$($res.StdOut)\n$($res.StdErr)"
  } else {
    Write-Host "    OK" -ForegroundColor Green
  }
}

function Ensure-TestSigning {
  if(-not $EnableTestSigning){ return }
  Write-Host "[*] Aktiviere Testsigning ..."
  & bcdedit /set testsigning on | Out-Null
  if($CertPath){
    if(Test-Path $CertPath){
      Write-Host "[*] Importiere Zertifikat: $CertPath"
      foreach($store in 'Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher'){
        try { Import-Certificate -FilePath $CertPath -CertStoreLocation $store | Out-Null } catch { Write-Warning "Zertifikatimport in $store fehlgeschlagen: $_" }
      }
    } else { Write-Warning "Zertifikat nicht gefunden: $CertPath" }
  }
  Write-Host "[i] Neustart erforderlich, damit Testsigning aktiv wird." -ForegroundColor Yellow
}

function Install-Inf {
  param([string]$Path)
  if(-not (Test-Path $Path)){ throw "INF nicht gefunden: $Path" }
  $full = (Resolve-Path $Path).Path
  Write-Host "[+] Installiere INF: $full" -ForegroundColor Cyan
  $res = Invoke-PnpUtil @('/add-driver', $full, '/install') -TimeoutSec $PnPutilTimeoutSec
  Write-Host $res.StdOut
  if($res.ExitCode -ne 0){ Write-Warning $res.StdErr }
}

function Toggle-BindingPhase {
  param([string]$Phase)
  $cid = $ComponentId.ToLower()
  $targets = if($AllAdapters){ Get-NetAdapter -Physical | Where-Object { $_.Status -in @('Up','Disabled') } } else { Get-NetAdapter -Name $AdapterName -ErrorAction Stop }
  foreach($ad in $targets){ if($Phase -eq 'Disable'){ Disable-LwfBinding -Adapter $ad.Name -Cid $cid } else { Enable-LwfBinding -Adapter $ad.Name -Cid $cid } }
}

function Show-NdiStatus {
  $key = 'HKLM:\SYSTEM\CurrentControlSet\Services\IntelAvbFilter\Ndi'
  if(Test-Path $key){
    Write-Host "[+] Ndi Schlüssel vorhanden:" -ForegroundColor Green
    Get-ItemProperty $key | Select-Object * | Out-Host
  } else {
    Write-Warning 'Ndi Schlüssel NICHT vorhanden.'
  }
}
#endregion

try {
  Assert-Admin
  Write-Host "=== IntelAvbFilter Cleanup & Reinstall ===" -ForegroundColor Cyan
  Write-Host "ComponentId=$ComponentId  AllAdapters=$AllAdapters  Timeout=${PnPutilTimeoutSec}s" -ForegroundColor Gray

  # 1) Binding deaktivieren
  Toggle-BindingPhase -Phase 'Disable'

  # 2) Vorhandene Treiberpakete entfernen
  $oems = Get-OemInfsByOriginalName -OriginalNameRegex 'intelavbfilter\.inf'
  if($oems.Count -eq 0){
    Write-Host '[i] Keine vorhandenen oem*.inf Pakete gefunden.' -ForegroundColor Yellow
  } else {
    Write-Host "[-] Entferne Pakete: $($oems -join ', ')" -ForegroundColor Yellow
    $i=0
    foreach($o in $oems){
      $i++
      Write-Progress -Activity 'Remove driver packages' -Status $o -PercentComplete (($i/$oems.Count)*100)
      Remove-OemInf -OemInf $o
    }
    Write-Progress -Activity 'Remove driver packages' -Completed
  }

  # 3) Optional Testsigning
  Ensure-TestSigning

  # 4) Neu installieren
  Install-Inf -Path $InfPath

  # 5) Binding aktivieren
  Toggle-BindingPhase -Phase 'Enable'

  # 6) Ndi prüfen
  Show-NdiStatus

  # 7) Kurzer Auszug der neuen Treibereinträge
  if(-not $SkipDriverEnum){
    Write-Host '[i] Sammle Treiberliste (/enum-drivers) ...' -ForegroundColor Gray
    $enum = Invoke-PnpUtil @('/enum-drivers') -TimeoutSec $PnPutilTimeoutSec
    ($enum.StdOut -split "`r?`n" | Select-String -Pattern 'intelavbfilter\.inf' -Context 0,6) | Out-Host
  } else {
    Write-Host '[i] /enum-drivers übersprungen (SkipDriverEnum gesetzt).'
  }
  # Fallback schnelle Prüfung via CIM falls pnputil übersprungen oder leer
  if($SkipDriverEnum){
    Write-Host '[i] Fallback: Suche via Win32_PnPSignedDriver ...'
    Get-CimInstance Win32_PnPSignedDriver -ErrorAction SilentlyContinue | Where-Object { $_.InfName -match 'intelavbfilter' } | Select-Object DeviceName, InfName, DriverVersion | Format-Table -AutoSize
  }

  Write-Host 'Fertig. DebugView / reg query zur weiteren Analyse verwenden.' -ForegroundColor Cyan
}
catch {
  Write-Error $_
  exit 1
}
