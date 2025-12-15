---
specType: component-design
standard: IEEE 1016-2009
component: IOCTL Dispatcher (ARC-C-IOCTL-005)
phase: 04-design
version: 1.0.0
author: Design Team
date: "2025-12-15"
status: ✅ approved
formal_review: DES-REVIEW-001 (2025-12-15)
approval_decision: APPROVED WITHOUT CONDITIONS
traceability:
  architecture: "#142 (ARC-C-IOCTL-005: IOCTL Dispatcher)"
  requirements:
    - "#30 (REQ-F-IOCTL-001: Centralized IOCTL Handler)"
    - "#91 (REQ-NF-SEC-IOCTL-001: IOCTL Security Validation)"
    - "#92 (REQ-NF-PERF-IOCTL-001: IOCTL Performance <10µs)"
  adrs:
    - "#ADR-PERF-SEC-001 (Performance vs Security Trade-offs)"
    - "#ADR-ARCH-001 (Layered Architecture Pattern)"
---

# DES-C-IOCTL-005: IOCTL Dispatcher - Detailed Component Design

**Component**: IOCTL Dispatcher (Centralized Validation and Routing)  
**Architecture Reference**: GitHub Issue #142 (ARC-C-IOCTL-005)  
**Phase**: 04-design (Detailed Design per IEEE 1016-2009)  
**XP Practice**: Simple Design (YAGNI), Test-First Design (TDD-ready interfaces)

---

## 1. Component Overview

### 1.1 Purpose

The IOCTL Dispatcher is the **security boundary and central validation gateway** for all user-mode → kernel-mode IOCTL requests. It provides:

- **Validation Pipeline**: 3-stage validation (buffer → security → state)
- **Routing Table**: Fast O(1) dispatch to 40+ IOCTL handlers
- **Error Handling**: Centralized error logging with event IDs
- **Performance**: <10µs total IOCTL latency (p95)
- **Security**: Whitelist-based IOCTL filtering, privilege checks, rate limiting

**Design Philosophy**: "Fail fast, log everything, no surprises"

---

### 1.2 Responsibilities

| Responsibility | Details |
|---------------|---------|
| **Buffer Validation** | Input/output buffer size checks, alignment validation, null pointer guards, structure version checks |
| **Security Validation** | IOCTL code whitelist, privilege checks (admin-only IOCTLs), rate limiting, audit logging |
| **State Validation** | Adapter context availability, hardware state checks, capability verification |
| **Routing** | Fast dispatch to handler functions via function pointer table |
| **Error Handling** | Centralized error logging, NTSTATUS mapping, diagnostic counters |
| **ABI Versioning** | Handle `AVB_REQUEST_HEADER.abi_version` mismatch detection |

---

### 1.3 Constraints

