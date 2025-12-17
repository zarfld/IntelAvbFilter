# Physical View: Hardware Deployment and Interrupt Mapping

**View Type**: Physical View  
**Standard**: ISO/IEC/IEEE 42010:2011  
**Last Updated**: 2025-12-17  
**Status**: Approved

## Purpose

Describes hardware topology, interrupt routing (MSI-X), CPU affinity, NUMA considerations, and physical resource allocation for the IntelAvbFilter driver.

## Stakeholders

- **System Administrators**: Configure interrupt affinity and NUMA policies
- **Performance Engineers**: Optimize interrupt distribution and CPU placement
- **Hardware Engineers**: Understand NIC capabilities and PCIe topology  
- **DevOps**: Deploy to multi-socket servers with optimal settings

## Related Architecture Decisions

- **Relates to**: #91 (ADR-REGACCESS-001: BAR0 MMIO)
- **Relates to**: #147 (ADR-OBS-001: Observer Pattern)
- **Relates to**: #134 (ADR-MULTI-001: Multi-NIC Support)

---

## Hardware Topology

### Single-NIC System

```
┌────────────────────────────────────────────────────────────┐
│                     CPU Socket 0 (NUMA Node 0)             │
│  ┌──────────┬──────────┬──────────┬──────────┐            │
│  │  Core 0  │  Core 1  │  Core 2  │  Core 3  │            │
│  │          │          │          │          │            │
│  │  ISR     │  DPC     │  App     │  App     │            │
│  │ (MSI-X 0)│ (Softirq)│(PASSIVE) │(PASSIVE) │            │
│  └──────────┴──────────┴──────────┴──────────┘            │
│                        │                                   │
│                        │ QPI/UPI                           │
│                        ▼                                   │
│  ┌────────────────────────────────────────────┐           │
│  │           PCIe Root Complex                │           │
│  │              (Gen3 x8)                     │           │
│  └────────────────────┬───────────────────────┘           │
└───────────────────────┼───────────────────────────────────┘
                        │ PCIe 3.0 x4 (4 GB/s)
                        ▼
        ┌───────────────────────────────┐
        │  Intel I225-LM (NIC)          │
        │  ┌─────────────────────────┐  │
        │  │ BAR0: 128 KB MMIO       │  │
        │  │ - Registers (0x00000)   │  │
        │  │ - PHC Regs  (0x0B600)   │  │
        │  │ - Queue Regs(0x0E000)   │  │
        │  └─────────────────────────┘  │
        │  ┌─────────────────────────┐  │
        │  │ MSI-X Table (4 vectors) │  │
        │  │ - Vec 0: Queue 0 TX     │  │
        │  │ - Vec 1: Queue 0 RX     │  │
        │  │ - Vec 2: Other (PTP)    │  │
        │  │ - Vec 3: Admin/Link     │  │
        │  └─────────────────────────┘  │
        └───────────────────────────────┘
                        │
                        ▼ Ethernet Cable (2.5 GbE)
                   [TSN Network]
```

### Multi-Socket NUMA System (2-Socket Server)

```
┌─────────────────────────────────────┐  ┌─────────────────────────────────────┐
│   CPU Socket 0 (NUMA Node 0)        │  │   CPU Socket 1 (NUMA Node 1)        │
│  ┌────┬────┬────┬────┬────┬────┐   │  │  ┌────┬────┬────┬────┬────┬────┐   │
│  │ C0 │ C1 │ C2 │ C3 │ C4 │ C5 │   │  │  │ C6 │ C7 │ C8 │ C9 │C10 │C11 │   │
│  │ISR0│DPC0│App │App │ISR2│DPC2│   │  │  │ISR1│DPC1│App │App │ISR3│DPC3│   │
│  └────┴────┴────┴────┴────┴────┘   │  │  └────┴────┴────┴────┴────┴────┘   │
│            │                  │     │  │            │                  │     │
│            │ Local Memory     │     │  │            │ Local Memory     │     │
│            │ (Fast Access)    │     │  │            │ (Fast Access)    │     │
│  ┌─────────▼──────────┐       │     │  │  ┌─────────▼──────────┐       │     │
│  │  RAM (64 GB)       │       │     │  │  │  RAM (64 GB)       │       │     │
│  │  - DMA Buffers     │       │     │  │  │  - DMA Buffers     │       │     │
│  │  - Ring Buffers    │       │     │  │  │  - Ring Buffers    │       │     │
│  └────────────────────┘       │     │  │  └────────────────────┘       │     │
│            │                  │     │  │            │                  │     │
│  ┌─────────▼──────────┐       │     │  │  ┌─────────▼──────────┐       │     │
│  │  PCIe Root (16x)   │       │     │  │  │  PCIe Root (16x)   │       │     │
│  └────────┬───────────┘       │     │  │  └────────┬───────────┘       │     │
└───────────┼───────────────────┼─────┘  └───────────┼───────────────────┼─────┘
            │                   │                    │                   │
            │ QPI/UPI Link (Remote Memory Access)   │
            │ ◄──────────────────────────────────────┤
            ▼                                        ▼
    ┌───────────────┐                        ┌───────────────┐
    │ NIC 0 (I225)  │                        │ NIC 1 (I225)  │
    │ MSI-X → CPU 0 │                        │ MSI-X → CPU 1 │
    └───────────────┘                        └───────────────┘
            │                                        │
            ▼                                        ▼
        [Port 0]                                [Port 1]
```

