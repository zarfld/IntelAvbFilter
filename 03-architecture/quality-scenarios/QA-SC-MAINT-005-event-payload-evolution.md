# Quality Attribute Scenario: QA-SC-MAINT-005 - Event Payload Evolution

**Scenario ID**: QA-SC-MAINT-005  
**Quality Attribute**: Maintainability (Evolvability)  
**Priority**: P1 (High)  
**Status**: Proposed  
**Created**: 2025-01-15  
**Related Issues**: TBD (will be created)

---

## ATAM Quality Attribute Scenario Elements

### 1. Source of Stimulus
- **Who/What**: Developer implementing new IEEE 802.1AS-2020 feature
- **Context**: Enhancement request from customer for extended PTP diagnostics
- **Expertise**: Windows kernel developer familiar with event subsystem architecture
- **Timeline**: Sprint 3 of product roadmap (6-week development cycle)

### 2. Stimulus
- **Event**: Add new fields to existing `PTP_TX_TIMESTAMP_EVENT` structure
- **Trigger**: IEEE 802.1AS-2020 adds "correction field" and "gPTP domain number" to PTP frames
- **New Fields Required**:
  - `CorrectionField` (INT64): ns/2^16 fixed-point correction value
  - `DomainNumber` (UINT8): gPTP domain (0-127)
  - `LogMessageInterval` (INT8): Message transmission interval exponent
  - `Reserved` (UINT8[5]): Padding for future extensions
- **Constraint**: Existing user-mode applications (deployed in production) must continue working without recompilation

### 3. Artifact
- **System Element**: Event payload structures, ring buffer serialization, user-mode IOCTL interface
- **Components Affected**:
  - `PTP_TX_TIMESTAMP_EVENT` structure definition (kernel)
  - Ring buffer write logic (`RingBuffer_Write`)
  - User-mode header files (`filteruser.h`)
  - User-mode libraries consuming events (`avb_lib.dll`)
  - Documentation (API reference, migration guide)

### 4. Environment
- **Development**: Visual Studio 2022, WDK 10.0.26100
- **Production**: Windows 10 22H2 / Windows Server 2022
- **Deployed Applications**: 5 customer applications using v1.0 event structures
- **Backward Compatibility Window**: Minimum 2 years (support old and new apps simultaneously)

### 5. Response
1. **Define Versioned Schema** (Week 1):
   - Add `SchemaVersion` field to `EVENT_HEADER` (8 bits)
   - Current version: v1 (legacy), new version: v2 (extended)
   - Ring buffer header includes schema version
2. **Extend Event Structure** (Week 1):
   - Create `PTP_TX_TIMESTAMP_EVENT_V2` with new fields
   - Keep `PTP_TX_TIMESTAMP_EVENT_V1` unchanged (typedef alias)
   - Kernel always populates v2 format internally
3. **Implement Version Negotiation** (Week 2):
   - User-mode app queries schema version via `IOCTL_GET_SCHEMA_VERSION`
   - App requests v1 or v2 format via `IOCTL_SET_SCHEMA_VERSION`
   - Kernel translates v2 → v1 if app requests legacy format (drop new fields)
4. **Update Ring Buffer Logic** (Week 2):
   - Serialize events in v2 format (larger size: 64 bytes vs 48 bytes)
   - If app requests v1, translate on read: Copy only v1 fields, ignore extensions
   - Ring buffer dynamically adjusts slot size based on schema version
5. **Provide Migration Path** (Week 3):
   - Update documentation with migration guide
   - Provide code examples for v1 → v2 migration
   - Add deprecation warning to v1 header (remove in 2 years)
6. **Maintain Test Coverage** (Week 4):
   - Unit tests: v1 app reads v2 events (translated correctly)
   - Unit tests: v2 app reads v2 events (no translation)
   - Integration tests: Mixed apps (v1 + v2) reading same event stream

### 6. Response Measure
| Metric | Target | Measurement |
|--------|--------|-------------|
| **Development Time** | <2 hours | Time to define v2 structure and implement translation |
| **Backward Compatibility** | 100% | All existing v1 apps work without changes |
| **Code Changes** | <150 LOC | New code added (excluding comments/docs) |
| **Breaking Changes** | 0 | No changes to v1 API or ABI |
| **Documentation Time** | <4 hours | Update API docs, write migration guide |
| **Test Coverage** | ≥95% | Branch coverage for version negotiation logic |
| **Performance Impact** | <2% overhead | v1 translation latency vs native v1 |

