# C4 Architecture Diagrams (Mermaid Format)

**Purpose**: Visual architecture documentation using C4 model (Context, Container, Component, Code)

---

## C4 Level 1: System Context

Shows the Intel AVB Filter Driver in its operating environment with users and external systems.

```mermaid
graph TB
    %% Actors
    AVBApp["AVB/TSN Application<br/>(User-Mode)<br/><br/>• Audio streaming<br/>• Industrial control<br/>• Automotive"]
    
    %% System boundary
    subgraph "Intel AVB Filter Driver System"
        Driver["IntelAvbFilter.sys<br/>(NDIS 6.0 LWF)<br/><br/>• PTP clock control<br/>• TSN configuration<br/>• Hardware timestamping"]
    end
    
    %% External systems
    WindowsNetwork["Windows Network Stack<br/>(NDIS Framework)<br/><br/>• Packet routing<br/>• Protocol drivers<br/>• Filter chain"]
    IntelMiniport["Intel Miniport Driver<br/>(e1000e/igb)<br/><br/>• Vendor: 0x8086<br/>• Devices: i210, i217, i219,<br/>  i225, i226, i350, etc."]
    EthernetHW["Intel Ethernet Hardware<br/>(PCI Device)<br/><br/>• IEEE 1588 PTP clock<br/>• IEEE 802.1Qbv TAS<br/>• IEEE 802.1Qav CBS"]
    
    %% Relationships
    AVBApp -->|"IOCTL requests<br/>(DeviceIoControl)"| Driver
    Driver -->|"Packet filter hooks<br/>(Send/Receive)"| WindowsNetwork
    WindowsNetwork -->|"Attach/Detach callbacks<br/>OID requests"| IntelMiniport
    Driver -.->|"Direct BAR0 MMIO<br/>(bypasses miniport)"| EthernetHW
    IntelMiniport -->|"Standard Ethernet I/O<br/>(DMA, interrupts)"| EthernetHW
    
    %% Styling
    classDef external fill:#e1f5ff,stroke:#333,stroke-width:2px
    classDef system fill:#ffeb3b,stroke:#333,stroke-width:3px
    classDef actor fill:#b3e5fc,stroke:#333,stroke-width:2px
    
    class WindowsNetwork,IntelMiniport,EthernetHW external
    class Driver system
    class AVBApp actor
```

**Key Context Points**:
- **User**: AVB/TSN applications (audio streaming, industrial control)
- **System**: IntelAvbFilter.sys (NDIS 6.0 Lightweight Filter)
- **External Systems**:
  - Windows NDIS Framework (packet routing)
  - Intel Miniport Drivers (base Ethernet functionality)
  - Intel Ethernet Hardware (PTP/TSN capabilities)
- **Key Relationship**: Direct BAR0 MMIO bypasses miniport for low-latency PTP control

---

## C4 Level 2: Container Diagram

Shows high-level technical building blocks within the driver system.

