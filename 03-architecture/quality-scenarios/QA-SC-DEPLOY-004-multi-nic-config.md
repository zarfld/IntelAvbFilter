# Quality Attribute Scenario: QA-SC-DEPLOY-004 - Multi-NIC NUMA Configuration

**Scenario ID**: QA-SC-DEPLOY-004  
**Quality Attribute**: Deployability  
**Priority**: P1 (High)  
**Status**: Proposed  
**Created**: 2025-01-15  
**Related Issues**: TBD (will be created)

---

## ATAM Quality Attribute Scenario Elements

### 1. Source of Stimulus
- **Who/What**: System administrator deploying driver on enterprise server
- **Context**: Production datacenter with multi-socket NUMA architecture
- **Expertise**: Network administrator familiar with Windows Server, basic driver installation
- **Tools**: DevCon, PowerShell, INF installer

### 2. Stimulus
- **Event**: Install IntelAvbFilter driver on multi-socket server with 10 NICs
- **Trigger**: Administrator runs installation script or INF installer
- **Configuration Requirements**:
  - Assign MSI-X interrupt vectors with CPU affinity
  - Allocate DMA buffers on correct NUMA nodes
  - Configure per-NIC ring buffers with appropriate sizing
  - Set up event observers for each NIC instance

### 3. Artifact
- **System Element**: Driver INF file, installation script, registry configuration, NDIS miniport instances
- **Hardware Environment**:
  - **Processor**: Dual-socket Intel Xeon (2 × 16 cores = 32 cores total)
  - **NUMA Nodes**: 2 nodes (NUMA node 0: CPU 0-15, NUMA node 1: CPU 16-31)
  - **PCIe Topology**:
    - Socket 0 root complex: 5 NICs (bus 0x00-0x04)
    - Socket 1 root complex: 5 NICs (bus 0x80-0x84)
  - **NICs**: 10 × Intel I225-V (AVB/TSN capable, 4 MSI-X vectors each)

### 4. Environment
- **OS**: Windows Server 2022 (21H2)
- **State**: Fresh installation, no existing IntelAvbFilter instances
- **Network Load**: Offline (configuration before production use)
- **Administrator Access**: Full privileges, local console or remote desktop

### 5. Response
1. **Automated Discovery** (during DriverEntry):
   - Enumerate all I225-V NICs via PCI bus scan
   - Detect NUMA topology using KeQueryNodeActiveAffinity()
   - Map each NIC to its NUMA node based on PCIe root complex
2. **MSI-X Interrupt Configuration**:
   - Allocate 4 interrupt vectors per NIC (40 total vectors)
   - Set CPU affinity for each vector to local NUMA node
   - Example: NIC on bus 0x02 → NUMA node 0 → CPU affinity mask 0x0000FFFF (CPUs 0-15)
3. **NUMA-Aware DMA Allocation**:
   - Allocate ring buffers on local NUMA node (minimize memory latency)
   - Use MmAllocateContiguousMemorySpecifyCache() with NUMA node hint
   - Example: NIC on NUMA node 1 → allocate from node 1's memory pool
4. **Per-NIC Configuration**:
   - Read registry settings (ring buffer size, event queue depth, observer list)
   - Create 10 independent NDIS filter instances
   - Initialize event dispatcher, observers, ring buffers per NIC
5. **Validation and Reporting**:
   - Log configuration to Windows Event Log (NUMA mapping, interrupt affinity)
   - Verify all NICs initialized successfully
   - Report any configuration errors (mismatched affinity, allocation failures)

### 6. Response Measure
| Metric | Target | Measurement |
|--------|--------|-------------|
| **Installation Time** | <30 minutes | Time from INF install to all NICs operational |
| **Administrator Actions** | <5 manual steps | Automated configuration via INF/script |
| **Configuration Accuracy** | 100% | All interrupt/DMA allocations match NUMA topology |
| **Deployment Success Rate** | >99% | Success on first attempt (assuming correct hardware) |
| **Documentation Completeness** | 100% | All steps documented in deployment guide |
| **Error Diagnostics** | <5 minutes | Administrator can identify misconfiguration from logs |

---

## Detailed Implementation

### 1. NUMA Topology Detection

