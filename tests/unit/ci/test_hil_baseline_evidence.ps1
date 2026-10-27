# test_hil_baseline_evidence.ps1
# TEST-HILBASE-*: baseline evidence producer tools/test/New-AvbHilBaselineEvidence.ps1 (D3, Q 2026-10-22) - offline.
#
# ALL OBSERVATIONS HERE ARE SYNTHETIC FIXTURES (provenance 'synthetic'): they characterize the document/verdict rules
# only and are NOT hardware evidence.  The producer's collector (real Get-AvbHilState on the HIL host) is not executed.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++; if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green } else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }
Write-Host ""; Write-Host "TEST-HILBASE: baseline evidence producer (offline, synthetic observations only)" -ForegroundColor Cyan

$Producer = Join-Path $RepoRoot 'tools\test\New-AvbHilBaselineEvidence.ps1'
$Runner   = Join-Path $RepoRoot 'tools\test\Run-Tests-CI.ps1'
$missing = @()
function Import-Functions([string]$Path, [string[]]$Names) {
    $a = [System.Management.Automation.Language.Parser]::ParseFile($Path, [ref]$null, [ref]$null)
    foreach ($n in $Names) {
        $fn = $a.Find({ param($x) $x -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $x.Name -eq $n }, $true)
        if ($fn) { Set-Item -Path "function:script:$n" -Value $fn.Body.GetScriptBlock() } else { $script:missing += "$n ($Path)" }
    }
}
if (Test-Path -LiteralPath $Producer) { Import-Functions $Producer @('Get-AvbBaselinePhcCharacterization', 'New-AvbHilBaselineDocument') } else { $missing += $Producer }
Import-Functions $Runner @('Format-AvbDid', 'ConvertTo-AvbDid', 'Compare-AvbHilState', 'ConvertTo-AvbHilPhcWindow')
. (Join-Path $RepoRoot 'tools\lib\PhcRestoration.ps1')
. (Join-Path $RepoRoot 'tools\lib\HilLock.ps1')
tc_check 'TEST-HILBASE-000-producer-and-reused-functions-present' ($missing.Count -eq 0) ($missing -join ', ')
if ($missing.Count) { exit 1 }
$script:IsSandbox = $true   # ConvertTo-AvbHilPhcWindow labels the fixture windows 'synthetic'