```mermaid
graph TB
    %% User-mode
    AVBApp["AVB/TSN Application<br/>(User-Mode)"]
    
    %% System boundary
    subgraph "IntelAvbFilter.sys (Kernel-Mode)"
        subgraph "NDIS Filter Core"
            FilterEntry["DriverEntry<br/>filter.c<br/><br/>• NDIS registration<br/>• Global init"]
            FilterLifecycle["Filter Lifecycle<br/>filter.c<br/><br/>• FilterAttach<br/>• FilterDetach<br/>• FilterRestart<br/>• FilterPause"]
            PacketHooks["Packet Hooks<br/>filter.c<br/><br/>• FilterSendNetBufferLists<br/>• FilterReceiveNetBufferLists"]
            DeviceObject["Device Object<br/>device.c<br/><br/>• /Device/IntelAvbFilter<br/>• IOCTL dispatcher"]
        end
        
        subgraph "AVB Integration Layer"
            AVBContext["AVB Context Manager<br/>avb_integration_fixed.c<br/><br/>• AvbInitializeDevice<br/>• AvbHandleIoctl<br/>• AvbCleanupDevice"]
            StateMachine["HW State Machine<br/><br/>UNBOUND → BOUND<br/>→ BAR_MAPPED → PTP_READY"]
        end
        
        subgraph "Hardware Access Layer"
            BAR0Discovery["BAR0 Discovery<br/>avb_bar0_discovery.c<br/><br/>• PCI config read<br/>• Physical address"]
            BAR0Mapping["BAR0 Mapping<br/>avb_bar0_enhanced.c<br/><br/>• MmMapIoSpace<br/>• MMIO base address"]
            RegisterAccess["Register Access<br/>avb_hardware_access.c<br/><br/>• AvbReadRegister32<br/>• AvbWriteRegister32"]
        end
        
        subgraph "Device Abstraction Layer"
            DeviceRegistry["Device Registry<br/>intel_device_registry.c<br/><br/>• Device lookup<br/>• Ops dispatch"]
            DeviceOpsInterface["Device Ops Interface<br/>intel_device_interface.h<br/><br/>• intel_device_ops_t<br/>• 28 operations"]
        end
        
        subgraph "Device Implementations"
            I210Impl["i210 Implementation<br/>intel_i210_impl.c"]
            I226Impl["i226 Implementation<br/>intel_i226_impl.c"]
            OtherImpls["6 more implementations<br/>i217, i219, i225,<br/>i350, 82575, 82576, 82580"]
        end
    end
    
    %% External
    NDIS["Windows NDIS Framework"]
    IntelHW["Intel Ethernet Hardware<br/>(BAR0 Registers)"]
    
    %% Relationships - User-mode to kernel
    AVBApp -->|"CreateFile + DeviceIoControl"| DeviceObject
    
    %% Relationships - NDIS Filter Core
    FilterEntry -->|"NdisFRegisterFilterDriver"| NDIS
    NDIS -->|"Attach callback"| FilterLifecycle
    FilterLifecycle -->|"Initialize AVB"| AVBContext
    PacketHooks -->|"Inspect packets"| AVBContext
    DeviceObject -->|"Route IOCTL"| AVBContext
    
    %% Relationships - AVB Integration
    AVBContext -->|"Discover HW"| BAR0Discovery
    AVBContext -->|"Map HW"| BAR0Mapping
    AVBContext -->|"Lookup device"| DeviceRegistry
    AVBContext -->|"Track state"| StateMachine
    
    %% Relationships - Hardware Access
    BAR0Discovery -->|"Query PCI config"| NDIS
    BAR0Mapping -->|"MmMapIoSpace"| IntelHW
    RegisterAccess -->|"MMIO read/write"| IntelHW
    
    %% Relationships - Device Abstraction
    DeviceRegistry -->|"Return ops"| DeviceOpsInterface
    AVBContext -->|"Call ops->set_systime()"| DeviceOpsInterface
    DeviceOpsInterface -->|"Dispatch to device"| I210Impl
    DeviceOpsInterface -->|"Dispatch to device"| I226Impl
    DeviceOpsInterface -->|"Dispatch to device"| OtherImpls
    I210Impl -->|"Register I/O"| RegisterAccess
    I226Impl -->|"Register I/O"| RegisterAccess
    OtherImpls -->|"Register I/O"| RegisterAccess
    
    %% Styling
    classDef coreLayer fill:#ffeb3b,stroke:#333,stroke-width:2px
    classDef avbLayer fill:#4caf50,stroke:#333,stroke-width:2px
    classDef hwLayer fill:#ff9800,stroke:#333,stroke-width:2px
    classDef deviceLayer fill:#9c27b0,stroke:#333,stroke-width:2px
    classDef implLayer fill:#e91e63,stroke:#333,stroke-width:2px
    classDef external fill:#e1f5ff,stroke:#333,stroke-width:2px
    
    class FilterEntry,FilterLifecycle,PacketHooks,DeviceObject coreLayer
    class AVBContext,StateMachine avbLayer
    class BAR0Discovery,BAR0Mapping,RegisterAccess hwLayer
    class DeviceRegistry,DeviceOpsInterface deviceLayer
    class I210Impl,I226Impl,OtherImpls implLayer
    class NDIS,IntelHW external
```

