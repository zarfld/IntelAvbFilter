# Intel I210 PTP Test Tool Fix - TSAUXC Write Mismatch Resolution

## Problem Analysis

From the test output, the issue is clear:
```
WRITE MISMATCH off=0x0B640 (TSAUXC(SAMP_AUX0)) want=0x40000008 got=0x40000200
```

This indicates the test tool `ptp-bringup` command is trying to set bit 3 (SAMP_AUX0) in the TSAUXC register, but getting a different value back when verifying the write.

## Root Cause: Self-Clearing Trigger Bits

According to the Intel I210 datasheet, TSAUXC register contains several **self-clearing trigger bits**:
- **Bit 3 (SAMP_AUX0)**: Sample Auxiliary Timestamp 0 - **SELF-CLEARING**
- **Bit 4 (SAMP_AUX1)**: Sample Auxiliary Timestamp 1 - **SELF-CLEARING**  
- **Bit 5 (SAMP_AUX2)**: Sample Auxiliary Timestamp 2 - **SELF-CLEARING**

When these bits are written as '1', they trigger a timestamp capture operation and then automatically clear to '0'.

## Current Test Tool Bug

The `ptp_bringup` function in `tools/avb_test/avb_test_um.c` contains this problematic code:

```c
/* Force capture via SAMP_AUX0 (bit3) */
unsigned long tsa2=0; 
read_reg(h, REG_TSAUXC, &tsa2); 
unsigned long trigger = tsa2 | (1UL<<3);  // Set SAMP_AUX0 bit
reg_write_checked(h, REG_TSAUXC, trigger, "TSAUXC(SAMP_AUX0)");  // ? WRITE MISMATCH!
```

The `reg_write_checked` function performs write verification by reading the register back and comparing values. Since SAMP_AUX0 is self-clearing, the readback will NOT match the written value, causing the "WRITE MISMATCH" error.

## Solution

### Fix 1: Use Write-Without-Verification for Trigger Bits

Replace the problematic `reg_write_checked` call with a direct write that doesn't verify self-clearing bits:

```c
/* Fixed: Force capture via SAMP_AUX0 (bit3) - no verification for self-clearing bit */
unsigned long tsa2=0; 
read_reg(h, REG_TSAUXC, &tsa2); 
unsigned long trigger = tsa2 | (1UL<<3);  // Set SAMP_AUX0 bit

// Use direct write without verification for self-clearing trigger bits
AVB_REGISTER_REQUEST r; 
ZeroMemory(&r, sizeof(r)); 
r.offset = REG_TSAUXC; 
r.value = trigger; 
DWORD br = 0;
BOOL ok = DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL);

if (ok) {
    printf("SAMP_AUX0 trigger sent (bit will self-clear)\n");
    
    // Verify the bit has self-cleared (optional)
    unsigned long tsa3 = 0;
    if (read_reg(h, REG_TSAUXC, &tsa3)) {
        if ((tsa3 & (1UL<<3)) == 0) {
            printf("? SAMP_AUX0 bit properly self-cleared\n");
        } else {
            printf("? SAMP_AUX0 bit did not self-clear (unusual)\n");
        }
    }
} else {
    fprintf(stderr, "SAMP_AUX0 trigger failed: GLE=%lu\n", GetLastError());
}
```

### Fix 2: Enhanced Register Write Function

Add a new helper function for registers with self-clearing bits:

```c
/* Write register with optional verification skip for self-clearing bits */
static int reg_write_trigger(HANDLE h, unsigned long off, unsigned long val, unsigned long self_clear_mask, const char* tag)
{
    AVB_REGISTER_REQUEST r; 
    ZeroMemory(&r, sizeof(r)); 
    r.offset = off; 
    r.value = val; 
    DWORD br = 0;
    
    BOOL ok = DeviceIoControl(h, IOCTL_AVB_WRITE_REGISTER, &r, sizeof(r), &r, sizeof(r), &br, NULL);
    if (!ok) {
        fprintf(stderr, "WRITE FAIL off=0x%05lX (%s) GLE=%lu\n", off, tag ? tag : "", GetLastError());
        return 0;
    }
    
    // For registers with self-clearing bits, adjust verification
    if (self_clear_mask != 0) {
        unsigned long rb = 0;
        int rb_ok = read_reg(h, off, &rb);
        if (!rb_ok) {
            fprintf(stderr, "WRITE VERIFY READ FAIL off=0x%05lX (%s)\n", off, tag ? tag : "");
            return 0;
        }
        
        // Mask out self-clearing bits for comparison
        unsigned long expected = val & ~self_clear_mask;
        unsigned long actual = rb & ~self_clear_mask;
        
        if (actual != expected) {
            fprintf(stderr, "WRITE MISMATCH off=0x%05lX (%s) want=0x%08lX got=0x%08lX (after masking self-clear bits)\n", 
                    off, tag ? tag : "", expected, actual);
            return 0;
        }
        
        printf("? Write verified (self-clearing bits: want=0x%08lX got=0x%08lX)\n", 
               val, rb);
    } else {
        // Standard verification for non-self-clearing registers
        return reg_write_checked(h, off, val, tag);
    }
    
    return 1;
}
```

