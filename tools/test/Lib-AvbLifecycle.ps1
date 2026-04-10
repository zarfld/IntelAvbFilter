# Lib-AvbLifecycle.ps1
# Shared lifecycle metrics snapshot helpers  --  single source of truth (SSOT).
# Dot-sourced by Run-Tests.ps1 and Run-Tests-CI.ps1.
#
# Implements: #27 (REQ-NF-SCRIPTS-001: Consolidated Script Architecture)
#
# What this does:
#   Get-AvbLifecycleSnapshot   --  opens \\.\IntelAvbFilter, runs ENUM_ADAPTERS →
#       OPEN_ADAPTER → GET_STATISTICS, returns a hashtable of 14 key driver
#       counters, or $null (with a [LCY] diagnostic) if the driver is absent.
#   Write-LifecycleDiff        --  prints the before/after delta for a test, with
#       colour-coded warnings for underflows, errors, and unexpected detaches.
#
# Why snapshot before/after every test?
#   The driver maintains cumulative counters and gauges in AVB_DRIVER_STATISTICS.
#   Taking a before/after snapshot around each test binary makes the following
#   discrepancies visible *immediately* rather than only surfacing as failures
#   in later test cases or in production:
#
#   Counter / field            Discrepancy detected
#   ────────────────────────   ────────────────────────────────────────────────
#   OutstandingReceiveNBLs !!UNDERFLOW!!  NBL gauge rolls to ~UINT64_MAX when
#   OutstandingSendNBLs                   the driver returns more NBLs to NDIS
#                                         than it indicated outstanding (refcount
#                                         bug  --  was TC-LCY-003 / PR #fix-filter)
#   OutstandingOids        !!UNDERFLOW!!  OID pipeline undercount; OID was
#                                         completed without prior increment.
#   ErrorCount             YELLOW         Any driver error during the test.
#   FilterDetachCount      YELLOW         Adapter unexpectedly unbound during
#                                         the test run.
#   OidRequestCount vs     mismatch       OIDs dispatched but not completed →
#   OidCompleteCount                      OID pipeline leak / deadlock.
#   IoctlCount             unexpected +N  Driver processed more IOCTLs than the
#                                         test is supposed to send → routing bug
#                                         or spurious calls from another process.
#   TxPackets / RxPackets  >0 change      Traffic activity when no traffic was
#                                         expected (useful as baseline sanity).
#
# SSOT IOCTL code rationale:
#   CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD=0x17, fn, METHOD_BUFFERED=0, FILE_ANY_ACCESS=0)
#   Modern WDK (10.0.26100+) ndis.h does NOT define _NDIS_CONTROL_CODE  --  both
#   kernel and user-mode code reach these values via the avb_ioctl.h fallback
#   that calls winioctl.h CTL_CODE directly.  Old NDIS headers produced the
#   0x9C40Axxx family which is WRONG for this environment.
#
# The 192-byte AVB_DRIVER_STATISTICS struct layout (all avb_u64, 8-byte packed):
#   offset   0: TxPackets          offset   8: RxPackets
#   offset  16: TxBytes            offset  24: RxBytes
#   offset  32: PhcQueryCount      offset  40: PhcAdjustCount
#   offset  48: PhcSetCount        offset  56: TimestampCount
#   offset  64: IoctlCount         offset  72: ErrorCount
#   offset  80: MemoryAllocFail    offset  88: HardwareFaults
#   offset  96: FilterAttachCount  offset 104: FilterPauseCount
#   offset 112: FilterRestartCount offset 120: FilterDetachCount
#   offset 128: OutstandingSendNBLs offset 136: OutstandingReceiveNBLs
#   offset 144: OidRequestCount    offset 152: OidCompleteCount
#   offset 160: OutstandingOids    offset 168: FilterStatusCount
#   offset 176: FilterNetPnPCount  offset 184: PauseRestartGeneration