```c
// NumaTopology.c - Detect NUMA nodes and CPU affinity
typedef struct _NUMA_NODE_INFO {
    ULONG NodeNumber;           // 0, 1, 2, ... (up to 64 nodes)
    GROUP_AFFINITY Affinity;    // CPU affinity mask for this node
    ULONG CoreCount;            // Number of cores in this node
} NUMA_NODE_INFO;

NTSTATUS DetectNumaTopology(
    _Out_ NUMA_NODE_INFO** NodeInfo,
    _Out_ ULONG* NodeCount
)
{
    ULONG maxNodes = KeQueryMaximumGroupCount();  // Typically 1 for <64 cores
    ULONG activeNodes = KeQueryActiveGroupCount();
    
    *NodeInfo = ExAllocatePoolWithTag(NonPagedPool,
                                      activeNodes * sizeof(NUMA_NODE_INFO),
                                      'munN');  // 'Nnum' = NUMA
    if (!*NodeInfo) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    *NodeCount = activeNodes;
    
    for (ULONG node = 0; node < activeNodes; node++) {
        NUMA_NODE_INFO* info = &(*NodeInfo)[node];
        info->NodeNumber = node;
        
        // Query active CPUs for this NUMA node
        GROUP_AFFINITY affinity = {0};
        NTSTATUS status = KeQueryNodeActiveAffinity(node, &affinity, NULL);
        if (!NT_SUCCESS(status)) {
            DbgPrint("[WARN] Failed to query NUMA node %lu affinity: 0x%08X\n", node, status);
            continue;
        }
        
        info->Affinity = affinity;
        info->CoreCount = RtlNumberOfSetBits((ULONG_PTR)affinity.Mask);
        
        DbgPrint("[NUMA] Node %lu: Group %u, Mask 0x%016llX, Cores %lu\n",
                 node, affinity.Group, affinity.Mask, info->CoreCount);
    }
    
    return STATUS_SUCCESS;
}
```

### 2. PCIe Topology Mapping

```c
// PciTopology.c - Map PCI bus to NUMA node
typedef struct _PCI_DEVICE_LOCATION {
    ULONG Bus;                  // PCI bus number (0x00, 0x80, etc.)
    ULONG Device;               // PCI device number (0-31)
    ULONG Function;             // PCI function number (0-7)
    ULONG NumaNode;             // NUMA node this device is attached to
} PCI_DEVICE_LOCATION;

// Get NUMA node for a PCI device
NTSTATUS GetPciDeviceNumaNode(
    _In_ DEVICE_OBJECT* PhysicalDeviceObject,
    _Out_ ULONG* NumaNode
)
{
    // Get PCI bus/device/function from PDO
    ULONG busNumber, deviceNumber, functionNumber;
    NTSTATUS status = IoGetDeviceProperty(
        PhysicalDeviceObject,
        DevicePropertyBusNumber,
        sizeof(ULONG),
        &busNumber,
        NULL
    );
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // Get address (device:function encoded)
    ULONG address;
    status = IoGetDeviceProperty(
        PhysicalDeviceObject,
        DevicePropertyAddress,
        sizeof(ULONG),
        &address,
        NULL
    );
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    deviceNumber = (address >> 16) & 0x1F;    // Bits 20-16
    functionNumber = address & 0x7;            // Bits 2-0
    
    // Heuristic: Bus 0x00-0x7F → NUMA node 0, Bus 0x80-0xFF → NUMA node 1
    // (This is Intel platform-specific; real implementation should query ACPI SRAT table)
    if (busNumber < 0x80) {
        *NumaNode = 0;
    } else {
        *NumaNode = 1;
    }
    
    DbgPrint("[PCI] Bus %02X, Dev %02X, Func %X → NUMA Node %lu\n",
             busNumber, deviceNumber, functionNumber, *NumaNode);
    
    return STATUS_SUCCESS;
}
```

### 3. NUMA-Aware DMA Allocation

