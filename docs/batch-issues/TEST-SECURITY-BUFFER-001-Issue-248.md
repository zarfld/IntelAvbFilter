# TEST-SECURITY-BUFFER-001: Verify Buffer Overflow Protection

**Issue**: #248  
**Type**: Test Case (TEST)  
**Priority**: P1 (Critical Security)  
**Status**: In Progress  
**Phase**: 07 - Verification & Validation

## Test Case Information

**Test ID**: TEST-SECURITY-BUFFER-001  
**Test Type**: Security Test  
**Priority**: P1 (Critical Security)  
**Test Level**: Unit + Integration

## Objective

Verify that the driver implements buffer overflow protection for all IOCTL input/output buffers, preventing security vulnerabilities.

## Traceability

- Traces to: #89 (REQ-NF-SECURITY-BUFFER-001: Buffer Overflow Protection (Security Critical))

## Test Scope

This test verifies:
1. All IOCTL buffer lengths validated before use
2. Overflow attempts rejected with proper error codes
3. Safe buffer copy operations used (ProbeForRead/Write, RtlCopyMemory with bounds)
4. No buffer overruns detected by Driver Verifier

## Preconditions

- Driver installed with Driver Verifier enabled (Pool Tracking + Special Pool)
- Test application with IOCTL sending capability
- Debug build with ASSERT checks enabled
- Administrator privileges for IOCTL access

## Test Steps

### Setup
1. Enable Driver Verifier for IntelAvbFilter.sys:
   ```
   verifier /standard /driver IntelAvbFilter.sys
   ```
2. Reboot system
3. Compile test harness application (send IOCTLs with crafted buffers)

### Execution

**Test 1: Zero-Length Buffer**
1. Send IOCTL with InputBufferLength = 0 (but non-NULL buffer pointer)
2. Send IOCTL with OutputBufferLength = 0 (but non-NULL buffer pointer)
3. Verify driver returns STATUS_INVALID_PARAMETER
4. Confirm no bugcheck or crash

**Test 2: Undersized Input Buffer**
1. Send IOCTL_AVB_PHC_GETTIME with InputBufferLength = 4 (expected: 16+ bytes)
2. Verify driver returns STATUS_BUFFER_TOO_SMALL
3. Send IOCTL_AVB_SET_CBS with truncated CBS structure
4. Verify driver rejects with STATUS_INVALID_BUFFER_SIZE

**Test 3: Oversized Output Buffer**
1. Send IOCTL_AVB_PHC_GETTIME with OutputBufferLength = 1MB (expected: 16 bytes)
2. Verify driver accepts but only writes expected bytes
3. Confirm no overflow beyond expected output size
4. Use Driver Verifier to detect any overflow writes

**Test 4: NULL Buffer Pointers**
1. Send IOCTL with InputBuffer = NULL (but InputBufferLength > 0)
2. Send IOCTL with OutputBuffer = NULL (but OutputBufferLength > 0)
3. Verify driver returns STATUS_INVALID_PARAMETER
4. Confirm no null pointer dereference (no bugcheck)

**Test 5: Integer Overflow in Length Calculation**
1. Send IOCTL with InputBufferLength = 0xFFFFFFFF (max ULONG)
2. Send IOCTL with OutputBufferLength = 0x80000000 (2GB)
3. Verify driver detects overflow and rejects
4. Confirm no arithmetic overflow in size calculations

**Test 6: Boundary Conditions**
1. Send IOCTL with exact minimum required buffer size
2. Send IOCTL with exact maximum allowed buffer size
3. Verify both cases handled correctly (no off-by-one errors)
4. Confirm Driver Verifier reports no boundary violations

**Test 7: Unaligned Buffers**
1. Send IOCTL with buffer address not 4-byte aligned
2. Verify driver handles unaligned access safely (no crash)
3. Confirm correct data copied despite alignment issues

**Test 8: ProbeForRead/Write Validation**
1. Send IOCTL from user-mode with invalid user-mode buffer address
2. Verify driver calls ProbeForRead/ProbeForWrite correctly
3. Confirm STATUS_ACCESS_VIOLATION returned (not bugcheck)

### Cleanup
1. Disable Driver Verifier:
   ```
   verifier /reset
   ```