$f = [double][System.Diagnostics.Stopwatch]::Frequency
$ids = [ordered]@{ '0x125B' = 'PCI\VEN_8086&DEV_125B&SUBSYS_00008086&REV_04\FX01'; '0x1533' = 'PCI\VEN_8086&DEV_1533&SUBSYS_00008086&REV_03\FX02'; '0x15BB' = 'PCI\VEN_8086&DEV_15BB&SUBSYS_00008086&REV_10\FX03' }
$supported = @{ '0x125B' = 'INTEL_DEV_I226_V'; '0x1533' = 'INTEL_DEV_I210'; '0x15BB' = 'INTEL_DEV_I219_LM7' }
# One pass at QPC second $T: three healthy adapters, 5 paired PHC reads each (PHC = 300 s + T on the QPC timeline).
function New-Pass([double]$T, [hashtable]$Timinca = @{}, [hashtable]$PhcOffsetNs = @{}, [string]$Unbound = '', [switch]$Backwards, [long]$PhcBase = 300000000000) {
    $ad = @(); $ck = @(); $i = 0
    foreach ($d in $ids.Keys) {
        $ad += [pscustomobject]@{ InstanceId = $ids[$d]; Did = $d; PnpStatus = 'OK'; Problem = 0; AdminStatus = 'Up'; MediaStatus = 'Up'
                                  FilterBound = $(if ($d -eq $Unbound) { 'False' } else { 'True' }); Name = "FX $d" }
        $s = @()
        for ($r = 0; $r -lt 5; $r++) {
            $q0 = [long](($T + $r * 0.002) * $f); $q1 = $q0 + 20
            $phc = $PhcBase + [long](([double]($q0 + 10) / $f) * 1e9) + [long]$(if ($PhcOffsetNs.ContainsKey($d)) { $PhcOffsetNs[$d] } else { 0 })
            if ($Backwards -and $d -eq '0x1533' -and $r -eq 3) { $phc -= 50000000 }
            $s += ,([ordered]@{ qpcBefore = $q0; qpcAfter = $q1; phcNs = $phc })
        }
        $ck += [pscustomobject]@{ Index = $i; Did = $d; Ok = $true; Error = ''; Tsauxc = 0; Samples = $s
                                  Timinca = $(if ($Timinca.ContainsKey($d)) { $Timinca[$d] } else { 0x08000000 }) }
        $i++
    }
    return @{ Ok = $true; Error = ''; Adapters = $ad; ClockOk = $true; ClockError = ''; Clock = $ck }
}
function New-Identity([string]$NoMacDid = '') {
    $m = @{}; $n = 0
    foreach ($d in $ids.Keys) { $n++; $m[$ids[$d]] = @{ Mac = $(if ($d -eq $NoMacDid) { '' } else { "00-11-22-33-44-0$n" }); PciLocation = "PCIROOT(0)#PCI(1C0$n)#PCI(0000)"
                                                       LocationInfo = "PCI bus $n, device 0, function 0"; DriverVersion = '1.0'; Description = "FX $d" } }
    return $m
}
$drv = [ordered]@{ path = 'C:\Windows\System32\drivers\IntelAvbFilter.sys'; Sha256 = ('B' * 64); fileVersion = '1.0'; serviceStatus = 'Running' }
$saf = [ordered]@{ state = 'UNAVAILABLE'; why = 'fixture'; path = 'fixture' }
function New-Doc([object[]]$Passes, [hashtable]$Identity = (New-Identity), [string[]]$Expect = @('0x1533', '0x15BB', '0x125B'), $Driver = $drv) {
    return (New-AvbHilBaselineDocument -Passes $Passes -Identity $Identity -Driver $Driver -Supported $supported -ExpectDids $Expect -Safety $saf `
                -IntervalSeconds 5 -HostName $env:COMPUTERNAME -CreatedUtc '2026-10-22T00:00:00Z')
}
$Tmp = Join-Path $env:TEMP ("hilbase_{0}" -f (Get-Random)); New-Item -ItemType Directory -Path $Tmp -Force | Out-Null
function Test-Doc($Doc, [string]$Name) { $p = Join-Path $Tmp $Name; ($Doc | ConvertTo-Json -Depth 10) | Set-Content -LiteralPath $p -Encoding UTF8; return (Test-AvbHilBaselineEvidence -Path $p) }

# 001 healthy observation -> BASELINE_VERIFIED, accepted by the existing approval gate, never approved by the producer,
#     PHC characterized per adapter and interval but judged INCONCLUSIVE (no tolerance is derived)
$ok = New-Doc @((New-Pass 100), (New-Pass 105), (New-Pass 110))
$g1 = Test-Doc $ok 'ok.json'
$ch = @($ok.phcCharacterization)
tc_check 'TEST-HILBASE-001-healthy-observation-is-baseline-verified-and-passes-the-approval-gate' `
    ($ok.verdict -eq 'BASELINE_VERIFIED' -and @($ok.findings).Count -eq 0 -and $g1.Ok -and $ok.approval -match '^NOT APPROVED' -and
     @($ok.inventory).Count -eq 3 -and @($ok.inventory | Where-Object { $_.mac -and $_.pciLocation }).Count -eq 3 -and
     $ch.Count -eq 6 -and @($ch | Where-Object { $_.verdict -eq 'INCONCLUSIVE_RESTORATION' -and ($_.reasons -join ' ') -match 'no hardware-derived tolerance' }).Count -eq 6 -and
     @($ch | Where-Object { $null -ne $_.metrics.offsetNs }).Count -eq 6) `
    ("verdict={0} findings={1} gate={2} characterization={3}" -f $ok.verdict, (@($ok.findings) -join ' | '), $g1.Why, (($ch | ForEach-Object { "$($_.did):$($_.verdict)" }) -join ','))

# 002..007 each finding makes the document NOT_VERIFIED and the approval gate rejects it
$cases = [ordered]@{
    '002-missing-mac'              = @{ Doc = (New-Doc @((New-Pass 100), (New-Pass 105)) -Identity (New-Identity '0x15BB')); Rx = '0x15BB: MAC address not observed' }
    '003-timinca-changed'          = @{ Doc = (New-Doc @((New-Pass 100), (New-Pass 105 -Timinca @{ '0x125B' = 0x18000000 }))); Rx = 'TIMINCA 0x08000000 -> 0x18000000' }
    '004-phc-backwards-in-window'  = @{ Doc = (New-Doc @((New-Pass 100), (New-Pass 105 -Backwards))); Rx = '0x1533: PHC pass 0 -> 1: CLEANUP_FAILED' }
    '005-phc-near-epoch'           = @{ Doc = (New-Doc @((New-Pass 1 -PhcBase 0), (New-Pass 6 -PhcBase 0))); Rx = 'within 10 s of its epoch' }
    '006-expected-adapter-missing' = @{ Doc = (New-Doc @((New-Pass 100), (New-Pass 105)) -Expect @('0x1533', '0x15BB', '0x125B', '0x1539')); Rx = 'expected adapter 0x1539 not present' }
    '007-filter-not-bound'         = @{ Doc = (New-Doc @((New-Pass 100 -Unbound '0x1533'), (New-Pass 105 -Unbound '0x1533'))); Rx = "0x1533: IntelAvbFilter binding 'False'" }
    '008-no-driver-identity'       = @{ Doc = (New-Doc @((New-Pass 100), (New-Pass 105)) -Driver ([ordered]@{ path = 'x'; Sha256 = ''; fileVersion = ''; serviceStatus = 'not found' })); Rx = 'driver identity' }
}
foreach ($k in $cases.Keys) {
    $d = $cases[$k].Doc; $g = Test-Doc $d "$k.json"
    tc_check "TEST-HILBASE-$k-is-not-verified-and-rejected-by-the-approval-gate" `
        ($d.verdict -eq 'NOT_VERIFIED' -and (@($d.findings) -join ' | ') -match [regex]::Escape($cases[$k].Rx) -and -not $g.Ok) `
        ("verdict={0} gate={1} findings={2}" -f $d.verdict, $g.Ok, (@($d.findings) -join ' | '))
}

# 009 static: the producer never writes the safety record, never approves/clears/rebaselines, and issues no IOCTL of its
#     own (every device access goes through the runner's read observer Get-AvbHilState)
$src = [System.IO.File]::ReadAllText($Producer)
$forbidden = @('Approve-AvbHilSafetyBaseline', 'Initialize-AvbHilSafetyRecord', 'Clear-AvbHilSafetyHalt', 'Invoke-AvbHilSafetyRebaseline', 'Add-AvbHilSafetyEvent',
               'Write-AvbHilSafetyFile', 'DeviceIoControl', 'Set-NetAdapter', 'Disable-', 'Enable-', 'Restart-')
$codeOnly = ($src -split "`r?`n" | Where-Object { $_ -notmatch '^\s*#' } | ForEach-Object { ($_ -replace "'[^']*'", "''") }) -join "`n"
$hits = @($forbidden | Where-Object { $codeOnly -match [regex]::Escape($_) })
tc_check 'TEST-HILBASE-009-producer-never-writes-the-safety-record-or-hardware' ($hits.Count -eq 0) ("forbidden calls: {0}" -f ($hits -join ', '))

# 010: the documented command line (-ExpectDids 0x1533,0x15BB,0x125B, unquoted) must expect exactly those devices.
#      2026-10-22 12:46 HIL run: [string[]] binding turned them into '5427','5563','4699' -> 0x5427/0x5563/0x4699 and
#      every present adapter was reported "not present" (hil-baseline-evidence-20261022_124638.json).
$pAst = [System.Management.Automation.Language.Parser]::ParseFile($Producer, [ref]$null, [ref]$null)
$bind = [scriptblock]::Create($pAst.ParamBlock.Extent.Text + "`n" + '@($ExpectDids | ForEach-Object { ConvertTo-AvbDid $_ }) -join '',''')
$asTyped = & ([scriptblock]::Create('param($b) & $b -ExpectDids 0x1533,0x15BB,0x125B')) $bind
# ... and the bound values must survive the hand-over to New-AvbHilBaselineDocument (its own -ExpectDids parameter).
$bound = & ([scriptblock]::Create($pAst.ParamBlock.Extent.Text + "`n" + ',$ExpectDids')) -ExpectDids 0x1533,0x15BB,0x125B
$docE = New-AvbHilBaselineDocument -Passes @((New-Pass 100), (New-Pass 105), (New-Pass 110)) -Identity (New-Identity) -Driver $drv -Supported $supported `
    -ExpectDids $bound -Safety $saf -IntervalSeconds 5 -HostName $env:COMPUTERNAME -CreatedUtc '2026-10-22T00:00:00Z'
$notPresent = @($docE.findings | Where-Object { $_ -match 'not present' })
tc_check 'TEST-HILBASE-010-unquoted-hex-expected-dids-bind-to-the-intended-devices' `
    ($asTyped -eq '0x1533,0x15BB,0x125B' -and $notPresent.Count -eq 0) ("unquoted -> [{0}]; document findings: {1}" -f $asTyped, ($notPresent -join ' | '))

# 011 (cause 4b-i, 2026-10-27): the observer's per-adapter IOCTL_AVB_QUERY_HW_SNAPSHOT result is recorded raw in the
#     document - every entry (id, offset, value, AVB_HWREG_STATE_* code) per pass and adapter, a failed snapshot as an
#     explicit HWREG_SNAPSHOT error - and is not judged yet (hardware characterization precedes any comparison rule).
#     Offsets/values below are synthetic placeholders, not register definitions.
$pR = New-Pass 100
$pR.Clock[0] | Add-Member -NotePropertyName HwRegs -NotePropertyValue @([ordered]@{ id = 1; offset = 0x100; value = 0x18000000; state = 1 }, [ordered]@{ id = 3; offset = 0x104; value = 0x10; state = 1 }, [ordered]@{ id = 5; offset = 0; value = 0; state = 2 })
$pR.Clock[1] | Add-Member -NotePropertyName HwRegsError -NotePropertyValue 'QUERY_HW_SNAPSHOT failed (Win32 31)'
$docR = New-Doc @($pR, (New-Pass 105))
$hw = @($docR.measurements | Where-Object { $_.name -eq 'HWREG' })
$he = @($docR.measurements | Where-Object { $_.name -eq 'HWREG_SNAPSHOT' })
$hw3 = @($hw | Where-Object { $_.did -eq '0x125B' -and $_.id -eq 3 -and $_.pass -eq 0 })
tc_check 'TEST-HILBASE-011-hw-snapshot-is-recorded-raw-per-pass-and-adapter-not-judged' `
    ($hw.Count -eq 3 -and $hw3.Count -eq 1 -and $hw3[0].value -eq '0x00000010' -and $hw3[0].offset -eq '0x00000104' -and $hw3[0].state -eq 1 -and
     @($hw | Where-Object { $_.id -eq 5 -and $_.state -eq 2 }).Count -eq 1 -and
     $he.Count -eq 1 -and $he[0].did -eq '0x1533' -and $he[0].error -match 'failed' -and $docR.verdict -eq 'BASELINE_VERIFIED') `
    ("HWREG={0} HWREG_SNAPSHOT={1} verdict={2} findings={3}" -f $hw.Count, $he.Count, $docR.verdict, (@($docR.findings) -join ' | '))

Remove-Item $Tmp -Recurse -Force -ErrorAction SilentlyContinue
Write-Host ""
Write-Host ("  Summary: {0} passed, {1} failed of {2} tests" -f $passed, $failed, $total) -ForegroundColor Cyan
if ($failed -gt 0) { Write-Host "[FAIL] TEST-HILBASE: baseline evidence producer contract violated" -ForegroundColor Red; exit 1 }
Write-Host "[PASS] TEST-HILBASE: baseline evidence producer verified (offline, synthetic)" -ForegroundColor Green
exit 0
