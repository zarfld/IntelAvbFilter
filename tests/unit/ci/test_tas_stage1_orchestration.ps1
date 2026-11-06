# test_tas_stage1_orchestration.ps1
# TEST-TAS-STAGE1-*: orchestration of tests/diagnostic/Invoke-TasStage1DryRun.ps1 (#364 Stage 1 dry run, and its -CaseA mode).
# Run the interrupt cases from a process with a console: under the Bash tool the Ctrl+C never arrives and they fail.
#
# Offline.  Nothing here touches a driver, an adapter, a trace session or the machine lock: the runner, logman, the tool,
# the preflight and the adapter queries are stand-ins, and HilLock.ps1 is the real file pointed at a per-case lock path.
# A pass says the orchestration logic holds.  It is never HIL evidence, and says nothing about driver behaviour, adapter
# capabilities, observer operation, restoration or TAS function.  HIL verification pending until Q runs the script.
#
# Contract under test:
#   - the gate is per adapter and tied to this execution: the runner's results document (schema, revision, clean tree,
#     freshness, one executed record of this test, provenance, safety, driver) plus Phase 1 and the register block of
#     every passing adapter in the log; only PASS and SKIP_UNSUPPORTED get through, and at least one adapter must pass;
#   - a legacy failure outside the gate stays visible and stays FAILED;
#   - every adapter is represented, every ELIGIBLE adapter passed before and after, whatever its identity or order;
#   - missing evidence is never success; matching registers alone never produce a clean verdict; never COMPLETE;
#   - cleanup is independent: a failure or an interrupt in one step does not skip the next, and the lock is released;
#   - evidence paths are absolute, and evidence of an earlier run is never overwritten;
#   - a mutant is KILLED only by a failed assertion on a complete run of a valid mutant whose unmutated case passed;
#     Get-Stage1MutantVerdict decides, and every other outcome keeps its own verdict and blocks complete coverage.
#
# Fixtures are verbatim excerpts of my own HIL evidence, stamp 20261106_101054: I226 0x125B, I210 0x1533, 0x15BB,
# Debug driver EA3DA3EE7CBCDFC9..., commit 76e11d9.  logs/diag-i364-tsnreg-pre-20261106_101054.log and its results document.
#
# Recovered from a harness that lived outside the repository; the case names are unchanged.
# Traces to: #364.  TEST issue: pending, drafted for Q's review.

param([switch]$ListOnly, [string[]]$Only = @(), [switch]$KeepSandbox)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path (Split-Path (Split-Path $ScriptDir -Parent) -Parent) -Parent
$Subject   = Join-Path $RepoRoot 'tests\diagnostic\Invoke-TasStage1DryRun.ps1'
$HilLock   = Join-Path $RepoRoot 'tools\lib\HilLock.ps1'
$Clock     = [Diagnostics.Stopwatch]::StartNew()
$Throttle  = [Environment]::ProcessorCount    # twice this was no faster: the cases are CPU bound
$Rev       = '76e11d9b7ffef29bf08e1f16585ca9ef50d45adf'
$OutOfRange = if ($Only.Count -gt 0) { 30 } else { 240 }      # whole-test bound, seconds

$total = 0; $passed = 0; $failed = 0
function tc_check { param([string]$n, [bool]$ok, [string]$why)
    $script:total++
    if ($ok) { $script:passed++; Write-Host ("  [PASS] {0}" -f $n) -ForegroundColor Green }
    else { $script:failed++; Write-Host ("  [FAIL] {0}: {1}" -f $n, $why) -ForegroundColor Red } }

Write-Host ""
Write-Host "TEST-TAS-STAGE1: Stage 1 dry run orchestration (offline, stand-ins only, not HIL evidence)" -ForegroundColor Cyan

$FxLog = @'
=> ssot_register_validation_test.exe  [ReadOnly]
    === Intel AVB Filter - SSOT Register Validation Test ===
    Device Info: Intel I219 Gigabit Ethernet - Enhanced PTP
    Capabilities: 0x000000C3

    === Phase 1: Compile-Time SSOT Validation ===
      I226_SYSTIML         SSOT=0x0B600  Golden=0x0B600  [PASS]
      I226_BASET_L         SSOT=0x03314  Golden=0x03314  [PASS]

    === Phase 2: Hardware Register Accessibility Test ===
    --- Hardware Validation: Current Device PTP Block ---
      SYSTIM 1: 0x0000000000000000
      SYSTIM 2: 0x0000000000000000
      Delta:    ZERO or NEGATIVE [FAIL - PTP clock not running]
    --- Hardware Validation: Current Device TSAUXC Register ---
      TSAUXC: 0x00000000
      [PASS - TSAUXC register accessible]

    === Phase 3: TSN Registers On Every TAS Adapter ===
      adapter:0 did=0x125B luid=0x0006008006000000 ifIndex=8
        tasArmed=0
        TQAVCTRL     0x03570 = 0x00000000
        TXPBSIZE     0x03404 = 0x04000014
        DTXMXPKTSZ   0x0355C = 0x00000098
        BASET_L      0x03314 = 0x00000000
        BASET_H      0x03318 = 0x00000000
        QBVCYCLET    0x0331C = 0x3B9ACA00
        QBVCYCLET_S  0x03320 = 0x00000000
        STQT0        0x03324 = 0x00000000
        ENDQT0       0x03334 = 0x00000000
        TXQCTL0      0x03344 = 0x00000000
        TDLEN0       0x0E008 = 0x00008000
        TXDCTL0      0x0E028 = 0x02040000
        sample 0 TDH/TDT q0=70/70 q1=0/0 q2=0/0 q3=0/0
    [PASS] TC-TSNREG-001/adapter:0 did=0x125B
    [SKIP_UNSUPPORTED] TC-TSNREG-001/adapter:1 did=0x1533: adapter does not advertise INTEL_CAP_TSN_TAS
    [SKIP_UNSUPPORTED] TC-TSNREG-001/adapter:2 did=0x15BB: adapter does not advertise INTEL_CAP_TSN_TAS

    === Phase 4: QUERY_HW_SNAPSHOT On Every Adapter ===
      snapshot adapter:0 did=0x125B luid=0x0006008006000000 ifIndex=8
        id=1  offset=0x0B608 value=0x02F42400 state=1
    [PASS] TC-HWSNAP-001/adapter:0 did=0x125B

    === Test Summary ===
    Tests Passed: 60
    Tests Failed: 1
[FAIL] ssot_register_validation_test.exe FAILED (exit 1)
[FAIL] OVERALL: 1 test(s) FAILED
'@
$FxJson = @'
{"schema":"avb-runner-results/1","startedUtc":"__NOW__","stateHalt":"",
 "source":{"ok":true,"revision":"76e11d9b7ffef29bf08e1f16585ca9ef50d45adf","dirty":false},
 "installedDriver":{"matchesConfiguration":true},"machineSafety":{"stateAtStart":"CLEAR"},
 "adapters":[{"did":"0x1533"},{"did":"0x15BB"},{"did":"0x125B"}],
 "records":[{"entry":"ssot_register_validation_test","executed":true,"provenance":{"verified":true},
  "testcases":[{"Id":"TC-TSNREG-001","Verdict":"PASS","AdapterIndex":0,"Detail":"did=0x125B"},
   {"Id":"TC-TSNREG-001","Verdict":"SKIP_UNSUPPORTED","AdapterIndex":1,"Detail":"did=0x1533: adapter does not advertise INTEL_CAP_TSN_TAS"},
   {"Id":"TC-TSNREG-001","Verdict":"SKIP_UNSUPPORTED","AdapterIndex":2,"Detail":"did=0x15BB: adapter does not advertise INTEL_CAP_TSN_TAS"}]}]}
'@
$T = '[LOCAL_DIAGNOSTIC]'
$FxTool = @"
$T plan luid=0x0006008006000000 did=0x125B hwCaps=0x000001FF driverCaps=0x000000FF hwState=3 link=1 managementLink=1 class=ELIGIBLE
$T plan luid=0x0006008000000000 did=0x1533 hwCaps=0x000000C3 driverCaps=0x000000C3 hwState=3 link=1 managementLink=1 class=SKIP_UNSUPPORTED
$T plan luid=0x0006008001000000 did=0x15BB hwCaps=0x000000C3 driverCaps=0x000000C3 hwState=3 link=1 managementLink=1 class=SKIP_UNSUPPORTED
$T observer proved luid=0x0006008000000000 did=0x1533 frames=10 of 10
$T fact register compared=42 changed=0 unreadable=0
$T dry run complete: instance and observer proved, polling resolution measured, nothing triggered
$T adapter result luid=0x0006008006000000 did=0x125B class=ELIGIBLE exit=0
$T adapter result luid=0x0006008000000000 did=0x1533 class=SKIP_UNSUPPORTED not a target
$T adapter result luid=0x0006008001000000 did=0x15BB class=SKIP_UNSUPPORTED not a target
$T summary adapters=3 eligible=1 exit=0
"@

# Case A fixtures.  UNVERIFIED: no FilterPause, FilterRestart or event 10312 has ever been observed on this stack.  The tool
# lines follow printf formats in tests/diagnostic/tas_gate_investigation.c, and the trace layout follows Stage 0 and the
# provider metadata (logs/diag-i364-stage0-20261105_204526.etl.xml, Microsoft-Windows-NDIS on build 26300).  Replace them
# with real evidence after the first run; if the two differ, investigate before changing an expectation.
$FxInstance = '{5989BA55-D568-47A4-AFBF-B47F27E1B435}-{3F74AE86-14F9-4E79-9445-5B1E52CCD192}-0000'
$FxToolA = @"
$T plan luid=0x0006008006000000 did=0x125B hwCaps=0x000001FF driverCaps=0x000000FF hwState=3 link=1 managementLink=1 class=ELIGIBLE
$T plan luid=0x0006008000000000 did=0x1533 hwCaps=0x000000C3 driverCaps=0x000000C3 hwState=3 link=1 managementLink=1 class=SKIP_UNSUPPORTED
$T plan luid=0x0006008001000000 did=0x15BB hwCaps=0x000000C3 driverCaps=0x000000C3 hwState=3 link=1 managementLink=1 class=SKIP_UNSUPPORTED
$T --- adapter luid=0x0006008006000000 did=0x125B ---
$T instance luid=0x0006008006000000 name=$FxInstance
$T observer proved luid=0x0006008000000000 did=0x1533 frames=10 of 10
    timing qpcHz=10000000 passive=0 baselinePolls=100 meanGapUs=1.1 worstGapUs=48.4
    marker trigger-issued luid=0x0006008006000000 qpcBefore=1000000 qpcAfter=1000200 hostUs=1 status=0
    edge pause entry between qpc=1000300 and qpc=1000310 widthUs=1.0
    edge restart entry between qpc=1000800 and qpc=1000810 widthUs=1.0
$T other adapters changed=0 of 2
$T fact register compared=11 changed=0 unreadable=0
    marker run-end luid=0x0006008006000000 qpcBefore=2000000 qpcAfter=2000200 hostUs=2 status=0
