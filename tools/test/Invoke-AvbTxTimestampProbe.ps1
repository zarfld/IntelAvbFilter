# Invoke-AvbTxTimestampProbe.ps1 - D4 (Q 2026-10-22) TC-CORR-TX-RX-001 hardware characterization, phase 1.
#
# Question: does the adapter latch a hardware TX timestamp for a frame injected by IntelAvbFilter (TEST_SEND_PTP)?
# Observation only through TSYNCTXCTL (0x0B614, all three families per intel-ethernet-regs/gen):
#   bit 4 EN  - TX timestamping enabled (I210/I219/I226 SSOT)
#   bit 0     - TXTT "TX timestamp valid / latched" (SSOT: I226, I225, I217; NOT in the I210/I219 SSOT headers -> the
#               decoding of bit 0 on I210/I219 is UNVERIFIED and reported as such)
# Per exact target (ENUM_ADAPTERS -> OPEN_ADAPTER, the handle's own adapter context): TSYNCTXCTL x3 before, then per send
# one TEST_SEND_PTP and TSYNCTXCTL polled every ~1 ms for 200 ms.
#
# NEVER: TXSTMPL/TXSTMPH reads (0x0B618/0x0B61C: reading TXSTMPH releases the latch), GET_TX_TIMESTAMP (its FIFO path
# reads TXSTMP), WRITE_REGISTER, INIT_DEVICE, SET_HW_TIMESTAMPING.  READ_REGISTER exists only in Debug driver builds
# (include/avb_ioctl.h #ifndef NDEBUG): with a Release driver every target is BLOCKED and nothing is sent.
#
# State the probe can change: PTP frames on the wire (as every ReadOnly TX test), the driver's last_ndis_tx_record, the
# driver-global g_AvbContext (OPEN_ADAPTER) - and, if the hardware latches one of the probe's frames, TSYNCTXCTL bit 0
# stays SET (latch held) because TXSTMPH is deliberately not read.  The result reports this per adapter
# ("latchLeftHeld").  Release of a held latch = one TXSTMPL+TXSTMPH read: phase 2, only after Q reviewed phase 1.
#
# Usage (elevated PowerShell, repository root, Debug driver installed, no other HIL run - takes the machine lock):
#   .\tools\test\Invoke-AvbTxTimestampProbe.ps1 -TargetDids 0x125B,0x1533,0x15BB
# Exit: 0 = characterization recorded for every target; 1 = some target BLOCKED / failed; 2 = not run.
param(
    # [object[]]: an unquoted 0x125B must reach ConvertTo-AvbDid as a number, not as the text '4699'.
    [Parameter(Mandatory = $true)][object[]]$TargetDids,
    [int]$Sends = 3,
    [int]$PollMs = 200,
    [string]$OutPath = ''
)

$TSYNCTXCTL = 0x0B614
# Pure: interpretation of one target's observations (hypotheses for the next RED/GREEN step, never conclusions).
function Get-AvbTxProbeInterpretation {
    param([string]$Did, [uint32[]]$Before, [object[]]$SendResults)
    $bit0Verified = ($Did -eq '0x125B')   # I226 TXTT in SSOT; I210 0x1533 / I219 0x15BB: not in SSOT
    $en = @($Before | ForEach-Object { ($_ -shr 4) -band 1 })
    $pre0 = @($Before | ForEach-Object { $_ -band 1 })
    $latched = @($SendResults | Where-Object { $_.bit0SetAfterMs -ne $null })
    $notes = @()
    if ($en -contains 0) { $notes += 'TSYNCTXCTL.EN = 0: TX timestamping disabled in hardware - no egress latch is possible (H2)' }
    elseif ($pre0 -contains 1) { $notes += 'TSYNCTXCTL bit 0 already SET before any probe frame: a latch is held (stale / owned by another consumer); new frames cannot latch until TXSTMPH is read (H-stale-latch)' }
    elseif ($latched.Count -eq 0) { $notes += 'EN = 1, bit 0 stayed 0 after every probe frame: the hardware did not latch the injected frames (descriptor timestamp request not set for filter-injected NBLs?) (H3)' }
    else { $notes += ("EN = 1, bit 0 0 -> 1 after {0} of {1} probe frame(s): a TX timestamp was latched after the send; association with the probe frame is UNVERIFIED (other PTP traffic on the port could latch)" -f $latched.Count, @($SendResults).Count) }
    if (-not $bit0Verified) { $notes += "bit 0 decoding for $Did is UNVERIFIED (not defined in the repository register SSOT for this family)" }
    return [ordered]@{ enBit = $(if ($en -contains 0) { 0 } else { 1 }); bit0BeforeAnySend = $(if ($pre0 -contains 1) { 1 } else { 0 })
                       framesLatched = $latched.Count; bit0DecodingVerified = $bit0Verified; notes = @($notes) }
}