**Key Container Points**:
- **4 Major Layers**: NDIS Core → AVB Integration → Hardware Access → Device Abstraction
- **8 Device Implementations**: Strategy pattern for device-specific logic
- **State Machine**: UNBOUND → BOUND → BAR_MAPPED → PTP_READY
- **Direct MMIO**: Hardware Access Layer bypasses miniport for register I/O

---

## C4 Level 3: Component Diagram (AVB Integration Layer)

Detailed view of the AVB Integration Layer showing internal components and interactions.

```mermaid
graph TB
    %% External inputs
    NDISFilterCore["NDIS Filter Core<br/>(ARC-C-001)"]
    UserModeApp["User-Mode App"]
    
    subgraph "AVB Integration Layer (ARC-C-002)"
        subgraph "Context Management"
            AvbInit["AvbInitializeDevice<br/>avb_integration_fixed.c<br/><br/>1. Allocate context<br/>2. Discover hardware<br/>3. Map BAR0<br/>4. Validate PTP"]
            AvbCleanup["AvbCleanupDevice<br/>avb_integration_fixed.c<br/><br/>1. Call device cleanup<br/>2. Unmap BAR0<br/>3. Free context"]
            Context["AVB_DEVICE_CONTEXT<br/>avb_integration.h<br/><br/>• device_t intel_device<br/>• AVB_HW_STATE hw_state<br/>• INTEL_HARDWARE_CONTEXT*<br/>• Timestamp ring buffer<br/>• TSN config cache"]
        end
        
        subgraph "IOCTL Routing"
            AvbIoctl["AvbHandleIoctl<br/>avb_integration_fixed.c<br/><br/>1. Validate hw_state<br/>2. Parse IOCTL code<br/>3. Lookup device ops<br/>4. Dispatch to device<br/>5. Return result"]
            IoctlDispatch{"IOCTL Switch"}
        end
        
        subgraph "State Machine"
            StateUnbound["UNBOUND<br/><br/>Initial state"]
            StateBound["BOUND<br/><br/>Intel adapter<br/>attached"]
            StateBarMapped["BAR_MAPPED<br/><br/>Registers<br/>accessible"]
            StatePtpReady["PTP_READY<br/><br/>Clock<br/>operational"]
        end
        
        subgraph "Hardware Validation"
            AvbValidate["AvbValidateHardwareAccess<br/><br/>• Check mapping<br/>• Bounds check"]
            AvbPtpCheck["AvbValidatePtpClock<br/><br/>• Read SYSTIML twice<br/>• Verify increment"]
        end
    end
    
    %% External outputs
    HardwareAccessLayer["Hardware Access Layer<br/>(ARC-C-003)"]
    DeviceAbstractionLayer["Device Abstraction Layer<br/>(ARC-C-004)"]
    
    %% Relationships - Initialization
    NDISFilterCore -->|"FilterAttach"| AvbInit
    AvbInit -->|"Allocate"| Context
    AvbInit -->|"State: UNBOUND → BOUND"| StateBound
    AvbInit -->|"AvbDiscoverBar0"| HardwareAccessLayer
    AvbInit -->|"AvbMapBar0"| HardwareAccessLayer
    AvbInit -->|"State: BOUND → BAR_MAPPED"| StateBarMapped
    AvbInit -->|"Validate PTP"| AvbPtpCheck
    AvbPtpCheck -->|"AvbReadRegister32"| HardwareAccessLayer
    AvbPtpCheck -->|"State: BAR_MAPPED → PTP_READY"| StatePtpReady
    AvbInit -->|"intel_get_device_ops"| DeviceAbstractionLayer
    
    %% Relationships - IOCTL
    UserModeApp -->|"DeviceIoControl"| NDISFilterCore
    NDISFilterCore -->|"AvbHandleIoctl"| AvbIoctl
    AvbIoctl -->|"Check state"| StatePtpReady
    AvbIoctl -->|"Parse"| IoctlDispatch
    IoctlDispatch -->|"GET_SYSTIME"| DeviceAbstractionLayer
    IoctlDispatch -->|"SET_SYSTIME"| DeviceAbstractionLayer
    IoctlDispatch -->|"CONFIGURE_TAS"| DeviceAbstractionLayer
    IoctlDispatch -->|"CONFIGURE_CBS"| DeviceAbstractionLayer
    
    %% Relationships - Cleanup
    NDISFilterCore -->|"FilterDetach"| AvbCleanup
    AvbCleanup -->|"device->cleanup()"| DeviceAbstractionLayer
    AvbCleanup -->|"AvbUnmapBar0"| HardwareAccessLayer
    AvbCleanup -->|"Free"| Context
    AvbCleanup -->|"State: PTP_READY → UNBOUND"| StateUnbound
    
    %% Styling
    classDef contextMgmt fill:#4caf50,stroke:#333,stroke-width:2px
    classDef ioctlMgmt fill:#2196f3,stroke:#333,stroke-width:2px
    classDef stateMachine fill:#ff9800,stroke:#333,stroke-width:2px
    classDef validation fill:#9c27b0,stroke:#333,stroke-width:2px
    classDef external fill:#e1f5ff,stroke:#333,stroke-width:2px
    
    class AvbInit,AvbCleanup,Context contextMgmt
    class AvbIoctl,IoctlDispatch ioctlMgmt
    class StateUnbound,StateBound,StateBarMapped,StatePtpReady stateMachine
    class AvbValidate,AvbPtpCheck validation
    class NDISFilterCore,UserModeApp,HardwareAccessLayer,DeviceAbstractionLayer external
```