---

## Detailed Implementation

### 1. Versioned Event Header

```c
// EventTypes.h - Add schema version to header
typedef struct _EVENT_HEADER {
    EVENT_TYPE Type;                // 4 bytes (PTP_TX_TIMESTAMP, PTP_RX_TIMESTAMP, etc.)
    UINT8 SchemaVersion;            // NEW: 1 = v1 (legacy), 2 = v2 (extended)
    UINT8 Reserved[3];              // Padding for alignment
    LARGE_INTEGER Timestamp;        // 8 bytes (QPC timestamp)
    UINT32 SequenceNumber;          // 4 bytes
    UINT32 Flags;                   // 4 bytes (error flags, etc.)
    UCHAR Padding[4];               // Align to 32 bytes
} EVENT_HEADER;  // Total: 32 bytes (unchanged)

// Schema version constants
#define SCHEMA_VERSION_V1  1  // Original (48-byte PTP_TX_TIMESTAMP_EVENT)
#define SCHEMA_VERSION_V2  2  // Extended (64-byte PTP_TX_TIMESTAMP_EVENT_V2)
#define SCHEMA_VERSION_CURRENT  SCHEMA_VERSION_V2
```

### 2. Event Structure Evolution

```c
// PtpEvents.h - Versioned event structures

// V1 (Legacy - 48 bytes total)
typedef struct _PTP_TX_TIMESTAMP_EVENT_V1 {
    EVENT_HEADER Header;            // 32 bytes
    UINT64 HardwareTimestamp;       // 8 bytes (TXSTMPL + TXSTMPH)
    UINT16 SequenceId;              // 2 bytes (PTP sequence ID)
    UINT8 MessageType;              // 1 byte (Sync=0, Delay_Req=1, etc.)
    UINT8 Reserved[5];              // 5 bytes padding
} PTP_TX_TIMESTAMP_EVENT_V1;        // Total: 48 bytes

// V2 (Extended - 64 bytes total) - NEW
typedef struct _PTP_TX_TIMESTAMP_EVENT_V2 {
    EVENT_HEADER Header;            // 32 bytes (SchemaVersion = 2)
    UINT64 HardwareTimestamp;       // 8 bytes
    UINT16 SequenceId;              // 2 bytes
    UINT8 MessageType;              // 1 byte
    UINT8 Reserved1;                // 1 byte alignment
    
    // NEW FIELDS (IEEE 802.1AS-2020)
    INT64 CorrectionField;          // 8 bytes (ns/2^16 fixed-point)
    UINT8 DomainNumber;             // 1 byte (0-127)
    INT8 LogMessageInterval;        // 1 byte (-128 to 127)
    UINT8 Reserved2[6];             // 6 bytes (future extensions)
} PTP_TX_TIMESTAMP_EVENT_V2;        // Total: 64 bytes

// Kernel uses v2 internally, typedef for backward compat
typedef PTP_TX_TIMESTAMP_EVENT_V2 PTP_TX_TIMESTAMP_EVENT;
```

### 3. Version Negotiation IOCTL