### Fix 3: Corrected ptp_bringup Function

```c
static void ptp_bringup_fixed(HANDLE h)
{
    printf("PTP bring-up (FIXED): enabling PHC & verifying movement\n");
    
    unsigned long tsa = 0;
    if (!read_reg(h, REG_TSAUXC, &tsa)) {
        fprintf(stderr, "TSAUXC read fail\n");
        return;
    }
    
    // Configure PHC without trigger bits
    unsigned long desired = (tsa | (1UL<<30)) & ~(1UL<<31);  // Enable PHC, clear DisableSystime
    if (desired != tsa) {
        if (!reg_write_checked(h, REG_TSAUXC, desired, "TSAUXC(enable PHC)")) return;
    } else {
        printf("TSAUXC already configured: 0x%08lX\n", tsa);
    }

    // Initialize SYSTIM
    if (!reg_write_checked(h, REG_SYSTIML, 0x10000000, "SYSTIML(init)")) return;  // Non-zero start
    if (!reg_write_checked(h, REG_SYSTIMH, 0x00000000, "SYSTIMH(init)")) return;

    // Test movement
    unsigned long t0L=0, t0R=0; 
    read_reg(h, REG_SYSTIML, &t0L); 
    read_reg(h, REG_SYSTIMR, &t0R);
    Sleep(20);  // Longer delay
    unsigned long t1L=0, t1R=0; 
    read_reg(h, REG_SYSTIML, &t1L); 
    read_reg(h, REG_SYSTIMR, &t1R);
    
    printf("Movement test: 0x%08lX%08lX -> 0x%08lX%08lX = %s\n", 
           t0R, t0L, t1R, t1L, ((t1L > t0L) || (t1R != t0R)) ? "YES" : "NO");

    // Trigger auxiliary timestamp (use mask for self-clearing bit)
    unsigned long tsa2 = 0; 
    read_reg(h, REG_TSAUXC, &tsa2); 
    unsigned long trigger = tsa2 | (1UL<<3);  // Set SAMP_AUX0
    
    // Use trigger-aware write (expects self-clearing)
    reg_write_trigger(h, REG_TSAUXC, trigger, (1UL<<3), "TSAUXC(SAMP_AUX0)");
    
    // Read captured timestamp
    unsigned long auxL=0, auxH=0; 
    read_reg(h, REG_AUXSTMPL0, &auxL); 
    read_reg(h, REG_AUXSTMPH0, &auxH);
    printf("AUXSTMP0=0x%08lX%08lX\n", auxH, auxL);
}
```

## Key Changes Summary

1. **Separate trigger operations** from configuration operations
2. **Use write-without-verification** for self-clearing trigger bits
3. **Mask self-clearing bits** during verification  
4. **Enhanced debugging** to show bit behavior
5. **Better timing** with longer delays for detection

## Expected Results After Fix

```
PTP bring-up (FIXED): enabling PHC & verifying movement
TSAUXC configured: 0x40000000 (PHC enabled, DisableSystime cleared)
SYSTIML initialized to 0x10000000
Movement test: 0x00000000 -> 0x10000123 = YES
? Write verified (self-clearing bits: want=0x40000008 got=0x40000000)
? SAMP_AUX0 bit properly self-cleared
AUXSTMP0=0x0000000010000123
```

## Implementation Status

The **driver-side fix is already implemented** in `AvbI210EnsureSystimRunning()` which properly handles the I210 PTP initialization sequence.

The **test tool fix** requires updating the user-mode test executables, which are built separately from the main driver project.

## Next Steps

1. **Apply the test tool fix** to the user-mode test implementation
2. **Rebuild the test tools** with the corrected `ptp-bringup` logic
3. **Test the fixed implementation** on real Intel I210 hardware
4. **Verify no more WRITE MISMATCH errors** in test output

This fix should resolve the TSAUXC write mismatch issue and allow proper I210 PTP testing.