**NUMA Affinity Rules**:
1. Interrupt affinity matches socket hosting NIC's PCIe root
2. DMA buffers allocated from local NUMA node
3. Application threads pinned to same NUMA node
4. Cross-NUMA access penalty: ~100ns (vs ~50ns local)

---

## PCIe Address Space (BAR Mapping)

### Base Address Register 0 (BAR0) - MMIO Region

```
Physical Address Range: Varies (assigned by PCIe enumeration)
Size: 128 KB (0x20000 bytes)
Access: Uncached, Write-Combining (USWC)
Protection: Read/Write from kernel mode only

┌─────────────────────────────────────────────────┐
│ Offset      │ Size   │ Description              │
├─────────────┼────────┼──────────────────────────┤
│ 0x00000     │ 32 KB  │ Device Control Registers │
│             │        │ - CTRL, STATUS, EEPROM   │
│             │        │ - Interrupt Control      │
│             │        │ - Flow Control           │
├─────────────┼────────┼──────────────────────────┤
│ 0x08000     │ 16 KB  │ Statistics Registers     │
│             │        │ - Packet counters        │
│             │        │ - Error counters         │
├─────────────┼────────┼──────────────────────────┤
│ 0x0B600     │ 1 KB   │ PTP/PHC Registers        │
│             │        │ - SYSTIM (0x0B600)       │
│             │        │ - TIMINCA (0x0B608)      │
│             │        │ - TSAUXC (0x0B640)       │
│             │        │ - TRGTTIML0 (0x0B644)    │
├─────────────┼────────┼──────────────────────────┤
│ 0x0E000     │ 8 KB   │ Queue Registers (4 queues)│
│             │        │ - TxDESC_BASE/LEN/HEAD   │
│             │        │ - RxDESC_BASE/LEN/TAIL   │
├─────────────┼────────┼──────────────────────────┤
│ 0x10000     │ 4 KB   │ MSI-X Table              │
│             │        │ - Vector 0-3 entries     │
│             │        │ - Message Address/Data   │
├─────────────┼────────┼──────────────────────────┤
│ 0x11000     │ 1 KB   │ MSI-X Pending Bit Array  │
└─────────────────────────────────────────────────┘
```

**Memory Mapping Code**:
```c
PHYSICAL_ADDRESS phys_addr;
phys_addr.QuadPart = pci_resource[0].Start.QuadPart;  // From PCI config

PVOID bar0 = MmMapIoSpaceEx(
    phys_addr,
    0x20000,                          // 128 KB
    PAGE_READWRITE | PAGE_NOCACHE     // Uncached
);

// Access register at offset 0x0B600 (SYSTIM)
UINT64 systim = READ_REGISTER_ULONG64((PULONG64)((PUCHAR)bar0 + 0x0B600));
```

---

## MSI-X Interrupt Configuration

### Interrupt Vector Table (4 Vectors)

| Vector | Interrupt Source | CPU Affinity | Priority | Handler | Latency Target |
|--------|------------------|--------------|----------|---------|----------------|
| **0** | TX Queue 0 Complete | Core 0 (ISR) | DIRQL 28 | `TxInterruptIsr()` | <2µs |
| **1** | RX Queue 0 Ready | Core 0 (ISR) | DIRQL 28 | `RxInterruptIsr()` | <2µs |
| **2** | PTP Events (TSICR) | Core 0 (ISR) | DIRQL 28 | `PtpInterruptIsr()` | <5µs |
| **3** | Admin/Link Change | Core 0 (ISR) | DIRQL 28 | `AdminInterruptIsr()` | <10µs |