```c
// IOCTL definitions
#define IOCTL_GET_SCHEMA_VERSION  CTL_CODE(FILE_DEVICE_NETWORK, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_SET_SCHEMA_VERSION  CTL_CODE(FILE_DEVICE_NETWORK, 0x901, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _SCHEMA_VERSION_REQUEST {
    UINT8 RequestedVersion;  // 1 = v1, 2 = v2
    UINT8 Reserved[3];
} SCHEMA_VERSION_REQUEST;

typedef struct _SCHEMA_VERSION_RESPONSE {
    UINT8 CurrentVersion;    // Kernel's internal version (always v2)
    UINT8 NegotiatedVersion; // Version kernel will provide to this app
    UINT8 MinimumSupported;  // Oldest version still supported (v1)
    UINT8 MaximumSupported;  // Newest version (v2)
} SCHEMA_VERSION_RESPONSE;

// Handler
NTSTATUS IOCTL_GetSchemaVersion(
    _In_ DEVICE_OBJECT* DeviceObject,
    _In_ IRP* Irp
)
{
    IO_STACK_LOCATION* stack = IoGetCurrentIrpStackLocation(Irp);
    SCHEMA_VERSION_RESPONSE* response = (SCHEMA_VERSION_RESPONSE*)Irp->AssociatedIrp.SystemBuffer;
    
    if (stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(SCHEMA_VERSION_RESPONSE)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    FILTER_CONTEXT* context = GetFilterContext(DeviceObject);
    
    response->CurrentVersion = SCHEMA_VERSION_CURRENT;  // 2
    response->NegotiatedVersion = context->SchemaVersion;  // App's requested version
    response->MinimumSupported = SCHEMA_VERSION_V1;     // 1
    response->MaximumSupported = SCHEMA_VERSION_V2;     // 2
    
    Irp->IoStatus.Information = sizeof(SCHEMA_VERSION_RESPONSE);
    return STATUS_SUCCESS;
}

NTSTATUS IOCTL_SetSchemaVersion(
    _In_ DEVICE_OBJECT* DeviceObject,
    _In_ IRP* Irp
)
{
    IO_STACK_LOCATION* stack = IoGetCurrentIrpStackLocation(Irp);
    SCHEMA_VERSION_REQUEST* request = (SCHEMA_VERSION_REQUEST*)Irp->AssociatedIrp.SystemBuffer;
    
    if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(SCHEMA_VERSION_REQUEST)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    // Validate requested version
    if (request->RequestedVersion < SCHEMA_VERSION_V1 || 
        request->RequestedVersion > SCHEMA_VERSION_V2) {
        return STATUS_INVALID_PARAMETER;
    }
    
    FILTER_CONTEXT* context = GetFilterContext(DeviceObject);
    
    // Set negotiated version for this app instance
    context->SchemaVersion = request->RequestedVersion;
    
    DbgPrint("[SCHEMA] App requested version %u (kernel uses v%u)\n",
             request->RequestedVersion, SCHEMA_VERSION_CURRENT);
    
    return STATUS_SUCCESS;
}
```

### 4. Translation Logic (v2 → v1)

```c
// RingBuffer.c - Read with version translation
NTSTATUS RingBuffer_Read(
    _In_ RING_BUFFER* RingBuffer,
    _Out_ VOID* OutputBuffer,
    _In_ SIZE_T OutputBufferSize,
    _In_ UINT8 SchemaVersion  // NEW: Requested schema version
)
{
    // Read from ring buffer (always v2 internally)
    PTP_TX_TIMESTAMP_EVENT_V2 eventV2;
    NTSTATUS status = RingBuffer_ReadInternal(RingBuffer, &eventV2, sizeof(eventV2));
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // Translate if app requests v1
    if (SchemaVersion == SCHEMA_VERSION_V1) {
        if (OutputBufferSize < sizeof(PTP_TX_TIMESTAMP_EVENT_V1)) {
            return STATUS_BUFFER_TOO_SMALL;
        }
        
        // Copy v1 fields only (drop v2 extensions)
        PTP_TX_TIMESTAMP_EVENT_V1* eventV1 = (PTP_TX_TIMESTAMP_EVENT_V1*)OutputBuffer;
        
        eventV1->Header = eventV2.Header;
        eventV1->Header.SchemaVersion = SCHEMA_VERSION_V1;  // Override to v1
        eventV1->HardwareTimestamp = eventV2.HardwareTimestamp;
        eventV1->SequenceId = eventV2.SequenceId;
        eventV1->MessageType = eventV2.MessageType;
        RtlZeroMemory(eventV1->Reserved, sizeof(eventV1->Reserved));
        
        return STATUS_SUCCESS;
    }
    
    // No translation needed for v2
    if (OutputBufferSize < sizeof(PTP_TX_TIMESTAMP_EVENT_V2)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    RtlCopyMemory(OutputBuffer, &eventV2, sizeof(eventV2));
    return STATUS_SUCCESS;
}
```

### 5. Ring Buffer Size Adjustment

