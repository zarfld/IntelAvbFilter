# uac-parent-probe.ps1 - does a UAC-elevated child see its requester as an OS ancestor? (read-only; no hardware)
#
# Question (Q 2026-10-21): tools/lib/HilLock.ps1 Resolve-AvbHilLockContext treats a child as owning the machine lock
# when the lock owner's PID is one of its OS ancestors.  Run-Tests-Elevated.ps1 launches the runner with
# Start-Process -Verb RunAs.  Whether Windows reports the requesting process as the elevated child's parent is
# UNVERIFIED; this probe produces the evidence.  It touches no driver, adapter, lock or safety record.
#
# Usage (normal, NON-elevated PowerShell, repository root; one UAC prompt appears - approve it):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\investigations\uac-parent-probe.ps1
# Evidence: logs\uac-parent-probe-<timestamp>.json
param([switch]$Child, [string]$Out = '')

$repo = Split-Path $PSScriptRoot -Parent
. (Join-Path $repo 'tools\lib\HilLock.ps1')
function Get-Token {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    return [ordered]@{ user = $id.Name; elevatedAdmin = ([Security.Principal.WindowsPrincipal]$id).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) }
}
if ($Child) {
    $self = Get-CimInstance Win32_Process -Filter "ProcessId=$PID"
    ([ordered]@{ pid = $PID; parentPid = [int]$self.ParentProcessId; ancestors = @(Get-AvbProcessAncestors -ProcessId $PID)
                 token = Get-Token; utc = (Get-Date).ToUniversalTime().ToString('o') } | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath $Out -Encoding UTF8
    exit 0
}
$logs = Join-Path $repo 'logs'; New-Item -ItemType Directory -Path $logs -Force | Out-Null
$childOut = Join-Path $env:TEMP ("uac-probe-child-{0}.json" -f [guid]::NewGuid())
$p = Start-Process powershell -Verb RunAs -Wait -PassThru -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-Child', '-Out', $childOut)
$c = if (Test-Path -LiteralPath $childOut) { Get-Content -LiteralPath $childOut -Raw | ConvertFrom-Json } else { $null }
$res = [ordered]@{ schema = 'avb-uac-parent-probe/1'; host = $env:COMPUTERNAME; requesterPid = $PID; requesterToken = Get-Token
                   childExitCode = $(if ($p) { $p.ExitCode }); child = $c
                   requesterIsParent = [bool]($c -and $c.parentPid -eq $PID); requesterIsAncestor = [bool]($c -and @($c.ancestors) -contains $PID)
                   utc = (Get-Date).ToUniversalTime().ToString('o') }
$f = Join-Path $logs ("uac-parent-probe-{0}.json" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
($res | ConvertTo-Json -Depth 5) | Set-Content -LiteralPath $f -Encoding UTF8
Write-Host ("requester PID {0}; elevated child PID {1}, parent {2}; requester is ancestor: {3}; child elevated: {4}" -f $PID, $(if ($c) { $c.pid }), $(if ($c) { $c.parentPid }), $res.requesterIsAncestor, $(if ($c) { $c.token.elevatedAdmin }))
Write-Host "Evidence: $f"
Remove-Item -LiteralPath $childOut -ErrorAction SilentlyContinue
