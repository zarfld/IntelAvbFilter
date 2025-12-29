/*++

Module Name:

    debug_setup_guide.c

Abstract:

    Debug output setup guide for Intel AVB Filter Driver on Windows 11
    Shows exactly how to capture and interpret debug messages

--*/

/*

=== WINDOWS 11 DEBUG OUTPUT SETUP GUIDE ===

Step 1: Download DebugView
- Go to: https://docs.microsoft.com/en-us/sysinternals/downloads/debugview
- Download DebugView.exe (no installation needed)
- Run DebugView.exe as Administrator

Step 2: Configure DebugView for Kernel Messages
- Options ? "Capture Kernel" (check this)
- Options ? "Enable Verbose Kernel Output" (check this)
- Options ? "Capture Events" (check this)
- Edit ? "Filter/Highlight" ? Add filter for "Avb*" to highlight our messages

Step 3: Expected Debug Messages for I219 Testing

=== SUCCESS PATTERN (Real Hardware Working) ===
[TRACE] ==>AvbInitializeDevice: Transitioning to real hardware access
[TRACE] ==>AvbDiscoverIntelControllerResources
[INFO]  Intel controller resources discovered: VID=0x8086, DID=0x15B7
[INFO]  BAR0 Address: 0xf7a00000, Length: 0x20000
[TRACE] ==>AvbMapIntelControllerMemory: PA=0xf7a00000, Length=0x20000
[TRACE] <==AvbMapIntelControllerMemory: Success, VA=0xfffff8a000f40000
[INFO]  Real hardware access enabled: BAR0=0xf7a00000, Length=0x20000
[TRACE] AvbMmioReadReal: offset=0x00000, value=0x48100248 (REAL HARDWARE)
[TRACE] AvbMmioReadReal: offset=0x00008, value=0x80080783 (REAL HARDWARE)
[TRACE] AvbMmioReadReal: offset=0x15F84, value=0x12AB34CD (REAL HARDWARE)

=== FAILURE PATTERN (Still Simulated) ===
[TRACE] ==>AvbInitializeDevice: Transitioning to real hardware access
[TRACE] ==>AvbDiscoverIntelControllerResources
[ERROR] Failed to query PCI configuration: 0xC00000BB
[ERROR] Failed to discover Intel controller resources: 0xC0000001
[WARN]  AvbMmioReadReal: No hardware mapping, using Intel spec simulation
[TRACE] AvbMmioReadReal: offset=0x00000, value=0x48100248 (Intel spec-based)
[TRACE] AvbMmioReadReal: offset=0x00008, value=0x80080783 (Intel spec-based)

=== DIAGNOSTIC MEANINGS ===

? GOOD SIGNS:
- "Real hardware access enabled"
- "(REAL HARDWARE)" in register reads
- Actual BAR0 physical addresses (not 0x00000000)
- Virtual addresses starting with 0xfffff8...

? PROBLEM SIGNS:
- "Failed to query PCI configuration"
- "No hardware mapping"
- "(Intel spec-based)" or "(SIMULATED)"
- BAR0 address is 0x00000000

?? TROUBLESHOOTING:
- If you see PCI configuration errors: Check if driver has proper permissions
- If BAR0 discovery fails: I219 might be using different OID requests
- If MMIO mapping fails: Windows might be blocking direct hardware access

=== I219-SPECIFIC DEBUG PATTERNS ===

I219 uses different register offsets than I210:
- I210 Timestamp: 0x0B600/0x0B604
- I219 Timestamp: 0x15F84/0x15F88 (this is what you should see)

Look for these I219-specific messages:
[TRACE] AvbMdioReadI219DirectReal: phy=0x2, reg=0x02, value=0x0141 (I219 spec-based)
[INFO]  AvbReadTimestampReal: I219 hardware timestamp low=0x..., high=0x...

*/

#include <stdio.h>

int main() {
    printf("This file contains debug setup instructions in comments above.\n");
    printf("Compile with: cl debug_setup_guide.c\n");
    printf("Then read the comments in the source file for detailed debug setup.\n");
    return 0;
}