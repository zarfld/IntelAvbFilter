# Invoke-TasStage1DryRun.ps1
# #364 Stage 1: capability-driven dry run of the filter lifecycle diagnostic.  LOCAL_DIAGNOSTIC, Debug only.
# Nothing here restarts a filter, cycles a NIC, installs a driver or writes a register: the tool runs --lifecycle-a --dry-run
# and nothing else.  Run it elevated, from any directory, after tools\build\Build-Tests.ps1 -Configuration Debug.
#
# Order: readiness (no CI job, preflight, installed driver equals the build, manifest equals HEAD and this tool), machine
# lock, adapter state, register precheck, per-adapter gate, NDIS trace, dry run, then cleanup and post checks, lock release,
# report.  Every cleanup step runs in the finally of the one before it, so an error or a Ctrl+C in one does not skip the next.
#
# The gate is per adapter and tied to this execution.  A runner failure outside it, such as the legacy Phase 2 result
# tracked in #389, does not block the dry run and stays FAILED in the report and in exit code 3.
# Exit: 0 complete, 3 complete with a runner failure outside the gate, 1 failed, 2 stopped before anything ran.
# A complete dry run is not a Stage 1 pass until its evidence has been read, and it never claims restoration.
#
# If this host is killed outright no finally runs: the lock is taken over by the next HIL run, which stops, and a trace
# left open is closed with: logman stop AvbI364S1 -ets
#
# Evidence: logs\diag-i364-*-<stamp>.*
# Verified offline by tests/unit/ci/test_tas_stage1_orchestration.ps1 (TEST-TAS-STAGE1-*), which is not HIL evidence.
# Traces to: #364.

[CmdletBinding()]
param(
    [string]$Stamp = (Get-Date -Format yyyyMMdd_HHmmss),
    [int]$ToolTimeoutSeconds = 300,
    [string]$Logman = (Get-Command logman.exe).Source
)