```c
// DmaAllocation.c - Allocate DMA buffers on local NUMA node
PVOID AllocateDmaBufferOnNumaNode(
    _In_ ULONG NumaNode,
    _In_ SIZE_T Size,
    _Out_ PHYSICAL_ADDRESS* PhysicalAddress
)
{
    // Specify NUMA node preference
    PHYSICAL_ADDRESS lowestAcceptable = {0};
    PHYSICAL_ADDRESS highestAcceptable;
    highestAcceptable.QuadPart = -1;  // No upper limit
    PHYSICAL_ADDRESS boundaryAddress = {0};  // No alignment boundary
    
    // Use MmAllocateContiguousNodeMemory (Windows 10 1803+)
    PVOID virtualAddress = MmAllocateContiguousNodeMemory(
        Size,
        lowestAcceptable,
        highestAcceptable,
        boundaryAddress,
        PAGE_SIZE,                  // Cache alignment
        NumaNode                    // Preferred NUMA node
    );
    
    if (!virtualAddress) {
        DbgPrint("[ERROR] Failed to allocate %Iu bytes on NUMA node %lu\n", Size, NumaNode);
        return NULL;
    }
    
    // Get physical address for DMA
    *PhysicalAddress = MmGetPhysicalAddress(virtualAddress);
    
    DbgPrint("[DMA] Allocated %Iu bytes: VA %p, PA 0x%016llX (NUMA node %lu)\n",
             Size, virtualAddress, PhysicalAddress->QuadPart, NumaNode);
    
    return virtualAddress;
}

// Allocate ring buffer on local NUMA node
NTSTATUS AllocateRingBufferNumaAware(
    _In_ ULONG NumaNode,
    _In_ ULONG Capacity,
    _In_ ULONG EventSize,
    _Out_ RING_BUFFER** RingBuffer
)
{
    SIZE_T totalSize = sizeof(RING_BUFFER_HEADER) + (Capacity * EventSize);
    PHYSICAL_ADDRESS physicalAddress;
    
    PVOID buffer = AllocateDmaBufferOnNumaNode(NumaNode, totalSize, &physicalAddress);
    if (!buffer) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize ring buffer structure
    RING_BUFFER* ringBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                                     sizeof(RING_BUFFER),
                                                     'bgnR');  // 'Rngb' = Ring buffer
    if (!ringBuffer) {
        MmFreeContiguousMemory(buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    ringBuffer->Header = (RING_BUFFER_HEADER*)buffer;
    ringBuffer->Buffer = (UCHAR*)buffer + sizeof(RING_BUFFER_HEADER);
    ringBuffer->Capacity = Capacity;
    ringBuffer->EventSize = EventSize;
    ringBuffer->PhysicalAddress = physicalAddress;
    ringBuffer->NumaNode = NumaNode;
    
    // Initialize header
    ringBuffer->Header->WriteIndex = 0;
    ringBuffer->Header->ReadIndex = 0;
    ringBuffer->Header->Capacity = Capacity;
    ringBuffer->Header->EventSize = EventSize;
    
    *RingBuffer = ringBuffer;
    return STATUS_SUCCESS;
}
```

### 4. MSI-X Interrupt Affinity

```c
// InterruptConfig.c - Configure MSI-X with CPU affinity
NTSTATUS ConfigureMsiXAffinity(
    _In_ DEVICE_OBJECT* DeviceObject,
    _In_ ULONG NumaNode,
    _In_ GROUP_AFFINITY* NodeAffinity
)
{
    // Get interrupt information from NDIS
    NDIS_HANDLE miniportHandle = GetMiniportHandle(DeviceObject);
    
    IO_INTERRUPT_MESSAGE_INFO* messageInfo;
    NTSTATUS status = IoGetInterruptMessageInfo(DeviceObject, &messageInfo);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[ERROR] Failed to get interrupt message info: 0x%08X\n", status);
        return status;
    }
    
    ULONG vectorCount = messageInfo->MessageCount;
    DbgPrint("[MSI-X] Device has %lu interrupt vectors\n", vectorCount);
    
    // Set affinity for each MSI-X vector
    for (ULONG i = 0; i < vectorCount; i++) {
        IO_INTERRUPT_MESSAGE_INFO_ENTRY* entry = &messageInfo->MessageInfo[i];
        
        // Set affinity to local NUMA node CPUs
        status = IoSetInterruptAffinity(
            entry->MessageAddress,
            NodeAffinity
        );
        
        if (!NT_SUCCESS(status)) {
            DbgPrint("[WARN] Failed to set affinity for vector %lu: 0x%08X\n", i, status);
            continue;
        }
        
        DbgPrint("[MSI-X] Vector %lu: Affinity set to NUMA node %lu (Group %u, Mask 0x%016llX)\n",
                 i, NumaNode, NodeAffinity->Group, NodeAffinity->Mask);
    }
    
    return STATUS_SUCCESS;
}
```