```c
// RingBuffer.h - Dynamic slot sizing
typedef struct _RING_BUFFER {
    RING_BUFFER_HEADER* Header;
    UCHAR* Buffer;
    ULONG Capacity;              // Number of slots
    ULONG EventSize;             // Bytes per slot (48 for v1, 64 for v2)
    UINT8 SchemaVersion;         // NEW: Schema version for this buffer
    PHYSICAL_ADDRESS PhysicalAddress;
    ULONG NumaNode;
} RING_BUFFER;

// Allocate ring buffer with schema-specific slot size
NTSTATUS AllocateRingBuffer(
    _In_ ULONG Capacity,
    _In_ UINT8 SchemaVersion,
    _Out_ RING_BUFFER** RingBuffer
)
{
    SIZE_T eventSize;
    switch (SchemaVersion) {
        case SCHEMA_VERSION_V1:
            eventSize = sizeof(PTP_TX_TIMESTAMP_EVENT_V1);  // 48 bytes
            break;
        case SCHEMA_VERSION_V2:
            eventSize = sizeof(PTP_TX_TIMESTAMP_EVENT_V2);  // 64 bytes
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }
    
    SIZE_T totalSize = sizeof(RING_BUFFER_HEADER) + (Capacity * eventSize);
    // ... allocate memory ...
    
    (*RingBuffer)->EventSize = (ULONG)eventSize;
    (*RingBuffer)->SchemaVersion = SchemaVersion;
    
    DbgPrint("[RINGBUF] Allocated %lu slots × %Iu bytes = %Iu bytes (schema v%u)\n",
             Capacity, eventSize, totalSize, SchemaVersion);
    
    return STATUS_SUCCESS;
}
```

### 6. User-Mode Migration Example

```c
// Legacy v1 application (no changes required)
void LegacyApp_ReadEvents(HANDLE hDevice)
{
    PTP_TX_TIMESTAMP_EVENT_V1 event;  // 48 bytes
    DWORD bytesRead;
    
    // Works as before - kernel translates v2 → v1
    if (!DeviceIoControl(hDevice, IOCTL_READ_EVENT,
                         NULL, 0,
                         &event, sizeof(event),
                         &bytesRead, NULL)) {
        printf("Error reading event\n");
        return;
    }
    
    printf("Timestamp: %llu, Seq: %u\n", 
           event.HardwareTimestamp, event.SequenceId);
}

// Modern v2 application (uses new fields)
void ModernApp_ReadEvents(HANDLE hDevice)
{
    // 1. Negotiate schema version
    SCHEMA_VERSION_REQUEST request = { .RequestedVersion = SCHEMA_VERSION_V2 };
    DWORD bytesReturned;
    
    if (!DeviceIoControl(hDevice, IOCTL_SET_SCHEMA_VERSION,
                         &request, sizeof(request),
                         NULL, 0,
                         &bytesReturned, NULL)) {
        printf("Failed to set schema version\n");
        return;
    }
    
    // 2. Read v2 events (no translation)
    PTP_TX_TIMESTAMP_EVENT_V2 event;  // 64 bytes
    
    if (!DeviceIoControl(hDevice, IOCTL_READ_EVENT,
                         NULL, 0,
                         &event, sizeof(event),
                         &bytesReturned, NULL)) {
        printf("Error reading event\n");
        return;
    }
    
    // 3. Use new v2 fields
    printf("Timestamp: %llu, Seq: %u\n", 
           event.HardwareTimestamp, event.SequenceId);
    printf("Correction: %lld (ns/2^16), Domain: %u, Interval: %d\n",
           event.CorrectionField, event.DomainNumber, event.LogMessageInterval);
}
```

---

## Validation Criteria

### 1. Backward Compatibility
- ✅ **Legacy Apps**: All v1 apps work without recompilation or source changes
- ✅ **ABI Stability**: v1 event size unchanged (48 bytes)
- ✅ **Default Behavior**: Apps not calling `IOCTL_SET_SCHEMA_VERSION` receive v1 events
- ✅ **Translation Accuracy**: v2 → v1 translation preserves all v1 fields exactly

### 2. Evolvability
- ✅ **Schema Versioning**: Header includes `SchemaVersion` field for future extensions
- ✅ **Reserved Fields**: v2 includes 6 bytes reserved for future additions (v3, v4, ...)
- ✅ **Extensibility**: New versions can be added without breaking v1 or v2
- ✅ **Deprecation Path**: v1 deprecated in docs, removal planned for 2027

### 3. Code Maintainability
- ✅ **LOC Added**: <150 lines (version negotiation + translation logic)
- ✅ **Complexity**: Low (simple field copying, no complex transformations)
- ✅ **Documentation**: Migration guide, API reference, deprecation notice
- ✅ **Test Coverage**: ≥95% branch coverage for version negotiation

### 4. Performance
- ✅ **Translation Overhead**: <100ns (simple memcpy + field assignment)
- ✅ **Memory Overhead**: +16 bytes per event (48 → 64 bytes = +33%)
- ✅ **No Latency Impact**: Translation on read (not on event capture)