$root = (Resolve-Path "$PSScriptRoot\..\..").Path; $L = "$root\logs"
. "$root\tools\lib\HilLock.ps1"
$head = (git -C $root rev-parse HEAD); $t0 = (Get-Date).ToUniversalTime()
$tool = "$root\build\tools\avb_test\x64\Debug\tas_gate_investigation.exe"; $transcript = "$L\diag-i364-transcript-$Stamp.txt"
$etl = "$L\diag-i364-stage1-$Stamp.etl"; $dry = "$L\diag-i364-dryrun-$Stamp.log"; $result = "$L\diag-i364-result-$Stamp.txt"
$statePre = "$L\diag-i364-state-pre-$Stamp.txt"; $statePost = "$L\diag-i364-state-post-$Stamp.txt"
$preLog = "$L\diag-i364-tsnreg-pre-$Stamp.log"; $postLog = "$L\diag-i364-tsnreg-post-$Stamp.log"; $preJson = "$L\diag-i364-tsnreg-pre-$Stamp.json"; $postJson = "$L\diag-i364-tsnreg-post-$Stamp.json"
$preRc = $null; $postRc = $null; $dryRc = 'NOT_RUN'; $gate = 'NOT_EVALUATED'; $corr = 'NOT_EVALUATED'; $corrWhy = @(); $pre = $null; $post = $null; $p = $null; $traced = $false; $traceOk = $false; $finished = $false
$life = 'unknown'; $diff = 'unknown'; $regDiff = 'unknown'; $newFails = 'unknown'; $goneFails = 'unknown'; $problems = New-Object System.Collections.ArrayList
$exitCode = 2
$check = {
    param($log, $json)
    $r = @{ Ok = $false; Parsed = $false; Why = @(); N = 0; Pass = @(); Regs = @(); Fails = @(); Tc = @() }
    try {
        if (-not (Test-Path -LiteralPath $json)) { $r.Why += 'results JSON missing'; return $r }
        if (-not (Test-Path -LiteralPath $log)) { $r.Why += 'runner log missing'; return $r }
        $d = Get-Content -LiteralPath $json -Raw | ConvertFrom-Json
        $rec = @($d.records)
        if ($d.schema -ne 'avb-runner-results/1') { $r.Why += 'unknown results schema' }
        if ($d.source.revision -ne $head -or $d.source.dirty -ne $false) { $r.Why += 'results not from this clean revision' }
        if ([DateTimeOffset]::Parse("$($d.startedUtc)").UtcDateTime -lt $t0) { $r.Why += 'results older than this run' }
        if ("$($d.stateHalt)" -ne '') { $r.Why += 'runner state halt' }
        if ($d.machineSafety.stateAtStart -ne 'CLEAR') { $r.Why += 'machine safety not CLEAR' }
        if ($d.installedDriver.matchesConfiguration -ne $true) { $r.Why += 'installed driver does not match the configuration' }
        if ($rec.Count -ne 1) { $r.Why += "expected 1 test record, got $($rec.Count)"; return $r }
        if ($rec[0].entry -ne 'ssot_register_validation_test' -or $rec[0].executed -ne $true) { $r.Why += 'record is not an executed ssot_register_validation_test' }
        if ($rec[0].provenance.verified -ne $true) { $r.Why += 'test artifact provenance not verified' }
        $tc = @($rec[0].testcases | Where-Object { $_.Id -eq 'TC-TSNREG-001' })
        $r.N = @($d.adapters).Count; $r.Tc = $tc
        if ($r.N -lt 1 -or $tc.Count -ne $r.N) { $r.Why += "adapters=$($r.N) but TC-TSNREG-001 results=$($tc.Count)" }
        if ((@($tc | ForEach-Object { [int]$_.AdapterIndex } | Sort-Object) -join ',') -ne ((@(0..([Math]::Max($r.N, 1) - 1))) -join ',')) { $r.Why += 'adapter indexes are not one per adapter' }
        foreach ($t in $tc) { if ($t.Verdict -notin 'PASS', 'SKIP_UNSUPPORTED') { $r.Why += "adapter:$($t.AdapterIndex) verdict=$($t.Verdict)" } }
        $lines = @(Get-Content -LiteralPath $log)
        $at = @{}
        foreach ($m in 'Phase 1', 'Phase 2', 'Phase 3', 'Test Summary') {
            $hit = @(0..($lines.Count - 1) | Where-Object { $lines[$_] -match "=== $m" })
            if ($hit.Count -ne 1) { $r.Why += "section '$m' seen $($hit.Count) times" } else { $at[$m] = $hit[0] }
        }
        if ($at.Count -eq 4 -and $at['Phase 1'] -lt $at['Phase 2'] -and $at['Phase 2'] -lt $at['Phase 3'] -and $at['Phase 3'] -lt $at['Test Summary']) {
            $r.Parsed = $true
            $r.Fails = @($lines[$at['Phase 1']..$at['Test Summary']] | Where-Object { $_ -match '\[FAIL' } | ForEach-Object { $_.Trim() })
            if (@($lines[$at['Phase 1']..$at['Phase 2']] | Where-Object { $_ -match '\[FAIL' }).Count -gt 0) { $r.Why += 'Phase 1 SSOT failure' }
            $sec = @($lines[$at['Phase 3']..$at['Test Summary']])
            foreach ($t in @($tc | Where-Object { $_.Verdict -eq 'PASS' })) {
                $i = [int]$t.AdapterIndex
                $s = @(0..($sec.Count - 1) | Where-Object { $sec[$_] -match "^\s*adapter:$i did=0x[0-9A-Fa-f]{4} luid=(0x[0-9A-Fa-f]{16}) " })
                $e = @(0..($sec.Count - 1) | Where-Object { $sec[$_] -match "^\s*\[PASS\] TC-TSNREG-001/adapter:$i did=" })
                if ($s.Count -ne 1 -or $e.Count -ne 1 -or $e[0] -lt $s[0]) { $r.Why += "adapter:$i PASS has no matching register block"; continue }
                $null = $sec[$s[0]] -match 'luid=(0x[0-9A-Fa-f]{16})'; $luid = $Matches[1].ToUpper().Replace('0X', '0x')
                $blk = @($sec[$s[0]..$e[0]])
                $regs = @($blk | Where-Object { $_ -match '^\s+[A-Z0-9_]+\s+0x[0-9A-Fa-f]{5} = 0x' -or $_ -match 'tasArmed=' } | ForEach-Object { "$luid $($_.Trim())" })
                if (@($blk | Where-Object { $_ -match '^\s*tasArmed=0\s*$' }).Count -ne 1) { $r.Why += "adapter:$i tasArmed is not 0" }
                if (@($regs | Where-Object { $_ -notmatch 'tasArmed=' }).Count -lt 1) { $r.Why += "adapter:$i has no register values" }
                $r.Pass += $luid; $r.Regs += $regs
            }
            if ($r.Pass.Count -lt 1) { $r.Why += 'no adapter passed TC-TSNREG-001' }
        }
        $r.Ok = ($r.Parsed -and $r.Why.Count -eq 0)
    } catch { $r.Why += "evaluation failed: $_" }
    return $r
}
if (-not $Stamp) { 'STOP stamp is empty, nothing ran' } elseif (@(Get-ChildItem -LiteralPath $L -Filter "diag-i364-*-$Stamp.*" | Where-Object { $_.Name -notlike 'diag-i364-transcript-*' }).Count -gt 0) { "STOP evidence for stamp $Stamp already exists, nothing ran" } else {
    try { Start-Transcript -Path $transcript | Out-Null } catch { 'transcript did not start' }
    $notReady = New-Object System.Collections.ArrayList
    try {
        if (@(Get-Process -Name 'Runner.Worker' -ErrorAction SilentlyContinue).Count -gt 0) { [void]$notReady.Add('STOP a CI job is running on this machine') }
        $global:LASTEXITCODE = 0; & "$root\tools\test\Test-DriverPreFlight.ps1"
        if ($LASTEXITCODE -ne 0) { [void]$notReady.Add("STOP preflight exit=$LASTEXITCODE") }
        $svc = Get-CimInstance Win32_SystemDriver -Filter "Name='IntelAvbFilter'"
        $inst = (Get-FileHash ($svc.PathName -replace '^\\\?\?\\', '')).Hash
        $built = (Get-FileHash "$root\build\x64\Debug\IntelAvbFilter\IntelAvbFilter.sys").Hash
        if ($svc.State -ne 'Running' -or $inst -ne $built) { [void]$notReady.Add("STOP driver state=$($svc.State) installed=$inst built=$built") }
        $m = Get-Content "$root\build\tools\avb_test\x64\Debug\test-build-manifest.json" -Raw | ConvertFrom-Json
        $e = @($m.entries | Where-Object { $_.name -eq 'tas_gate_investigation' })
        if ($m.provenanceMode -ne 'clean-commit' -or $m.sourceRevision -ne $head -or $m.sourceDirty -ne $false -or $e.Count -ne 1 -or -not (Test-Path -LiteralPath $tool) -or (Get-FileHash -LiteralPath $tool).Hash -ne $e[0].sha256) { [void]$notReady.Add("STOP build manifest does not match HEAD and this tool: mode=$($m.provenanceMode) rev=$($m.sourceRevision) dirty=$($m.sourceDirty)") }
    } catch { [void]$notReady.Add("STOP readiness check failed: $_") }
    if ($notReady.Count -gt 0) { [IO.File]::WriteAllLines($result, [string[]]$notReady); $notReady } else {
    $lock = Enter-AvbHilLock -Kind 'local' -Label 'i364 dry run' -WaitSeconds 60
    if (-not $lock.Ok) { $m = "STOP lock not acquired, nothing ran: $($lock.Why)"; [IO.File]::WriteAllText($result, $m); $m } elseif ($lock.PreviousOwner) { $m = 'STOP a stale lock was taken over, machine state unknown, nothing ran'; [IO.File]::WriteAllText($result, $m); $m; try { [void](Exit-AvbHilLock -Owner $lock.Owner) } catch { "LOCK NOT RELEASED: $_" } } else {
        try {
            try {
                Get-NetAdapter | Sort-Object InterfaceGuid | Format-Table InterfaceGuid, Status, MediaConnectionState, LinkSpeed, PromiscuousMode -AutoSize | Out-File -LiteralPath $statePre
                $global:LASTEXITCODE = 0
                & "$root\tools\test\Run-Tests-CI.ps1" -Configuration Debug -Suite HardwareIntegration -Tests ssot_register_validation_test -ResultsJson $preJson *>&1 | Tee-Object -FilePath $preLog
                $preRc = $LASTEXITCODE
                $pre = & $check $preLog $preJson
                $gate = if ($pre.Ok) { 'PASS' } else { 'FAIL' }
                if ($pre.Ok) {
                    & $Logman create trace AvbI364S1 -p Microsoft-Windows-NDIS '0x4316' '0x5' -o $etl -ct perf -ets
                    $traced = ($LASTEXITCODE -eq 0)
                    if (-not $traced) { [void]$problems.Add('trace not started') }
                    $p = Start-Process -FilePath $tool -ArgumentList '--lifecycle-a --dry-run' -NoNewWindow -PassThru -RedirectStandardOutput $dry -RedirectStandardError "$dry.stderr"
                    $null = $p.Handle
                    $sw = [Diagnostics.Stopwatch]::StartNew()
                    while (-not $p.WaitForExit(250) -and $sw.Elapsed.TotalSeconds -lt $ToolTimeoutSeconds) { }
                    if ($p.HasExited) { $dryRc = if ($null -ne $p.ExitCode) { $p.ExitCode } else { 'NO_EXIT_CODE' } } else { $dryRc = 'TIMEOUT' }
                }
                $finished = $true
            } catch { [void]$problems.Add("main: $_") } finally {
                $steps = @(
                    { if (-not $finished) { [void]$problems.Add('run did not finish: interrupted or failed before the dry run ended'); if ($p -and $dryRc -eq 'NOT_RUN') { $dryRc = 'INTERRUPTED' } }; if ($p -and -not $p.HasExited) { $p.Kill(); if (-not $p.WaitForExit(10000)) { [void]$problems.Add("tool still running pid=$($p.Id)") } } },
                    { if ($traced) {
                        $global:LASTEXITCODE = 0; & $Logman stop AvbI364S1 -ets
                        if ($LASTEXITCODE -ne 0) { [void]$problems.Add("trace stop returned $LASTEXITCODE") }
                        $global:LASTEXITCODE = 0; & $Logman query AvbI364S1 -ets 2>$null | Out-Null
                        if ($LASTEXITCODE -eq 0) { [void]$problems.Add('trace session still running') } elseif ($LASTEXITCODE -ne -2144337918) { [void]$problems.Add("trace session state unknown, query returned $LASTEXITCODE") } else { $traceOk = @($problems | Where-Object { $_ -like 'trace stop*' }).Count -eq 0 }
                    } },
                    { if ($traced -and -not $traceOk) {
                        $q = [Diagnostics.Process]::Start([Diagnostics.ProcessStartInfo]@{ FileName = $Logman; Arguments = 'query AvbI364S1 -ets'; UseShellExecute = $false })
                        if ($q.WaitForExit(30000) -and $q.ExitCode -eq 0) {
                            [void]$problems.Add('trace session still open after the first stop, stopping again')
                            $q = [Diagnostics.Process]::Start([Diagnostics.ProcessStartInfo]@{ FileName = $Logman; Arguments = 'stop AvbI364S1 -ets'; UseShellExecute = $false })
                            $null = $q.WaitForExit(30000)
                            $q = [Diagnostics.Process]::Start([Diagnostics.ProcessStartInfo]@{ FileName = $Logman; Arguments = 'query AvbI364S1 -ets'; UseShellExecute = $false })
                            $null = $q.WaitForExit(30000)
                        }
                        if ($q.HasExited -and $q.ExitCode -eq -2144337918) { $traceOk = $true } else { [void]$problems.Add('TRACE SESSION NOT CONFIRMED CLOSED: run logman stop AvbI364S1 -ets') }
                    } },
                    { if ($traced) { try { $global:LASTEXITCODE = 0; tracerpt $etl -o "$etl.xml" -of XML -y | Out-Null; if ($LASTEXITCODE -ne 0) { $traceOk = $false; [void]$problems.Add("trace decode returned $LASTEXITCODE") } } catch { $traceOk = $false; [void]$problems.Add("decode trace: $_") } } },
                    { Get-NetAdapter | Sort-Object InterfaceGuid | Format-Table InterfaceGuid, Status, MediaConnectionState, LinkSpeed, PromiscuousMode -AutoSize | Out-File -LiteralPath $statePost },
                    { if (-not [IO.File]::Exists($statePost) -or ([IO.FileInfo]$statePost).Length -eq 0) { [void]$problems.Add('adapter state capture repeated: the first attempt gave no result'); Get-NetAdapter | Sort-Object InterfaceGuid | Format-Table InterfaceGuid, Status, MediaConnectionState, LinkSpeed, PromiscuousMode -AutoSize | Out-File -LiteralPath $statePost } },
                    { $global:LASTEXITCODE = 0; & "$root\tools\test\Run-Tests-CI.ps1" -Configuration Debug -Suite HardwareIntegration -Tests ssot_register_validation_test -ResultsJson $postJson *>&1 | Tee-Object -FilePath $postLog; $postRc = $LASTEXITCODE },
                    { if ($null -eq $postRc) {
                        [void]$problems.Add('postcheck repeated in a child process: the first attempt gave no result')
                        $ch = [Diagnostics.Process]::Start([Diagnostics.ProcessStartInfo]@{ FileName = 'cmd.exe'; Arguments = "/c powershell -NoProfile -ExecutionPolicy Bypass -File `"$root\tools\test\Run-Tests-CI.ps1`" -Configuration Debug -Suite HardwareIntegration -Tests ssot_register_validation_test -ResultsJson `"$postJson`" > `"$postLog`" 2>&1"; UseShellExecute = $false; WorkingDirectory = $root })
                        if ($ch.WaitForExit(600000)) { $postRc = $ch.ExitCode } else { $ch.Kill(); [void]$problems.Add('repeated postcheck timed out') }
                    } },
                    { $post = & $check $postLog $postJson },
                    { if ((Test-Path -LiteralPath $statePre) -and (Test-Path -LiteralPath $statePost) -and ([IO.FileInfo]$statePre).Length -gt 0 -and ([IO.FileInfo]$statePost).Length -gt 0) { $diff = @(Compare-Object (Get-Content -LiteralPath $statePre) (Get-Content -LiteralPath $statePost)).Count } },
                    { if ($pre -and $post -and $pre.Parsed -and $post.Parsed -and $pre.Regs.Count -gt 0 -and $post.Regs.Count -gt 0) { $regDiff = @(Compare-Object $pre.Regs $post.Regs).Count } },
                    { if ($pre -and $post -and $pre.Parsed -and $post.Parsed) { $newFails = @($post.Fails | Where-Object { $_ -notin $pre.Fails }).Count; $goneFails = @($pre.Fails | Where-Object { $_ -notin $post.Fails }).Count } },
                    { if ($traced -and (Test-Path -LiteralPath "$etl.xml")) { $life = @(Select-String -LiteralPath "$etl.xml" -Pattern '<EventID[^>]*>(10312|10103|10007|10058|10050)<').Count } },
                    { try {
                        if ($gate -eq 'PASS' -and (Test-Path -LiteralPath $dry)) {
                            $d = @(Get-Content -LiteralPath $dry)
                            $plan = @($d | Where-Object { $_ -match ' plan luid=0x[0-9A-Fa-f]{16} .* class=\S+\s*$' })
                            $sum = @($d | Where-Object { $_ -match ' summary adapters=\d+ eligible=\d+ exit=-?\d+\s*$' })
                            $luids = @($plan | ForEach-Object { $null = $_ -match 'luid=(0x[0-9A-Fa-f]{16})'; $Matches[1].ToUpper().Replace('0X', '0x') })
                            if ($sum.Count -ne 1) { $corrWhy += "summary lines=$($sum.Count)" } else {
                                $null = $sum[0] -match 'adapters=(\d+) eligible=(\d+) exit=(-?\d+)'; $sa = [int]$Matches[1]; $se = [int]$Matches[2]; $sx = [int]$Matches[3]
                                if ($sa -ne $pre.N) { $corrWhy += "tool adapters=$sa but runner adapters=$($pre.N)" }
                                if ($se -lt 1) { $corrWhy += 'no eligible adapter' }
                                if ("$sx" -ne "$dryRc") { $corrWhy += "summary exit=$sx but process exit=$dryRc" }
                                if (@($plan | Where-Object { $_ -match 'class=ELIGIBLE\s*$' }).Count -ne $se) { $corrWhy += 'eligible count differs from plan' }
                                if ($dryRc -eq 0 -and @($d | Where-Object { $_ -match ' dry run complete: ' }).Count -ne $se) { $corrWhy += 'dry run completion not shown for every eligible adapter' }
                            }
                            if ($plan.Count -ne $pre.N -or @($luids | Sort-Object -Unique).Count -ne $plan.Count) { $corrWhy += "plan lines=$($plan.Count) unique=$(@($luids | Sort-Object -Unique).Count) adapters=$($pre.N)" }
                            foreach ($q in $plan) {
                                $null = $q -match 'luid=(0x[0-9A-Fa-f]{16}) .* class=(\S+)\s*$'; $u = $Matches[1].ToUpper().Replace('0X', '0x'); $c = $Matches[2]
                                if (@($d | Where-Object { $_ -match " adapter result luid=(?i:$u) .* class=$c " }).Count -ne 1) { $corrWhy += "no result for $u" }
                                if ($c -eq 'ELIGIBLE' -and ($u -notin $pre.Pass -or -not $post -or -not $post.Ok -or $u -notin $post.Pass)) { $corrWhy += "$u eligible without PASS before and after" }
                            }
                            $corr = if ($corrWhy.Count -eq 0) { 'OK' } else { 'FAIL' }
                        } elseif ($gate -eq 'PASS') { $corr = 'FAIL'; $corrWhy += 'dry run log missing' }
                    } catch { $corr = 'FAIL'; $corrWhy += "correlation failed: $_" } }
                )
                $stepDone = @{}
                $run = { param($i) if ($i -lt $steps.Count) { try { . $steps[$i]; $stepDone[$i] = $true } catch { [void]$problems.Add("cleanup step $i failed: $_") } finally { . $run ($i + 1) } } }
                . $run 0
            }
        } finally {
            $rel = $null
            try { $rel = Exit-AvbHilLock -Owner $lock.Owner } catch { $rel = @{ Ok = $false; Why = "$_" } } finally {
                $changed = (($diff -is [int] -and $diff -gt 0) -or ($life -is [int] -and $life -gt 0) -or ($regDiff -is [int] -and $regDiff -gt 0) -or ($newFails -is [int] -and $newFails -gt 0) -or ($preRc -eq 0 -and $null -ne $postRc -and $postRc -ne 0))
                $clean = ($post -and $post.Ok -and $null -ne $postRc -and $regDiff -eq 0 -and $diff -eq 0 -and $life -eq 0 -and $newFails -eq 0 -and $goneFails -eq 0 -and $traceOk)
                $cleanup = if ($changed) { 'CLEANUP_FAILED' } elseif ($clean) { 'NO_CHANGE_OBSERVED (measured facts only, not COMPLETE_RESTORATION, PHC not judged)' } else { 'INCONCLUSIVE_RESTORATION' }
                $report = New-Object System.Collections.ArrayList
                foreach ($x in @(@('pre', $pre), @('post', $post))) { if ($x[1]) { foreach ($t in $x[1].Tc) { [void]$report.Add("REGISTER CHECK $($x[0]) adapter:$($t.AdapterIndex) verdict=$($t.Verdict) $($t.Detail)") } } }
                if (Test-Path -LiteralPath $dry) { foreach ($l in @(Select-String -LiteralPath $dry -Pattern ' plan luid=| observer proved | fact register | adapter result | dry run complete| summary ' | ForEach-Object Line)) { [void]$report.Add($l) } }
                foreach ($x in @(@('pre', $pre), @('post', $post))) { if ($x[1]) { foreach ($f in $x[1].Fails) { [void]$report.Add("FAILED LINE $($x[0]): $f") } } }
                [void]$report.Add("RUNNER RESULT  pre=$preRc post=$postRc (the runner verdict stands, a nonzero exit stays FAILED)")
                [void]$report.Add("TEST RESULT    gate=$gate dryRun=$dryRc correlation=$corr")
                if ($pre) { foreach ($w in $pre.Why) { [void]$report.Add("GATE REASON pre: $w") } }
                if ($post) { foreach ($w in $post.Why) { [void]$report.Add("GATE REASON post: $w") } }
                foreach ($w in $corrWhy) { [void]$report.Add("CORRELATION REASON: $w") }
                [void]$report.Add("CLEANUP RESULT registersDiffering=$regDiff stateDifferences=$diff lifecycleEvents=$life newFailures=$newFails goneFailures=$goneFails traceClosed=$traceOk verdict=$cleanup")
                if ($steps) { foreach ($i in 0..($steps.Count - 1)) { if (-not $stepDone[$i]) { [void]$report.Add("NOTE cleanup step $i did not complete") } } } else { [void]$report.Add('NOTE cleanup steps never started') }
                foreach ($n in $problems) { [void]$report.Add("NOTE $n") }
                if ($rel -and $rel.Ok) { [void]$report.Add('NOTE lock released ok=True') } else { [void]$report.Add("NOTE LOCK NOT RELEASED: $($rel.Why)") }
                $completed = ($gate -eq 'PASS' -and $dryRc -eq 0 -and $corr -eq 'OK' -and $clean -and -not $changed -and $rel -and $rel.Ok)
                $exitCode = if (-not $completed) { 1 } elseif ($preRc -eq 0 -and $postRc -eq 0) { 0 } else { 3 }
                [void]$report.Add("DRY RUN $(if ($completed) { 'COMPLETED' } else { 'NOT COMPLETED' }) exit=$exitCode (0 complete, 3 complete with a runner failure outside the gate, 1 failed, 2 stopped; LOCAL_DIAGNOSTIC, nothing here is a restoration claim)")
                [IO.File]::WriteAllLines($result, [string[]]$report)
                $report
            }
        }
    }
    }
}
try { Stop-Transcript } catch { 'transcript was not running' }
"evidence stamp $Stamp"
exit $exitCode