$T result: exit=0 lifecycle=RESUMED_BY_TRAFFIC cleanup=INCONCLUSIVE_RESTORATION; recovery, if needed, is the operator's: this mode never cycles the adapter
$T adapter result luid=0x0006008006000000 did=0x125B class=ELIGIBLE exit=0
$T adapter result luid=0x0006008000000000 did=0x1533 class=SKIP_UNSUPPORTED not a target
$T adapter result luid=0x0006008001000000 did=0x15BB class=SKIP_UNSUPPORTED not a target
$T summary adapters=3 eligible=1 exit=0
"@
function New-Etl {
    param([string[]]$Values = @('a', 'b', 'c', 'd'), [int[]]$At = @(200, 400, 700, 900), [string]$Name = $FxInstance, [switch]$NoMarkers, [int]$Lost = 0, [switch]$NoOther, [int]$Id = 10312)
    $ns = 'xmlns="http://schemas.microsoft.com/win/2004/08/events/event"'
    $time = { param([int]$ticks) '2026-11-06T16:00:{0:00}.{1:0000000}00+00:59' -f [Math]::Floor($ticks / 10000000), ($ticks % 10000000) }
    $sys = { param($g, $e, $t) "<System><Provider Guid=`"$g`" /><EventID>$e</EventID><TimeCreated SystemTime=`"$(& $time $t)`" /></System>" }
    $mark = '{b7e1f3c2-6a54-4d0e-9c37-3640e8a1d364}'; $ndis = '{cdead503-17f5-4a3e-b7ae-df8cc2902eb9}'
    $e = @("<Event $ns>$(& $sys '{9e814aad-3204-11d2-9a82-006008a86939}' 0 0)<EventData><Data Name=`"EventsLost`">  $Lost</Data></EventData></Event>")
    if (-not $NoMarkers) { $e += "<Event $ns>$(& $sys $mark 0 0)<Data>i364 trigger-issued luid=0x0006008006000000 qpc=1000000</Data></Event>" }
    if (-not $NoOther) { $e += "<Event $ns>$(& $sys $ndis 10302 100)<EventData><Data Name=`"NetLuidIndex`">32774</Data></EventData></Event>" }
    for ($i = 0; $i -lt $Values.Count; $i++) {
        $e += "<Event $ns>$(& $sys $ndis $Id $At[$i])<EventData><Data Name=`"State`">$($Values[$i])</Data><Data Name=`"MiniportIfGuid`">{5989BA55-D568-47A4-AFBF-B47F27E1B435}</Data><Data Name=`"FilterInstanceName`">$Name</Data></EventData></Event>"
    }
    if (-not $NoMarkers) { $e += "<Event $ns>$(& $sys $mark 0 5000)<Data>i364 run-end luid=0x0006008006000000 qpc=1005000</Data></Event>" }
    "<Events>$($e -join '')</Events>"
}

$StandIn = @'
using System; using System.IO; using System.Text; using System.Threading; using System.Runtime.InteropServices;
class S {
  [DllImport("kernel32.dll")] static extern bool FreeConsole();
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool AttachConsole(uint p);
  [DllImport("kernel32.dll")] static extern bool SetConsoleCtrlHandler(IntPtr h, bool add);
  [DllImport("kernel32.dll", SetLastError=true)] static extern bool GenerateConsoleCtrlEvent(uint e, uint g);
  // kernel32 directly: the Process class loads System.dll and walks the module list, and this runs four times per case.
  [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern uint GetModuleFileNameW(IntPtr h, StringBuilder b, uint n);
  [DllImport("kernel32.dll")] static extern uint GetCurrentProcessId();
  static string C;
  static void T(string s) { for (int i = 0; i < 50; i++) { try { File.AppendAllText(C + "\\trace.txt", s + "\r\n"); return; } catch (IOException) { Thread.Sleep(10); } } }
  static string R(string f) { string p = C + "\\" + f; return File.Exists(p) ? File.ReadAllText(p).Trim() : ""; }
  static int Main(string[] a) {
    StringBuilder mb = new StringBuilder(1024); GetModuleFileNameW(IntPtr.Zero, mb, 1024);
    string me = mb.ToString(), n = Path.GetFileNameWithoutExtension(me).ToLowerInvariant();
    if (n == "inject") {
      FreeConsole(); if (!AttachConsole(uint.Parse(a[0]))) return 3;
      SetConsoleCtrlHandler(IntPtr.Zero, true); bool ok = GenerateConsoleCtrlEvent(0, 0); Thread.Sleep(150); FreeConsole(); return ok ? 0 : 4; }
    C = Path.GetDirectoryName(me); while (!File.Exists(C + "\\fi.txt")) C = Path.GetDirectoryName(C);
    string fi = R("fi.txt");
    if (n == "logman") { string v = a.Length > 0 ? a[0] : "", flag = C + "\\session", once = C + "\\stoponce";
      if (v == "create") { T("trace-start"); if (fi == "trace-fail") return 1; File.WriteAllText(flag, "x"); return 0; }
      if (v == "stop") { T("trace-stop");
        if (R("slowstop.txt") == "1" && !File.Exists(once)) { File.WriteAllText(once, "x"); T("trace-stop-wait"); Thread.Sleep(5000); }
        if (fi == "stop-fail") return 1;
        if (fi == "stop-fail-once" && !File.Exists(once)) { File.WriteAllText(once, "x"); return 1; }
        if (fi == "still-running") return 0;
        if (File.Exists(flag)) File.Delete(flag); return 0; }
      if (v == "query") { T("trace-query"); if (fi == "query-denied") return -2147024891; return File.Exists(flag) ? 0 : -2144337918; }
      if (v == "update") { T("trace-update:" + string.Join(" ", a)); return fi == "ca-marker-fail" ? 1 : 0; }
      return 9; }
    T("tool:" + string.Join(" ", a)); T("tool-pid:" + GetCurrentProcessId());
    Console.Write(R("tool.txt") + "\r\n"); Console.Out.Flush();
    string s = R("toolsleep.txt");
    if (s != "") { T("tool-wait"); Thread.Sleep(int.Parse(s)); T("tool-finished"); }
    return int.Parse(R("tool.rc")); } }
'@

$StubRunner = @'
param($Configuration, $Suite, $Tests, $ResultsJson)
$c = (Resolve-Path "$PSScriptRoot\..\..").Path
$fi = (Get-Content "$c\fi.txt" -Raw).Trim()
function T([string]$s) { for ($q = 0; $q -lt 100; $q++) { try { [IO.File]::AppendAllText("$c\trace.txt", "$s`r`n"); return } catch { Start-Sleep -Milliseconds 10 } } }
$phase = if ([IO.Path]::GetFileName($ResultsJson) -match '-pre-') { 'pre' } else { 'post' }
T "runner-$phase"
T "json:$ResultsJson"
if ($fi -notin 'lock-fail', 'lock-stale', 'lock-stale-exit-throw', 'exit-throw') { . "$c\tools\lib\HilLock.ps1"; $x = Resolve-AvbHilLockContext; T "lockctx:$phase`:$($x.How):$($x.Why)" }
if ($fi -eq 'tool-missing' -and $phase -eq 'pre') { Remove-Item -LiteralPath "$c\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe" -Force }
if ((Test-Path "$c\throw.txt") -and (Get-Content "$c\throw.txt" -Raw).Trim() -eq $phase) { throw "$phase runner threw" }
if ((Test-Path "$c\throwonce.txt") -and (Get-Content "$c\throwonce.txt" -Raw).Trim() -eq $phase -and -not (Test-Path "$c\once")) { Set-Content "$c\once" x; throw "$phase runner threw once" }
if ((Test-Path "$c\sleep.txt") -and (Get-Content "$c\sleep.txt" -Raw).Trim() -eq $phase -and -not (Test-Path "$c\slept")) { Set-Content "$c\slept" x; T "runner-$phase-wait"; Start-Sleep -Milliseconds 2500 }
if (Test-Path "$c\$phase.txt") { Get-Content "$c\$phase.txt" }
if ((Test-Path "$c\$phase.json") -and [IO.Path]::IsPathRooted($ResultsJson)) { [IO.File]::WriteAllText($ResultsJson, (Get-Content "$c\$phase.json" -Raw).Replace('__NOW__', (Get-Date).ToUniversalTime().ToString('o'))) }
exit [int](Get-Content "$c\$phase.rc")
'@
$StubLock = @'
$c = (Resolve-Path "$PSScriptRoot\..\..").Path
if ((Get-Content "$c\fi.txt" -Raw).Trim() -notin 'lock-fail', 'lock-stale', 'lock-stale-exit-throw', 'exit-throw') {
    . "$PSScriptRoot\HilLock.real.ps1"
    . ([scriptblock]::Create("function Get-AvbHilLockPath { '$c\lock' }"))   # same scope as the real one, or the real path wins
    return }
Set-Item -Path Function:\global:Enter-AvbHilLock -Value ([scriptblock]::Create(@"
param(`$Kind, `$Label, `$WaitSeconds) `$f = (Get-Content '$c\fi.txt' -Raw).Trim(); Add-Content '$c\trace.txt' 'lock-enter'
if (`$f -eq 'lock-fail') { return @{ Ok = `$false; Why = 'busy'; Owner = `$null; PreviousOwner = `$null } }
if (`$f -like 'lock-stale*') { return @{ Ok = `$true; Owner = 'o'; PreviousOwner = @{ a = 1 }; Why = '' } }
return @{ Ok = `$true; Owner = 'o'; PreviousOwner = `$null; Why = '' }
"@))
Set-Item -Path Function:\global:Exit-AvbHilLock -Value ([scriptblock]::Create(@"
param(`$Owner) Add-Content '$c\trace.txt' 'lock-exit'
if ((Get-Content '$c\fi.txt' -Raw).Trim() -like '*exit-throw') { throw 'exit failed' }
return @{ Ok = `$true; Why = '' }
"@))
'@
$Prelude = @'
if ($global:FI -ne 'kill-throw') { Remove-Item -Path Function:\Start-Process -Force -ErrorAction SilentlyContinue }
if ($global:FI -ne 'kill-throw' -and (Get-Command Start-Process).CommandType -ne 'Cmdlet') { throw 'a fake from an earlier case is still defined' }
function global:CT([string]$s) { for ($q = 0; $q -lt 100; $q++) { try { [IO.File]::AppendAllText("$global:CaseDir\trace.txt", "$s`r`n"); return } catch { Start-Sleep -Milliseconds 10 } } }
function global:git { '__REV__' }
function global:tracerpt { CT 'decode'
    if ($global:FI -eq 'decode-throw') { throw 'decode failed' }
    if ($global:FI -eq 'decode-rc') { $global:LASTEXITCODE = 1; return }
    $body = if ($global:FI -eq 'lifecycle-event') { '<EventID>10312</EventID>' } else { '<EventID>0</EventID>' }
    if (Test-Path -LiteralPath "$global:CaseDir\etl.xml") { $body = Get-Content -LiteralPath "$global:CaseDir\etl.xml" -Raw }
    Set-Content "$($args[0]).xml" $body; $global:LASTEXITCODE = 0 }
function global:CF([string]$n, [string]$d) { $p = "$global:CaseDir\$n"; if (Test-Path -LiteralPath $p) { (Get-Content -LiteralPath $p -Raw).Trim() } else { $d } }
function global:Get-NetIPAddress { [CmdletBinding()] param([int]$InterfaceIndex, $AddressFamily)
    CT "ifindex:$InterfaceIndex"; if ($global:FI -eq 'ca-management-check-throws') { throw 'address query failed' }; if ($global:FI -eq 'ca-no-target-ip') { return }; [pscustomobject]@{ IPAddress = '10.0.0.12' } }
function global:Get-NetTCPConnection { [CmdletBinding()] param($OwningProcess, $State)
    $t = CF 'mgmt-tcp.txt' '10.0.0.71'; if ($t -ne 'none') { [pscustomobject]@{ LocalAddress = $t } } }
function global:Get-NetUDPEndpoint { [CmdletBinding()] param($OwningProcess)
    [pscustomobject]@{ LocalAddress = '0.0.0.0' }; [pscustomobject]@{ LocalAddress = '::' }
    $u = CF 'mgmt-udp.txt' 'none'; if ($u -ne 'none') { [pscustomobject]@{ LocalAddress = $u } } }
function global:Get-NetAdapter { $global:gn++; CT "state-$global:gn"
    if ($global:FI -eq 'state-pre-throw' -and $global:gn -eq 1) { throw 'state failed' }
    if ($global:FI -eq 'state-post-throw' -and $global:gn -eq 2) { throw 'state failed' }
    if ($global:FI -eq 'state-post-always' -and $global:gn -ge 2) { throw 'state failed' }
    $s = if ($global:FI -eq 'state-diff' -and $global:gn -eq 2) { 'Down' } else { 'Up' }
    [pscustomobject]@{ InterfaceGuid = '{x}'; Status = $s; MediaConnectionState = 'Connected'; LinkSpeed = '1 Gbps'; PromiscuousMode = $false } }
function global:Get-CimInstance { [CmdletBinding()] param($ClassName, $Filter)
    [pscustomobject]@{ State = $(if ($global:FI -eq 'stop-driver-stopped') { 'Stopped' } else { 'Running' }); PathName = "\??\$global:CaseDir\installed.sys" } }
function global:Get-Process { [CmdletBinding()] param([string]$Name, [int]$Id)
    if ($Name -eq 'Runner.Worker') { if ($global:FI -eq 'stop-ci-job') { return [pscustomobject]@{ Id = 1 } }; return }
    if ($Name -eq 'vncserver') { if ($global:FI -eq 'ca-no-process') { return }; return [pscustomobject]@{ Id = 7 } }
    if ($Id) { Microsoft.PowerShell.Management\Get-Process -Id $Id -ErrorAction SilentlyContinue } }
function global:Start-Transcript { param($Path) }
function global:Stop-Transcript { }
'@
$KillFake = @'
function global:Start-Process { param($FilePath, $ArgumentList, [switch]$NoNewWindow, [switch]$PassThru, $RedirectStandardOutput, $RedirectStandardError)
    CT "tool:$ArgumentList"; Set-Content $RedirectStandardOutput 'x'
    $o = New-Object psobject -Property @{ Handle = 1; HasExited = $false; ExitCode = 0; Id = 1 }
    $o | Add-Member ScriptMethod WaitForExit { param($ms) Start-Sleep -Milliseconds 20; $false }
    $o | Add-Member ScriptMethod Kill { CT 'tool-kill'; throw 'kill failed' }
    return $o }
'@

function Set-V($j, $v) { [regex]::Replace($j, '"Verdict":\s*"PASS",\s*"AdapterIndex":\s*0', "`"Verdict`":`"$v`",`"AdapterIndex`":0") }
$ok = 'NO_CHANGE'; $inc = 'INCONCLUSIVE_RESTORATION'; $cf = 'CLEANUP_FAILED'
$cases = @(
 @{n='legacy-fail-clean'; g='PASS'; d='0'; k='OK'; v=$ok; x=3}
 @{n='all-pass'; m={param($c) $c.pre=$c.pre -replace '\[FAIL - PTP clock not running\]','[PASS]'; $c.post=$c.pre; $c.preRc=0; $c.postRc=0}; g='PASS'; d='0'; k='OK'; v=$ok; x=0}
 @{n='phase1-fail'; m={param($c) $c.pre=$c.pre -replace 'I226_BASET_L( +)SSOT=0x03314  Golden=0x03314  \[PASS\]','I226_BASET_L$1SSOT=0x03314  Golden=0x03999  [FAIL]'; $c.post=$c.pre}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc; x=1}
 @{n='tc-fail'; m={param($c) $c.preJ=Set-V $c.preJ 'FAIL'}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc; x=1}
 @{n='tc-skip-unknown'; m={param($c) $c.preJ=Set-V $c.preJ 'SKIP'}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='tc-inconclusive'; m={param($c) $c.preJ=Set-V $c.preJ 'INCONCLUSIVE'}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='tc-blocked'; m={param($c) $c.preJ=Set-V $c.preJ 'BLOCKED'}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='other-adapter-inconclusive'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"Verdict":\s*"SKIP_UNSUPPORTED",\s*"AdapterIndex":\s*1','"Verdict":"INCONCLUSIVE","AdapterIndex":1')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='other-adapter-failed'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"Verdict":\s*"SKIP_UNSUPPORTED",\s*"AdapterIndex":\s*2','"Verdict":"FAIL","AdapterIndex":2')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='all-skip'; m={param($c) $c.preJ=Set-V $c.preJ 'SKIP_UNSUPPORTED'}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='log-truncated'; m={param($c) $c.pre=$c.pre.Substring(0,$c.pre.IndexOf('=== Phase 3'))}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='no-summary'; m={param($c) $c.pre=$c.pre.Replace('=== Test Summary ===','')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='duplicate-execution'; m={param($c) $c.pre=$c.pre+$c.pre}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='log-empty'; m={param($c) $c.pre=''}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-missing'; m={param($c) $c.preJ=$null}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-corrupt'; m={param($c) $c.preJ='{not json'}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-stale'; m={param($c) $c.preJ=$c.preJ.Replace('__NOW__','2026-11-05T09:36:12.0000000Z')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-wrong-test'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"entry":\s*"ssot_register_validation_test"','"entry":"other_test"')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-other-revision'; m={param($c) $c.preJ=$c.preJ.Replace('76e11d9b7ffef29bf08e1f16585ca9ef50d45adf','1111111111111111111111111111111111111111')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-dirty'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"dirty":\s*false','"dirty":true')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-no-provenance'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"verified":\s*true','"verified":false')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='json-not-executed'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"executed":\s*true','"executed":false')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='safety-not-clear'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"stateAtStart":\s*"CLEAR"','"stateAtStart":"UNSAFE"')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='driver-mismatch'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"matchesConfiguration":\s*true','"matchesConfiguration":false')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='state-halt'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"stateHalt":\s*""','"stateHalt":"halted"')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='adapter-unrepresented'; m={param($c) $c.preJ=[regex]::Replace($c.preJ,'"AdapterIndex":\s*2','"AdapterIndex":7')}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='tasarmed-1'; m={param($c) $c.pre=$c.pre.Replace('tasArmed=0','tasArmed=1'); $c.post=$c.pre}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='pass-no-luid-block'; m={param($c) $c.pre=$c.pre -replace 'adapter:0 did=0x125B luid=','adapter:9 did=0x125B luid='}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='pass-no-registers'; m={param($c) $c.pre=$c.pre -replace '(?m)^\s+[A-Z0-9_]+\s+0x[0-9A-Fa-f]{5} = 0x.*$',''; $c.post=$c.pre}; g='FAIL'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='eligible-not-passed'; m={param($c) $c.tool=$c.tool.Replace('luid=0x0006008006000000','luid=0x0006008009000000')}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='adapter-count-differs'; m={param($c) $c.tool=$c.tool.Replace('summary adapters=3','summary adapters=2')}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='adapter-missing-from-plan'; m={param($c) $c.tool=($c.tool -split "`n" | Where-Object { $_ -notmatch 'plan luid=0x0006008001000000' }) -join "`n"}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='duplicate-plan-luid'; m={param($c) $c.tool=$c.tool.Replace('plan luid=0x0006008001000000','plan luid=0x0006008000000000')}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='no-tool-summary'; m={param($c) $c.tool=($c.tool -split "`n" | Where-Object { $_ -notmatch 'summary adapters' }) -join "`n"}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='eligible-zero'; m={param($c) $c.tool=(($c.tool.Replace('class=ELIGIBLE','class=SKIP_UNSUPPORTED').Replace('eligible=1','eligible=0')) -split "`n" | Where-Object { $_ -notmatch 'dry run complete' }) -join "`n"}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='summary-exit-differs'; m={param($c) $c.toolRc=1}; g='PASS'; d='1'; k='FAIL'; v=$ok}
 @{n='no-result-line'; m={param($c) $c.tool=($c.tool -split "`n" | Where-Object { $_ -notmatch 'adapter result luid=0x0006008000000000' }) -join "`n"}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='no-dry-complete'; m={param($c) $c.tool=($c.tool -split "`n" | Where-Object { $_ -notmatch 'dry run complete' }) -join "`n"}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='empty-tool-output'; m={param($c) $c.tool=''}; g='PASS'; d='0'; k='FAIL'; v=$ok}
 @{n='identity-and-order-independent'; m={param($c) foreach ($p in 'pre','post') { $c.$p=$c.$p.Replace('0x125B','0xABCD').Replace('0x0006008006000000','0x00060080AA000000') }; $c.tool=$c.tool.Replace('0x125B','0xABCD').Replace('0x0006008006000000','0x00060080AA000000'); $l=@($c.tool -split "`n"); $c.tool=(@($l[2],$l[1],$l[0])+$l[3..($l.Count-1)]) -join "`n"}; g='PASS'; d='0'; k='OK'; v=$ok}
 @{n='post-register-changed'; m={param($c) $c.post=$c.post.Replace('TQAVCTRL     0x03570 = 0x00000000','TQAVCTRL     0x03570 = 0x00000009')}; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='post-new-failure'; m={param($c) $c.post=$c.post.Replace('[PASS - TSAUXC register accessible]','[FAIL - TSAUXC register]')}; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='post-failure-vanished'; m={param($c) $c.post=$c.post -replace '\[FAIL - PTP clock not running\]','[PASS]'; $c.postRc=0}; g='PASS'; d='0'; k='OK'; v=$inc}
 @{n='post-clean-to-fail'; m={param($c) $c.pre=$c.pre -replace '\[FAIL - PTP clock not running\]','[PASS]'; $c.post=$c.pre; $c.preRc=0; $c.postRc=1}; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='post-truncated'; m={param($c) $c.post=$c.post.Substring(0,$c.post.IndexOf('=== Phase 3'))}; g='PASS'; d='0'; k='FAIL'; v=$inc}
 @{n='post-json-missing'; m={param($c) $c.postJ=$null}; g='PASS'; d='0'; k='FAIL'; v=$inc}
 @{n='post-json-stale'; m={param($c) $c.postJ=$c.postJ.Replace('__NOW__','2026-11-05T09:36:12.0000000Z')}; g='PASS'; d='0'; k='FAIL'; v=$inc; want=@('registersDiffering=0','GATE REASON post: results older')}
 @{n='post-safety-not-clear'; m={param($c) $c.postJ=[regex]::Replace($c.postJ,'"stateAtStart":\s*"CLEAR"','"stateAtStart":"UNSAFE"')}; g='PASS'; d='0'; k='FAIL'; v=$inc; want=@('registersDiffering=0')}
 @{n='post-adapter-fail'; m={param($c) $c.postJ=Set-V $c.postJ 'FAIL'}; g='PASS'; d='0'; k='FAIL'; v=$inc}
 @{n='post-throw'; f=@{'throw.txt'='post'}; g='PASS'; d='0'; k='FAIL'; v=$inc; want=@('postcheck repeated')}
 @{n='post-retry-succeeds'; f=@{'throwonce.txt'='post'}; g='PASS'; d='0'; k='OK'; v=$ok; want=@('postcheck repeated')}
 @{n='pre-throw'; f=@{'throw.txt'='pre'}; g='NOT_EVALUATED'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='state-pre-throw'; g='NOT_EVALUATED'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='state-post-throw'; g='PASS'; d='0'; k='OK'; v=$ok; want=@('adapter state capture repeated','stateDifferences=0')}
 @{n='state-post-always'; g='PASS'; d='0'; k='OK'; v=$inc; want=@('adapter state capture repeated')}
 @{n='state-diff'; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='lifecycle-event'; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='trace-fail'; g='PASS'; d='0'; k='OK'; v=$inc}
 @{n='stop-throw'; g='PASS'; d='0'; k='OK'; v=$ok; want=@('trace session still open','traceClosed=True')}
 @{n='stop-fail'; g='PASS'; d='0'; k='OK'; v=$inc; want=@('TRACE SESSION NOT CONFIRMED CLOSED')}
 @{n='stop-fail-once'; g='PASS'; d='0'; k='OK'; v=$ok; want=@('trace stop returned 1','traceClosed=True')}
 @{n='still-running'; g='PASS'; d='0'; k='OK'; v=$inc; want=@('TRACE SESSION NOT CONFIRMED CLOSED')}
 @{n='query-denied'; g='PASS'; d='0'; k='OK'; v=$inc; want=@('TRACE SESSION NOT CONFIRMED CLOSED')}
 @{n='decode-throw'; g='PASS'; d='0'; k='OK'; v=$inc}
 @{n='decode-rc'; g='PASS'; d='0'; k='OK'; v=$inc}
 @{n='tool-fail'; m={param($c) $c.toolRc=1; $c.tool=$c.tool.Replace('exit=0','exit=1')}; g='PASS'; d='1'; k='OK'; v=$ok}
 @{n='tool-missing'; g='PASS'; d='NOT_RUN'; k='FAIL'; v=$ok}
 @{n='timeout'; f=@{'toolsleep.txt'='20000'}; limit=1; g='PASS'; d='TIMEOUT'; k='FAIL'; v=$ok; gone=$true}
 @{n='kill-throw'; limit=1; g='PASS'; d='TIMEOUT'; k='FAIL'; v=$ok}
 @{n='exit-throw'; g='PASS'; d='0'; k='OK'; v=$ok}
 @{n='lock-fail'; stop='STOP lock'}
 @{n='lock-stale'; stop='STOP a stale'}
 @{n='lock-stale-exit-throw'; stop='STOP a stale'; want2='LOCK NOT RELEASED'}
 @{n='stamp-empty'; stop='STOP stamp is empty'; stamp=''}
 @{n='stamp-reused'; stop='STOP evidence for stamp'; seed=$true}
 @{n='stop-preflight'; stop='STOP preflight'; noLockUse=$true}
 @{n='stop-driver-stopped'; stop='STOP driver'; noLockUse=$true}
 @{n='stop-driver-differs'; stop='STOP driver'; noLockUse=$true}
 @{n='stop-manifest-dirty'; stop='STOP build manifest'; noLockUse=$true}
 @{n='stop-manifest-revision'; stop='STOP build manifest'; noLockUse=$true}
 @{n='stop-tool-differs'; stop='STOP build manifest'; noLockUse=$true}
 @{n='stop-ci-job'; stop='STOP a CI job'; noLockUse=$true}
 @{n='ctrlc-pre'; cc='runner-pre-wait'; f=@{'sleep.txt'='pre'}; g='NOT_EVALUATED'; d='NOT_RUN'; k='NOT_EVALUATED'; v=$inc}
 @{n='ctrlc-tool'; cc='tool-wait'; f=@{'toolsleep.txt'='15000'}; g='PASS'; dAny=@('-1073741510','INTERRUPTED'); k='FAIL'; vAny=$true; gone=$true; want=@('stateDifferences=0','registersDiffering=0')}
 @{n='ctrlc-cleanup'; cc='trace-stop-wait'; f=@{'slowstop.txt'='1'}; g='PASS'; d='0'; k='OK'; vAny=$true; want=@('stateDifferences=0','registersDiffering=0','newFailures=0','traceClosed=True')}
 @{n='ctrlc-post'; cc='runner-post-wait'; f=@{'sleep.txt'='post'}; g='PASS'; d='0'; k='OK'; v=$ok; want=@('postcheck repeated','stateDifferences=0','registersDiffering=0')}
 @{n='ctrlc-twice'; cc='tool-wait'; cc2='runner-post-wait'; f=@{'toolsleep.txt'='15000'; 'sleep.txt'='post'}; g='PASS'; dAny=@('-1073741510','INTERRUPTED'); k='FAIL'; vAny=$true; gone=$true; want=@('stateDifferences=0')}
 @{n='tool-killed-externally'; killTool=$true; f=@{'toolsleep.txt'='15000'}; g='PASS'; d='-1'; k='FAIL'; v=$ok}
 @{n='host-killed'; killHost=$true; f=@{'toolsleep.txt'='15000'}}
 # Case A (-CaseA).  ca=@{...}: file name -> content in the case directory; tool stays the Case A fixture unless m changes it.
 @{n='ca-resumed'; caseA=$true; g='PASS'; d='0'; k='OK'; v=$inc; x=0; deny=@('NO_CHANGE'); want=@('STATE SEQUENCE luid=0x0006008006000000 class=SEQUENCE_CONSISTENT values=a,b,c,d events=4 pausedNdisUs=30.0 bracketUs=20.0','STATE CLAIM luid=0x0006008006000000 sequence=SEQUENCE_CONSISTENT lifecycle=RESUMED_BY_TRAFFIC supported=True','stateEvents=4','toolCleanup=INCONCLUSIVE_RESTORATION','MANAGEMENT class=MANAGEMENT_NOT_ON_TARGET','CASE A COMPLETED'); trace=@('trace-update:update trace AvbI364S1 -p {B7E1F3C2-6A54-4D0E-9C37-3640E8A1D364} 0xffffffffffffffff 0x5 -ets','ifindex:8','dbgview-start','dbgview-stop'); checksums=$true}
 @{n='ca-phc-discontinuous'; caseA=$true; m={param($c) $c.toolRc=2; $c.tool=$c.tool.Replace('result: exit=0 lifecycle=RESUMED_BY_TRAFFIC cleanup=INCONCLUSIVE_RESTORATION','result: exit=2 lifecycle=RESUMED_BY_TRAFFIC cleanup=CLEANUP_FAILED').Replace('class=ELIGIBLE exit=0','class=ELIGIBLE exit=2').Replace('eligible=1 exit=0','eligible=1 exit=2')}; g='PASS'; d='2'; k='OK'; v=$cf; x=3; want=@('toolCleanup=CLEANUP_FAILED','STATE CLAIM luid=0x0006008006000000 sequence=SEQUENCE_CONSISTENT lifecycle=RESUMED_BY_TRAFFIC supported=True')}
 @{n='ca-tool-fail'; caseA=$true; m={param($c) $c.toolRc=1; $c.tool=$c.tool.Replace('result: exit=0 lifecycle=RESUMED_BY_TRAFFIC','result: exit=1 lifecycle=PAUSE_ONLY').Replace('class=ELIGIBLE exit=0','class=ELIGIBLE exit=1').Replace('eligible=1 exit=0','eligible=1 exit=1')}; g='PASS'; d='1'; k='OK'; v=$inc; x=3; want=@('lifecycle=PAUSE_ONLY supported=False'); deny=@('supported=True')}
 # A management session on a target is reported and never stops the run (Q 2026-11-06): losing it is an accepted consequence.
 @{n='ca-management-udp-on-target'; caseA=$true; f=@{'mgmt-udp.txt'='10.0.0.12'}; g='PASS'; d='0'; k='OK'; v=$inc; x=0; want=@('MANAGEMENT class=MANAGEMENT_ON_TARGET','WARNING management session','CASE A COMPLETED'); deny=@('STOP before the trigger')}
 @{n='ca-management-tcp-on-target'; caseA=$true; f=@{'mgmt-tcp.txt'='10.0.0.12'}; g='PASS'; d='0'; k='OK'; v=$inc; x=0; want=@('MANAGEMENT class=MANAGEMENT_ON_TARGET','WARNING management session')}
 @{n='ca-management-no-socket'; caseA=$true; f=@{'mgmt-tcp.txt'='none'}; g='PASS'; d='0'; k='OK'; v=$inc; x=0; want=@('MANAGEMENT class=MANAGEMENT_UNATTRIBUTED','WARNING management session')}
 @{n='ca-no-process'; caseA=$true; g='PASS'; d='0'; k='OK'; v=$inc; x=0; want=@('MANAGEMENT class=MANAGEMENT_UNATTRIBUTED','WARNING management session')}
 @{n='ca-no-target-ip'; caseA=$true; g='PASS'; d='0'; k='OK'; v=$inc; x=0; want=@('MANAGEMENT class=MANAGEMENT_UNATTRIBUTED','WARNING management session')}
 @{n='ca-management-check-throws'; caseA=$true; g='PASS'; d='0'; k='OK'; v=$inc; x=0; want=@('check failed','WARNING management session')}
 @{n='ca-management-clean-no-warning'; caseA=$true; g='PASS'; d='0'; k='OK'; v=$inc; x=0; want=@('MANAGEMENT class=MANAGEMENT_NOT_ON_TARGET'); deny=@('WARNING management session')}
 # Negative: the warning changes no verdict.  A real failure with the session on the target stays that failure.
 @{n='ca-management-tool-failure-stays'; caseA=$true; f=@{'mgmt-udp.txt'='10.0.0.12'}; m={param($c) $c.toolRc=1; $c.tool=$c.tool.Replace('result: exit=0 lifecycle=RESUMED_BY_TRAFFIC','result: exit=1 lifecycle=PAUSE_ONLY').Replace('class=ELIGIBLE exit=0','class=ELIGIBLE exit=1').Replace('eligible=1 exit=0','eligible=1 exit=1')}; g='PASS'; d='1'; k='OK'; v=$inc; x=3; want=@('lifecycle=PAUSE_ONLY supported=False'); deny=@('supported=True')}
 @{n='ca-management-cleanup-failed-stays'; caseA=$true; f=@{'mgmt-udp.txt'='10.0.0.12'}; m={param($c) $c.toolRc=2; $c.tool=$c.tool.Replace('result: exit=0 lifecycle=RESUMED_BY_TRAFFIC cleanup=INCONCLUSIVE_RESTORATION','result: exit=2 lifecycle=RESUMED_BY_TRAFFIC cleanup=CLEANUP_FAILED').Replace('class=ELIGIBLE exit=0','class=ELIGIBLE exit=2').Replace('eligible=1 exit=0','eligible=1 exit=2')}; g='PASS'; d='2'; k='OK'; v=$cf; x=3; want=@('toolCleanup=CLEANUP_FAILED')}
 @{n='ca-management-stop-gate-stays'; caseA=$true; f=@{'mgmt-udp.txt'='10.0.0.12'}; fi='ca-marker-fail'; g='PASS'; d='NOT_RUN'; noTool=$true; x=2; want=@('marker provider not enabled','CASE A STOPPED BEFORE THE TRIGGER')}
 @{n='ca-marker-fail'; caseA=$true; g='PASS'; d='NOT_RUN'; noTool=$true; x=2; want=@('marker provider not enabled')}
 @{n='ca-trace-fail'; caseA=$true; fi='trace-fail'; g='PASS'; d='NOT_RUN'; noTool=$true; x=2; want=@('NDIS trace not running')}
 @{n='ca-dbgview-fail'; caseA=$true; g='PASS'; d='NOT_RUN'; noTool=$true; x=2; want=@('DbgView capture not confirmed')}
 @{n='ca-gate-fail'; caseA=$true; m={param($c) $c.preJ=Set-V $c.preJ 'FAIL'}; g='FAIL'; d='NOT_RUN'; noTool=$true; v=$inc; x=1}
 @{n='ca-no-events'; caseA=$true; etl=@{Values=@()}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=NOT_EMITTED','supported=False')}
 @{n='ca-trace-silent'; caseA=$true; etl=@{Values=@(); NoOther=$true}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=TRACE_SILENT')}
 @{n='ca-lost-events'; caseA=$true; etl=@{Lost=3}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=TRACE_SILENT'); deny=@('supported=True')}
 @{n='ca-no-markers'; caseA=$true; etl=@{NoMarkers=$true}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=NO_WINDOW'); deny=@('supported=True')}
 @{n='ca-failed-restart'; caseA=$true; etl=@{Values=@('a','b','c','b')}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=SEQUENCE_FAILED_RESTART values=a,b,c,b'); deny=@('supported=True')}
 @{n='ca-context-replaced'; caseA=$true; m={param($c) $c.tool=$c.tool.Replace('lifecycle=RESUMED_BY_TRAFFIC','lifecycle=CONTEXT_REPLACED')}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=SEQUENCE_FAILED_RESTART'); deny=@('supported=True')}
 @{n='ca-unexpected-count'; caseA=$true; etl=@{Values=@('a','b','c'); At=@(200,400,700)}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=SEQUENCE_UNEXPECTED values=a,b,c'); deny=@('supported=True')}
 @{n='ca-adjacent-repeat'; caseA=$true; etl=@{Values=@('a','a','c','d')}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=SEQUENCE_UNEXPECTED'); deny=@('supported=True')}
 @{n='ca-pause-edge-outside'; caseA=$true; m={param($c) $c.tool=$c.tool.Replace('edge pause entry between qpc=1000300 and qpc=1000310','edge pause entry between qpc=1090000 and qpc=1090010')}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=SEQUENCE_UNEXPECTED'); deny=@('supported=True')}
 @{n='ca-restart-edge-early'; caseA=$true; m={param($c) $c.tool=$c.tool.Replace('edge restart entry between qpc=1000800 and qpc=1000810','edge restart entry between qpc=1000100 and qpc=1000110')}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=SEQUENCE_UNEXPECTED'); deny=@('supported=True')}
 @{n='ca-other-instance'; caseA=$true; etl=@{Name='{5989BA55-D568-47A4-AFBF-B47F27E1B435}-{FFFFFFFF-14F9-4E79-9445-5B1E52CCD192}-0000'}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=NOT_EMITTED')}
 @{n='ca-outside-window'; caseA=$true; etl=@{At=@(60000,60100,60200,60300)}; g='PASS'; d='0'; k='OK'; v=$inc; want=@('class=NOT_EMITTED','outsideWindow=4')}
 @{n='ca-stop-event'; caseA=$true; etl=@{Id=10058}; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='ca-no-result-line'; caseA=$true; m={param($c) $c.tool=($c.tool -split "`n" | Where-Object { $_ -notmatch ' result: exit=' }) -join "`n"}; g='PASS'; d='0'; k='FAIL'; v=$inc; x=1}
 @{n='ca-post-register-changed'; caseA=$true; m={param($c) $c.post=$c.post.Replace('TQAVCTRL     0x03570 = 0x00000000','TQAVCTRL     0x03570 = 0x00000009')}; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='ca-post-clean-to-fail'; caseA=$true; m={param($c) $c.pre=$c.pre -replace '\[FAIL - PTP clock not running\]','[PASS]'; $c.post=$c.pre; $c.preRc=0; $c.postRc=1}; g='PASS'; d='0'; k='OK'; v=$cf}
 @{n='ca-ctrlc-tool'; caseA=$true; cc='tool-wait'; f=@{'toolsleep.txt'='15000'}; g='PASS'; dAny=@('-1073741510','INTERRUPTED'); k='FAIL'; vAny=$true; gone=$true; want=@('stateDifferences=0'); trace=@('dbgview-stop')}
)
$InterruptNames = 'ctrlc-pre', 'ctrlc-tool', 'ctrlc-cleanup', 'ctrlc-post', 'ctrlc-twice', 'tool-killed-externally', 'host-killed', 'ca-ctrlc-tool'
$InterruptRepeats = 5
$Mutants = @(
 ,@('phase1', "if (@(`$lines[`$at['Phase 1']..`$at['Phase 2']] | Where-Object { `$_ -match '\[FAIL' }).Count -gt 0) { `$r.Why += 'Phase 1 SSOT failure' }", '', 'phase1-fail')
 ,@('allowlist', "`$t.Verdict -notin 'PASS', 'SKIP_UNSUPPORTED'", "`$t.Verdict -notin 'PASS', 'SKIP_UNSUPPORTED', 'SKIP', 'INCONCLUSIVE', 'BLOCKED'", 'other-adapter-inconclusive')
 ,@('fresh', "if ([DateTimeOffset]::Parse(`"`$(`$d.startedUtc)`").UtcDateTime -lt `$t0) { `$r.Why += 'results older than this run' }", '', 'json-stale')
 ,@('revision', "`$d.source.revision -ne `$head -or ", '', 'json-other-revision')
 ,@('dirty', " -or `$d.source.dirty -ne `$false", '', 'json-dirty')
 ,@('entry', "`$rec[0].entry -ne 'ssot_register_validation_test' -or ", '', 'json-wrong-test')
 ,@('executed', " -or `$rec[0].executed -ne `$true", '', 'json-not-executed')
 ,@('provenance', "if (`$rec[0].provenance.verified -ne `$true) { `$r.Why += 'test artifact provenance not verified' }", '', 'json-no-provenance')
 ,@('indexes', "if ((@(`$tc | ForEach-Object { [int]`$_.AdapterIndex } | Sort-Object) -join ',') -ne ((@(0..([Math]::Max(`$r.N, 1) - 1))) -join ',')) { `$r.Why += 'adapter indexes are not one per adapter' }", '', 'adapter-unrepresented')
 ,@('tasarmed', "if (@(`$blk | Where-Object { `$_ -match '^\s*tasArmed=0\s*`$' }).Count -ne 1) { `$r.Why += `"adapter:`$i tasArmed is not 0`" }", '', 'tasarmed-1')
 ,@('noregs', "if (@(`$regs | Where-Object { `$_ -notmatch 'tasArmed=' }).Count -lt 1) { `$r.Why += `"adapter:`$i has no register values`" }", '', 'pass-no-registers')
 ,@('safety', "if (`$d.machineSafety.stateAtStart -ne 'CLEAR') { `$r.Why += 'machine safety not CLEAR' }", '', 'safety-not-clear')
 ,@('driver', "if (`$d.installedDriver.matchesConfiguration -ne `$true) { `$r.Why += 'installed driver does not match the configuration' }", '', 'driver-mismatch')
 ,@('halt', "if (`"`$(`$d.stateHalt)`" -ne '') { `$r.Why += 'runner state halt' }", '', 'state-halt')
 ,@('nopass', "if (`$r.Pass.Count -lt 1) { `$r.Why += 'no adapter passed TC-TSNREG-001' }", '', 'all-skip')
 ,@('sections', "if (`$hit.Count -ne 1) { `$r.Why += `"section '`$m' seen `$(`$hit.Count) times`" } else { `$at[`$m] = `$hit[0] }", "if (`$hit.Count -ge 1) { `$at[`$m] = `$hit[0] }", 'duplicate-execution')
 ,@('gate', "if (`$pre.Ok) {`n", "if (`$true) {`n", 'tc-fail')
 ,@('corr-pass', "if (`$c -eq 'ELIGIBLE' -and (`$u -notin `$pre.Pass -or -not `$post -or -not `$post.Ok -or `$u -notin `$post.Pass)) { `$corrWhy += `"`$u eligible without PASS before and after`" }", '', 'eligible-not-passed')
 ,@('corr-post', "`$u -notin `$pre.Pass -or -not `$post -or -not `$post.Ok -or `$u -notin `$post.Pass", "`$u -notin `$pre.Pass", 'post-adapter-fail')
 ,@('corr-post-ok', " -or -not `$post.Ok", '', 'post-json-stale')
 ,@('corr-count', "if (`$sa -ne `$pre.N) { `$corrWhy += `"tool adapters=`$sa but runner adapters=`$(`$pre.N)`" }", '', 'adapter-count-differs')
 ,@('corr-plan', "if (`$plan.Count -ne `$pre.N -or @(`$luids | Sort-Object -Unique).Count -ne `$plan.Count) { `$corrWhy += `"plan lines=`$(`$plan.Count) unique=`$(@(`$luids | Sort-Object -Unique).Count) adapters=`$(`$pre.N)`" }", '', 'duplicate-plan-luid')
 ,@('corr-exit', "if (`"`$sx`" -ne `"`$dryRc`") { `$corrWhy += `"summary exit=`$sx but process exit=`$dryRc`" }", '', 'summary-exit-differs')
 ,@('corr-done', "if (`$dryRc -eq 0 -and @(`$d | Where-Object { `$_ -match ' dry run complete: ' }).Count -ne `$se) { `$corrWhy += 'dry run completion not shown for every eligible adapter' }", '', 'no-dry-complete')
 ,@('corr-zero', "if (`$se -lt 1) { `$corrWhy += 'no eligible adapter' }", '', 'eligible-zero')
 ,@('corr-result', "if (@(`$d | Where-Object { `$_ -match `" adapter result luid=(?i:`$u) .* class=`$c `" }).Count -ne 1) { `$corrWhy += `"no result for `$u`" }", '', 'no-result-line')
 ,@('clean-trace', " -and `$goneFails -eq 0 -and `$traceOk)", " -and `$goneFails -eq 0)", 'stop-fail')
 ,@('clean-gone', " -and `$goneFails -eq 0", '', 'post-failure-vanished')
 ,@('clean-post', "(`$post -and `$post.Ok -and `$null -ne `$postRc -and ", "(", 'post-json-stale')
 ,@('chg-reg', " -or (`$regDiff -is [int] -and `$regDiff -gt 0)", '', 'post-register-changed')
 ,@('chg-new', " -or (`$newFails -is [int] -and `$newFails -gt 0)", '', 'post-new-failure')
 ,@('chg-life', " -or (`$life -is [int] -and `$life -gt 0)", '', 'lifecycle-event')
 ,@('chg-state', "((`$diff -is [int] -and `$diff -gt 0) -or ", "(", 'state-diff')
 ,@('chg-pre0', " -or (`$preRc -eq 0 -and `$null -ne `$postRc -and `$postRc -ne 0)", '', 'post-clean-to-fail')
 ,@('no-post-retry', "{ if (`$null -eq `$postRc) {`n", "{ if (`$false) {`n", 'post-retry-succeeds')
 ,@('no-chain', "finally { . `$run (`$i + 1) } } }", "; . `$run (`$i + 1) } }", 'ctrlc-cleanup')
 ,@('no-state-retry', "if (-not [IO.File]::Exists(`$statePost) -or ([IO.FileInfo]`$statePost).Length -eq 0) {", "if (`$false) {", 'state-post-throw')
 ,@('state-retry-empty', " -or ([IO.FileInfo]`$statePost).Length -eq 0", '', 'state-post-throw')
 ,@('no-trace-retry', "{ if (`$traced -and -not `$traceOk) {`n", "{ if (`$false) {`n", 'stop-fail-once')
 ,@('no-kill', "if (`$p -and -not `$p.HasExited) { `$p.Kill(); if", "if (`$false) { if", 'timeout')
 ,@('no-release', "try { `$rel = Exit-AvbHilLock -Owner `$lock.Owner } catch", "try { `$rel = @{ Ok = `$true; Why = '' } } catch", 'legacy-fail-clean')
 ,@('relative-json', "`$preJson = `"`$L\diag-i364-tsnreg-pre-`$Stamp.json`"", "`$preJson = `".\logs\diag-i364-tsnreg-pre-`$Stamp.json`"", 'legacy-fail-clean')
 ,@('stamp-guard', "elseif (@(Get-ChildItem -LiteralPath `$L -Filter `"diag-i364-*-`$Stamp.*`" | Where-Object { `$_.Name -notlike 'diag-i364-transcript-*' }).Count -gt 0)", "elseif (`$false)", 'stamp-reused')
 ,@('args', "'--lifecycle-a --dry-run'", "'--lifecycle-a'", 'legacy-fail-clean')
 ,@('hide-runner', "[void]`$report.Add(`"RUNNER RESULT  pre=`$preRc post=`$postRc (the runner verdict stands, a nonzero exit stays FAILED)`")", '', 'legacy-fail-clean')
 ,@('hide-fail', "[void]`$report.Add(`"FAILED LINE `$(`$x[0]): `$f`")", '', 'legacy-fail-clean')
 ,@('hide-adapter', "[void]`$report.Add(`"REGISTER CHECK `$(`$x[0]) adapter:`$(`$t.AdapterIndex) verdict=`$(`$t.Verdict) `$(`$t.Detail)`")", '', 'legacy-fail-clean')
)
# Case A: every guard in front of the trigger, every rule of the state reduction and every verdict path has a mutant.
$Mutants += ,@('ca-default-trigger', "`$toolArgs = if (`$CaseA) { '--lifecycle-a' } else { '--lifecycle-a --dry-run' }", "`$toolArgs = '--lifecycle-a'", 'legacy-fail-clean')
$Mutants += ,@('ca-switch-dry', "`$toolArgs = if (`$CaseA) { '--lifecycle-a' } else { '--lifecycle-a --dry-run' }", "`$toolArgs = '--lifecycle-a --dry-run'", 'ca-resumed')
$Mutants += ,@('ca-mgmt-refuses', "if (`$mgmtLine -notmatch 'class=MANAGEMENT_NOT_ON_TARGET ') {", "if (`$mgmtLine -notmatch 'class=MANAGEMENT_NOT_ON_TARGET ') { `$hold += 'refused';", 'ca-management-udp-on-target')
$Mutants += ,@('ca-mgmt-silent', "[void]`$problems.Add('WARNING management session is on a target adapter or not attributed: it may be interrupted, and this run does not depend on it')", '', 'ca-management-udp-on-target')
$Mutants += ,@('ca-mgmt-always-warns', "if (`$mgmtLine -notmatch 'class=MANAGEMENT_NOT_ON_TARGET ') {", 'if ($true) {', 'ca-management-clean-no-warning')
$Mutants += ,@('ca-mgmt-check-fatal', "try { `$mgmtLine = & `$mgmt `$pre.Ifs } catch { `$mgmtLine = `"MANAGEMENT class=MANAGEMENT_UNATTRIBUTED check failed: `$_`" }", "`$mgmtLine = & `$mgmt `$pre.Ifs", 'ca-management-check-throws')
$Mutants += ,@('ca-mgmt-unattributed', "if (`$ips.Count -eq 0 -or `$locals.Count -eq 0) { 'MANAGEMENT_UNATTRIBUTED' } elseif", "if (`$false) { 'MANAGEMENT_UNATTRIBUTED' } elseif", 'ca-management-no-socket')
$Mutants += ,@('ca-mgmt-udp', "`$locals += @(Get-NetUDPEndpoint -OwningProcess `$pr.Id -ErrorAction SilentlyContinue | ForEach-Object { `$_.LocalAddress })", '', 'ca-management-udp-on-target')
$Mutants += ,@('ca-mgmt-tcp', "`$locals += @(Get-NetTCPConnection -OwningProcess `$pr.Id -State Established -ErrorAction SilentlyContinue | ForEach-Object { `$_.LocalAddress })", '', 'ca-management-tcp-on-target')
$Mutants += ,@('ca-marker', "if (`$LASTEXITCODE -ne 0) { `$hold += 'marker provider not enabled' }", '', 'ca-marker-fail')
$Mutants += ,@('ca-dbgview', "{ `$hold += 'DbgView capture not confirmed' }", '{ }', 'ca-dbgview-fail')
$Mutants += ,@('ca-trace', "if (-not `$traced) { `$hold += 'NDIS trace not running' } else {", "if (`$false) { } else {", 'ca-trace-fail')
$Mutants += ,@('ca-life-all', "if (`$CaseA) { '10007|10058|10050' } else {", "if (`$false) { '10007|10058|10050' } else {", 'ca-resumed')
$Mutants += ,@('ca-life-none', "if (`$CaseA) { '10007|10058|10050' } else {", "if (`$CaseA) { '99999' } else {", 'ca-stop-event')
$Mutants += ,@('ca-tool-cleanup', "if (@(`$toolCleanup) -contains 'CLEANUP_FAILED') { `$changed = `$true }", '', 'ca-phc-discontinuous')
$Mutants += ,@('ca-no-change', "if (`$CaseA -and `$p -and -not `$changed) { `$cleanup = 'INCONCLUSIVE_RESTORATION' }", '', 'ca-resumed')
$Mutants += ,@('ca-result-corr', "{ `$corrWhy += 'tool result not shown for every eligible adapter' }", '{ }', 'ca-no-result-line')
$Mutants += ,@('ca-instance', " -and (& `$field `$_ 'FilterInstanceName') -ieq `$name", '', 'ca-other-instance')
$Mutants += ,@('ca-window-end', " -and `$w -le `$t1", '', 'ca-outside-window')
$Mutants += ,@('ca-lost', " -or `$lost -gt 0", '', 'ca-lost-events')
$Mutants += ,@('ca-no-window', "elseif (`$start.Count -ne 1 -or `$hz -le 0) { `$class = 'NO_WINDOW' }", '', 'ca-no-markers')
$Mutants += ,@('ca-count', "`$vals.Count -eq 4 -and `$vals[0] -ne", "`$vals.Count -ge 3 -and `$vals[0] -ne", 'ca-unexpected-count')
$Mutants += ,@('ca-adjacent', "`$vals[0] -ne `$vals[1] -and ", '', 'ca-adjacent-repeat')
$Mutants += ,@('ca-pause-edge', "`$edge['pause'][0] -le (`$q[2] + `$slack) -and ", '', 'ca-pause-edge-outside')
$Mutants += ,@('ca-restart-edge', " -and `$edge['restart'][1] -ge (`$q[1] - `$slack)", '', 'ca-restart-edge-early')
$Mutants += ,@('ca-failed-class', "(`$vals.Count -ge 3 -and `$vals[-1] -eq `$vals[1])", "`$false", 'ca-failed-restart')
$Mutants += ,@('ca-claim-life', " -and `$life -eq 'RESUMED_BY_TRAFFIC' -and `$others", " -and `$others", 'ca-tool-fail')
$Mutants += ,@('ca-checksum-path', "-LiteralPath `"`$root\logs`" -Filter", "-LiteralPath `$L -Filter", 'ca-resumed')
$Mutants += ,@('ca-exit-hold', "if (`$hold.Count -gt 0) { 2 } elseif (-not `$completed) { 1 }", "if (-not `$completed) { 1 }", 'ca-marker-fail')
$Mutants += ,@('ca-exit-tool', "elseif (`"`$dryRc`" -eq '0') { 0 } else { 3 }", "else { 0 }", 'ca-phc-discontinuous')
if ($ListOnly) {
    "cases=$(@($cases).Count) interrupts=$(@($InterruptNames).Count * $InterruptRepeats) mutants=$(@($Mutants).Count)"
    $cases | ForEach-Object { "case $($_.n)" }
    $Mutants | ForEach-Object { "mutant $($_[0]) $($_[3])" }
    exit 0
}

$Only = @($Only | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
$subjectThere = Test-Path -LiteralPath $Subject
tc_check 'TEST-TAS-STAGE1-000-subject-script-exists' $subjectThere "missing: $Subject"
if (-not $subjectThere) { Write-Host "`nTEST-TAS-STAGE1: $passed passed, $failed failed of $total" -ForegroundColor Red; exit 1 }
$SubjectText = [IO.File]::ReadAllText($Subject).Replace("`r`n", "`n")    # mutation text must not depend on line endings
$RealLock = Join-Path $env:ProgramData 'IntelAvbFilter-hil.lock'
$RealLockBefore = if (Test-Path -LiteralPath $RealLock) { (Get-Item -LiteralPath $RealLock).LastWriteTimeUtc.Ticks } else { 0 }


# The verdict of one mutant.  KILLED needs a valid mutant, a passing unmutated case in this run, a run that was judged
# completely, and an assertion that failed.  What stopped a run from being judged is never a detection.
function Get-Stage1MutantVerdict {
    param($e)
    if (-not $e.Parsed) { return @{ Verdict = 'INCONCLUSIVE'; Reason = 'invalid mutant: does not parse' } }
    if (-not $e.BasePassed) { return @{ Verdict = 'INCONCLUSIVE'; Reason = 'the unmutated case did not pass in this run' } }
    if ($e.Hung) { return @{ Verdict = 'INCONCLUSIVE'; Reason = 'did not finish, no assertion result' } }
    if (@($e.Infra).Count -gt 0) { return @{ Verdict = 'INFRA_FAILURE'; Reason = (@($e.Infra) -join '; ') } }
    if (-not $e.Evidence) { return @{ Verdict = 'INCONCLUSIVE'; Reason = 'no exit code or result file, the script did not end normally' } }
    if (@($e.Detected).Count -gt 0) { return @{ Verdict = 'KILLED'; Reason = (@($e.Detected) -join '; ') } }
    return @{ Verdict = 'SURVIVED'; Reason = 'the mutated script passed every assertion of this case' }
}

# Negative controls: none of these may ever read as a kill.
$Good = @{ Parsed = $true; BasePassed = $true; Hung = $false; Infra = @(); Evidence = $true; Detected = @('want gate=FAIL') }
$Controls = @(
    @('launch-failure', @{ Infra = @('run failed: not found') }, 'INFRA_FAILURE'),
    @('harness-termination', @{ Infra = @('script did not end') }, 'INFRA_FAILURE'),
    @('injection-failure', @{ Infra = @('inject rc=3') }, 'INFRA_FAILURE'),
    @('invalid-exit', @{ Evidence = $false }, 'INCONCLUSIVE'),
    @('timeout', @{ Hung = $true }, 'INCONCLUSIVE'),
    @('missing-evidence', @{ Evidence = $false; Detected = @() }, 'INCONCLUSIVE'),
    @('parser-failure', @{ Parsed = $false }, 'INCONCLUSIVE'),
    @('unexpected-crash', @{ Evidence = $false; Detected = @('no runner line') }, 'INCONCLUSIVE'),
    @('invalid-baseline', @{ BasePassed = $false }, 'INCONCLUSIVE'),
    @('infrastructure-beats-assertions', @{ Infra = @('marker x not reached'); Detected = @('want gate=FAIL') }, 'INFRA_FAILURE'),
    @('positive-kill', @{}, 'KILLED'),
    @('positive-survivor', @{ Detected = @() }, 'SURVIVED')
)
foreach ($ctl in $Controls) {
    $e = @{}; foreach ($k in $Good.Keys) { $e[$k] = $Good[$k] }; foreach ($k in $ctl[1].Keys) { $e[$k] = $ctl[1][$k] }
    $got = (Get-Stage1MutantVerdict $e).Verdict
    tc_check "TEST-TAS-STAGE1-CTRL-$($ctl[0])" ($got -eq $ctl[2]) "got $got, want $($ctl[2])"
}
$Verdicts = @{ KILLED = 0; SURVIVED = 0; INFRA_FAILURE = 0; INCONCLUSIVE = 0 }; $Base = @{}
$Revision = (& git -C $RepoRoot rev-parse HEAD 2>$null)
$Dirty = @(& git -C $RepoRoot status --porcelain --untracked-files=no 2>$null).Count
Write-Host ("  [PROVENANCE] revision {0} tracked changes {1} subject {2} hillock {3}" -f $Revision, $Dirty, (Get-FileHash -LiteralPath $Subject).Hash, (Get-FileHash -LiteralPath $HilLock).Hash)

$Tmp = Join-Path $env:TEMP ('tas_stage1_{0}' -f [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Tmp -Force | Out-Null
try {
    $Bin = Join-Path $Tmp 'bin'; New-Item -ItemType Directory -Path $Bin -Force | Out-Null
    $StandInExe = Join-Path $Bin 'standin.exe'
    Add-Type -OutputType ConsoleApplication -OutputAssembly $StandInExe -TypeDefinition $StandIn
    $ToolSha = (Get-FileHash -LiteralPath $StandInExe).Hash
    $Elsewhere = Join-Path $Tmp 'elsewhere'; New-Item -ItemType Directory -Path $Elsewhere -Force | Out-Null

    $NewCase = {
        param($spec, $label, $text)
        $dir = Join-Path $Tmp $label
        foreach ($d in 'tests\diagnostic', 'tools\lib', 'tools\test', 'logs', 'bin', 'build\tools\avb_test\x64\Debug', 'build\x64\Debug\IntelAvbFilter') { [void][IO.Directory]::CreateDirectory("$dir\$d") }
        $isA = $spec.ContainsKey('caseA')
        $baseTool = if ($isA) { $FxToolA } else { $FxTool }
        $st = @{ pre = $FxLog; post = $FxLog; preJ = $FxJson; postJ = $FxJson; tool = $baseTool; preRc = 1; postRc = 1; toolRc = 0 }
        $vac = $false
        if ($spec.ContainsKey('m')) {
            & $spec.m $st
            $vac = ($st.pre -ceq $FxLog -and $st.post -ceq $FxLog -and $st.preJ -ceq $FxJson -and $st.postJ -ceq $FxJson -and $st.tool -ceq $baseTool -and $st.preRc -eq 1 -and $st.postRc -eq 1 -and $st.toolRc -eq 0)
        }
        if ($isA) {
            $etlArgs = if ($spec.ContainsKey('etl')) { $spec.etl } else { @{} }
            [IO.File]::WriteAllText("$dir\etl.xml", (New-Etl @etlArgs))
            [void][IO.Directory]::CreateDirectory("$dir\.github\skills\DbgView")
            [IO.File]::WriteAllText("$dir\.github\skills\DbgView\Start-DbgViewCapture.ps1", "param(`$LogFile)`nCT 'dbgview-start'`nif (`$global:FI -ne 'ca-dbgview-fail') { [IO.File]::WriteAllText(`$LogFile, 'kernel log') }`n[pscustomobject]@{ Id = 1; HasExited = (`$global:FI -eq 'ca-dbgview-fail') }")
            [IO.File]::WriteAllText("$dir\.github\skills\DbgView\Stop-DbgViewCapture.ps1", "param(`$ProcessId)`nCT 'dbgview-stop'")
        }
        if ($null -ne $st.pre) { [IO.File]::WriteAllText("$dir\pre.txt", $st.pre) }
        [IO.File]::WriteAllText("$dir\post.txt", $st.post)
        if ($null -ne $st.preJ) { [IO.File]::WriteAllText("$dir\pre.json", $st.preJ) }
        if ($null -ne $st.postJ) { [IO.File]::WriteAllText("$dir\post.json", $st.postJ) }
        [IO.File]::WriteAllText("$dir\pre.rc", "$($st.preRc)"); [IO.File]::WriteAllText("$dir\post.rc", "$($st.postRc)")
        [IO.File]::WriteAllText("$dir\tool.txt", $st.tool); [IO.File]::WriteAllText("$dir\tool.rc", "$($st.toolRc)")
        $fiName = if ($spec.ContainsKey('fi')) { $spec.fi } else { $spec.n }
        [IO.File]::WriteAllText("$dir\fi.txt", $fiName); [IO.File]::WriteAllText("$dir\trace.txt", '')
        if ($spec.ContainsKey('f')) { foreach ($k in $spec.f.Keys) { [IO.File]::WriteAllText("$dir\$k", $spec.f[$k]) } }
        [IO.File]::WriteAllText("$dir\tests\diagnostic\Invoke-TasStage1DryRun.ps1", $text)
        [IO.File]::WriteAllText("$dir\tools\test\Run-Tests-CI.ps1", $StubRunner)
        [IO.File]::WriteAllText("$dir\tools\test\Test-DriverPreFlight.ps1", "exit $(if ($spec.n -eq 'stop-preflight') { 1 } else { 0 })")
        [IO.File]::WriteAllText("$dir\tools\lib\HilLock.ps1", $StubLock)
        [IO.File]::Copy($HilLock, "$dir\tools\lib\HilLock.real.ps1")
        $null = New-Item -ItemType HardLink -Path "$dir\bin\logman.exe" -Value $StandInExe    # the same file, so no new scan per case
        $null = New-Item -ItemType HardLink -Path "$dir\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe" -Value $StandInExe
        [IO.File]::WriteAllText("$dir\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys", 'driver')
        [IO.File]::WriteAllText("$dir\installed.sys", $(if ($spec.n -eq 'stop-driver-differs') { 'other' } else { 'driver' }))
        $mRev = if ($spec.n -eq 'stop-manifest-revision') { '2222222222222222222222222222222222222222' } else { $Rev }
        $mDirty = if ($spec.n -eq 'stop-manifest-dirty') { 'true' } else { 'false' }
        $mSha = if ($spec.n -eq 'stop-tool-differs') { 'AB' } else { $ToolSha }
        [IO.File]::WriteAllText("$dir\build\tools\avb_test\x64\Debug\test-build-manifest.json", "{`"provenanceMode`":`"clean-commit`",`"sourceRevision`":`"$mRev`",`"sourceDirty`":$mDirty,`"entries`":[{`"name`":`"tas_gate_investigation`",`"sha256`":`"$mSha`"}]}")
        if ($spec.ContainsKey('seed')) { [IO.File]::WriteAllText("$dir\logs\diag-i364-dryrun-t.log", 'old evidence') }
        $stampArg = if ($spec.ContainsKey('stamp')) { "''" } else { "'t'" }
        $limit = if ($spec.ContainsKey('limit')) { $spec.limit } else { 300 }
        $extra = if ($spec.n -eq 'kill-throw') { $KillFake } elseif ($spec.n -eq 'stop-throw') { 'function global:Invoke-TasLogman { if ($args[0] -eq "stop") { throw "stop failed" }; & "' + "$dir\bin\.\logman.exe" + '" @args }' + "`nSet-Alias -Scope Global -Name '$dir\bin\logman.exe' -Value Invoke-TasLogman" } else { '' }
        $caseSwitch = if ($isA) { ' -CaseA' } else { '' }
        $tail = "& '$dir\tests\diagnostic\Invoke-TasStage1DryRun.ps1' -Stamp $stampArg -ToolTimeoutSeconds $limit -Logman '$dir\bin\logman.exe'$caseSwitch`n[IO.File]::WriteAllText('$dir\exit.txt', `"`$LASTEXITCODE`")`n"
        $head = "`$global:CaseDir = '$dir'; `$global:FI = '$fiName'; `$global:gn = 0`n" + $Prelude.Replace('__REV__', $Rev) + "`n$extra`n"
        [IO.File]::WriteAllText("$dir\run.ps1", $head + $tail)
        return @{ Dir = $dir; Spec = $spec; Label = $label; Vac = $vac; St = $st; Out = ''; Pre = @(); Host = $null }
    }

    # A pid can be reused: a process counts as the stand-in only if it runs from this case's sandbox.
    $Mine = { param($id, $dir)
        $o = Microsoft.PowerShell.Management\Get-Process -Id $id -ErrorAction SilentlyContinue
        if ($o -and "$($o.Path)" -like "$dir\*") { $o } }

    $Judge = {
        param($w)
        $c = $w.Spec; $dir = $w.Dir; $checks = New-Object System.Collections.ArrayList
        if ($w.Vac) { [void]$checks.Add('mutation changed nothing') }
        $t = @(Get-Content -LiteralPath "$dir\trace.txt")
        $resF = "$dir\logs\diag-i364-result-t.txt"
        $res = if (Test-Path -LiteralPath $resF) { [IO.File]::ReadAllText($resF) } else { '' }
        $out = "$($w.Out)"
        $lockFile = "$dir\lock"
        $exit = if (Test-Path -LiteralPath "$dir\exit.txt") { ([IO.File]::ReadAllText("$dir\exit.txt")).Trim() } else { '' }
        $toolPid = @($t | Where-Object { $_ -like 'tool-pid:*' } | ForEach-Object { [int]$_.Substring(9) })
        if ($c.ContainsKey('killHost')) {
            foreach ($l in $w.HostChecks) { [void]$checks.Add($l) }
        } elseif ($c.ContainsKey('stop')) {
            if (@($t | Where-Object { $_ -like 'runner-*' -or $_ -like 'tool:*' }).Count -gt 0) { [void]$checks.Add('ran although it had to stop') }
            if (($out + $res) -notmatch [regex]::Escape($c.stop)) { [void]$checks.Add("stop text missing: $($c.stop)") }
            if ($exit -ne '2') { [void]$checks.Add("exit=$exit, a stop must be 2") }
            if ($c.n -like 'lock-stale*' -and $t -notcontains 'lock-exit') { [void]$checks.Add('stale lock kept') }
            if ($c.ContainsKey('want2') -and $out -notmatch $c.want2) { [void]$checks.Add("want $($c.want2)") }
            if ($c.ContainsKey('noLockUse') -and (Test-Path -LiteralPath $lockFile)) { [void]$checks.Add('lock taken before readiness was proved') }
            if ($c.n -eq 'stamp-reused' -and ([IO.File]::ReadAllText("$dir\logs\diag-i364-dryrun-t.log")) -ne 'old evidence') { [void]$checks.Add('old evidence overwritten') }
        } else {
            if ($c.n -ne 'exit-throw' -and (Test-Path -LiteralPath $lockFile)) { [void]$checks.Add('lock file left behind') }
            if ($c.n -eq 'exit-throw' -and $res -notmatch 'LOCK NOT RELEASED') { [void]$checks.Add('lock failure hidden') }
            $pc = @($t | Where-Object { $_ -eq 'runner-post' }).Count
            $hasWant = $c.ContainsKey('want')
            if ($pc -lt 1 -or $pc -gt 2 -or ($pc -eq 2 -and -not $hasWant -and -not $c.ContainsKey('cc'))) { [void]$checks.Add("postcheck attempts=$pc") }
            if ($res -match 'postcheck repeated' -and -not $c.ContainsKey('cc2') -and -not @($t | Where-Object { $_ -like 'lockctx:post:ancestor:*' }).Count) { [void]$checks.Add('repeated postcheck was not accepted as ancestor') }
            $threw = ($c.ContainsKey('f') -and $c.f.ContainsKey('throw.txt'))
            if (-not @($t | Where-Object { $_ -match '^lockctx:(pre|post):(self|ancestor):' }).Count -and $c.n -ne 'exit-throw' -and -not $threw) { [void]$checks.Add('lock owner not recognized') }
            if ($hasWant) { foreach ($x in $c.want) { if ($res -notmatch [regex]::Escape($x)) { [void]$checks.Add("want $x") } } }
            if ($res -match 'CLEANUP_FAILED' -and $c.ContainsKey('vAny')) { [void]$checks.Add('false CLEANUP_FAILED after interrupt') }
            if (@($t | Where-Object { $_ -like 'state-*' }).Count -lt 2) { [void]$checks.Add('state check not attempted twice') }
            if (($t -contains 'trace-start') -and ($c.n -notlike '*trace-fail') -and ($t -notcontains 'trace-stop')) { [void]$checks.Add('trace not stopped') }
            foreach ($j in @($t | Where-Object { $_ -like 'json:*' })) { if ($j -notmatch '^json:[A-Za-z]:\\') { [void]$checks.Add("relative JSON path $j") } }
            if (@(Get-ChildItem -LiteralPath $Elsewhere -Recurse -File).Count -gt 0) { [void]$checks.Add('file written outside logs') }
            foreach ($k in @(@('gate=', 'g'), @('dryRun=', 'd'), @('correlation=', 'k'), @('verdict=', 'v'))) { if ($c.ContainsKey($k[1]) -and $res -notmatch ([regex]::Escape($k[0] + $c[$k[1]]))) { [void]$checks.Add("want $($k[0])$($c[$k[1]])") } }
            if ($c.ContainsKey('dAny') -and -not @($c.dAny | Where-Object { $res -match [regex]::Escape("dryRun=$_ ") }).Count) { [void]$checks.Add('dry run result not one of the interrupted forms') }
            if ($res -notmatch 'RUNNER RESULT') { [void]$checks.Add('no runner line') }
            if ($w.St.preRc -ne 0 -and $null -ne $w.St.pre -and $c.n -notmatch 'pre-throw|ctrlc-pre|state-pre' -and $res -notmatch 'RUNNER RESULT  pre=1') { [void]$checks.Add('runner failure not visible') }
            if ($w.St.pre -match 'PTP clock not running' -and $c.n -notmatch 'pre-throw|ctrlc-pre|state-pre|log-truncated|no-summary|duplicate|log-empty|json-missing|json-corrupt|json-wrong|adapter-unrep|pass-no' -and $res -notmatch 'FAILED LINE pre: Delta') { [void]$checks.Add('legacy failure line lost') }
            if ($res -match 'verdict=COMPLETE') { [void]$checks.Add('claimed COMPLETE') }
            if ($c.ContainsKey('g') -and $c.g -eq 'FAIL' -and @($t | Where-Object { $_ -like 'tool:*' }).Count -gt 0) { [void]$checks.Add('tool ran after a failed gate') }
            if ($c.ContainsKey('g') -and $c.g -eq 'FAIL' -and $res -notmatch 'GATE REASON pre') { [void]$checks.Add('gate reason missing') }
            if ($c.ContainsKey('k') -and $c.k -eq 'FAIL' -and $c.g -eq 'PASS' -and $res -notmatch 'CORRELATION REASON') { [void]$checks.Add('correlation reason missing') }
            if ($c.ContainsKey('gone') -and ($t -contains 'tool-finished')) { [void]$checks.Add('tool ran to its end, it was not stopped') }
            if ($c.ContainsKey('gone')) { foreach ($q in $toolPid) { if (& $Mine $q $dir) { [void]$checks.Add('tool still alive') } } }
            if ($c.ContainsKey('x') -and $exit -ne "$($c.x)") { [void]$checks.Add("exit=$exit, want $($c.x)") }
            if ($c.n -eq 'legacy-fail-clean') { foreach ($x in 'REGISTER CHECK pre adapter:0 verdict=PASS', 'REGISTER CHECK pre adapter:1 verdict=SKIP_UNSUPPORTED', 'REGISTER CHECK post adapter:2 verdict=SKIP_UNSUPPORTED') { if ($res -notmatch [regex]::Escape($x)) { [void]$checks.Add("lost: $x") } } }
            if ($c.ContainsKey('noTool') -and @($t | Where-Object { $_ -like 'tool:*' }).Count -gt 0) { [void]$checks.Add('the trigger ran although it had to stop') }
            if ($c.ContainsKey('deny')) { foreach ($x in $c.deny) { if ($res -match [regex]::Escape($x)) { [void]$checks.Add("must not say $x") } } }
            if ($c.ContainsKey('trace')) { foreach ($x in $c.trace) { if ($t -notcontains $x) { [void]$checks.Add("trace lacks $x") } } }
            if ($c.ContainsKey('checksums')) {
                $sumF = "$dir\logs\diag-i364-checksums-t.txt"
                if (-not (Test-Path -LiteralPath $sumF)) { [void]$checks.Add('no checksum file') } else {
                    $sum = @(Get-Content -LiteralPath $sumF)
                    if ($sum.Count -lt 5) { [void]$checks.Add("checksum file lists $($sum.Count) files") }
                    foreach ($n in @(Get-ChildItem -LiteralPath "$dir\logs" -File | Where-Object { $_.Name -ne 'diag-i364-checksums-t.txt' } | ForEach-Object { $_.Name })) {
                        $h = (Get-FileHash -LiteralPath "$dir\logs\$n" -Algorithm SHA256).Hash
                        if ($sum -notcontains "$h  $n") { [void]$checks.Add("checksum missing or wrong: $n") } }
                }
            }
        }
        $wantArgs = if ($c.ContainsKey('caseA')) { 'tool:--lifecycle-a' } else { 'tool:--lifecycle-a --dry-run' }
        if (@($t | Where-Object { $_ -like 'tool:*' -and $_ -ne $wantArgs }).Count -gt 0) { [void]$checks.Add('tool arguments changed') }
        foreach ($q in $toolPid) { $o = & $Mine $q $dir; if ($o) { $o | Stop-Process -Force -ErrorAction SilentlyContinue } }
        # Assertions on what the script did, kept apart from what stopped the run from being judged at all.
        $interrupted = ($c.ContainsKey('cc') -or $c.ContainsKey('killTool') -or $c.ContainsKey('killHost'))
        $evidence = if ($interrupted) { Test-Path -LiteralPath $resF } else { $exit -match '^-?\d+$' }
        return @{ Asserts = @($checks); Infra = @($w.Pre); Evidence = [bool]$evidence }
    }

    # What a failing case left behind, so a failure explains itself.
    $Tail = { param($w)
        $r = "$($w.Dir)\logs\diag-i364-result-t.txt"
        $x = if (Test-Path -LiteralPath $r) { @(Get-Content -LiteralPath $r | Where-Object { $_ -match '^(STOP|NOTE|TEST RESULT|CLEANUP RESULT)' } | Select-Object -Last 5) -join ' / ' } else { 'no result file' }
        $o = "$($w.Out)".Trim(); if ($o.Length -gt 300) { $o = $o.Substring($o.Length - 300) }
        " [$x] [$o]" }

    # Interrupt driver: a real console, a real Ctrl+C or a real kill.  Runs on a pool thread, so it uses no shared variable.
    $Driver = {
        param($w, $inject, $else)
        $c = $w.Spec; $dir = $w.Dir; $trace = "$dir\trace.txt"; $pre = New-Object System.Collections.ArrayList; $hostChecks = New-Object System.Collections.ArrayList
        $has = { param($m) try { $fs = [IO.File]::Open($trace, 'Open', 'Read', 'ReadWrite,Delete'); try { $tx = (New-Object IO.StreamReader $fs).ReadToEnd() } finally { $fs.Dispose() }; ($tx -split "`r?`n") -contains $m } catch { $false } }
        $si = New-Object Diagnostics.ProcessStartInfo 'powershell.exe', "-NoProfile -ExecutionPolicy Bypass -File `"$dir\run.ps1`""
        $si.UseShellExecute = $true; $si.WindowStyle = 'Hidden'; $si.WorkingDirectory = $else
        $proc = [Diagnostics.Process]::Start($si)
        $marker = if ($c.ContainsKey('cc')) { $c.cc } else { 'tool-wait' }
        $sw = [Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 40 -and -not (& $has $marker)) { Start-Sleep -Milliseconds 30 }
        if (-not (& $has $marker)) { [void]$pre.Add("marker $marker not reached") }
        if ($c.ContainsKey('cc')) {
            $i = [Diagnostics.Process]::Start((New-Object Diagnostics.ProcessStartInfo $inject, "$($proc.Id)" -Property @{ UseShellExecute = $false; CreateNoWindow = $true }))
            $null = $i.WaitForExit(10000); if ($i.ExitCode -ne 0) { [void]$pre.Add("inject rc=$($i.ExitCode)") }
            if ($c.ContainsKey('cc2')) {
                $sw.Restart(); while ($sw.Elapsed.TotalSeconds -lt 20 -and -not (& $has $c.cc2)) { Start-Sleep -Milliseconds 30 }
                Start-Sleep -Milliseconds 100
                $i = [Diagnostics.Process]::Start((New-Object Diagnostics.ProcessStartInfo $inject, "$($proc.Id)" -Property @{ UseShellExecute = $false; CreateNoWindow = $true })); $null = $i.WaitForExit(10000)
            }
        }
        $tp = @(Get-Content -LiteralPath $trace | Where-Object { $_ -like 'tool-pid:*' } | ForEach-Object { [int]$_.Substring(9) })
        $mine = { param($id) $o = Microsoft.PowerShell.Management\Get-Process -Id $id -ErrorAction SilentlyContinue; if ($o -and "$($o.Path)" -like "$dir\*") { $o } }
        if ($c.ContainsKey('killTool')) { foreach ($q in $tp) { $o = & $mine $q; if ($o) { $o | Stop-Process -Force -ErrorAction SilentlyContinue } } }
        if ($c.ContainsKey('killHost')) {
            Stop-Process -Id $proc.Id -Force; $null = $proc.WaitForExit(5000)
            if (@(Get-Content -LiteralPath $trace) -contains 'lock-exit') { [void]$hostChecks.Add('unexpected release') }
            if (-not (Test-Path -LiteralPath "$dir\lock")) { [void]$hostChecks.Add('dead-host lock file absent') }
            foreach ($q in $tp) { $o = & $mine $q; if ($o) { $o | Stop-Process -Force; $null = $o.WaitForExit(5000) } }
            Get-ChildItem -LiteralPath "$dir\logs" -File | Remove-Item -Force
            if (@(Get-ChildItem -LiteralPath "$dir\logs" -File).Count -gt 0) { [void]$hostChecks.Add('could not clear logs') }
            Remove-Item -LiteralPath "$dir\toolsleep.txt" -Force
            $before = @(Get-Content -LiteralPath $trace).Count
            $o2 = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$dir\run.ps1" 2>&1 | Out-String
            $t2 = @(Get-Content -LiteralPath $trace | Select-Object -Skip $before)
            if ($o2 -notmatch 'STOP a stale lock') { [void]$hostChecks.Add("rerun did not stop on the stale lock: $(($o2 -split "`n" | Where-Object { $_.Trim() } | Select-Object -First 2) -join ' | ')") }
            if (@($t2 | Where-Object { $_ -like 'tool:*' -or $_ -like 'runner-*' }).Count -gt 0) { [void]$hostChecks.Add('rerun ran after a stale lock') }
            if (Test-Path -LiteralPath "$dir\lock") { [void]$hostChecks.Add('stale lock not released by rerun') }
        } elseif (-not $proc.WaitForExit(45000)) { [void]$pre.Add('script did not end'); Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
        return @{ Pre = @($pre); HostChecks = @($hostChecks) }
    }

    # Work list: every case once, every mutant once, every interrupt kind five times.
    $work = New-Object System.Collections.ArrayList
    $byName = @{}; foreach ($c in $cases) { $byName[$c.n] = $c }
    $mutantProblems = New-Object System.Collections.ArrayList
    $tPrep = $Clock.Elapsed.TotalSeconds
    foreach ($c in $cases) { if ($InterruptNames -notcontains $c.n -and ($Only.Count -eq 0 -or $Only -contains $c.n)) { $w = & $NewCase $c "c_$($c.n)" $SubjectText; $w['Kind'] = 'case'; [void]$work.Add($w) } }
    foreach ($n in $InterruptNames) { if ($Only.Count -gt 0 -and $Only -notcontains $n) { continue }; foreach ($r in 1..$InterruptRepeats) { $w = & $NewCase $byName[$n] "i_${n}_$r" $SubjectText; $w['Kind'] = 'interrupt'; $w['Run'] = $r; [void]$work.Add($w) } }
    foreach ($m in $Mutants) {
        if ($Only.Count -gt 0 -and $Only -notcontains $m[3]) { continue }
        if (-not $SubjectText.Contains($m[1])) { [void]$mutantProblems.Add($m[0]); continue }
        $text = $SubjectText.Replace($m[1], $m[2])
        $w = & $NewCase $byName[$m[3]] "m_$($m[0])" $text; $w['Kind'] = 'mutant'; $w['Mutant'] = $m[0]
        $errs = $null; $null = [Management.Automation.Language.Parser]::ParseInput($text, [ref]$null, [ref]$errs); $w['Parsed'] = ($errs.Count -eq 0)
        [void]$work.Add($w)
    }

    $tRun = $Clock.Elapsed.TotalSeconds
    # A run of nothing must not read as a pass.
    $wantCases = @($cases | Where-Object { $InterruptNames -notcontains $_.n -and ($Only.Count -eq 0 -or $Only -contains $_.n) }).Count
    $wantInts = @($InterruptNames | Where-Object { $Only.Count -eq 0 -or $Only -contains $_ }).Count * $InterruptRepeats
    $wantMuts = @($Mutants | Where-Object { $Only.Count -eq 0 -or $Only -contains $_[3] }).Count
    $haveCases = @($work | Where-Object { $_.Kind -eq 'case' }).Count; $haveInts = @($work | Where-Object { $_.Kind -eq 'interrupt' }).Count; $haveMuts = @($work | Where-Object { $_.Kind -eq 'mutant' }).Count
    tc_check 'TEST-TAS-STAGE1-002-all-defined-work-ran' ($work.Count -gt 0 -and $haveCases -eq $wantCases -and $haveInts -eq $wantInts -and $haveMuts -eq $wantMuts -and ($Only.Count -gt 0 -or ($haveCases -ge 82 -and $haveInts -ge 35 -and $haveMuts -ge 47))) "cases $haveCases/$wantCases interrupts $haveInts/$wantInts mutants $haveMuts/$wantMuts"
    if ($Only.Count -gt 0) { Write-Host "  [NOTE] -Only is a development filter: this run is partial and proves nothing about the rest" }

    $iss = [Management.Automation.Runspaces.InitialSessionState]::CreateDefault()
    $iss.ExecutionPolicy = 'Bypass'
    $pool = [runspacefactory]::CreateRunspacePool(1, $Throttle, $iss, $Host); $pool.Open()
    $inject = Join-Path $Bin 'inject.exe'; [IO.File]::Copy($StandInExe, $inject)
    $running = New-Object System.Collections.ArrayList
    foreach ($w in $work) {
        $ps = [powershell]::Create(); $ps.RunspacePool = $pool
        if ($w.Spec.ContainsKey('cc') -or $w.Spec.ContainsKey('killTool') -or $w.Spec.ContainsKey('killHost')) {
            [void]$ps.AddScript($Driver.ToString()).AddArgument($w).AddArgument($inject).AddArgument($Elsewhere)
        } else {
            [void]$ps.AddScript("& '$($w.Dir)\run.ps1' *>&1 | Out-String")
        }
        [void]$running.Add(@{ W = $w; Ps = $ps; H = $ps.BeginInvoke() })
    }
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while ($deadline.Elapsed.TotalSeconds -lt $OutOfRange) {
        $open = 0
        foreach ($r in $running) { if ($r.H.IsCompleted) { if (-not $r.ContainsKey('Done')) { $r['Done'] = $deadline.Elapsed.TotalSeconds } } else { $open++ } }
        if ($open -eq 0) { break }
        Start-Sleep -Milliseconds 50
    }
    foreach ($r in $running) {
        $w = $r.W
        if (-not $r.H.IsCompleted) { $w['Hung'] = $true; $w.Pre += 'did not finish within the test bound'; $r.Ps.BeginStop($null, $null) | Out-Null; continue }
        try {
            $o = $r.Ps.EndInvoke($r.H)
            if ($w.Spec.ContainsKey('cc') -or $w.Spec.ContainsKey('killTool') -or $w.Spec.ContainsKey('killHost')) { $w.Pre += @($o[0].Pre); $w['HostChecks'] = @($o[0].HostChecks) }
            else { $w.Out = ($o | Out-String); foreach ($e in $r.Ps.Streams.Error) { $w.Out += "`n$e" } }
        } catch { $w.Pre += "run failed: $_" }
    }
    $byKind = @($running | Group-Object { $_.W.Kind } | ForEach-Object { "$($_.Name) finished by {0:N1} s" -f (@($_.Group | ForEach-Object { if ($_.ContainsKey('Done')) { $_.Done } else { 999 } } | Measure-Object -Maximum).Maximum) })
    Write-Host ("  [TIME] " + ($byKind -join ', '))
    $hung = @($running | Where-Object { -not $_.ContainsKey('Done') } | ForEach-Object { $_.W.Label })
    if ($hung.Count) { Write-Host ("  [NOTE] not finished at the bound: " + ($hung -join ', ')) }
    $pool.BeginClose($null, $null) | Out-Null
    $tJudge = $Clock.Elapsed.TotalSeconds
    Write-Host ("  [TIME] setup {0:N1} s, prepare {1:N1} s, run {2:N1} s" -f $tPrep, ($tRun - $tPrep), ($tJudge - $tRun))

    foreach ($w in $work) {
        $j = & $Judge $w
        $bad = @($j.Infra) + @($j.Asserts)
        switch ($w.Kind) {
            'case'      { $Base[$w.Spec.n] = ($bad.Count -eq 0); tc_check "TEST-TAS-STAGE1-CASE-$($w.Spec.n)" ($bad.Count -eq 0) (($bad -join '; ') + (& $Tail $w)) }
            'interrupt' { $Base[$w.Spec.n] = ($bad.Count -eq 0 -and (-not $Base.ContainsKey($w.Spec.n) -or $Base[$w.Spec.n])); tc_check "TEST-TAS-STAGE1-INT-$($w.Spec.n)-run$($w.Run)" ($bad.Count -eq 0) (($bad -join '; ') + (& $Tail $w)) }
            'mutant'    {
                $e = @{ Parsed = $w.Parsed; BasePassed = [bool]$Base[$w.Spec.n]; Hung = $w.ContainsKey('Hung'); Infra = @($j.Infra); Evidence = $j.Evidence; Detected = @($j.Asserts) }
                $v = Get-Stage1MutantVerdict $e
                $Verdicts[$v.Verdict]++
                Write-Host ("  [MUTANT] {0} {1} on {2}: {3}" -f $w.Mutant, $v.Verdict, $w.Spec.n, $v.Reason)
                tc_check "TEST-TAS-STAGE1-MUT-$($w.Mutant)-killed-by-$($w.Spec.n)" ($v.Verdict -eq 'KILLED') "$($v.Verdict): $($v.Reason)"
            }
        }
    }
    Write-Host ("  [MUTATION] killed={0} survived={1} infra_failure={2} inconclusive={3} of {4}" -f $Verdicts.KILLED, $Verdicts.SURVIVED, $Verdicts.INFRA_FAILURE, $Verdicts.INCONCLUSIVE, $haveMuts)
    if ($Only.Count -eq 0) { tc_check 'TEST-TAS-STAGE1-MUT-complete-mutation-coverage' ($Verdicts.KILLED -eq @($Mutants).Count) "not complete: killed $($Verdicts.KILLED) of $(@($Mutants).Count)" }
    tc_check 'TEST-TAS-STAGE1-MUT-all-patterns-present' ($mutantProblems.Count -eq 0) ("mutation target text not found, the mutant did not run: " + ($mutantProblems -join ', '))
    $RealLockAfter = if (Test-Path -LiteralPath $RealLock) { (Get-Item -LiteralPath $RealLock).LastWriteTimeUtc.Ticks } else { 0 }
    tc_check 'TEST-TAS-STAGE1-001-real-machine-lock-untouched' ($RealLockAfter -eq $RealLockBefore) "the sandbox reached $RealLock"
} finally {
    if ($KeepSandbox) { Write-Host "  [NOTE] sandbox kept on request: $Tmp" }
    else {
        try { Remove-Item -LiteralPath $Tmp -Recurse -Force -ErrorAction Stop }
        catch { Write-Host "  [CLEANUP_FAILED] TEST-TAS-STAGE1: sandbox kept: $Tmp ($_)"; $failed++ }
    }
}

Write-Host ""
Write-Host ("Runtime {0:N1} s, throttle {1}." -f $Clock.Elapsed.TotalSeconds, $Throttle)
Write-Host ("TEST-TAS-STAGE1: {0} passed, {1} failed of {2} (offline, not HIL evidence)" -f $passed, $failed, $total) -ForegroundColor $(if ($failed) { 'Red' } else { 'Green' })
if ($failed) { exit 1 }
exit 0