**Interrupt Flow**:
```
NIC Hardware
    ↓ (Triggers interrupt on PCIe bus)
PCIe Root Complex
    ↓ (Routes to CPU based on MSI-X vector address)
APIC (Advanced Programmable Interrupt Controller)
    ↓ (Raises IRQL to DIRQL=28, masks other interrupts)
Windows Kernel (Interrupt Dispatcher)
    ↓ (Invokes registered ISR)
Driver ISR (filter.c)
    ↓ (Queues DPC, returns INTERRUPT_RECOGNIZED)
DPC (Deferred Procedure Call)
    ↓ (Runs at DISPATCH_LEVEL=2)
Event Dispatcher
    ↓ (Notifies observers)
Ring Buffer Write
    ↓ (User-mode polls ring buffer)
Application
```

### CPU Affinity Configuration (Windows)

**Set via Registry**:
```powershell
# Pin MSI-X vector 2 (PTP) to Core 0
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Enum\PCI\<device>\Device Parameters\Interrupt Management\Affinity Policy" `
    -Name "DevicePolicy" -Value 4  # IrqPolicySpecifiedProcessors
Set-ItemProperty -Path "..." `
    -Name "AssignmentSetOverride" -Value 0x01  # Core 0 (bitmask)
```

**Query Current Affinity**:
```powershell
Get-NetAdapterHardwareInfo -Name "Ethernet 2" | Select-Object -ExpandProperty MsiInterruptProcessors
```

---

## DMA Buffer Allocation

### Descriptor Ring Placement (NUMA-Aware)

```c
// Allocate RX descriptor ring on local NUMA node
MM_NODE_HANDLE numa_node = KeGetCurrentNodeNumber();

PHYSICAL_ADDRESS phys_addr = MmAllocateContiguousNodeMemory(
    PAGE_SIZE * 4,                    // 16 KB (256 descriptors)
    (PHYSICAL_ADDRESS){.QuadPart = 0},
    (PHYSICAL_ADDRESS){.QuadPart = -1},
    (PHYSICAL_ADDRESS){.QuadPart = 0},
    PAGE_SIZE,                        // 4 KB alignment
    numa_node,                        // Preferred NUMA node
    MM_ALLOCATE_PREFER_CONTIGUOUS
);

// Map to kernel virtual address space
PVOID rx_desc_ring = MmMapLockedPagesSpecifyCache(
    mdl,
    KernelMode,
    MmCached,
    NULL,
    FALSE,
    NormalPagePriority
);
```

**Memory Layout**:
```
┌────────────────────────────────────────────────────┐
│  RX Descriptor Ring (16 KB)                        │
│  ┌──────────────────────────────────────────────┐  │
│  │ Desc 0: Buffer Addr, Length, Status          │  │
│  │ Desc 1: ...                                  │  │
│  │ ...                                          │  │
│  │ Desc 255: ...                                │  │
│  └──────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────┤
│  RX Data Buffers (256 × 2048 bytes = 512 KB)       │
│  ┌──────────────────────────────────────────────┐  │
│  │ Buffer 0: [Ethernet Frame Data]             │  │
│  │ Buffer 1: ...                                │  │
│  │ ...                                          │  │
│  └──────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────┘

Total: 528 KB per queue × 4 queues = 2.1 MB
```

**Cache Alignment**:
- Descriptor rings: 64-byte aligned (cache line size)
- Data buffers: 2048-byte aligned (page-aligned not required)
- Reason: Avoid false sharing between CPU cores

---

## Multi-NIC Deployment Strategies

### Strategy 1: Single-Socket Server (Load Balancing)

```
CPU Socket 0
  ├── Core 0: NIC 0 ISR/DPC
  ├── Core 1: NIC 1 ISR/DPC
  ├── Core 2: Application threads (NIC 0)
  └── Core 3: Application threads (NIC 1)

NIC 0 (I225 #0) ─► Port 0 (TSN VLAN 100)
NIC 1 (I225 #1) ─► Port 1 (TSN VLAN 200)
```

**Rationale**: Isolate interrupt processing to separate cores, avoid contention

### Strategy 2: Multi-Socket Server (NUMA Locality)

