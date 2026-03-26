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

    try {
        $h = [AvbIoctl]::CreateFile('\\.\IntelAvbFilter',
            [AvbIoctl]::GENERIC_READ -bor [AvbIoctl]::GENERIC_WRITE,
            0, [IntPtr]::Zero, [AvbIoctl]::OPEN_EXISTING,
            [AvbIoctl]::FILE_ATTRIBUTE_NORMAL, [IntPtr]::Zero)
        if ($h -eq [AvbIoctl]::INVALID_HANDLE_VALUE) {
            Write-Host "  [LCY] (driver device not accessible  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }

        # Step 1: ENUM_ADAPTERS  --  get vendor/device ID for adapter 0
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
        $adapterCount = [System.BitConverter]::ToUInt32($enumBuf, 4)
        if ($adapterCount -eq 0) {
            [AvbIoctl]::CloseHandle($h) | Out-Null
            Write-Host "  [LCY] (no bound adapters  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }
        $vendorId = [System.BitConverter]::ToUInt16($enumBuf, 8)
        $deviceId = [System.BitConverter]::ToUInt16($enumBuf, 10)

        # Step 2: OPEN_ADAPTER  --  bind handle to adapter 0
        # AVB_OPEN_REQUEST layout: vendor_id(u16)+device_id(u16)+index(u32)+status(u32) = 12 bytes
        $openBuf = New-Object byte[] 12
        $v = [System.BitConverter]::GetBytes([uint16]$vendorId)
        $d = [System.BitConverter]::GetBytes([uint16]$deviceId)
        $openBuf[0] = $v[0]; $openBuf[1] = $v[1]
        $openBuf[2] = $d[0]; $openBuf[3] = $d[1]
        # index=0 at offset 4 (already zero)
        $openRet = [uint32]0
        $ok = [AvbIoctl]::DeviceIoControlInOut($h, [AvbIoctl]::IOCTL_AVB_OPEN_ADAPTER,
            $openBuf, 12, $openBuf, 12, [ref]$openRet, [IntPtr]::Zero)
        if (-not $ok -or $openRet -lt 12) {
            [AvbIoctl]::CloseHandle($h) | Out-Null
            Write-Host "  [LCY] (OPEN_ADAPTER failed  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }

        # Step 3: GET_STATISTICS  --  read 192-byte stats for adapter 0
        $buf = New-Object byte[] 192
        $returned = [uint32]0
        $ok = [AvbIoctl]::DeviceIoControlOut($h, [AvbIoctl]::IOCTL_AVB_GET_STATISTICS,
            [IntPtr]::Zero, 0, $buf, 192, [ref]$returned, [IntPtr]::Zero)
        [AvbIoctl]::CloseHandle($h) | Out-Null
        if (-not $ok -or $returned -lt 192) {
            Write-Host "  [LCY] (GET_STATISTICS failed  --  snapshot skipped)" -ForegroundColor DarkGray
            return $null
        }

        # Parse AVB_DRIVER_STATISTICS fields (little-endian uint64, avb_ioctl.h offsets)
        function u64([byte[]]$b,[int]$off){ [System.BitConverter]::ToUInt64($b,$off) }
        return @{
            TxPackets              = u64 $buf   0
            RxPackets              = u64 $buf   8
            IoctlCount             = u64 $buf  64
            ErrorCount             = u64 $buf  72
            FilterAttachCount      = u64 $buf  96
            FilterPauseCount       = u64 $buf 104
            FilterRestartCount     = u64 $buf 112
            FilterDetachCount      = u64 $buf 120
            OutstandingSendNBLs    = u64 $buf 128
            OutstandingReceiveNBLs = u64 $buf 136
            OidRequestCount        = u64 $buf 144
            OidCompleteCount       = u64 $buf 152
            OutstandingOids        = u64 $buf 160
            PauseRestartGeneration = u64 $buf 184
        }
    } catch {
        Write-Host "  [LCY] (exception in snapshot: $_)" -ForegroundColor DarkGray
        return $null
    }
}

# Print a before/after diff of lifecycle metrics (only changed fields shown).
# Colour coding:
#   RED     --  !!UNDERFLOW!! on a gauge (OutstandingXxx wrapped to ~UINT64_MAX)
#   YELLOW  --  ErrorCount / FilterDetachCount / HardwareFaults increased
#   GRAY    --  normal counter activity
function Write-LifecycleDiff {
    param($Before, $After, [string]$Label)
    if ($null -eq $Before -or $null -eq $After) {
        # Snapshot messages already printed by Get-AvbLifecycleSnapshot; no diff available
        return
    }

    $changed = @()
    foreach ($key in $After.Keys | Sort-Object) {
        $bVal = $Before[$key]
        $aVal = $After[$key]
        if ($aVal -ne $bVal) {
            # Highlight gauge underflows (wraps to near UINT64_MAX)
            $flag = if ($aVal -gt 0xFFFFFFF000000000 -and $bVal -lt $aVal) { ' !!UNDERFLOW!!' } else { '' }
            $delta = if ($aVal -ge $bVal) { "+$($aVal - $bVal)" } else { "-$($bVal - $aVal)$flag" }
            $changed += "    {0,-28} {1,20} -> {2,20}  ({3})" -f $key, $bVal, $aVal, $delta
        }
    }
    if ($changed.Count -eq 0) {
        Write-Host "  [LCY] $Label  --  no lifecycle metric changes" -ForegroundColor DarkGray
    } else {
        Write-Host "  [LCY] $Label  --  lifecycle metric delta:" -ForegroundColor DarkGray
        foreach ($line in $changed) {
            $color = if ($line -match 'UNDERFLOW') { 'Red' } elseif ($line -match 'Error|Fault|Detach') { 'Yellow' } else { 'DarkGray' }
            Write-Host $line -ForegroundColor $color
        }
    }
}