function Get-AvbLifecycleSnapshot {
    # Phase A: Enumerate ALL bound adapters and collect per-adapter AVB_DRIVER_STATISTICS.
    # Returns @{ Adapters = @( @{ AdapterIndex=0; VendorId=...; DeviceId=...; IoctlCount=...; ... }, ... ) }
    # or $null when the driver is absent or misconfigured.
    #
    # Each adapter has independent counters — only snapshotting adapter 0 was blind to IOCTLs
    # dispatched to adapters 1+ (e.g. avb_test_i210.exe always showed +2 even when sending 4 IOCTLs).
    # Per-adapter snapshots fix that gap.

    # Load DeviceIoControl P/Invoke (idempotent  --  Add-Type skipped if type already loaded)
    if (-not ([System.Management.Automation.PSTypeName]'AvbIoctl').Type) {
        Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class AvbIoctl {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr CreateFile(string lpFileName, uint dwDesiredAccess,
        uint dwShareMode, IntPtr lpSecurityAttributes, uint dwCreationDisposition,
        uint dwFlagsAndAttributes, IntPtr hTemplateFile);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool CloseHandle(IntPtr hObject);
    // Two overloads: one for in+out byte arrays, one for null input (GET_STATISTICS)
    [DllImport("kernel32.dll", SetLastError=true, EntryPoint="DeviceIoControl")]
    public static extern bool DeviceIoControlInOut(IntPtr hDevice, uint dwIoControlCode,
        byte[] lpInBuffer, uint nInBufferSize,
        byte[] lpOutBuffer, uint nOutBufferSize,
        out uint lpBytesReturned, IntPtr lpOverlapped);
    [DllImport("kernel32.dll", SetLastError=true, EntryPoint="DeviceIoControl")]
    public static extern bool DeviceIoControlOut(IntPtr hDevice, uint dwIoControlCode,
        IntPtr lpInBuffer, uint nInBufferSize,
        byte[] lpOutBuffer, uint nOutBufferSize,
        out uint lpBytesReturned, IntPtr lpOverlapped);
    public const uint GENERIC_READ  = 0x80000000;
    public const uint GENERIC_WRITE = 0x40000000;
    public const uint OPEN_EXISTING = 3;
    public const uint FILE_ATTRIBUTE_NORMAL = 0x80;
    public static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);
    // Real user-mode IOCTL codes: CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD=0x17, fn, METHOD_BUFFERED, FILE_ANY_ACCESS)
    // (Modern WDK ndis.h does not define _NDIS_CONTROL_CODE, so kernel and UM both use the avb_ioctl.h fallback)
    public const uint IOCTL_AVB_ENUM_ADAPTERS  = 0x0017007C;  // fn=31
    public const uint IOCTL_AVB_OPEN_ADAPTER   = 0x00170080;  // fn=32
    public const uint IOCTL_AVB_GET_STATISTICS = 0x00172020;  // fn=0x808
}
"@ -ErrorAction SilentlyContinue
    }

    # Helper: read a little-endian uint64 from a byte array at offset $off
    function Read-u64([byte[]]$b, [int]$off) { [System.BitConverter]::ToUInt64($b, $off) }

    try {
        $h = [AvbIoctl]::CreateFile('\\.\IntelAvbFilter',
            [AvbIoctl]::GENERIC_READ -bor [AvbIoctl]::GENERIC_WRITE,
            0, [IntPtr]::Zero, [AvbIoctl]::OPEN_EXISTING,
            [AvbIoctl]::FILE_ATTRIBUTE_NORMAL, [IntPtr]::Zero)
        if ($h -eq [AvbIoctl]::INVALID_HANDLE_VALUE) {
            Write-Host "  [LCY] (driver device not accessible  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }

        # Step 1: ENUM_ADAPTERS(index=0) to learn total adapter count.
        # AVB_ENUM_REQUEST layout: index(u32)+count(u32)+vendor_id(u16)+device_id(u16)+capabilities(u32)+status(u32) = 20 bytes
        $enumBuf = New-Object byte[] 20  # index=0 already zero
        $enumRet = [uint32]0
        $ok = [AvbIoctl]::DeviceIoControlInOut($h, [AvbIoctl]::IOCTL_AVB_ENUM_ADAPTERS,
            $enumBuf, 20, $enumBuf, 20, [ref]$enumRet, [IntPtr]::Zero)
        if (-not $ok -or $enumRet -lt 20) {
            [AvbIoctl]::CloseHandle($h) | Out-Null
            Write-Host "  [LCY] (ENUM_ADAPTERS failed  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }
        $totalAdapters = [System.BitConverter]::ToUInt32($enumBuf, 4)
        if ($totalAdapters -eq 0) {
            [AvbIoctl]::CloseHandle($h) | Out-Null
            Write-Host "  [LCY] (no bound adapters  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }

        # Step 2: For each adapter i: ENUM_ADAPTERS(i) -> OPEN_ADAPTER(i) -> GET_STATISTICS.
        # AVB_OPEN_REQUEST layout: vendor_id(u16)+device_id(u16)+index(u32)+status(u32) = 12 bytes
        # Each OPEN_ADAPTER rebinds the handle context; GET_STATISTICS returns that adapter's stats.
        $adapters = @()
        for ($i = 0; $i -lt $totalAdapters; $i++) {
            # ENUM_ADAPTERS(i)  --  get vendor/device ID for adapter i
            $eBuf = New-Object byte[] 20
            $iBytes = [System.BitConverter]::GetBytes([uint32]$i)
            [System.Array]::Copy($iBytes, 0, $eBuf, 0, 4)
            $eRet = [uint32]0
            $ok = [AvbIoctl]::DeviceIoControlInOut($h, [AvbIoctl]::IOCTL_AVB_ENUM_ADAPTERS,
                $eBuf, 20, $eBuf, 20, [ref]$eRet, [IntPtr]::Zero)
            if (-not $ok -or $eRet -lt 20) { continue }
            $vid = [System.BitConverter]::ToUInt16($eBuf, 8)
            $did = [System.BitConverter]::ToUInt16($eBuf, 10)

            # OPEN_ADAPTER(vid, did, i)  --  bind handle context to adapter i
            $oBuf = New-Object byte[] 12
            $v  = [System.BitConverter]::GetBytes([uint16]$vid)
            $d  = [System.BitConverter]::GetBytes([uint16]$did)
            $ix = [System.BitConverter]::GetBytes([uint32]$i)
            $oBuf[0] = $v[0];  $oBuf[1] = $v[1]
            $oBuf[2] = $d[0];  $oBuf[3] = $d[1]
            $oBuf[4] = $ix[0]; $oBuf[5] = $ix[1]; $oBuf[6] = $ix[2]; $oBuf[7] = $ix[3]
            $oRet = [uint32]0
            $ok = [AvbIoctl]::DeviceIoControlInOut($h, [AvbIoctl]::IOCTL_AVB_OPEN_ADAPTER,
                $oBuf, 12, $oBuf, 12, [ref]$oRet, [IntPtr]::Zero)
            if (-not $ok -or $oRet -lt 12) { continue }

            # GET_STATISTICS  --  read 192-byte AVB_DRIVER_STATISTICS for adapter i
            $sBuf = New-Object byte[] 192
            $sRet = [uint32]0
            $ok = [AvbIoctl]::DeviceIoControlOut($h, [AvbIoctl]::IOCTL_AVB_GET_STATISTICS,
                [IntPtr]::Zero, 0, $sBuf, 192, [ref]$sRet, [IntPtr]::Zero)
            if (-not $ok -or $sRet -lt 192) { continue }

            $adapters += @{
                AdapterIndex           = $i
                VendorId               = $vid
                DeviceId               = $did
                TxPackets              = Read-u64 $sBuf   0
                RxPackets              = Read-u64 $sBuf   8
                IoctlCount             = Read-u64 $sBuf  64
                ErrorCount             = Read-u64 $sBuf  72
                FilterAttachCount      = Read-u64 $sBuf  96
                FilterPauseCount       = Read-u64 $sBuf 104
                FilterRestartCount     = Read-u64 $sBuf 112
                FilterDetachCount      = Read-u64 $sBuf 120
                OutstandingSendNBLs    = Read-u64 $sBuf 128
                OutstandingReceiveNBLs = Read-u64 $sBuf 136
                OidRequestCount        = Read-u64 $sBuf 144
                OidCompleteCount       = Read-u64 $sBuf 152
                OutstandingOids        = Read-u64 $sBuf 160
                PauseRestartGeneration = Read-u64 $sBuf 184
            }
        }

        [AvbIoctl]::CloseHandle($h) | Out-Null
        if ($adapters.Count -eq 0) {
            Write-Host "  [LCY] (no adapter statistics collected  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }
        return @{ Adapters = $adapters }
    } catch {
        Write-Host "  [LCY] (exception in snapshot: $_)" -ForegroundColor DarkGray
        return $null
    }
}

# Print a before/after per-adapter diff of lifecycle metrics (only changed fields shown).
# Colour coding:
#   RED     --  !!UNDERFLOW!! on a gauge (OutstandingXxx wrapped to ~UINT64_MAX)
#   YELLOW  --  ErrorCount / FilterDetachCount increase, or OID dispatch imbalance
#   GRAY    --  normal counter activity
#
# Phase B: Returns [bool] $true if any anomaly was detected so callers can emit [WARN:LCY].
# Anomaly conditions:
#   - UNDERFLOW on any gauge (OutstandingSendNBLs / OutstandingReceiveNBLs / OutstandingOids)
#   - ErrorCount increased during the test
#   - FilterDetachCount increased during the test (unexpected adapter detach)
#   - OidRequestCount delta != OidCompleteCount delta (OID pipeline drain incomplete)
function Write-LifecycleDiff {
    param($Before, $After, [string]$Label)
    if ($null -eq $Before -or $null -eq $After) {
        # Snapshot messages already printed by Get-AvbLifecycleSnapshot; no diff available
        return $false
    }

    # Ordered list of stats fields to diff (excludes identity keys AdapterIndex/VendorId/DeviceId)
    $statsFields = @(
        'TxPackets','RxPackets','IoctlCount','ErrorCount',
        'FilterAttachCount','FilterPauseCount','FilterRestartCount','FilterDetachCount',
        'OutstandingSendNBLs','OutstandingReceiveNBLs',
        'OidRequestCount','OidCompleteCount','OutstandingOids','PauseRestartGeneration'
    )

    $hasAnomalies = $false

    foreach ($afterAdp in $After.Adapters) {
        $idx      = $afterAdp.AdapterIndex
        $beforeAdp = $Before.Adapters | Where-Object { $_.AdapterIndex -eq $idx }
        if (-not $beforeAdp) { continue }

        $adpTag = 'Adapter{0}[VEN_{1:X4}:DEV_{2:X4}]' -f $idx, $afterAdp.VendorId, $afterAdp.DeviceId

        # Gauge fields represent "in-flight" counts and should always be near 0.
        # Underflow = UINT64 wraparound: a gauge that was 0 decremented → wraps to ~UINT64_MAX.
        # We detect this as: gauge field AND after-value > 1,000,000 (physically impossible in-flight count).
        # NOTE: Do NOT use the hex literal 0xFFFFFFF000000000 here — in PS5.1 that overflows Int64
        #       and becomes negative, causing ALL UInt64 comparisons to evaluate as true (false positive).
        $gaugeFields = @('OutstandingSendNBLs', 'OutstandingReceiveNBLs', 'OutstandingOids')

        # Build list of changed fields for this adapter
        $changed = @()
        foreach ($key in $statsFields) {
            $bVal = $beforeAdp[$key]
            $aVal = $afterAdp[$key]
            if ($aVal -ne $bVal) {
                # Underflow only possible on gauge fields; threshold of 1,000,000 is well above any
                # sane in-flight count but safely below a wrapped UINT64 value (> 1.8e19).
                $underflow = ($key -in $gaugeFields) -and ($aVal -gt [uint64]1000000)
                $flag  = if ($underflow) { ' !!UNDERFLOW!!' } else { '' }
                $delta = if ($aVal -ge $bVal) { "+$($aVal - $bVal)" } else { "-$($bVal - $aVal)$flag" }
                $changed += [PSCustomObject]@{
                    Key       = $key
                    Before    = $bVal
                    After     = $aVal
                    Delta     = $delta
                    Underflow = $underflow
                }
            }
        }

        # Phase B: anomaly detection for this adapter
        foreach ($c in $changed) {
            if ($c.Underflow) { $hasAnomalies = $true }
            elseif ($c.Key -in 'ErrorCount','FilterDetachCount' -and $c.After -gt $c.Before) {
                $hasAnomalies = $true
            }
        }
        # OID dispatch/complete imbalance: OIDs sent but not all drained during this test window
        $oidReq = $changed | Where-Object { $_.Key -eq 'OidRequestCount' }
        $oidCmp = $changed | Where-Object { $_.Key -eq 'OidCompleteCount' }
        $oidImbalance = $false
        if ($oidReq -and $oidCmp) {
            $dReq = $oidReq.After - $oidReq.Before
            $dCmp = $oidCmp.After - $oidCmp.Before
            if ($dReq -ne $dCmp) { $oidImbalance = $true; $hasAnomalies = $true }
        }

        if ($changed.Count -eq 0) {
            Write-Host "  [LCY] $Label ($adpTag)  --  no lifecycle metric changes" -ForegroundColor DarkGray
        } else {
            Write-Host "  [LCY] $Label ($adpTag)  --  lifecycle metric delta:" -ForegroundColor DarkGray
            foreach ($c in $changed) {
                $line  = '    {0,-28} {1,20} -> {2,20}  ({3})' -f $c.Key, $c.Before, $c.After, $c.Delta
                $color = if ($c.Underflow) { 'Red' }
                         elseif ($c.Key -in 'ErrorCount','FilterDetachCount') { 'Yellow' }
                         else { 'DarkGray' }
                Write-Host $line -ForegroundColor $color
            }
            if ($oidImbalance) {
                $dReq = $oidReq.After - $oidReq.Before
                $dCmp = $oidCmp.After - $oidCmp.Before
                Write-Host "    !OID imbalance: +$dReq dispatched vs +$dCmp completed" -ForegroundColor Yellow
            }
        }
    }

    return $hasAnomalies
}