### 5. INF Configuration (Multi-Instance)

```ini
; IntelAvbFilter.inf - Multi-NIC support

[Version]
Signature="$Windows NT$"
Class=NetService
ClassGUID={4D36E974-E325-11CE-BFC1-08002BE10318}
Provider=%ManufacturerName%
DriverVer=01/15/2025,1.0.0.0

[Manufacturer]
%ManufacturerName%=Standard,NTamd64

[Standard.NTamd64]
%IntelAvbFilter.DeviceDesc%=IntelAvbFilter_Install, PCI\VEN_8086&DEV_15F3  ; I225-V

[IntelAvbFilter_Install]
CopyFiles=IntelAvbFilter.CopyFiles

[IntelAvbFilter_Install.Services]
AddService=IntelAvbFilter,,IntelAvbFilter_Service

[IntelAvbFilter_Service]
DisplayName=%IntelAvbFilter.ServiceDesc%
ServiceType=1  ; SERVICE_KERNEL_DRIVER
StartType=3    ; SERVICE_DEMAND_START
ErrorControl=1 ; SERVICE_ERROR_NORMAL
ServiceBinary=%12%\IntelAvbFilter.sys

[IntelAvbFilter_Install.HW]
AddReg=IntelAvbFilter.HW.Reg

[IntelAvbFilter.HW.Reg]
; Per-NIC ring buffer size (default: 8192 events)
HKR,Parameters,RingBufferSize,0x00010001,8192

; Event queue depth (default: 256 events)
HKR,Parameters,EventQueueDepth,0x00010001,256

; NUMA-aware allocation (1 = enabled, 0 = disabled)
HKR,Parameters,NumaAwareAllocation,0x00010001,1

; MSI-X affinity policy (0 = automatic, 1 = per-NUMA-node)
HKR,Parameters,MsiXAffinityPolicy,0x00010001,1

[IntelAvbFilter.CopyFiles]
IntelAvbFilter.sys

[DestinationDirs]
IntelAvbFilter.CopyFiles=12  ; %windir%\system32\drivers

[Strings]
ManufacturerName="Intel Corporation"
IntelAvbFilter.DeviceDesc="Intel AVB Filter Driver"
IntelAvbFilter.ServiceDesc="Intel AVB Filter Service"
```

### 6. PowerShell Deployment Script