2. Reboot system

## Expected Results

### Pass Criteria
- ✅ All zero-length buffers rejected with STATUS_INVALID_PARAMETER
- ✅ All undersized buffers rejected with STATUS_BUFFER_TOO_SMALL
- ✅ Oversized buffers accepted but only expected bytes written
- ✅ NULL pointers rejected without crashing
- ✅ Integer overflow attempts detected and rejected
- ✅ Boundary conditions (exact sizes) handled correctly
- ✅ Unaligned buffers handled safely
- ✅ ProbeForRead/Write detects invalid user-mode addresses
- ✅ Driver Verifier reports 0 violations (no buffer overruns)
- ✅ No bugchecks or crashes during any test

### Fail Criteria
- ❌ Buffer overflow detected by Driver Verifier
- ❌ Driver writes beyond output buffer boundaries
- ❌ Null pointer dereference causes bugcheck
- ❌ Integer overflow leads to incorrect size calculation
- ❌ Driver crashes on invalid buffer sizes
- ❌ ProbeForRead/Write not used for user-mode buffers

## Test Environment

**Hardware**:
- Intel i210/i225/i226 NIC

**Software**:
- Windows 10/11 (with Driver Verifier support)
- Debug build of IntelAvbFilter.sys (with ASSERT checks)
- WinDbg (kernel debugger) for crash analysis

**Tools**:
- Driver Verifier (verifier.exe)
- Test harness application (custom IOCTL sender)
- WinDbg (https://learn.microsoft.com/windows-hardware/drivers/debugger/)

## Automation

**Automation Status**: Fully Automated

**Automated Tests**:
1. Fuzzing harness sends random buffer sizes/pointers
2. Driver Verifier enabled in CI environment
3. Automated IOCTL test suite covers all boundary cases
4. WinDbg script detects bugchecks and parses crash dumps

## Test Data

**Test IOCTLs** (from avb_ioctl.h):
- IOCTL_AVB_PHC_GETTIME (0x40 - IOCTL 16): Expected input 0, output 16 bytes
- IOCTL_AVB_PHC_SETTIME (0x44 - IOCTL 17): Expected input 16, output 0 bytes
- IOCTL_AVB_SET_CBS (0x8C - IOCTL 35): Expected input 24, output 0 bytes
- IOCTL_AVB_SET_TAS (0x68 - IOCTL 26): Expected input variable, output 0 bytes

**Invalid Buffer Scenarios**:
| Test Case | InputBuffer | InputLength | OutputBuffer | OutputLength | Expected Result |
|-----------|-------------|-------------|--------------|--------------|-----------------|
| Null Input | NULL | 16 | Valid | 16 | STATUS_INVALID_PARAMETER |
| Zero Length | Valid | 0 | Valid | 16 | STATUS_INVALID_PARAMETER |
| Undersized | Valid | 4 | Valid | 16 | STATUS_BUFFER_TOO_SMALL |
| Oversized | Valid | 16 | Valid | 1MB | STATUS_SUCCESS (write 16 bytes only) |
| Integer Overflow | Valid | 0xFFFFFFFF | Valid | 16 | STATUS_INVALID_PARAMETER |

## Notes

- **Driver Verifier**: MUST be enabled during testing (Special Pool detects buffer overruns)
- **ProbeForRead/Write**: Required for user-mode buffers (IOCTL_TYPE3_INPUT_BUFFER)
- **Safe Copy Functions**: Use RtlCopyMemory with explicit size checks (never strcpy/memcpy without bounds)
- **Security Impact**: Buffer overflows can lead to privilege escalation or kernel code execution

## References

- REQ-NF-SECURITY-BUFFER-001 (Issue #89)
- Microsoft Driver Verifier: https://learn.microsoft.com/windows-hardware/drivers/devtest/driver-verifier
- ProbeForRead/Write: https://learn.microsoft.com/windows-hardware/drivers/ddi/wdm/nf-wdm-probeforread
- Safe String Functions: https://learn.microsoft.com/windows-hardware/drivers/kernel/using-safe-string-functions

---

**Created**: 2025-12-19  
**Status**: Draft  
**Assigned To**: Security Team