---

## Test Cases

### TC-1: Legacy App Reads v2 Events (Translation)
**Setup**: Kernel populates v2 events (64 bytes), legacy app requests v1 (48 bytes)  
**Steps**:
1. Kernel writes `PTP_TX_TIMESTAMP_EVENT_V2` to ring buffer
   - CorrectionField = 1234567, DomainNumber = 5, LogMessageInterval = -3
2. Legacy app reads without calling `IOCTL_SET_SCHEMA_VERSION` (defaults to v1)
3. Kernel translates v2 → v1 (drops new fields)
4. App receives 48-byte v1 event

**Expected**:
- App receives: HardwareTimestamp, SequenceId, MessageType (v1 fields only)
- New v2 fields (CorrectionField, DomainNumber) are silently dropped
- No errors, no warnings
- bytesRead = 48

### TC-2: Modern App Reads v2 Events (No Translation)
**Setup**: Modern app negotiates v2 schema  
**Steps**:
1. App calls `IOCTL_SET_SCHEMA_VERSION` with RequestedVersion = 2
2. Kernel confirms (NegotiatedVersion = 2)
3. App reads event via `IOCTL_READ_EVENT`
4. Kernel returns full 64-byte v2 event (no translation)

**Expected**:
- App receives all v2 fields: HardwareTimestamp, SequenceId, CorrectionField, DomainNumber, LogMessageInterval
- bytesRead = 64
- SchemaVersion field in header = 2

### TC-3: Mixed Apps (v1 + v2) Reading Same Stream
**Setup**: 2 apps connected to same NIC (1 legacy v1, 1 modern v2)  
**Steps**:
1. Kernel captures single PTP TX timestamp event (v2)
2. Legacy app reads → receives v1 (48 bytes)
3. Modern app reads → receives v2 (64 bytes)
4. Verify both apps see same HardwareTimestamp and SequenceId

**Expected**:
- Legacy app: 48 bytes, SchemaVersion = 1
- Modern app: 64 bytes, SchemaVersion = 2
- Common fields (HardwareTimestamp, SequenceId) are identical
- No interference between apps

### TC-4: Invalid Schema Version Request
**Setup**: App requests unsupported schema version (v3 = 3)  
**Steps**:
1. App calls `IOCTL_SET_SCHEMA_VERSION` with RequestedVersion = 3
2. Kernel validates (MinimumSupported = 1, MaximumSupported = 2)
3. Kernel returns `STATUS_INVALID_PARAMETER`

**Expected**:
- IOCTL fails with error code
- App's schema version unchanged (remains at default v1)
- No events delivered with invalid schema version

### TC-5: Performance (Translation Overhead)
**Setup**: Measure latency of v2 → v1 translation  
**Steps**:
1. Populate 10,000 v2 events in ring buffer
2. Legacy app reads all 10,000 events (v1)
3. Measure time per read (QPC timestamps)
4. Compare to native v2 reads (no translation)

**Expected**:
- Translation overhead: <100ns per event
- Total overhead: <1ms for 10,000 events
- Performance impact: <2%

---

## Risk Analysis

### Risk 1: Forgotten Version Negotiation (App Assumes v2)
**Likelihood**: Medium  
**Impact**: High (app crashes reading wrong structure size)  
**Mitigation**:
- Default to v1 if app doesn't call `IOCTL_SET_SCHEMA_VERSION`
- Documentation prominently explains version negotiation requirement
- Runtime check: If app reads with wrong buffer size, return `STATUS_BUFFER_TOO_SMALL`

### Risk 2: Future v3 Schema Incompatible with v1/v2
**Likelihood**: Low  
**Impact**: High (translation logic becomes complex)  
**Mitigation**:
- Reserve 6 bytes in v2 for v3 extensions (keep compatible)
- Deprecate v1 after 2 years (require all apps migrate to v2+)
- v3 can remove v1 translation (only support v2 → v3)

### Risk 3: Memory Overhead (33% Increase)
**Likelihood**: High (always present)  
**Impact**: Low (48 → 64 bytes = +16 bytes)  
**Mitigation**:
- 64 bytes still small (8192 events = 512 KB ring buffer)
- Cache-aligned (64 bytes = 1 cache line)
- Justification: Extensibility worth 16 bytes overhead