```powershell
# Deploy-IntelAvbFilter.ps1 - Automated multi-NIC deployment

param(
    [switch]$Verbose = $false
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# 1. Detect I225-V NICs
Write-Host "[1/6] Detecting Intel I225-V NICs..." -ForegroundColor Cyan
$nics = Get-PnpDevice | Where-Object {
    $_.InstanceId -like "PCI\VEN_8086&DEV_15F3*"
}

if ($nics.Count -eq 0) {
    Write-Error "No Intel I225-V NICs found!"
    exit 1
}

Write-Host "  Found $($nics.Count) NIC(s)" -ForegroundColor Green

# 2. Check NUMA topology
Write-Host "[2/6] Checking NUMA topology..." -ForegroundColor Cyan
$numaNodes = Get-WmiObject -Class Win32_ComputerSystem | Select-Object -ExpandProperty NumberOfLogicalProcessors
$numaNodeCount = (Get-WmiObject -Query "SELECT * FROM Win32_Processor").Count

Write-Host "  NUMA nodes: $numaNodeCount" -ForegroundColor Green
Write-Host "  Total cores: $numaNodes" -ForegroundColor Green

# 3. Install driver INF
Write-Host "[3/6] Installing driver INF..." -ForegroundColor Cyan
$infPath = Join-Path $PSScriptRoot "IntelAvbFilter.inf"
if (-not (Test-Path $infPath)) {
    Write-Error "INF file not found: $infPath"
    exit 1
}

pnputil.exe /add-driver $infPath /install | Out-Host
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to install INF (exit code: $LASTEXITCODE)"
    exit $LASTEXITCODE
}

Write-Host "  INF installed successfully" -ForegroundColor Green

# 4. Configure registry settings (per-NIC)
Write-Host "[4/6] Configuring registry settings..." -ForegroundColor Cyan
foreach ($nic in $nics) {
    $instanceId = $nic.InstanceId
    $regPath = "HKLM:\SYSTEM\CurrentControlSet\Enum\$instanceId\Device Parameters"
    
    if (Test-Path $regPath) {
        Set-ItemProperty -Path $regPath -Name "RingBufferSize" -Value 8192 -Type DWord
        Set-ItemProperty -Path $regPath -Name "EventQueueDepth" -Value 256 -Type DWord
        Set-ItemProperty -Path $regPath -Name "NumaAwareAllocation" -Value 1 -Type DWord
        Set-ItemProperty -Path $regPath -Name "MsiXAffinityPolicy" -Value 1 -Type DWord
        
        Write-Host "  Configured NIC: $instanceId" -ForegroundColor Green
    }
}

# 5. Start driver service
Write-Host "[5/6] Starting driver service..." -ForegroundColor Cyan
sc.exe start IntelAvbFilter | Out-Host
if ($LASTEXITCODE -ne 0 -and $LASTEXITCODE -ne 1056) {  # 1056 = already running
    Write-Error "Failed to start service (exit code: $LASTEXITCODE)"
    exit $LASTEXITCODE
}

Write-Host "  Service started" -ForegroundColor Green

# 6. Validate deployment
Write-Host "[6/6] Validating deployment..." -ForegroundColor Cyan
$service = Get-Service -Name "IntelAvbFilter" -ErrorAction SilentlyContinue
if ($service -and $service.Status -eq "Running") {
    Write-Host "  ✓ Service running" -ForegroundColor Green
} else {
    Write-Error "Service not running!"
    exit 1
}

# Check Event Log for configuration messages
$events = Get-WinEvent -LogName "System" -FilterXPath "*[System[Provider[@Name='IntelAvbFilter'] and (EventID=100 or EventID=101)]]" -MaxEvents 20 -ErrorAction SilentlyContinue
if ($events) {
    Write-Host "  ✓ Configuration logged to Event Log" -ForegroundColor Green
    if ($Verbose) {
        $events | ForEach-Object { Write-Host "    $($_.Message)" -ForegroundColor Gray }
    }
}

Write-Host "`n✓ Deployment completed successfully!" -ForegroundColor Green
Write-Host "  NICs configured: $($nics.Count)" -ForegroundColor Green
Write-Host "  NUMA nodes: $numaNodeCount" -ForegroundColor Green
```

---

## Validation Criteria

### 1. Automated Configuration
- ✅ **NUMA Detection**: KeQueryNodeActiveAffinity() succeeds for all nodes
- ✅ **PCI Mapping**: All 10 NICs correctly mapped to NUMA nodes
- ✅ **Registry Settings**: Per-NIC ring buffer size, queue depth read successfully
- ✅ **No Manual Editing**: Administrator does not edit registry manually

### 2. Interrupt Affinity
- ✅ **Affinity Set**: All 40 MSI-X vectors (4 per NIC) have CPU affinity
- ✅ **NUMA Locality**: Vectors for NICs on NUMA node 0 → CPUs 0-15, node 1 → CPUs 16-31
- ✅ **Load Balancing**: Vectors distributed across CPUs (no single CPU handles >4 vectors)
- ✅ **Verification**: Query interrupt affinity via `Get-NetAdapterHardwareInfo` (PowerShell)

### 3. DMA Allocation
- ✅ **NUMA-Local Memory**: Ring buffers allocated on same NUMA node as NIC
- ✅ **Contiguous Memory**: Each ring buffer is physically contiguous (required for DMA)
- ✅ **Cache Alignment**: Buffers aligned to 4KB page boundaries
- ✅ **No Allocation Failures**: All 10 ring buffers allocated successfully

### 4. Deployment Time
- ✅ **Total Time**: <30 minutes from script start to all NICs operational
- ✅ **Administrator Actions**: <5 manual steps (run script, reboot if needed)
- ✅ **Rollback Time**: <5 minutes (uninstall INF, reboot)

### 5. Diagnostics
- ✅ **Event Log Entries**: Configuration logged (NUMA mapping, interrupt affinity)
- ✅ **Error Messages**: Clear diagnostics if allocation fails (e.g., "NUMA node 1 memory exhausted")
- ✅ **Health Check**: PowerShell script validates all NICs initialized

---

## Test Cases

### TC-1: Dual-Socket Server (10 NICs)
**Hardware**: 2 × Intel Xeon E5-2690v4 (2 × 14 cores), 10 × I225-V NICs  
**Topology**:
- NUMA node 0: CPUs 0-13, 5 NICs on buses 0x00-0x04
- NUMA node 1: CPUs 14-27, 5 NICs on buses 0x80-0x84

**Steps**:
1. Run `Deploy-IntelAvbFilter.ps1`
2. Check Event Log for NUMA mapping messages
3. Query interrupt affinity: `Get-NetAdapterHardwareInfo`
4. Verify DMA buffers allocated on correct NUMA nodes (driver debug log)

**Expected**:
- All 10 NICs initialized within 30 minutes
- NICs on NUMA node 0: Interrupt affinity to CPUs 0-13
- NICs on NUMA node 1: Interrupt affinity to CPUs 14-27
- Ring buffers allocated from local NUMA node memory
- Event Log: 10 entries showing successful configuration

### TC-2: Single-Socket Server (4 NICs)
**Hardware**: 1 × Intel Core i9-13900K (24 cores), 4 × I225-V NICs  
**Topology**:
- NUMA node 0: CPUs 0-23, all 4 NICs on buses 0x00-0x03

**Steps**:
1. Run deployment script
2. Verify all NICs on NUMA node 0
3. Check interrupt distribution across 24 CPUs

**Expected**:
- All 4 NICs initialized within 15 minutes
- Interrupt affinity: 16 vectors distributed across CPUs 0-23
- Ring buffers allocated from NUMA node 0 memory
- No NUMA-related warnings in Event Log

### TC-3: Deployment Failure (Insufficient Memory)
**Scenario**: Simulate NUMA node 1 memory exhaustion (artificially limit available memory)  
**Steps**:
1. Use Test Mode to limit NUMA node 1 memory to 512 MB
2. Run deployment script
3. Expect allocation failure for NICs on NUMA node 1

**Expected**:
- NICs on NUMA node 0: Initialize successfully
- NICs on NUMA node 1: Fail with STATUS_INSUFFICIENT_RESOURCES
- Event Log: Error messages indicating NUMA node 1 memory shortage
- Administrator can identify issue within 5 minutes from logs

### TC-4: Manual Reconfiguration (Change Ring Buffer Size)
**Scenario**: Administrator wants larger ring buffers (16384 instead of 8192)  
**Steps**:
1. Stop IntelAvbFilter service
2. Edit registry: Set RingBufferSize = 16384 for all NICs
3. Restart service
4. Verify new buffer size via driver debug log

**Expected**:
- Service restart succeeds
- All 10 NICs allocate 16384-slot ring buffers
- No reboot required
- Configuration change takes <5 minutes

---

## Risk Analysis

### Risk 1: Incorrect NUMA Mapping (Bus-Based Heuristic)
**Likelihood**: Medium  
**Impact**: Medium (suboptimal performance, not functional failure)  
**Mitigation**:
- Enhance PCI-to-NUMA mapping using ACPI SRAT (System Resource Affinity Table)
- Validate mapping against Windows Device Manager topology
- Allow manual override via registry (NumaNodeOverride parameter)

### Risk 2: DMA Allocation Failure (NUMA Memory Exhausted)
**Likelihood**: Low  
**Impact**: High (NIC initialization fails)  
**Mitigation**:
- Fallback to non-NUMA allocation (MmAllocateContiguousMemory without node hint)
- Log warning to Event Log (performance degradation expected)
- Reduce ring buffer size if allocation fails (8192 → 4096 → 2048 slots)

### Risk 3: MSI-X Affinity Not Supported (Older Chipsets)
**Likelihood**: Low (Windows 10+ supports MSI-X affinity)  
**Impact**: Low (automatic load balancing by Windows)  
**Mitigation**:
- Check IoSetInterruptAffinity() return status
- If unsupported, log informational message (not an error)
- System still functional, just not NUMA-optimized

### Risk 4: Registry Corruption (Incomplete Uninstall)
**Likelihood**: Low  
**Impact**: Medium (stale settings affect new installation)  
**Mitigation**:
- Uninstall script deletes all registry keys under Device Parameters
- INF includes DefaultUninstall section to clean up
- Provide "Reset to Defaults" PowerShell script

### Risk 5: Administrator Error (Wrong INF File)
**Likelihood**: Medium  
**Impact**: Low (PnP manager rejects mismatched INF)  
**Mitigation**:
- INF includes device-specific hardware IDs (VEN_8086&DEV_15F3)
- Deployment script validates INF path before installation
- Error message: "INF does not match any installed devices"

---

## Trade-offs

### Automatic NUMA Mapping vs. Manual Configuration
- **Benefit**: Zero administrator intervention, optimal performance
- **Cost**: Heuristic may be incorrect on non-standard topologies
- **Justification**: Correct for 95% of Intel platforms, manual override available

### NUMA-Local DMA vs. Global Memory Pool
- **Benefit**: Lower latency (~30% reduction for cross-NUMA access)
- **Cost**: Fragmentation risk if NUMA nodes have unbalanced memory
- **Justification**: Performance critical for TSN, fallback to global pool if needed

### INF-Based Configuration vs. Separate Config Tool
- **Benefit**: Standard Windows installation flow, no custom tools
- **Cost**: Limited UI for advanced settings
- **Justification**: Simpler deployment, PowerShell scripts for advanced use

---

## Traceability

### Requirements Verified
- **#44** (Hardware Detection): Automatic NIC enumeration
- **#134** (Multi-NIC Support): Independent configuration per NIC
- **#46** (Performance): NUMA-aware allocation reduces latency

### Architecture Decisions Satisfied
- **#134** (Multi-NIC Architecture): Per-instance isolation
- **#93** (Lock-Free Ring Buffer): Per-NIC buffers on local NUMA node
- **#91** (BAR0 MMIO Access): DMA buffers for hardware access

### Architecture Views Referenced
- [Physical View - Interrupt Mapping](../views/physical/physical-view-interrupt-mapping.md): MSI-X topology
- [Development View - Module Structure](../views/development/development-view-module-structure.md): Build configuration

---

## Implementation Checklist

- [ ] **Phase 1**: Implement NUMA detection (~2 hours)
  - KeQueryNodeActiveAffinity() wrapper
  - Log NUMA topology to debug output
  - Unit test: Mock 2-node topology

- [ ] **Phase 2**: Implement PCI-to-NUMA mapping (~4 hours)
  - Query PCI bus/device/function from PDO
  - Heuristic: Bus 0x00-0x7F → node 0, 0x80-0xFF → node 1
  - Future: Parse ACPI SRAT table for accurate mapping

- [ ] **Phase 3**: Implement NUMA-aware DMA allocation (~3 hours)
  - MmAllocateContiguousNodeMemory() wrapper
  - Fallback to global pool if node allocation fails
  - Test: Validate physical addresses are on correct node

- [ ] **Phase 4**: Implement MSI-X affinity configuration (~3 hours)
  - IoSetInterruptAffinity() for each vector
  - Map vectors to local NUMA node CPUs
  - Test: Query affinity via WMI/PowerShell

- [ ] **Phase 5**: Create INF and deployment script (~4 hours)
  - Multi-instance INF with registry parameters
  - PowerShell script for automated deployment
  - Test: Deploy on VM with 4 virtual NICs

- [ ] **Phase 6**: Write unit and integration tests (~4 hours)
  - TC-1: Dual-socket server test
  - TC-2: Single-socket server test
  - TC-3: Deployment failure test
  - TC-4: Manual reconfiguration test

**Estimated Total**: ~20 hours

---

## Acceptance Criteria

| Criterion | Threshold | Measurement |
|-----------|-----------|-------------|
| Installation time | <30 minutes | Script start to all NICs operational |
| Administrator actions | <5 manual steps | Count of manual interventions |
| Configuration accuracy | 100% | All NUMA/interrupt/DMA mappings correct |
| Deployment success rate | >99% | Success on first attempt (correct hardware) |
| Documentation completeness | 100% | All steps in deployment guide |
| Error diagnosis time | <5 minutes | Time to identify misconfiguration from logs |

---

**Scenario Status**: Ready for implementation  
**Next Steps**: Create GitHub issue linking to Requirements #44, #134, #46