**Key Component Points**:
- **Context Management**: Allocate/deallocate per-adapter AVB context
- **IOCTL Routing**: Dispatch user-mode requests to device implementations
- **State Machine**: 4-state lifecycle (UNBOUND → BOUND → BAR_MAPPED → PTP_READY)
- **Hardware Validation**: Safety checks before register access

---

## C4 Level 3: Component Diagram (Device Abstraction Layer)

Strategy pattern implementation for device family abstraction.

```mermaid
graph TB
    %% External input
    AVBIntegration["AVB Integration Layer<br/>(ARC-C-002)"]
    
    subgraph "Device Abstraction Layer (ARC-C-004)"
        subgraph "Registry"
            Registry["Device Registry<br/>intel_device_registry.c<br/><br/>• Map device ID → ops<br/>• Registration at init"]
            RegistryTable[("Registry Table<br/><br/>INTEL_I210 → &i210_ops<br/>INTEL_I217 → &i217_ops<br/>INTEL_I219 → &i219_ops<br/>INTEL_I225 → &i225_ops<br/>INTEL_I226 → &i226_ops<br/>INTEL_I350 → &i350_ops<br/>INTEL_82575 → &e82575_ops<br/>INTEL_82576 → &e82576_ops<br/>INTEL_82580 → &e82580_ops")]
        end
        
        subgraph "Interface"
            DeviceOps["intel_device_ops_t<br/>intel_device_interface.h<br/><br/>• device_name<br/>• supported_capabilities<br/>• init, cleanup<br/>• set_systime, get_systime<br/>• setup_tas, setup_cbs<br/>• read_register, write_register<br/>• ... (28 operations)"]
        end
    end
    
    subgraph "Device Implementations"
        I210["i210_ops<br/>intel_i210_impl.c<br/><br/>• CAP_PTP | CAP_CBS<br/>• No TAS/FP support"]
        I226["i226_ops<br/>intel_i226_impl.c<br/><br/>• CAP_PTP | CAP_TAS<br/>  | CAP_CBS | CAP_FP<br/>• Full TSN support"]
        I219["i219_ops<br/>intel_i219_impl.c<br/><br/>• CAP_PTP (limited)<br/>• No TSN support"]
        Others["6 more implementations<br/>i217, i225, i350,<br/>82575, 82576, 82580"]
    end
    
    %% External output
    HardwareAccess["Hardware Access Layer<br/>(ARC-C-003)"]
    
    %% Relationships - Lookup
    AVBIntegration -->|"intel_get_device_ops(device_type)"| Registry
    Registry -->|"Lookup in table"| RegistryTable
    RegistryTable -->|"Return &i210_ops"| DeviceOps
    
    %% Relationships - Dispatch
    AVBIntegration -->|"ops->set_systime(dev, time)"| DeviceOps
    DeviceOps -->|"Virtual dispatch"| I210
    DeviceOps -->|"Virtual dispatch"| I226
    DeviceOps -->|"Virtual dispatch"| I219
    DeviceOps -->|"Virtual dispatch"| Others
    
    %% Relationships - Hardware access
    I210 -->|"AvbWriteRegister32(0xB600, value)"| HardwareAccess
    I226 -->|"AvbReadRegister32(0xB600)"| HardwareAccess
    I219 -->|"AvbWriteRegister32(0xB608, freq)"| HardwareAccess
    Others -->|"Register I/O"| HardwareAccess
    
    %% Relationships - Registration (at driver init)
    I210 -.->|"Register at DriverEntry"| RegistryTable
    I226 -.->|"Register at DriverEntry"| RegistryTable
    I219 -.->|"Register at DriverEntry"| RegistryTable
    Others -.->|"Register at DriverEntry"| RegistryTable
    
    %% Styling
    classDef registry fill:#9c27b0,stroke:#333,stroke-width:2px
    classDef interface fill:#2196f3,stroke:#333,stroke-width:2px
    classDef implementation fill:#e91e63,stroke:#333,stroke-width:2px
    classDef external fill:#e1f5ff,stroke:#333,stroke-width:2px
    
    class Registry,RegistryTable registry
    class DeviceOps interface
    class I210,I226,I219,Others implementation
    class AVBIntegration,HardwareAccess external
```