**Performance Constraints** (from #106 QA-SC-PERF-001):
- Total IOCTL latency: <10µs (p95), <50µs (p99)
- Validation overhead: <500ns per stage
- Fast path (read-only IOCTLs): <10ns overhead

**Security Constraints** (from #91 REQ-NF-SEC-IOCTL-001):
- All IOCTLs must pass validation pipeline
- No direct handler calls without validation
- Audit logging for sensitive operations (register writes, config changes)

**Design Constraints**:
- IRQL: Run at PASSIVE_LEVEL (IRP_MJ_DEVICE_CONTROL)
- Synchronization: No locks in validation pipeline (read-only checks)
- Test-First: All interfaces mockable for TDD

---

## 2. Interface Definitions

### 2.1 Primary Entry Point

**Function**: `AvbHandleIoctl()`

```c
/**
 * Primary IOCTL dispatcher entry point.
 * Called from FilterDeviceControl() in device.c.
 * 
 * @param DeviceContext - Device context (contains adapter context registry)
 * @param Irp           - I/O Request Packet from I/O Manager
 * 
 * @return NTSTATUS
 *   - STATUS_SUCCESS: IOCTL handled successfully
 *   - STATUS_INVALID_PARAMETER: Buffer validation failed
 *   - STATUS_ACCESS_DENIED: Security validation failed
 *   - STATUS_DEVICE_NOT_READY: State validation failed
 *   - STATUS_NOT_SUPPORTED: IOCTL code not in whitelist
 *   - STATUS_PENDING: Async operation started (never for current design)
 * 
 * Preconditions:
 *   - DeviceContext != NULL
 *   - Irp != NULL
 *   - Irp->MajorFunction == IRP_MJ_DEVICE_CONTROL
 *   - IRQL == PASSIVE_LEVEL
 * 
 * Postconditions:
 *   - Irp->IoStatus.Status set to result
 *   - Irp->IoStatus.Information set to bytes written (if success)
 *   - Error logged via AVB_LOG_ERROR() if failure
 *   - IRP NOT completed (caller must complete)
 * 
 * Invariants:
 *   - Validation pipeline executes in order: Buffer → Security → State
 *   - Handlers never called without successful validation
 *   - All errors logged with event ID + context
 */
NTSTATUS
AvbHandleIoctl(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _Inout_ PIRP Irp
);
```

---

### 2.2 Validation Pipeline Interfaces

#### 2.2.1 Buffer Validator

```c
/**
 * Validate IOCTL buffer sizes and structure integrity.
 * 
 * @param IoControlCode - IOCTL code from IRP
 * @param InputBuffer   - Pointer to input buffer (SystemBuffer)
 * @param InputLength   - Size of input buffer in bytes
 * @param OutputBuffer  - Pointer to output buffer (same as SystemBuffer)
 * @param OutputLength  - Size of output buffer in bytes
 * 
 * @return NTSTATUS
 *   - STATUS_SUCCESS: Buffers valid
 *   - STATUS_INVALID_PARAMETER: Size mismatch or NULL pointer
 *   - STATUS_BUFFER_TOO_SMALL: Output buffer insufficient
 * 
 * Preconditions:
 *   - IoControlCode in [IOCTL_AVB_MIN, IOCTL_AVB_MAX]
 *   - InputBuffer != NULL if InputLength > 0
 *   - OutputBuffer != NULL if OutputLength > 0
 * 
 * Postconditions:
 *   - If SUCCESS: Buffers meet minimum size requirements
 *   - If FAILURE: Error logged with AVB_LOG_ERROR(3001, "Buffer validation failed")
 * 
 * Performance: <100ns (inline checks, no loops)
 */
NTSTATUS
AvbValidateIoctlBuffers(
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength
);
```

**Buffer Size Table** (compile-time constant):

```c
typedef struct _IOCTL_BUFFER_REQUIREMENTS {
    ULONG IoControlCode;
    ULONG MinInputSize;   // Minimum input buffer size (0 = not required)
    ULONG MinOutputSize;  // Minimum output buffer size (0 = not required)
    BOOLEAN InputRequired;  // TRUE = input buffer MUST be non-NULL
    BOOLEAN OutputRequired; // TRUE = output buffer MUST be non-NULL
} IOCTL_BUFFER_REQUIREMENTS, *PIOCTL_BUFFER_REQUIREMENTS;

// Compile-time table (avb_ioctl_validation.c)
static const IOCTL_BUFFER_REQUIREMENTS g_IoctlBufferTable[] = {
    // IOCTL Code                      MinIn  MinOut  InReq  OutReq
    { IOCTL_AVB_INIT_DEVICE,           0,     0,      FALSE, FALSE },
    { IOCTL_AVB_GET_DEVICE_INFO,       4,     sizeof(AVB_DEVICE_INFO_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_READ_REGISTER,         sizeof(AVB_REGISTER_REQUEST), sizeof(AVB_REGISTER_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_WRITE_REGISTER,        sizeof(AVB_REGISTER_REQUEST), 0, TRUE, FALSE },
    { IOCTL_AVB_GET_TIMESTAMP,         4,     sizeof(AVB_TIMESTAMP_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_SET_TIMESTAMP,         sizeof(AVB_TIMESTAMP_REQUEST), 0, TRUE, FALSE },
    { IOCTL_AVB_SETUP_TAS,             sizeof(AVB_TAS_REQUEST), 4, TRUE, TRUE },
    { IOCTL_AVB_SETUP_QAV,             sizeof(AVB_QAV_REQUEST), 4, TRUE, TRUE },
    { IOCTL_AVB_ENUM_ADAPTERS,         0,     sizeof(AVB_ADAPTER_LIST), FALSE, TRUE },
    { IOCTL_AVB_OPEN_ADAPTER,          sizeof(AVB_OPEN_ADAPTER_REQUEST), 0, TRUE, FALSE },
    { IOCTL_AVB_TS_SUBSCRIBE,          sizeof(AVB_TS_SUBSCRIBE_REQUEST), 0, TRUE, FALSE },
    { IOCTL_AVB_TS_RING_MAP,           sizeof(AVB_TS_RING_MAP_REQUEST), sizeof(AVB_TS_RING_MAP_RESPONSE), TRUE, TRUE },
    { IOCTL_AVB_GET_HW_STATE,          0,     sizeof(AVB_HW_STATE_RESPONSE), FALSE, TRUE },
    { IOCTL_AVB_ADJUST_FREQUENCY,      sizeof(AVB_FREQUENCY_REQUEST), sizeof(AVB_FREQUENCY_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_GET_CLOCK_CONFIG,      0,     sizeof(AVB_CLOCK_CONFIG), FALSE, TRUE },
    { IOCTL_AVB_SET_HW_TIMESTAMPING,   sizeof(AVB_HW_TIMESTAMPING_REQUEST), sizeof(AVB_HW_TIMESTAMPING_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_SET_RX_TIMESTAMP,      sizeof(AVB_RX_TIMESTAMP_REQUEST), sizeof(AVB_RX_TIMESTAMP_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_SET_QUEUE_TIMESTAMP,   sizeof(AVB_QUEUE_TIMESTAMP_REQUEST), sizeof(AVB_QUEUE_TIMESTAMP_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_SET_TARGET_TIME,       sizeof(AVB_TARGET_TIME_REQUEST), sizeof(AVB_TARGET_TIME_REQUEST), TRUE, TRUE },
    { IOCTL_AVB_GET_AUX_TIMESTAMP,     sizeof(AVB_AUX_TIMESTAMP_REQUEST), sizeof(AVB_AUX_TIMESTAMP_REQUEST), TRUE, TRUE },
    // ... (40+ total IOCTLs)
};

#define IOCTL_BUFFER_TABLE_SIZE (sizeof(g_IoctlBufferTable) / sizeof(g_IoctlBufferTable[0]))
```

**Lookup Algorithm** (O(log N) binary search):

```c
// Binary search by IOCTL code (table sorted at compile time)
static const IOCTL_BUFFER_REQUIREMENTS*
FindBufferRequirements(ULONG IoControlCode)
{
    ULONG left = 0;
    ULONG right = IOCTL_BUFFER_TABLE_SIZE - 1;
    
    while (left <= right) {
        ULONG mid = left + (right - left) / 2;
        
        if (g_IoctlBufferTable[mid].IoControlCode == IoControlCode) {
            return &g_IoctlBufferTable[mid];
        }
        
        if (g_IoctlBufferTable[mid].IoControlCode < IoControlCode) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return NULL; // IOCTL not found
}
```

---

#### 2.2.2 Security Checker

```c
/**
 * Validate IOCTL security constraints.
 * 
 * @param DeviceContext  - Device context
 * @param IoControlCode  - IOCTL code
 * @param RequestorMode  - User-mode or Kernel-mode (from Irp->RequestorMode)
 * 
 * @return NTSTATUS
 *   - STATUS_SUCCESS: Security checks passed
 *   - STATUS_ACCESS_DENIED: Insufficient privileges
 *   - STATUS_NOT_SUPPORTED: IOCTL not in whitelist
 *   - STATUS_QUOTA_EXCEEDED: Rate limit exceeded
 * 
 * Preconditions:
 *   - DeviceContext != NULL
 *   - IoControlCode in [IOCTL_AVB_MIN, IOCTL_AVB_MAX]
 * 
 * Postconditions:
 *   - If SUCCESS: Caller authorized to execute IOCTL
 *   - If FAILURE: Audit log entry created
 * 
 * Performance: <200ns (whitelist lookup + privilege check)
 */
NTSTATUS
AvbValidateIoctlSecurity(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ ULONG IoControlCode,
    _In_ KPROCESSOR_MODE RequestorMode
);
```

**Security Whitelist**:

```c
typedef enum _IOCTL_PRIVILEGE_LEVEL {
    IOCTL_PRIV_USER = 0,      // No special privileges required
    IOCTL_PRIV_ADMIN = 1,     // Administrator required
    IOCTL_PRIV_KERNEL = 2     // Kernel-mode only
} IOCTL_PRIVILEGE_LEVEL;

typedef struct _IOCTL_SECURITY_DESCRIPTOR {
    ULONG IoControlCode;
    IOCTL_PRIVILEGE_LEVEL PrivilegeLevel;
    BOOLEAN AuditOnUse;       // TRUE = log all invocations
    BOOLEAN RateLimited;      // TRUE = apply rate limiting
    ULONG MaxCallsPerSecond;  // Rate limit threshold (0 = no limit)
} IOCTL_SECURITY_DESCRIPTOR, *PIOCTL_SECURITY_DESCRIPTOR;

static const IOCTL_SECURITY_DESCRIPTOR g_IoctlSecurityTable[] = {
    // IOCTL Code                      Privilege      Audit  RateLimit  MaxRate
    { IOCTL_AVB_INIT_DEVICE,           IOCTL_PRIV_ADMIN,  TRUE,  TRUE,  10 },
    { IOCTL_AVB_GET_DEVICE_INFO,       IOCTL_PRIV_USER,   FALSE, FALSE, 0  },
    { IOCTL_AVB_READ_REGISTER,         IOCTL_PRIV_ADMIN,  TRUE,  TRUE,  100 }, // Debug only
    { IOCTL_AVB_WRITE_REGISTER,        IOCTL_PRIV_ADMIN,  TRUE,  TRUE,  10  }, // Debug only
    { IOCTL_AVB_GET_TIMESTAMP,         IOCTL_PRIV_USER,   FALSE, FALSE, 0  },
    { IOCTL_AVB_SET_TIMESTAMP,         IOCTL_PRIV_ADMIN,  TRUE,  TRUE,  10 },
    { IOCTL_AVB_SETUP_TAS,             IOCTL_PRIV_ADMIN,  TRUE,  TRUE,  5  },
    { IOCTL_AVB_SETUP_QAV,             IOCTL_PRIV_ADMIN,  TRUE,  TRUE,  5  },
    { IOCTL_AVB_ENUM_ADAPTERS,         IOCTL_PRIV_USER,   FALSE, FALSE, 0  },
    { IOCTL_AVB_OPEN_ADAPTER,          IOCTL_PRIV_USER,   FALSE, TRUE,  100 },
    { IOCTL_AVB_ADJUST_FREQUENCY,      IOCTL_PRIV_ADMIN,  TRUE,  TRUE,  10 },
    { IOCTL_AVB_GET_CLOCK_CONFIG,      IOCTL_PRIV_USER,   FALSE, FALSE, 0  },
    // ... (all 40+ IOCTLs)
};
```

**Rate Limiting Algorithm** (Token Bucket):

```c
typedef struct _RATE_LIMITER {
    LARGE_INTEGER LastRefillTime;  // KeQueryPerformanceCounter timestamp
    ULONG TokensAvailable;          // Current token count
    ULONG MaxTokens;                // Maximum tokens (MaxCallsPerSecond)
    ULONG RefillRatePerSecond;      // Tokens added per second
} RATE_LIMITER, *PRATE_LIMITER;

// Per-IOCTL rate limiter (global array indexed by IOCTL code)
static RATE_LIMITER g_RateLimiters[64]; // Support up to 64 rate-limited IOCTLs

static NTSTATUS
CheckRateLimit(ULONG IoControlCode, ULONG MaxCallsPerSecond)
{
    ULONG index = IoControlCode % 64;
    PRATE_LIMITER limiter = &g_RateLimiters[index];
    
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    
    // Refill tokens based on elapsed time
    LARGE_INTEGER elapsed;
    elapsed.QuadPart = now.QuadPart - limiter->LastRefillTime.QuadPart;
    
    ULONG tokensToAdd = (ULONG)((elapsed.QuadPart * MaxCallsPerSecond) / g_PerformanceFrequency.QuadPart);
    
    if (tokensToAdd > 0) {
        limiter->TokensAvailable = min(limiter->MaxTokens, limiter->TokensAvailable + tokensToAdd);
        limiter->LastRefillTime = now;
    }
    
    // Consume one token
    if (limiter->TokensAvailable > 0) {
        limiter->TokensAvailable--;
        return STATUS_SUCCESS;
    }
    
    // Rate limit exceeded
    AVB_LOG_WARNING(AVB_COMPONENT_IOCTL, 3002, "Rate limit exceeded for IOCTL 0x%08X", IoControlCode);
    return STATUS_QUOTA_EXCEEDED;
}
```

---

#### 2.2.3 State Validator

```c
/**
 * Validate adapter state and hardware availability.
 * 
 * @param DeviceContext - Device context
 * @param IoControlCode - IOCTL code
 * 
 * @return NTSTATUS
 *   - STATUS_SUCCESS: State checks passed
 *   - STATUS_DEVICE_NOT_READY: Adapter not initialized
 *   - STATUS_NO_SUCH_DEVICE: Adapter context not found
 *   - STATUS_INVALID_DEVICE_STATE: Hardware in error state
 * 
 * Preconditions:
 *   - DeviceContext != NULL
 *   - IoControlCode in [IOCTL_AVB_MIN, IOCTL_AVB_MAX]
 * 
 * Postconditions:
 *   - If SUCCESS: Adapter ready for IOCTL execution
 *   - If FAILURE: Error logged with context (adapter ID, state)
 * 
 * Performance: <200ns (context lookup + state check)
 */
NTSTATUS
AvbValidateIoctlState(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_ ULONG IoControlCode
);
```

**State Requirements Table**:

```c
typedef enum _REQUIRED_ADAPTER_STATE {
    STATE_ANY = 0,           // IOCTL works in any state
    STATE_INITIALIZED = 1,   // Adapter must be initialized
    STATE_READY = 2,         // Hardware must be ready
    STATE_OPEN = 3           // Adapter context must be open
} REQUIRED_ADAPTER_STATE;

typedef struct _IOCTL_STATE_REQUIREMENTS {
    ULONG IoControlCode;
    REQUIRED_ADAPTER_STATE RequiredState;
    BOOLEAN RequiresHardware; // TRUE = hardware access needed
} IOCTL_STATE_REQUIREMENTS, *PIOCTL_STATE_REQUIREMENTS;

static const IOCTL_STATE_REQUIREMENTS g_IoctlStateTable[] = {
    // IOCTL Code                      State                HW Required
    { IOCTL_AVB_INIT_DEVICE,           STATE_ANY,           FALSE },
    { IOCTL_AVB_GET_DEVICE_INFO,       STATE_INITIALIZED,   FALSE },
    { IOCTL_AVB_READ_REGISTER,         STATE_READY,         TRUE  },
    { IOCTL_AVB_WRITE_REGISTER,        STATE_READY,         TRUE  },
    { IOCTL_AVB_GET_TIMESTAMP,         STATE_READY,         TRUE  },
    { IOCTL_AVB_SET_TIMESTAMP,         STATE_READY,         TRUE  },
    { IOCTL_AVB_SETUP_TAS,             STATE_OPEN,          TRUE  },
    { IOCTL_AVB_SETUP_QAV,             STATE_OPEN,          TRUE  },
    { IOCTL_AVB_ENUM_ADAPTERS,         STATE_ANY,           FALSE },
    { IOCTL_AVB_OPEN_ADAPTER,          STATE_INITIALIZED,   FALSE },
    { IOCTL_AVB_ADJUST_FREQUENCY,      STATE_READY,         TRUE  },
    { IOCTL_AVB_GET_CLOCK_CONFIG,      STATE_READY,         TRUE  },
    // ... (all 40+ IOCTLs)
};
```

---

### 2.3 Routing Table Interface

```c
/**
 * IOCTL handler function pointer type.
 * All handlers follow this signature for uniform dispatch.
 * 
 * @param DeviceContext - Device context (contains adapter registry)
 * @param InputBuffer   - Validated input buffer
 * @param InputLength   - Size of input buffer
 * @param OutputBuffer  - Validated output buffer
 * @param OutputLength  - Size of output buffer
 * @param BytesWritten  - [out] Actual bytes written to output buffer
 * 
 * @return NTSTATUS
 *   - STATUS_SUCCESS: IOCTL executed successfully
 *   - STATUS_*: Handler-specific error codes
 * 
 * Preconditions:
 *   - All validation stages passed (buffers, security, state)
 *   - DeviceContext != NULL
 *   - Buffers meet minimum size requirements
 *   - Adapter in required state
 * 
 * Postconditions:
 *   - *BytesWritten set to actual output size
 *   - OutputBuffer populated if applicable
 *   - Error logged if failure
 */
typedef NTSTATUS (*PFN_IOCTL_HANDLER)(
    _In_ PDEVICE_CONTEXT DeviceContext,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_writes_bytes_to_(OutputLength, *BytesWritten) PVOID OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG BytesWritten
);

/**
 * Routing table entry.
 * Maps IOCTL code to handler function.
 */
typedef struct _IOCTL_HANDLER_ENTRY {
    ULONG IoControlCode;
    PFN_IOCTL_HANDLER Handler;
    const char* HandlerName; // For logging/debugging
} IOCTL_HANDLER_ENTRY, *PIOCTL_HANDLER_ENTRY;

/**
 * Global routing table (compile-time constant).
 * Sorted by IOCTL code for binary search.
 */
static const IOCTL_HANDLER_ENTRY g_IoctlHandlerTable[] = {
    // IOCTL Code                      Handler Function                      Name
    { IOCTL_AVB_INIT_DEVICE,           AvbHandleInitDevice,                  "InitDevice" },
    { IOCTL_AVB_GET_DEVICE_INFO,       AvbHandleGetDeviceInfo,               "GetDeviceInfo" },
    { IOCTL_AVB_READ_REGISTER,         AvbHandleReadRegister,                "ReadRegister" },
    { IOCTL_AVB_WRITE_REGISTER,        AvbHandleWriteRegister,               "WriteRegister" },
    { IOCTL_AVB_GET_TIMESTAMP,         AvbHandleGetTimestamp,                "GetTimestamp" },
    { IOCTL_AVB_SET_TIMESTAMP,         AvbHandleSetTimestamp,                "SetTimestamp" },
    { IOCTL_AVB_SETUP_TAS,             AvbHandleSetupTas,                    "SetupTAS" },
    { IOCTL_AVB_SETUP_FP,              AvbHandleSetupFp,                     "SetupFP" },
    { IOCTL_AVB_SETUP_PTM,             AvbHandleSetupPtm,                    "SetupPTM" },
    { IOCTL_AVB_MDIO_READ,             AvbHandleMdioRead,                    "MdioRead" },
    { IOCTL_AVB_MDIO_WRITE,            AvbHandleMdioWrite,                   "MdioWrite" },
    { IOCTL_AVB_ENUM_ADAPTERS,         AvbHandleEnumAdapters,                "EnumAdapters" },
    { IOCTL_AVB_OPEN_ADAPTER,          AvbHandleOpenAdapter,                 "OpenAdapter" },
    { IOCTL_AVB_TS_SUBSCRIBE,          AvbHandleTsSubscribe,                 "TsSubscribe" },
    { IOCTL_AVB_TS_RING_MAP,           AvbHandleTsRingMap,                   "TsRingMap" },
    { IOCTL_AVB_SETUP_QAV,             AvbHandleSetupQav,                    "SetupQAV" },
    { IOCTL_AVB_GET_HW_STATE,          AvbHandleGetHwState,                  "GetHwState" },
    { IOCTL_AVB_ADJUST_FREQUENCY,      AvbHandleAdjustFrequency,             "AdjustFrequency" },
    { IOCTL_AVB_GET_CLOCK_CONFIG,      AvbHandleGetClockConfig,              "GetClockConfig" },
    { IOCTL_AVB_SET_HW_TIMESTAMPING,   AvbHandleSetHwTimestamping,           "SetHwTimestamping" },
    { IOCTL_AVB_SET_RX_TIMESTAMP,      AvbHandleSetRxTimestamp,              "SetRxTimestamp" },
    { IOCTL_AVB_SET_QUEUE_TIMESTAMP,   AvbHandleSetQueueTimestamp,           "SetQueueTimestamp" },
    { IOCTL_AVB_SET_TARGET_TIME,       AvbHandleSetTargetTime,               "SetTargetTime" },
    { IOCTL_AVB_GET_AUX_TIMESTAMP,     AvbHandleGetAuxTimestamp,             "GetAuxTimestamp" },
    // ... (40+ total handlers)
};

#define IOCTL_HANDLER_TABLE_SIZE (sizeof(g_IoctlHandlerTable) / sizeof(g_IoctlHandlerTable[0]))

/**
 * Find handler for IOCTL code.
 * Uses binary search for O(log N) lookup.
 * 
 * @param IoControlCode - IOCTL code
 * @return Handler function pointer, or NULL if not found
 */
static PFN_IOCTL_HANDLER
FindIoctlHandler(ULONG IoControlCode)
{
    ULONG left = 0;
    ULONG right = IOCTL_HANDLER_TABLE_SIZE - 1;
    
    while (left <= right) {
        ULONG mid = left + (right - left) / 2;
        
        if (g_IoctlHandlerTable[mid].IoControlCode == IoControlCode) {
            return g_IoctlHandlerTable[mid].Handler;
        }
        
        if (g_IoctlHandlerTable[mid].IoControlCode < IoControlCode) {
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }
    
    return NULL; // Handler not found
}
```

---

## 3. Data Structures

### 3.1 Device Context Extension (IOCTL-Specific State)

```c
/**
 * IOCTL dispatcher state (part of DEVICE_CONTEXT).
 * Tracks statistics and rate limiting state.
 */
typedef struct _IOCTL_DISPATCHER_STATE {
    // Statistics (diagnostic counters)
    ULONG64 TotalIoctlsReceived;     // Total IOCTLs since driver load
    ULONG64 TotalIoctlsSucceeded;    // Successfully processed
    ULONG64 TotalIoctlsFailed;       // Failed (any stage)
    ULONG64 BufferValidationFailures; // Failed buffer validation
    ULONG64 SecurityValidationFailures; // Failed security validation
    ULONG64 StateValidationFailures; // Failed state validation
    ULONG64 HandlerExecutionFailures; // Handler returned error
    
    // Rate limiting state (per-IOCTL token buckets)
    RATE_LIMITER RateLimiters[64];   // Up to 64 rate-limited IOCTLs
    
    // Performance metrics (optional - debug builds only)
    #ifdef AVB_PERF_COUNTERS
    LARGE_INTEGER TotalValidationTime;  // Cumulative validation latency
    LARGE_INTEGER TotalHandlerTime;     // Cumulative handler execution time
    ULONG MaxValidationTimeMicrosec;    // Worst-case validation latency
    ULONG MaxHandlerTimeMicrosec;       // Worst-case handler execution latency
    #endif
    
    // Lock (only for statistics updates, NOT used in critical path)
    NDIS_SPIN_LOCK StatisticsLock;
    
} IOCTL_DISPATCHER_STATE, *PIOCTL_DISPATCHER_STATE;

// Invariants:
// - TotalIoctlsReceived >= TotalIoctlsSucceeded + TotalIoctlsFailed
// - Sum of validation failures <= TotalIoctlsFailed
// - StatisticsLock held ONLY during counter updates (not during validation)
```

---

### 3.2 IOCTL Context (Per-Request State)

```c
/**
 * Per-request IOCTL context.
 * Stack-allocated by AvbHandleIoctl() to track validation state.
 */
typedef struct _IOCTL_REQUEST_CONTEXT {
    ULONG IoControlCode;             // IOCTL code from IRP
    PVOID InputBuffer;               // SystemBuffer (input side)
    ULONG InputLength;               // Input buffer size
    PVOID OutputBuffer;              // SystemBuffer (output side, same pointer)
    ULONG OutputLength;              // Output buffer size
    ULONG BytesWritten;              // [out] Actual bytes written
    KPROCESSOR_MODE RequestorMode;   // User-mode or Kernel-mode
    PDEVICE_CONTEXT DeviceContext;   // Device context
    PAVB_DEVICE_CONTEXT AdapterContext; // Adapter context (if applicable)
    NTSTATUS ValidationStatus;       // Result of validation pipeline
    PFN_IOCTL_HANDLER Handler;       // Resolved handler function
    
    // Performance tracking (debug builds only)
    #ifdef AVB_PERF_COUNTERS
    LARGE_INTEGER StartTime;         // KeQueryPerformanceCounter() at entry
    LARGE_INTEGER ValidationEndTime; // After validation pipeline
    LARGE_INTEGER HandlerEndTime;    // After handler execution
    #endif
    
} IOCTL_REQUEST_CONTEXT, *PIOCTL_REQUEST_CONTEXT;

// Lifecycle:
// - Allocated on stack in AvbHandleIoctl()
// - Initialized with IRP data
// - Passed through validation pipeline
// - Passed to handler function
// - Destroyed when AvbHandleIoctl() returns
```

---

## 4. Control Flow

### 4.1 Main Dispatch Algorithm

**Pseudo-Code**:

```
FUNCTION AvbHandleIoctl(DeviceContext, Irp):
    // 1. Extract IRP parameters
    IrpStack = IoGetCurrentIrpStackLocation(Irp)
    IoControlCode = IrpStack->Parameters.DeviceIoControl.IoControlCode
    InputBuffer = Irp->AssociatedIrp.SystemBuffer
    InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength
    OutputBuffer = Irp->AssociatedIrp.SystemBuffer  // Same buffer for METHOD_BUFFERED
    OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength
    RequestorMode = Irp->RequestorMode
    
    // 2. Initialize request context (stack allocation)
    IOCTL_REQUEST_CONTEXT context
    context.IoControlCode = IoControlCode
    context.InputBuffer = InputBuffer
    context.InputLength = InputLength
    context.OutputBuffer = OutputBuffer
    context.OutputLength = OutputLength
    context.RequestorMode = RequestorMode
    context.DeviceContext = DeviceContext
    context.BytesWritten = 0
    
    #ifdef AVB_PERF_COUNTERS
    context.StartTime = KeQueryPerformanceCounter()
    #endif
    
    // 3. Update statistics (total received)
    InterlockedIncrement64(&DeviceContext->IoctlState.TotalIoctlsReceived)
    
    // 4. VALIDATION PIPELINE (3 stages)
    
    // Stage 1: Buffer Validation
    status = AvbValidateIoctlBuffers(
        IoControlCode,
        InputBuffer,
        InputLength,
        OutputBuffer,
        OutputLength
    )
    
    IF status != STATUS_SUCCESS THEN
        InterlockedIncrement64(&DeviceContext->IoctlState.BufferValidationFailures)
        InterlockedIncrement64(&DeviceContext->IoctlState.TotalIoctlsFailed)
        AVB_LOG_ERROR(AVB_COMPONENT_IOCTL, 3001, 
            "Buffer validation failed for IOCTL 0x%08X: InLen=%u, OutLen=%u",
            IoControlCode, InputLength, OutputLength)
        Irp->IoStatus.Status = status
        Irp->IoStatus.Information = 0
        RETURN status
    END IF
    
    // Stage 2: Security Validation
    status = AvbValidateIoctlSecurity(
        DeviceContext,
        IoControlCode,
        RequestorMode
    )
    
    IF status != STATUS_SUCCESS THEN
        InterlockedIncrement64(&DeviceContext->IoctlState.SecurityValidationFailures)
        InterlockedIncrement64(&DeviceContext->IoctlState.TotalIoctlsFailed)
        AVB_LOG_ERROR(AVB_COMPONENT_IOCTL, 3002,
            "Security validation failed for IOCTL 0x%08X: status=0x%08X",
            IoControlCode, status)
        Irp->IoStatus.Status = status
        Irp->IoStatus.Information = 0
        RETURN status
    END IF
    
    // Stage 3: State Validation
    status = AvbValidateIoctlState(
        DeviceContext,
        IoControlCode
    )
    
    IF status != STATUS_SUCCESS THEN
        InterlockedIncrement64(&DeviceContext->IoctlState.StateValidationFailures)
        InterlockedIncrement64(&DeviceContext->IoctlState.TotalIoctlsFailed)
        AVB_LOG_ERROR(AVB_COMPONENT_IOCTL, 3003,
            "State validation failed for IOCTL 0x%08X: status=0x%08X",
            IoControlCode, status)
        Irp->IoStatus.Status = status
        Irp->IoStatus.Information = 0
        RETURN status
    END IF
    
    #ifdef AVB_PERF_COUNTERS
    context.ValidationEndTime = KeQueryPerformanceCounter()
    #endif
    
    // 5. HANDLER DISPATCH
    
    // Find handler function
    handler = FindIoctlHandler(IoControlCode)
    
    IF handler == NULL THEN
        InterlockedIncrement64(&DeviceContext->IoctlState.TotalIoctlsFailed)
        AVB_LOG_ERROR(AVB_COMPONENT_IOCTL, 3004,
            "No handler found for IOCTL 0x%08X", IoControlCode)
        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED
        Irp->IoStatus.Information = 0
        RETURN STATUS_NOT_SUPPORTED
    END IF
    
    // Call handler
    status = handler(
        DeviceContext,
        InputBuffer,
        InputLength,
        OutputBuffer,
        OutputLength,
        &context.BytesWritten
    )
    
    #ifdef AVB_PERF_COUNTERS
    context.HandlerEndTime = KeQueryPerformanceCounter()
    // Update performance metrics
    UpdatePerfCounters(&DeviceContext->IoctlState, &context)
    #endif
    
    // 6. UPDATE STATISTICS AND RETURN
    
    IF status == STATUS_SUCCESS THEN
        InterlockedIncrement64(&DeviceContext->IoctlState.TotalIoctlsSucceeded)
        Irp->IoStatus.Status = STATUS_SUCCESS
        Irp->IoStatus.Information = context.BytesWritten
    ELSE
        InterlockedIncrement64(&DeviceContext->IoctlState.HandlerExecutionFailures)
        InterlockedIncrement64(&DeviceContext->IoctlState.TotalIoctlsFailed)
        AVB_LOG_ERROR(AVB_COMPONENT_IOCTL, 3005,
            "Handler execution failed for IOCTL 0x%08X: status=0x%08X",
            IoControlCode, status)
        Irp->IoStatus.Status = status
        Irp->IoStatus.Information = 0
    END IF
    
    RETURN status
END FUNCTION
```

---

### 4.2 Sequence Diagram (Validation Pipeline)

```
User-Mode App → Kernel32.dll → I/O Manager → device.c → AvbHandleIoctl()
                                                              ↓
                                                    1. Extract IRP params
                                                              ↓
                                                    2. Init request context
                                                              ↓
                                                    3. Update statistics
                                                              ↓
                                           ┌──────────────────┴──────────────────┐
                                           ↓                                      ↓
                                  VALIDATION PIPELINE                    (if any stage fails)
                                           ↓                                      ↓
                         ┌─────────────────┼─────────────────┐           Log error + return
                         ↓                 ↓                 ↓
                  Buffer Validator  Security Checker  State Validator
                         ↓                 ↓                 ↓
                    All PASS ──────────────┴─────────────────┘
                         ↓
                  4. Find handler (binary search)
                         ↓
                  5. Call handler function
                         ↓
                  6. Update statistics
                         ↓
                  7. Set IRP status + Information
                         ↓
                  8. Return to device.c (IRP completed by caller)
```

---

### 4.3 Error Handling Paths

**Error Path 1: Buffer Validation Failure**

```
IOCTL arrives with invalid buffer size
    ↓
AvbValidateIoctlBuffers() detects size mismatch
    ↓
Log: AVB_LOG_ERROR(3001, "Buffer validation failed")
    ↓
Increment: BufferValidationFailures counter
    ↓
Return: STATUS_INVALID_PARAMETER
    ↓
IRP completed with status=STATUS_INVALID_PARAMETER, Information=0
    ↓
User-mode receives: DeviceIoControl() returns FALSE, GetLastError()=ERROR_INVALID_PARAMETER
```

**Error Path 2: Security Validation Failure (Rate Limit)**

```
IOCTL arrives exceeding rate limit (e.g., 100 calls/sec for WRITE_REGISTER)
    ↓
AvbValidateIoctlSecurity() → CheckRateLimit() detects token bucket empty
    ↓
Log: AVB_LOG_WARNING(3002, "Rate limit exceeded")
    ↓
Increment: SecurityValidationFailures counter
    ↓
Return: STATUS_QUOTA_EXCEEDED
    ↓
IRP completed with status=STATUS_QUOTA_EXCEEDED, Information=0
    ↓
User-mode receives: DeviceIoControl() returns FALSE, GetLastError()=ERROR_NOT_ENOUGH_QUOTA
```

**Error Path 3: State Validation Failure**

```
IOCTL_AVB_SETUP_TAS arrives but adapter not in OPEN state
    ↓
AvbValidateIoctlState() checks g_IoctlStateTable[IOCTL_AVB_SETUP_TAS].RequiredState == STATE_OPEN
    ↓
Current adapter state is INITIALIZED (not OPEN)
    ↓
Log: AVB_LOG_ERROR(3003, "Adapter not in required state")
    ↓
Increment: StateValidationFailures counter
    ↓
Return: STATUS_DEVICE_NOT_READY
    ↓
IRP completed with status=STATUS_DEVICE_NOT_READY, Information=0
    ↓
User-mode receives: DeviceIoControl() returns FALSE, GetLastError()=ERROR_NOT_READY
```

**Error Path 4: Handler Execution Failure**

```
Handler AvbHandleSetupTas() called
    ↓
Handler detects invalid TAS config (e.g., cycle_time == 0)
    ↓
Handler returns STATUS_INVALID_PARAMETER
    ↓
AvbHandleIoctl() logs: AVB_LOG_ERROR(3005, "Handler execution failed")
    ↓
Increment: HandlerExecutionFailures counter
    ↓
Return: STATUS_INVALID_PARAMETER
    ↓
IRP completed with status=STATUS_INVALID_PARAMETER, Information=0
    ↓
User-mode receives: DeviceIoControl() returns FALSE, GetLastError()=ERROR_INVALID_PARAMETER
```

---

## 5. Algorithms

### 5.1 Binary Search for Handler Lookup

**Purpose**: O(log N) IOCTL handler lookup (vs. O(N) linear search)

**Input**: ULONG IoControlCode  
**Output**: PFN_IOCTL_HANDLER (or NULL if not found)

**Preconditions**:
- g_IoctlHandlerTable sorted by IoControlCode (ascending order)
- IOCTL_HANDLER_TABLE_SIZE > 0

**Algorithm**:

```c
static PFN_IOCTL_HANDLER
FindIoctlHandler(ULONG IoControlCode)
{
    ULONG left = 0;
    ULONG right = IOCTL_HANDLER_TABLE_SIZE - 1;
    
    // Binary search
    while (left <= right) {
        ULONG mid = left + (right - left) / 2;
        
        if (g_IoctlHandlerTable[mid].IoControlCode == IoControlCode) {
            // Found
            return g_IoctlHandlerTable[mid].Handler;
        }
        
        if (g_IoctlHandlerTable[mid].IoControlCode < IoControlCode) {
            left = mid + 1;  // Search right half
        } else {
            if (mid == 0) break; // Avoid underflow
            right = mid - 1;     // Search left half
        }
    }
    
    // Not found
    return NULL;
}
```

**Complexity**: O(log N) where N = IOCTL_HANDLER_TABLE_SIZE (~40)  
**Performance**: ~5 comparisons for 40 IOCTLs, <50ns on modern CPUs

---

### 5.2 Token Bucket Rate Limiting

**Purpose**: Prevent abuse by limiting IOCTL invocation rate

**Parameters**:
- MaxCallsPerSecond: Maximum allowed calls per second
- BurstSize: Maximum tokens (= MaxCallsPerSecond, allows short bursts)

**Algorithm**:

```c
typedef struct _RATE_LIMITER {
    LARGE_INTEGER LastRefillTime;  // Last token refill timestamp
    ULONG TokensAvailable;          // Current token count
    ULONG MaxTokens;                // Bucket capacity
    ULONG RefillRatePerSecond;      // Tokens added per second
} RATE_LIMITER;

static NTSTATUS
CheckRateLimit(PRATE_LIMITER Limiter, LARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER now;
    KeQueryPerformanceCounter(&now);
    
    // Calculate elapsed time since last refill
    LARGE_INTEGER elapsed;
    elapsed.QuadPart = now.QuadPart - Limiter->LastRefillTime.QuadPart;
    
    // Convert to seconds (fixed-point arithmetic to avoid floating-point)
    // tokens_to_add = (elapsed * RefillRatePerSecond) / PerformanceFrequency
    ULONG tokensToAdd = (ULONG)((elapsed.QuadPart * Limiter->RefillRatePerSecond) / PerformanceFrequency.QuadPart);
    
    if (tokensToAdd > 0) {
        // Refill bucket (capped at MaxTokens)
        Limiter->TokensAvailable = min(Limiter->MaxTokens, Limiter->TokensAvailable + tokensToAdd);
        Limiter->LastRefillTime = now;
    }
    
    // Try to consume one token
    if (Limiter->TokensAvailable > 0) {
        Limiter->TokensAvailable--;
        return STATUS_SUCCESS; // Rate limit OK
    }
    
    // Bucket empty = rate limit exceeded
    return STATUS_QUOTA_EXCEEDED;
}
```

**Example**:
- MaxCallsPerSecond = 10
- User calls IOCTL 15 times in 1 second
- First 10 calls succeed (consume 10 tokens)
- Calls 11-15 fail with STATUS_QUOTA_EXCEEDED
- After 1 second, bucket refills to 10 tokens

**Performance**: <100ns (one KeQueryPerformanceCounter call + integer arithmetic)

---

### 5.3 ABI Version Validation (Optional Header)

**Purpose**: Detect user-mode/kernel-mode ABI mismatch

**Input**: AVB_REQUEST_HEADER (optional struct at start of input buffer)  
**Output**: BOOLEAN (TRUE = version compatible, FALSE = mismatch)

**Algorithm**:

```c
#define AVB_IOCTL_ABI_VERSION_MAJOR(ver) ((ver) >> 16)
#define AVB_IOCTL_ABI_VERSION_MINOR(ver) ((ver) & 0xFFFF)

static BOOLEAN
ValidateAbiVersion(PVOID InputBuffer, ULONG InputLength)
{
    // Check if buffer large enough for header
    if (InputLength < sizeof(AVB_REQUEST_HEADER)) {
        // No header present (optional), assume compatible
        return TRUE;
    }
    
    PAVB_REQUEST_HEADER header = (PAVB_REQUEST_HEADER)InputBuffer;
    
    // Check header magic value (header_size must match struct size)
    if (header->header_size != sizeof(AVB_REQUEST_HEADER)) {
        // Not a valid header (or no header), assume compatible
        return TRUE;
    }
    
    // Extract major/minor versions
    ULONG userMajor = AVB_IOCTL_ABI_VERSION_MAJOR(header->abi_version);
    ULONG userMinor = AVB_IOCTL_ABI_VERSION_MINOR(header->abi_version);
    ULONG driverMajor = AVB_IOCTL_ABI_VERSION_MAJOR(AVB_IOCTL_ABI_VERSION);
    ULONG driverMinor = AVB_IOCTL_ABI_VERSION_MINOR(AVB_IOCTL_ABI_VERSION);
    
    // Major version MUST match (breaking changes)
    if (userMajor != driverMajor) {
        AVB_LOG_ERROR(AVB_COMPONENT_IOCTL, 3006,
            "ABI version mismatch: user=%u.%u, driver=%u.%u",
            userMajor, userMinor, driverMajor, driverMinor);
        return FALSE;
    }
    
    // Minor version: user <= driver (forward compatibility)
    if (userMinor > driverMinor) {
        AVB_LOG_WARNING(AVB_COMPONENT_IOCTL, 3007,
            "ABI minor version newer in user-mode: user=%u.%u, driver=%u.%u",
            userMajor, userMinor, driverMajor, driverMinor);
        return FALSE; // User-mode app too new for this driver
    }
    
    return TRUE; // Compatible
}
```

**Versioning Policy**:
- Major version change: Breaking ABI changes (struct layout, IOCTL codes)
- Minor version change: Backward-compatible additions (new IOCTLs, new fields at end of structs)
- User-mode MUST have major version <= driver major version
- User-mode MAY have minor version <= driver minor version

---

## 6. Test-First Design Considerations

### 6.1 Mockable Interfaces

**All validation functions are mockable**:

```c
// Production implementation
NTSTATUS AvbValidateIoctlBuffers(...) {
    // Real validation logic
}

// Test implementation (mock)
NTSTATUS Mock_AvbValidateIoctlBuffers(...) {
    // Return canned results based on test scenario
    if (TestScenario == SCENARIO_BUFFER_TOO_SMALL) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    return STATUS_SUCCESS;
}
```

**Handler table can be replaced for unit tests**:

```c
// Production table
extern const IOCTL_HANDLER_ENTRY g_IoctlHandlerTable[];

// Test table (override with mocks)
static const IOCTL_HANDLER_ENTRY g_TestIoctlHandlerTable[] = {
    { IOCTL_AVB_GET_TIMESTAMP, MockGetTimestamp, "MockGetTimestamp" },
    // ... (mock handlers for all IOCTLs)
};

// Inject test table
void InjectTestHandlerTable(void) {
    g_IoctlHandlerTable = g_TestIoctlHandlerTable;
}
```

---

### 6.2 Test Scenarios

**Unit Tests** (Phase 05):

1. **Buffer Validation Tests**:
   - Valid buffers (exact size)
   - Input buffer too small
   - Output buffer too small
   - Null input buffer when required
   - Null output buffer when required
   - Misaligned buffers (if alignment required)

2. **Security Validation Tests**:
   - Admin-only IOCTL called from user-mode (should fail)
   - Rate limit exceeded (burst of calls)
   - IOCTL not in whitelist (should fail)
   - Audit log generated for sensitive IOCTLs

3. **State Validation Tests**:
   - IOCTL requiring READY state called when adapter in INIT state
   - IOCTL requiring hardware access when hardware not mapped
   - IOCTL requiring OPEN context when no context open

4. **Handler Dispatch Tests**:
   - Valid IOCTL → correct handler called
   - Invalid IOCTL code → STATUS_NOT_SUPPORTED
   - Handler returns success → IRP completed with STATUS_SUCCESS
   - Handler returns error → error logged + IRP completed with error

5. **Statistics Tests**:
   - TotalIoctlsReceived incremented for every call
   - TotalIoctlsSucceeded incremented only on success
   - Validation failure counters incremented correctly

**Integration Tests** (Phase 06):

6. **End-to-End IOCTL Flow**:
   - User-mode app → DeviceIoControl() → kernel dispatcher → handler → response

7. **Error Recovery Tests**:
   - Multiple validation failures in sequence
   - Handler throws exception (should be caught + logged)

**Performance Tests** (Phase 07):

8. **Latency Tests**:
   - Measure IOCTL round-trip latency (should be <10µs p95)
   - Measure validation overhead (should be <500ns per stage)

9. **Throughput Tests**:
   - Sustained IOCTL load (10,000 calls/sec)
   - Rate limiting effectiveness

---

### 6.3 Dependency Injection

**Context Manager dependency**:

```c
// Real implementation
PAVB_DEVICE_CONTEXT AvbGetContextById(ULONG AdapterId) {
    // Lookup in global context registry
}

// Mock implementation (for tests)
PAVB_DEVICE_CONTEXT Mock_AvbGetContextById(ULONG AdapterId) {
    // Return pre-configured test context
    return &g_TestContext;
}

// Inject mock
typedef PAVB_DEVICE_CONTEXT (*PFN_GET_CONTEXT)(ULONG);
extern PFN_GET_CONTEXT g_GetContextFunc;

void InjectMockContextManager(void) {
    g_GetContextFunc = Mock_AvbGetContextById;
}
```

---

## 7. Performance Analysis

### 7.1 Latency Budget (Total <10µs)

| Stage | Budget | Notes |
|-------|--------|-------|
| IRP extraction | 50ns | Stack operations, no memory allocation |
| Buffer validation | 100ns | Table lookup (binary search) + size checks |
| Security validation | 200ns | Privilege check + rate limiter (no lock) |
| State validation | 200ns | Context lookup + state check |
| Handler dispatch | 50ns | Function pointer call |
| Handler execution | 9,000ns | Handler-specific (e.g., register read ~1µs) |
| Statistics update | 100ns | Interlocked increment (atomic) |
| IRP completion setup | 300ns | Set status + information |
| **Total** | **10,000ns** | **10µs (budget met)** |

---

### 7.2 Optimization Techniques

**Fast Path (Read-Only IOCTLs)**:
- Skip security validation for IOCTLs with `IOCTL_PRIV_USER` + no rate limit
- Inline buffer validation (compiler optimization)
- No audit logging

**Pre-Sorted Tables**:
- All tables (buffer, security, state, handler) sorted by IOCTL code at compile time
- Binary search O(log N) instead of linear O(N)

**Lock-Free Counters**:
- Use `InterlockedIncrement64()` for statistics (no spin lock)
- Rate limiters per-IOCTL (no global lock contention)

**Avoid Memory Allocation**:
- Request context stack-allocated (no ExAllocatePoolWithTag)
- Tables compile-time constants (no heap allocation)

---

## 8. Error Handling

### 8.1 Error Event IDs

All errors logged with component ID + event ID for diagnostics:

| Event ID | Description | Severity | Mitigation |
|----------|-------------|----------|------------|
| 3001 | Buffer validation failed | ERROR | User-mode: Check buffer sizes before DeviceIoControl() |
| 3002 | Security validation failed (rate limit) | WARNING | User-mode: Reduce call frequency |
| 3003 | State validation failed (adapter not ready) | ERROR | User-mode: Call IOCTL_AVB_INIT_DEVICE first |
| 3004 | Handler not found for IOCTL | ERROR | Driver: IOCTL code mismatch (update handler table) |
| 3005 | Handler execution failed | ERROR | Handler-specific (check handler logs) |
| 3006 | ABI version mismatch (major) | ERROR | User-mode: Update app to match driver version |
| 3007 | ABI version mismatch (minor) | WARNING | User-mode: Update driver to match app version |

---

### 8.2 Recovery Strategies

**Transient Failures** (retry possible):
- `STATUS_DEVICE_NOT_READY`: User-mode should retry after calling INIT_DEVICE
- `STATUS_QUOTA_EXCEEDED`: User-mode should back off and retry (exponential backoff)

**Permanent Failures** (no retry):
- `STATUS_INVALID_PARAMETER`: Fix caller code (incorrect buffer size)
- `STATUS_ACCESS_DENIED`: Fix caller privileges (run as admin)
- `STATUS_NOT_SUPPORTED`: IOCTL not implemented (driver version mismatch)

---

## 9. Traceability

### Requirements Satisfied

| Requirement | Satisfied By | Evidence |
|-------------|--------------|----------|
| #30 (REQ-F-IOCTL-001: Centralized Handler) | AvbHandleIoctl() entry point | Single dispatch function for all IOCTLs |
| #91 (REQ-NF-SEC-IOCTL-001: Security Validation) | AvbValidateIoctlSecurity() | Whitelist, privilege checks, rate limiting |
| #92 (REQ-NF-PERF-IOCTL-001: <10µs latency) | Performance budget analysis | Total latency 10µs (budget met) |

### ADRs Implemented

| ADR | Decision | Implementation |
|-----|----------|----------------|
| ADR-PERF-SEC-001 | Fast path for read-only IOCTLs | Security validation skipped for `IOCTL_PRIV_USER` + no rate limit |
| ADR-ARCH-001 | Layered architecture | Validation pipeline (buffer → security → state) before handler |

---

## 10. Open Issues and Future Work

### 10.1 Known Limitations

1. **No Async IOCTL Support**:
   - All IOCTLs currently synchronous (IRP completed before return)
   - Future: Add support for STATUS_PENDING (e.g., long-running TAS config)

2. **Fixed Rate Limiter Array**:
   - Currently supports 64 rate-limited IOCTLs (hash collision possible)
   - Future: Use dynamic hash table or per-IOCTL limiter array

3. **No Per-User Rate Limiting**:
   - Rate limits apply globally (all processes)
   - Future: Track rate limits per EPROCESS (user isolation)

---

### 10.2 Phase 05 Implementation Tasks

1. Implement `AvbHandleIoctl()` entry point
2. Implement validation functions (buffer, security, state)
3. Implement handler dispatch (binary search)
4. Implement rate limiter (token bucket)
5. Implement statistics counters
6. Write unit tests for all validation scenarios
7. Write integration tests (end-to-end IOCTL flow)
8. Measure performance (latency + throughput)
9. Update error logging (event IDs 3001-3007)

---

## 11. References

### Standards
- **IEEE 1016-2009**: Software Design Descriptions
- **ISO/IEC/IEEE 12207:2017**: Design Definition Process

### Related Components
- **#143 (ARC-C-CTX-006)**: Context Manager (adapter lookup)
- **#146 (ARC-C-LOG-009)**: Error Handler (centralized logging)
- **#139 (ARC-C-AVB-002)**: AVB Integration Layer (IOCTL handlers)

### Code References
- `include/avb_ioctl.h`: IOCTL definitions and structures
- `avb_integration_fixed.c`: Current IOCTL dispatcher implementation (to be refactored)

---

**Design Status**: DRAFT (awaiting review)  
**Next Review**: 2025-12-16 (Technical Lead + XP Coach)  
**Phase 05 Entry**: After design approval