if ($MyInvocation.InvocationName -ne '.') {
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repoRoot 'tools\lib\HilLock.ps1')
$runner = Join-Path $repoRoot 'tools\test\Run-Tests-CI.ps1'
$ast = [System.Management.Automation.Language.Parser]::ParseFile($runner, [ref]$null, [ref]$null)
foreach ($n in 'Format-AvbDid', 'ConvertTo-AvbDid', 'New-AvbHilAdapter', 'Get-AvbHilClockState', 'Get-AvbHilSandboxState', 'Get-AvbHilState', 'Get-AvbHwRegNames', 'Get-AvbHilRegs') {
    $fn = $ast.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $n }, $true)
    if (-not $fn) { Write-Host "[FAIL] $n not found in $runner" -ForegroundColor Red; exit 2 }
    . ([scriptblock]::Create($fn.Extent.Text))
}
$script:IsSandbox = $false
$id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
if (-not (New-Object System.Security.Principal.WindowsPrincipal($id)).IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host '[FAIL] run elevated' -ForegroundColor Red; exit 2
}
$targets = @($TargetDids | ForEach-Object { ConvertTo-AvbDid $_ })
$lock = Enter-AvbHilLock -Kind 'local' -Label 'TX timestamp probe (Invoke-AvbTxTimestampProbe.ps1)'
if (-not $lock.Ok) { Write-Host "[FAIL] machine lock not acquired: $($lock.Why)" -ForegroundColor Red; exit 2 }
$results = @(); $sys = Join-Path $env:SystemRoot 'System32\drivers\IntelAvbFilter.sys'
try {
    $pre = Get-AvbHilState -PhcReads 3   # the runner's observer: context (TIMINCA/TSAUXC/PHC) and defines [AvbHilIoctl]
    $IOCTL_ENUM = [uint32]0x0017007C; $IOCTL_OPEN = [uint32]0x00170080; $IOCTL_READ = [uint32]0x00170058; $IOCTL_SEND = [uint32]0x001700CC
    $f = [double][System.Diagnostics.Stopwatch]::Frequency
    foreach ($t in $targets) {
        $r = [ordered]@{ did = $t; driverIndex = $null; result = 'BLOCKED'; why = ''; tsynctxctlBefore = @(); sends = @(); latchLeftHeld = $null; interpretation = $null }
        $h = [AvbHilIoctl]::CreateFile('\\.\IntelAvbFilter', [uint32]3221225472, [uint32]3, [IntPtr]::Zero, [uint32]3, [uint32]0x80, [IntPtr]::Zero)
        if ($h -eq [IntPtr](-1)) { $r.why = "open \\.\IntelAvbFilter failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"; $results += ,$r; continue }
        try {
            $ret = [uint32]0; $e = New-Object byte[] 20
            if (-not [AvbHilIoctl]::DeviceIoControl($h, $IOCTL_ENUM, $e, 20, $e, 20, [ref]$ret, [IntPtr]::Zero)) { $r.why = 'ENUM_ADAPTERS failed'; $results += ,$r; continue }
            $count = [System.BitConverter]::ToUInt32($e, 4); $hits = @()
            for ($i = 0; $i -lt $count; $i++) {
                $e = New-Object byte[] 20; [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$i), 0, $e, 0, 4)
                if ([AvbHilIoctl]::DeviceIoControl($h, $IOCTL_ENUM, $e, 20, $e, 20, [ref]$ret, [IntPtr]::Zero) -and (Format-AvbDid ([System.BitConverter]::ToUInt16($e, 10))) -eq $t) {
                    $hits += ,@{ Index = $i; Vid = [System.BitConverter]::ToUInt16($e, 8); Did = [System.BitConverter]::ToUInt16($e, 10) }
                }
            }
            if ($hits.Count -ne 1) { $r.why = "DID $t enumerated $($hits.Count) times by the driver (exactly one required for an exact target)"; $results += ,$r; continue }
            $r.driverIndex = $hits[0].Index
            $o = New-Object byte[] 12
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint16]$hits[0].Vid), 0, $o, 0, 2)
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint16]$hits[0].Did), 0, $o, 2, 2)
            [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$hits[0].Index), 0, $o, 4, 4)
            if (-not [AvbHilIoctl]::DeviceIoControl($h, $IOCTL_OPEN, $o, 12, $o, 12, [ref]$ret, [IntPtr]::Zero)) { $r.why = "OPEN_ADAPTER failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error()))"; $results += ,$r; continue }
            $read = {   # AVB_REGISTER_REQUEST: offset, value, status
                $b = New-Object byte[] 12; [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$TSYNCTXCTL), 0, $b, 0, 4)
                if (-not [AvbHilIoctl]::DeviceIoControl($h, $IOCTL_READ, $b, 12, $b, 12, [ref]$ret, [IntPtr]::Zero)) { return @{ Ok = $false; Why = "READ_REGISTER failed (Win32 $([System.Runtime.InteropServices.Marshal]::GetLastWin32Error())) - Release driver (no raw register access) or adapter not BAR-mapped" } }
                return @{ Ok = $true; Value = [System.BitConverter]::ToUInt32($b, 4) }
            }
            $vals = @(); $err = ''
            for ($k = 0; $k -lt 3; $k++) { $x = & $read; if (-not $x.Ok) { $err = $x.Why; break }; $vals += [uint32]$x.Value; Start-Sleep -Milliseconds 10 }
            if ($err) { $r.why = $err; $results += ,$r; continue }   # nothing sent: no characterization without the register
            $r.tsynctxctlBefore = @($vals | ForEach-Object { '0x{0:X8}' -f $_ })
            for ($s = 0; $s -lt $Sends; $s++) {
                $p = New-Object byte[] 40
                [System.Array]::Copy([System.BitConverter]::GetBytes([uint32]$hits[0].Index), 0, $p, 0, 4)
                [System.Array]::Copy([System.BitConverter]::GetBytes([uint32](0x7A00 + $s)), 0, $p, 4, 4)
                $okSend = [AvbHilIoctl]::DeviceIoControl($h, $IOCTL_SEND, $p, 40, $p, 40, [ref]$ret, [IntPtr]::Zero)
                $sendErr = if ($okSend) { 0 } else { [System.Runtime.InteropServices.Marshal]::GetLastWin32Error() }
                $t0 = [System.Diagnostics.Stopwatch]::GetTimestamp(); $seen = @(); $setAt = $null; $last = $null
                while ((([System.Diagnostics.Stopwatch]::GetTimestamp() - $t0) * 1000.0 / $f) -lt $PollMs) {
                    $x = & $read; if (-not $x.Ok) { break }
                    $last = [uint32]$x.Value
                    if ($seen -notcontains $last) { $seen += $last }
                    if ($null -eq $setAt -and ($last -band 1)) { $setAt = [Math]::Round((([System.Diagnostics.Stopwatch]::GetTimestamp() - $t0) * 1000.0 / $f), 2) }
                    Start-Sleep -Milliseconds 1
                }
                $r.sends += ,([ordered]@{ sequenceId = ('0x{0:X4}' -f (0x7A00 + $s)); win32Error = $sendErr; ndisStatus = ('0x{0:X8}' -f [System.BitConverter]::ToUInt32($p, 12))
                                          packetsSent = [System.BitConverter]::ToUInt32($p, 8); preSendPhcNs = [System.BitConverter]::ToUInt64($p, 16)
                                          phcAtSendNs = [System.BitConverter]::ToUInt64($p, 24); tsProvenance = [System.BitConverter]::ToUInt32($p, 32)
                                          tsynctxctlSeen = @($seen | ForEach-Object { '0x{0:X8}' -f $_ }); bit0SetAfterMs = $setAt })
            }
            $fin = & $read
            $r.latchLeftHeld = $(if ($fin.Ok) { [bool]($fin.Value -band 1) } else { $null })
            $r.interpretation = Get-AvbTxProbeInterpretation -Did $t -Before $vals -SendResults $r.sends
            $r.result = 'CHARACTERIZED'
            $results += ,$r
        } finally { [void][AvbHilIoctl]::CloseHandle($h) }
    }
    $post = Get-AvbHilState -PhcReads 3
} finally {
    $null = Exit-AvbHilLock -Owner $lock.Owner
}
$doc = [ordered]@{ schema = 'avb-tx-timestamp-probe/1'; testcase = 'TC-CORR-TX-RX-001 (phase 1: TSYNCTXCTL, no TXSTMP read)'; host = $env:COMPUTERNAME
                   createdUtc = (Get-Date).ToUniversalTime().ToString('o'); evidenceClass = 'hardware'
                   driver = [ordered]@{ path = $sys; sha256 = $(if (Test-Path -LiteralPath $sys) { (Get-FileHash -LiteralPath $sys -Algorithm SHA256).Hash } else { '' }) }
                   targets = @($results)
                   clockBefore = @($pre.Clock | ForEach-Object { [ordered]@{ did = $_.Did; index = $_.Index; ok = $_.Ok; timinca = ('0x{0:X8}' -f [uint32]$_.Timinca); tsauxc = ('0x{0:X8}' -f [uint32]$_.Tsauxc) } })
                   clockAfter  = @($post.Clock | ForEach-Object { [ordered]@{ did = $_.Did; index = $_.Index; ok = $_.Ok; timinca = ('0x{0:X8}' -f [uint32]$_.Timinca); tsauxc = ('0x{0:X8}' -f [uint32]$_.Tsauxc) } }) }
if (-not $OutPath) {
    $dir = Join-Path $repoRoot 'logs\hil-tx-probe'; New-Item -ItemType Directory -Path $dir -Force | Out-Null
    $OutPath = Join-Path $dir ("tx-probe-{0}.json" -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
}
($doc | ConvertTo-Json -Depth 8) | Set-Content -LiteralPath $OutPath -Encoding UTF8
foreach ($r in $results) {
    Write-Host ("[{0}] {1} (driver index {2}) TSYNCTXCTL before {3}; latch left held: {4}; {5}" -f $r.result, $r.did, $r.driverIndex, ($r.tsynctxctlBefore -join ','), $r.latchLeftHeld,
                $(if ($r.interpretation) { $r.interpretation.notes -join ' | ' } else { $r.why }))
}
Write-Host "[INFO] evidence: $OutPath"
if (@($results | Where-Object { $_.result -ne 'CHARACTERIZED' }).Count -gt 0) { exit 1 }
exit 0
}