**Key Component Points**:
- **Strategy Pattern**: `intel_device_ops_t` interface + 8 implementations
- **Runtime Dispatch**: Lookup device ops by device ID, call function pointers
- **Optional Operations**: NULL for unsupported features (e.g., i210 has no TAS)
- **Extensibility**: Add new device = 1 new file + register in table

---

## Deployment Diagram

Shows how the driver is deployed in the Windows kernel environment.

```mermaid
graph TB
    subgraph "User-Mode (Ring 3)"
        AVBApp["AVB/TSN Application<br/>.exe<br/><br/>• CreateFile(//.//IntelAvbFilter)<br/>• DeviceIoControl(IOCTL_AVB_*)"]
        AppCRT["MSVCRT.dll<br/><br/>• C Runtime"]
        Kernel32["Kernel32.dll<br/><br/>• CreateFile<br/>• DeviceIoControl"]
    end
    
    subgraph "Kernel-Mode (Ring 0)"
        subgraph "Windows Kernel"
            NtKernel["ntoskrnl.exe<br/><br/>• I/O Manager<br/>• Memory Manager<br/>• PnP Manager"]
            HAL["hal.dll<br/><br/>• Hardware Abstraction<br/>• MMIO primitives"]
        end
        
        subgraph "NDIS Framework"
            NDIS["ndis.sys<br/><br/>• Filter framework<br/>• Packet routing<br/>• OID handling"]
        end
        
        subgraph "Intel AVB Filter Driver"
            IntelAvbFilter["IntelAvbFilter.sys<br/>(LWF Driver)<br/><br/>• NDIS 6.0 filter<br/>• Device object<br/>• AVB/TSN logic"]
        end
        
        subgraph "Intel Miniport Driver"
            E1000e["e1000e.sys<br/>(i219, i217)<br/><br/>• Miniport driver<br/>• Standard Ethernet"]
            IGB["igb.sys<br/>(i210, i350)<br/><br/>• Miniport driver<br/>• Standard Ethernet"]
            I225["i225.sys<br/>(i225, i226)<br/><br/>• Miniport driver<br/>• Standard Ethernet"]
        end
    end
    
    subgraph "Hardware (Ring -1)"
        IntelNIC["Intel Ethernet Controller<br/>(PCI Device)<br/><br/>• PCI Vendor: 0x8086<br/>• BAR0: 128KB MMIO<br/>• PTP/TSN hardware"]
    end
    
    %% Relationships - User to Kernel
    AVBApp -->|"Links to"| AppCRT
    AVBApp -->|"Links to"| Kernel32
    Kernel32 -->|"Syscall"| NtKernel
    
    %% Relationships - Kernel
    NtKernel -->|"I/O dispatch"| IntelAvbFilter
    IntelAvbFilter -->|"Links to"| NDIS
    IntelAvbFilter -->|"Links to"| NtKernel
    IntelAvbFilter -->|"Links to"| HAL
    NDIS -->|"Filter chain"| E1000e
    NDIS -->|"Filter chain"| IGB
    NDIS -->|"Filter chain"| I225
    
    %% Relationships - Miniport to Hardware
    E1000e -->|"DMA, interrupts"| IntelNIC
    IGB -->|"DMA, interrupts"| IntelNIC
    I225 -->|"DMA, interrupts"| IntelNIC
    
    %% Relationships - Filter to Hardware (direct)
    IntelAvbFilter -.->|"Direct BAR0 MMIO<br/>(MmMapIoSpace)"| IntelNIC
    
    %% Styling
    classDef userMode fill:#b3e5fc,stroke:#333,stroke-width:2px
    classDef kernelCore fill:#fff59d,stroke:#333,stroke-width:2px
    classDef ndisLayer fill:#c5e1a5,stroke:#333,stroke-width:2px
    classDef filterDriver fill:#ffeb3b,stroke:#333,stroke-width:3px
    classDef miniportDriver fill:#ffccbc,stroke:#333,stroke-width:2px
    classDef hardware fill:#e1f5ff,stroke:#333,stroke-width:2px
    
    class AVBApp,AppCRT,Kernel32 userMode
    class NtKernel,HAL kernelCore
    class NDIS ndisLayer
    class IntelAvbFilter filterDriver
    class E1000e,IGB,I225 miniportDriver
    class IntelNIC hardware
```

