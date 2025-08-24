<#  IntelAvbFilter-Cleanup.ps1
    - Entfernt alle oem*.inf-Pakete, deren "Original Name" intelavbfilter.inf ist
    - Löst/aktiviert das LWF-Binding ms_intelavbfilter auf einem angegebenen Adapter
    - Optional: aktiviert Testsigning und importiert ein Testzertifikat
    - Installiert eine gewünschte INF neu

    AUSFÜHRUNG (als Admin):
      .\IntelAvbFilter-Cleanup.ps1 -AdapterName "Ethernet 2" -InfPath ".\x64\Debug\intelavbfilter.inf" -EnableTestSigning -CertPath ".\IntelAvbFilterTest.cer"
#>

[CmdletBinding(SupportsShouldProcess=$true)]
param(
  [string]$AdapterName = "Ethernet 2",
  [string]$ComponentId = "ms_intelavbfilter",
  [Parameter(Mandatory=$true)]
  [string]$InfPath,
  [switch]$EnableTestSigning,
  [string]$CertPath
)

function Assert-Admin {
  if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
      ).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    throw "Bitte PowerShell als Administrator starten."
  }
}

function Invoke-PnpUtil {
  param([string[]]$Args)
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = "pnputil.exe"
  $psi.Arguments = ($Args -join ' ')
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError  = $true
  $psi.UseShellExecute = $false
  $p = [System.Diagnostics.Process]::Start($psi)
  $p.WaitForExit()
  $out = $p.StandardOutput.ReadToEnd()
  $err = $p.StandardError.ReadToEnd()
  [PSCustomObject]@{ ExitCode=$p.ExitCode; StdOut=$out; StdErr=$err }
}

function Get-OemInfsByOriginalName {
  param([string]$OriginalNameRegex)
  # Sprach-robust: fange englisch & deutsch ab
  $rPub = '^(Published Name|Veröffentlichter Name)\s*:\s*(\S+)\s*$'
  $rOrg = '^(Original Name|Originalname)\s*:\s*(.+?)\s*$'

  $pubName = $null
  $hits = New-Object System.Collections.Generic.List[string]

  $res = Invoke-PnpUtil @("/enum-drivers")
  foreach ($line in ($res.StdOut -split "`r?`n")) {
    if ($line -match $rPub) { $pubName = $Matches[2]; continue }
    if ($line -match $rOrg) {
      $org = $Matches[2].Trim()
      if ($pubName -and ($org -imatch $OriginalNameRegex)) {
        $hits.Add($pubName)
      }
      $pubName = $null
    }
  }
  $hits | Sort-Object -Unique
}

function Disable-LwfBinding {
  param([string]$Adapter,[string]$Cid)
  try {
    Write-Host "[-] LWF-Binding auf '$Adapter' deaktivieren ($Cid) ..."
    Disable-NetAdapterBinding -Name $Adapter -ComponentID $Cid -ErrorAction Stop | Out-Null
  } catch {
    Write-Warning "Konnte Binding nicht deaktivieren (evtl. bereits aus). $_"
  }
}

function Enable-LwfBinding {
  param([string]$Adapter,[string]$Cid)
  try {
    Write-Host "[+] LWF-Binding auf '$Adapter' aktivieren ($Cid) ..."
    Enable-NetAdapterBinding -Name $Adapter -ComponentID $Cid -ErrorAction Stop | Out-Null
  } catch {
    Write-Warning "Konnte Binding nicht aktivieren. $_"
  }
}

function Remove-OemInf {
  param([string]$OemInf)
  Write-Host "[-] Entferne $OemInf ..."
  $res = Invoke-PnpUtil @("/delete-driver", $OemInf, "/uninstall", "/force")
  if ($res.ExitCode -ne 0) {
    Write-Warning "pnputil Rückgabecode $($res.ExitCode). Ausgabe:`n$($res.StdOut)`n$($res.StdErr)"
  } else {
    Write-Host "    OK"
  }
}

function Ensure-TestSigning {
  if (-not $EnableTestSigning) { return }
  Write-Host "[*] Testsigning aktivieren ..."
  & bcdedit /set testsigning on | Out-Null
  # Optionaler Rettungsanker (nur wenn wirklich nötig):
  # & bcdedit /set nointegritychecks on | Out-Null

  if ($CertPath -and (Test-Path $CertPath)) {
    Write-Host "[*] Testzertifikat importieren: $CertPath"
    try {
      Import-Certificate -FilePath $CertPath -CertStoreLocation Cert:\LocalMachine\Root            | Out-Null
      Import-Certificate -FilePath $CertPath -CertStoreLocation Cert:\LocalMachine\TrustedPublisher | Out-Null
    } catch {
      Write-Warning "Zertifikat-Import fehlgeschlagen: $_"
    }
  } elseif ($CertPath) {
    Write-Warning "Zertifikat nicht gefunden: $CertPath"
  }
  Write-Host "[i] Ein Neustart ist ggf. nötig, damit Testsigning aktiv wird."
}

function Install-Inf {
  param([string]$Path)
  if (-not (Test-Path $Path)) { throw "INF nicht gefunden: $Path" }
  Write-Host "[+] Installiere INF: $Path"
  $res = Invoke-PnpUtil @("/add-driver", ('"'+(Resolve-Path $Path).Path+'"'), "/install")
  Write-Host $res.StdOut
  if ($res.ExitCode -ne 0) { Write-Warning $res.StdErr }
}

# ----------------- Ablauf -----------------
try {
  Assert-Admin

  Write-Host "=== IntelAvbFilter Cleanup & Reinstall ==="

  # 1) Binding lösen
  Disable-LwfBinding -Adapter $AdapterName -Cid $ComponentId

  # 2) passende oem*.inf ermitteln
  $oems = Get-OemInfsByOriginalName -OriginalNameRegex 'intelavbfilter\.inf'
  if ($oems.Count -eq 0) {
    Write-Host "[i] Keine oem*.inf zu intelavbfilter.inf im Driver Store gefunden."
  } else {
    Write-Host "[-] Zu entfernende Pakete: $($oems -join ', ')"
    foreach ($o in $oems) { Remove-OemInf -OemInf $o }
  }

  # 3) optional: Testsigning & Zertifikat
  Ensure-TestSigning

  # 4) Frisches Paket installieren
  Install-Inf -Path $InfPath

  # 5) Binding wieder aktivieren
  Enable-LwfBinding -Adapter $AdapterName -Cid $ComponentId

  # 6) Status zeigen
  Write-Host "`n=== Status ==="
  try {
    $bind = Get-NetAdapterBinding -Name $AdapterName | Where-Object { $_.ComponentID -eq $ComponentId }
    if ($bind) { "{0} : {1}" -f $bind.ComponentID, $bind.Enabled | Write-Host }
  } catch { }
  $check = Invoke-PnpUtil @("/enum-drivers")
  ($check.StdOut -split "`r?`n" | Select-String -Pattern "intelavbfilter\.inf" -Context 0,4) | Out-Host

  Write-Host "`n[i] Falls Test Mode benötigt wird: bitte neu starten."
}
catch {
  Write-Error $_
  exit 1
}