```
Socket 0 (NUMA Node 0)         Socket 1 (NUMA Node 1)
  ├── NIC 0 → Core 0/1           ├── NIC 2 → Core 6/7
  └── App Threads → Core 2-5     └── App Threads → Core 8-11

Cross-Socket Access Penalty: ~100ns
Strategy: Pin NIC affinity + app threads to same NUMA node
```

**Validation**:
```powershell
# Check NUMA topology
Get-WmiObject -Class Win32_NumaNode | Select-Object NodeId, ProcessorMask

# Verify NIC affinity
Get-NetAdapterBinding -Name "Ethernet*" | Select-Object Name, ComponentID
```

---

## Power Management (ACPI States)

| State | Power | Function | Entry Condition | Exit Latency |
|-------|-------|----------|-----------------|--------------|
| **D0** | Full | Active (PTP running) | Default | N/A |
| **D1** | Medium | Clock stopped, buffers retained | Idle >10 sec | <50ms |
| **D2** | Low | Partial context loss | Idle >60 sec | <100ms |
| **D3** | Off | Full context loss | System suspend | <500ms |

**Wake-On-LAN (WoL) Support**:
- Driver enables WoL magic packet filter in D3 state
- NIC asserts PME# signal on PCIe bus to wake system
- OS restores driver to D0 state, reloads PTP context

**Code Example**:
```c
NTSTATUS PowerTransitionToD3(DEVICE_CONTEXT* ctx) {
    // Save PTP clock offset before power down
    PTP_TIME current_time;
    GetPtpTime(ctx, &current_time);
    ctx->ptp_offset_ns = current_time.nanoseconds;
    
    // Enable WoL magic packet
    WRITE_REGISTER_ULONG(ctx->bar0 + WUFC, WUFC_MAG);
    
    // Disable DMA
    WRITE_REGISTER_ULONG(ctx->bar0 + CTRL, CTRL_RST);
    
    return STATUS_SUCCESS;
}
```

---

## Diagnostic Tools

### 1. PCIe Link Status

```powershell
# Check link speed and width
Get-NetAdapterHardwareInfo -Name "Ethernet 2" | Select-Object -ExpandProperty PCIExpressLinkSpeed
# Expected: 5.0 GT/s (Gen2 x1) for I225
```

### 2. Interrupt Distribution

```powershell
# View interrupt counts per CPU
Get-Counter "\Processor(*)\Interrupts/sec"

# View MSI-X table
bcdedit /set {current} debug on
!pic (WinDbg command)
```

### 3. NUMA Memory Access

```c
// Measure cross-NUMA latency
LARGE_INTEGER start, end, freq;
QueryPerformanceFrequency(&freq);

QueryPerformanceCounter(&start);
volatile UINT64 value = *(UINT64*)(remote_numa_buffer);  // Remote access
QueryPerformanceCounter(&end);

UINT64 latency_ns = ((end.QuadPart - start.QuadPart) * 1000000000) / freq.QuadPart;
// Typical: 50ns (local), 100ns (cross-socket)
```

---

## Traceability

### Physical View → Requirements

| Hardware Component | Requirements Satisfied |
|--------------------|------------------------|
| MSI-X Interrupts | #46 (Packet Processing <1µs), #58 (PHC Accuracy <500ns) |
| BAR0 MMIO | #91 (BAR0 Register Access), #34 (Get PHC Time) |
| DMA Buffers | #19 (Ring Buffer), #65 (Timestamp Extraction <1µs) |
| NUMA Affinity | #180 (QA-PERF-001: High Throughput) |

### Physical View → ADRs

| Deployment Aspect | Architecture Decision |
|-------------------|----------------------|
| Interrupt routing | ADR #147 (Observer Pattern for event dispatch) |
| Register access | ADR #91 (BAR0 MMIO vs Ported I/O) |
| Multi-NIC support | ADR #134 (Per-device context) |

---

## Validation Criteria

- ✅ MSI-X vectors configured correctly (verified with `!pic` in WinDbg)
- ✅ Interrupt affinity matches NUMA node (verified with Performance Monitor)
- ✅ DMA buffers allocated from local NUMA node (verified with `!address`)
- ✅ Cross-NUMA latency <150ns (verified with micro-benchmark)
- ✅ PCIe link active at correct speed (verified with `Get-NetAdapterHardwareInfo`)

---

## References

- Intel I225 Datasheet - PCIe Configuration and MSI-X
- Windows Driver Kit (WDK) - Interrupt Management
- ADR #91 (BAR0 MMIO Register Access)
- ADR #134 (Multi-NIC Support Architecture)