**Deployment Key Points**:
- **User-Mode**: AVB application uses standard Win32 APIs (CreateFile, DeviceIoControl)
- **Kernel-Mode**: Filter driver runs in kernel space, links to NDIS and HAL
- **Multiple Miniports**: Supports e1000e (i217/i219), igb (i210/i350), i225 (i225/i226)
- **Direct Hardware Access**: Filter uses MmMapIoSpace to access BAR0 directly
- **Filter Chain**: NDIS inserts filter between protocol and miniport

---

## Sequence Diagram: IOCTL Flow (Get PTP System Time)

Shows the detailed sequence of a user-mode IOCTL request to read PTP system time.

```mermaid
sequenceDiagram
    participant App as AVB Application<br/>(User-Mode)
    participant Kernel32 as Kernel32.dll
    participant IoMgr as I/O Manager<br/>(ntoskrnl.exe)
    participant FilterCore as NDIS Filter Core<br/>(filter.c)
    participant AVBInteg as AVB Integration<br/>(avb_integration_fixed.c)
    participant DeviceOps as Device Ops<br/>(intel_i210_impl.c)
    participant HWAccess as Hardware Access<br/>(avb_hardware_access.c)
    participant Hardware as Intel NIC<br/>(BAR0 Registers)
    
    App->>Kernel32: DeviceIoControl(hDevice, IOCTL_AVB_GET_SYSTIME, ...)
    Kernel32->>IoMgr: NtDeviceIoControlFile (syscall)
    IoMgr->>FilterCore: IRP_MJ_DEVICE_CONTROL<br/>FilterDeviceIoControl()
    
    FilterCore->>FilterCore: Extract IOCTL code: IOCTL_AVB_GET_SYSTIME
    FilterCore->>FilterCore: Validate input/output buffers
    FilterCore->>AVBInteg: AvbHandleIoctl(AvbContext, IOCTL_AVB_GET_SYSTIME, ...)
    
    AVBInteg->>AVBInteg: Check hw_state == PTP_READY
    alt State not ready
        AVBInteg-->>FilterCore: STATUS_DEVICE_NOT_READY
        FilterCore-->>IoMgr: Complete IRP (error)
        IoMgr-->>Kernel32: Return error
        Kernel32-->>App: FALSE (GetLastError)
    else State ready
        AVBInteg->>AVBInteg: Lookup device ops: intel_get_device_ops(INTEL_I210)
        AVBInteg->>DeviceOps: ops->get_systime(&device, &systime)
        
        DeviceOps->>HWAccess: AvbReadRegister32(mmio_base, 0x0B600)<br/>// SYSTIML
        HWAccess->>Hardware: READ_REGISTER_ULONG(BAR0 + 0x0B600)
        Hardware-->>HWAccess: 0x12345678 (low 32 bits)
        
        DeviceOps->>HWAccess: AvbReadRegister32(mmio_base, 0x0B604)<br/>// SYSTIMH
        HWAccess->>Hardware: READ_REGISTER_ULONG(BAR0 + 0x0B604)
        Hardware-->>HWAccess: 0x00000001 (high 32 bits)
        
        DeviceOps->>DeviceOps: Combine: systime = 0x0000000112345678
        DeviceOps-->>AVBInteg: STATUS_SUCCESS, systime
        
        AVBInteg->>AVBInteg: Copy systime to output buffer
        AVBInteg-->>FilterCore: STATUS_SUCCESS, BytesReturned=8
        
        FilterCore->>IoMgr: Irp->IoStatus.Status = STATUS_SUCCESS<br/>Irp->IoStatus.Information = 8
        FilterCore->>IoMgr: IoCompleteRequest(Irp)
        IoMgr-->>Kernel32: Return STATUS_SUCCESS
        Kernel32-->>App: TRUE, systime in output buffer
    end
    
    App->>App: Process systime (0x0000000112345678 nanoseconds)
```

**Sequence Key Points**:
- **Round-trip latency**: ~50µs total (syscall + dispatch + register I/O)
- **Direct register access**: Bypasses miniport (no OID overhead)
- **State validation**: Checks hw_state before hardware access
- **Strategy pattern**: Runtime dispatch to device-specific implementation
- **64-bit read**: Two 32-bit reads (SYSTIML + SYSTIMH) combined

---

## END OF C4 DIAGRAMS

**Next Steps**:
1. **Embed diagrams in GitHub Issues**: Copy Mermaid code into ADR/ARC-C issue descriptions
2. **Generate PNG/SVG**: Use Mermaid CLI or online tools for static images
3. **Create additional sequence diagrams** for:
   - Driver initialization (DriverEntry → FilterAttach → AvbInitializeDevice)
   - Packet send/receive flow
   - BAR0 discovery and mapping
   - State machine transitions