### Risk 4: Documentation Debt (Apps Don't Know About v2)
**Likelihood**: Medium  
**Impact**: Medium (v2 features unused)  
**Mitigation**:
- Add deprecation warning to v1 header (`#pragma message "v1 deprecated"`)
- Update documentation with migration guide
- Blog post / release notes announcing v2

### Risk 5: ABI Break on Alignment Change
**Likelihood**: Low (careful padding)  
**Impact**: Critical (production apps crash)  
**Mitigation**:
- Use explicit padding fields (Reserved1, Reserved2)
- Static assert: `sizeof(PTP_TX_TIMESTAMP_EVENT_V2) == 64`
- Test on x86 and x64 platforms

---

## Trade-offs

### Schema Versioning vs. Dynamic Field Discovery
- **Chosen**: Schema versioning (v1, v2, v3, ...)
- **Alternative**: Dynamic field discovery (TLV format, capability flags)
- **Justification**: Simpler implementation, lower runtime overhead, sufficient for predictable evolution

### Translation on Read vs. Dual Ring Buffers
- **Chosen**: Translation on read (single v2 ring buffer)
- **Alternative**: Separate v1 and v2 ring buffers
- **Justification**: Lower memory overhead, simpler code, <100ns translation acceptable

### Deprecate v1 Now vs. 2-Year Support
- **Chosen**: 2-year backward compatibility window
- **Alternative**: Immediate v1 deprecation (force all apps to v2)
- **Justification**: Customer stability, gradual migration, avoid production disruption

---

## Traceability

### Requirements Verified
- **#68** (Event Logging): Extended event payload for better diagnostics
- **#53** (Error Handling): Version negotiation errors handled gracefully
- **#19** (Ring Buffer): Schema-aware serialization

### Architecture Decisions Satisfied
- **#93** (Lock-Free Ring Buffer): Version translation doesn't affect lock-free design
- **#147** (Observer Pattern): Observers receive v2 events, user-mode gets translated version
- **#131** (Error Reporting): Invalid schema version errors logged

### Architecture Views Referenced
- [Data View - Event Payloads](../views/data/data-view-event-payloads.md): Event structure evolution
- [Logical View - Event Subsystem](../views/logical/logical-view-event-subsystem.md): Version negotiation flow

---

## Implementation Checklist

- [ ] **Phase 1**: Define v2 event structures (~1 hour)
  - Add `SchemaVersion` to `EVENT_HEADER`
  - Define `PTP_TX_TIMESTAMP_EVENT_V2` with new fields
  - Static asserts for size/alignment

- [ ] **Phase 2**: Implement version negotiation IOCTLs (~2 hours)
  - `IOCTL_GET_SCHEMA_VERSION` handler
  - `IOCTL_SET_SCHEMA_VERSION` handler
  - Validate requested version range

- [ ] **Phase 3**: Implement translation logic (~2 hours)
  - Modify `RingBuffer_Read` to accept `SchemaVersion` parameter
  - v2 → v1 translation (copy v1 fields only)
  - Unit test: Verify field values preserved

- [ ] **Phase 4**: Update ring buffer allocation (~1 hour)
  - Schema-specific slot size (48 vs 64 bytes)
  - Test: Allocate v1 and v2 buffers

- [ ] **Phase 5**: Write tests (~4 hours)
  - TC-1: Legacy app translation test
  - TC-2: Modern app v2 test
  - TC-3: Mixed apps test
  - TC-4: Invalid version test
  - TC-5: Performance benchmark

- [ ] **Phase 6**: Documentation (~4 hours)
  - Update API reference with v2 fields
  - Write migration guide (v1 → v2)
  - Add deprecation notice to v1 header
  - Code examples for both v1 and v2

**Estimated Total**: ~14 hours

---

## Acceptance Criteria

| Criterion | Threshold | Measurement |
|-----------|-----------|-------------|
| Development time | <2 hours | Time to implement v2 structure and translation |
| Backward compatibility | 100% | All v1 apps work without changes |
| Code changes | <150 LOC | New code added (excluding comments/docs) |
| Breaking changes | 0 | No changes to v1 API or ABI |
| Documentation time | <4 hours | Update API docs, write migration guide |
| Test coverage | ≥95% | Branch coverage for version negotiation |
| Performance impact | <2% | v1 translation overhead vs native v1 |

---

**Scenario Status**: Ready for implementation  
**Next Steps**: Create GitHub issue linking to Requirements #68, #53, #19
