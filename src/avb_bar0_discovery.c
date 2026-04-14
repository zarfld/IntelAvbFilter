/*++

Module Name:

    avb_bar0_discovery.c

Abstract:

    BAR0 hardware resource discovery implementation for Intel AVB Filter Driver
    Provides a path for an NDIS LWF to acquire MMIO (BAR0) for Intel NICs by
    querying PnP properties and reading PCI config via HAL.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_private.h"

// Some WDK SDKs do not declare this prototype in headers included by this project.
// Declare it here; the symbol is exported by ntoskrnl.
extern PDEVICE_OBJECT IoGetDeviceAttachmentBaseRef(_In_ PDEVICE_OBJECT DeviceObject);

// Simple case-insensitive substring search for wide strings
static
BOOLEAN
WideContainsInsensitive(
    _In_reads_(lenChars) const WCHAR* buf,
    _In_ USHORT lenChars,
    _In_ const WCHAR* needle
)
{
    if (!buf || !needle || lenChars == 0) return FALSE;

    // Compute needle length
    USHORT nlen = 0;
    while (needle[nlen] != L'\0') nlen++;
    if (nlen == 0 || nlen > lenChars) return FALSE;

    for (USHORT i = 0; i + nlen <= lenChars; ++i) {
        BOOLEAN match = TRUE;
        for (USHORT j = 0; j < nlen; ++j) {
            WCHAR c1 = buf[i + j];
            WCHAR c2 = needle[j];
            if (c1 >= L'a' && c1 <= L'z') c1 = (WCHAR)(c1 - L'a' + L'A');
            if (c2 >= L'a' && c2 <= L'z') c2 = (WCHAR)(c2 - L'a' + L'A');
            if (c1 != c2) { match = FALSE; break; }
        }
        if (match) return TRUE;
    }
    return FALSE;
}

// Whitelist based on friendly name (extra guard)
BOOLEAN
AvbIsSupportedIntelController(
    _In_ PMS_FILTER FilterModule,
    _Out_opt_ USHORT* OutVendorId,
    _Out_opt_ USHORT* OutDeviceId
)
{
    if (OutVendorId) *OutVendorId = 0;
    if (OutDeviceId) *OutDeviceId = 0;

    if (FilterModule == NULL || FilterModule->MiniportFriendlyName.Buffer == NULL) {
        DEBUGP(DL_ERROR, "AvbIsSupportedIntelController: NULL FilterModule or FriendlyName\n");
        return FALSE;
    }

    const WCHAR* name = FilterModule->MiniportFriendlyName.Buffer;
    USHORT lenChars = (USHORT)(FilterModule->MiniportFriendlyName.Length / sizeof(WCHAR));

    DEBUGP(DL_INFO, "AvbIsSupportedIntelController: Checking adapter: %wZ\n", &FilterModule->MiniportFriendlyName);
    
    if (!WideContainsInsensitive(name, lenChars, L"INTEL")) {
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: Not Intel device (no 'INTEL' string found)\n");
        return FALSE;
    }

    DEBUGP(DL_INFO, "AvbIsSupportedIntelController: Found Intel device, checking specific model...\n");

    if (WideContainsInsensitive(name, lenChars, L"I210")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = INTEL_DEV_I210_AT; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I210 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, INTEL_DEV_I210_AT);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I225")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = INTEL_DEV_I225_V; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I225 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, INTEL_DEV_I225_V);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I226")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = INTEL_DEV_I226_LM; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I226 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, INTEL_DEV_I226_LM);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I219")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = INTEL_DEV_I219_LM; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I219 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, INTEL_DEV_I219_LM);
        return TRUE; 
    }
    if (WideContainsInsensitive(name, lenChars, L"I217")) { 
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; 
        if (OutDeviceId) *OutDeviceId = INTEL_DEV_I217_LM; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I217 (VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, INTEL_DEV_I217_LM);
        return TRUE; 
    }

    // Also accept common marketing names
    if (WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I219") ||
        WideContainsInsensitive(name, lenChars, L"ETHERNET CONNECTION I217")) {
        if (OutVendorId) *OutVendorId = INTEL_VENDOR_ID; if (OutDeviceId) *OutDeviceId = INTEL_DEV_I219_LM; 
        DEBUGP(DL_INFO, "AvbIsSupportedIntelController: ? SUPPORTED - Intel I219/I217 (marketing name, VID:0x%04X, DID:0x%04X)\n", 
               INTEL_VENDOR_ID, INTEL_DEV_I219_LM);
        return TRUE;
    }

    // Explicitly reject unsupported Intel devices to prevent attachment failures
    if (WideContainsInsensitive(name, lenChars, L"82574L")) {
        DEBUGP(DL_WARN, "AvbIsSupportedIntelController: ? EXPLICITLY NOT SUPPORTED - Intel 82574L has no AVB/TSN features: %wZ\n", 
               &FilterModule->MiniportFriendlyName);
        return FALSE;
    }

    DEBUGP(DL_WARN, "AvbIsSupportedIntelController: ? NOT SUPPORTED - Intel device but no AVB/TSN support: %wZ\n", 
           &FilterModule->MiniportFriendlyName);
    return FALSE;
}

#ifndef PCI_WHICHSPACE_CONFIG
#define PCI_WHICHSPACE_CONFIG 0
#endif

typedef union _AVB_PCI_SLOT_NUMBER {
    struct {
        ULONG   FunctionNumber : 3;
        ULONG   DeviceNumber   : 5;
        ULONG   Reserved       : 24;
    } bits;
    ULONG AsULONG;
} AVB_PCI_SLOT_NUMBER, *PAVB_PCI_SLOT_NUMBER;

// Resolve PDO from the miniport device name by getting bottom attachment
static
NTSTATUS
AvbGetPdoFromFilter(
    _In_ PMS_FILTER FilterModule,
    _Outptr_ PDEVICE_OBJECT* PdoOut
)
{
    NTSTATUS status;
    PFILE_OBJECT fileObj = NULL;
    PDEVICE_OBJECT devTop = NULL;

    *PdoOut = NULL;

    if (FilterModule->MiniportName.Buffer == NULL || FilterModule->MiniportName.Length == 0) {
        return STATUS_OBJECT_NAME_INVALID;
    }

    status = IoGetDeviceObjectPointer(&FilterModule->MiniportName,
                                      FILE_READ_DATA,
                                      &fileObj,
                                      &devTop);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "AvbGetPdoFromFilter: IoGetDeviceObjectPointer failed 0x%x for %wZ\n", status, &FilterModule->MiniportName);
        return status;
    }

    // Resolve PDO using IoGetDeviceAttachmentBaseRef (exported by ntoskrnl)
    PDEVICE_OBJECT pdo = IoGetDeviceAttachmentBaseRef(devTop);

    ObDereferenceObject(fileObj);

    if (!pdo) {
        DEBUGP(DL_ERROR, "AvbGetPdoFromFilter: could not resolve PDO\n");
        return STATUS_UNSUCCESSFUL;
    }

    *PdoOut = pdo; // caller must ObDereferenceObject when done
    return STATUS_SUCCESS;
}

// Read a DWORD from PCI config using HAL
static
NTSTATUS
AvbReadPciConfigDword(
    _In_ ULONG BusNumber,
    _In_ AVB_PCI_SLOT_NUMBER Slot,
    _In_ ULONG Offset,
    _Out_ ULONG* Value
)
{
    ULONG read = HalGetBusDataByOffset(PCIConfiguration,
                                       BusNumber,
                                       Slot.AsULONG,
                                       Value,
                                       Offset,
                                       sizeof(ULONG));
    return (read == sizeof(ULONG)) ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

/* ===================================================================
 * ECAM (Enhanced Configuration Access Mechanism) PCI config reader
 * ===================================================================
 * On modern Intel PCH systems (I219/I217), HAL PCI config reads via
 * HalGetBusDataByOffset (legacy CF8/CFC port I/O) are blocked for
 * PCH-internal devices.  ECAM reads config space from MMIO whose base
 * is published in the ACPI MCFG table -- works for every PCIe endpoint
 * on UEFI systems.
 *
 * Reference: PCI Express Base Specification 3.0 ss7.2.2
 */

/* ACPI MCFG allocation entry (16 bytes, packed) */
#pragma pack(push, 1)
typedef struct _AVB_MCFG_ENTRY {
    ULONGLONG BaseAddress;   /* ECAM MMIO base for this segment */
    USHORT    SegmentGroup;
    UCHAR     StartBus;
    UCHAR     EndBus;
    ULONG     Reserved;
} AVB_MCFG_ENTRY;
#pragma pack(pop)

/* Entries begin at offset 44 = 36-byte ACPI header + 8-byte reserved */
#define AVB_MCFG_ENTRIES_OFFSET  44U

/* Query header for ZwQuerySystemInformation / SystemFirmwareTableInformation.
 * TableBuffer (variable-length MCFG data) follows this fixed 16-byte header
 * in the same allocation. */
typedef struct _AVB_FW_TABLE_QUERY {
    ULONG  ProviderSignature;   /* 'ACPI' = 0x49504341 */
    ULONG  Action;              /* 1 = Get               */
    ULONG  TableID;             /* 'MCFG' = 0x4746434D  */
    ULONG  TableBufferLength;   /* 0 on probe; needed size on output */
} AVB_FW_TABLE_QUERY;   /* 16 bytes; MCFG table data follows inline */

/* SystemFirmwareTableInformation = 76 (0x4C); available since Vista */
#define AVB_SFTI_CLASS (76UL)

/* ZwQuerySystemInformation is available in kernel mode but not always
 * prototyped in WDK KM headers for all info classes.  Declare explicitly. */
NTSYSAPI NTSTATUS NTAPI ZwQuerySystemInformation(
    _In_      ULONG  SystemInformationClass,
    _Inout_   PVOID  SystemInformation,
    _In_      ULONG  SystemInformationLength,
    _Out_opt_ PULONG ReturnLength);

/*
 * Retrieve the ECAM MMIO base for PCI bus BusNumber from ACPI MCFG.
 * Called once per discovery attempt; runs at PASSIVE_LEVEL.
 */
static NTSTATUS
AvbGetEcamBaseForBus(
    _In_  ULONG      BusNumber,
    _Out_ ULONGLONG *EcamBase,
    _Out_ UCHAR     *StartBus)
{
    *EcamBase = 0;
    *StartBus = 0;

    /* --- Probe: discover required table-data size --- */
    AVB_FW_TABLE_QUERY probe = { 0x49504341U, 1U, 0x4746434DU, 0U };
    ULONG returnLen = 0;
    NTSTATUS st = ZwQuerySystemInformation(AVB_SFTI_CLASS,
                                           &probe, sizeof(probe), &returnLen);
    if (st != STATUS_BUFFER_TOO_SMALL) {
        DEBUGP(DL_WARN, "?? ECAM: MCFG probe -> 0x%08X (table not available)\n", st);
        return NT_SUCCESS(st) ? STATUS_NOT_FOUND : st;
    }

    ULONG tableDataLen = (returnLen > (ULONG)sizeof(probe))
                        ?  returnLen - (ULONG)sizeof(probe)
                        : 512U;
    if (probe.TableBufferLength > tableDataLen) tableDataLen = probe.TableBufferLength;
    if (tableDataLen == 0 || tableDataLen > 65536U) return STATUS_INVALID_PARAMETER;

    /* --- Allocate and read --- */
    ULONG  allocSize = (ULONG)sizeof(AVB_FW_TABLE_QUERY) + tableDataLen;
    UCHAR *buf = (UCHAR *)ExAllocatePool2(POOL_FLAG_NON_PAGED, allocSize, 'ECav');
    if (!buf) return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(buf, allocSize);

    AVB_FW_TABLE_QUERY *sfti = (AVB_FW_TABLE_QUERY *)buf;
    sfti->ProviderSignature  = 0x49504341U;
    sfti->Action             = 1U;
    sfti->TableID            = 0x4746434DU;
    sfti->TableBufferLength  = tableDataLen;

    st = ZwQuerySystemInformation(AVB_SFTI_CLASS, sfti, allocSize, &returnLen);
    if (!NT_SUCCESS(st)) {
        DEBUGP(DL_WARN, "?? ECAM: MCFG read -> 0x%08X\n", st);
        ExFreePool(buf);
        return st;
    }

    /* --- Parse MCFG allocation entries --- */
    const UCHAR      *tableData = buf + sizeof(AVB_FW_TABLE_QUERY);
    ULONG             tableLen  = sfti->TableBufferLength;

    if (tableLen < AVB_MCFG_ENTRIES_OFFSET + (ULONG)sizeof(AVB_MCFG_ENTRY)) {
        DEBUGP(DL_WARN, "?? ECAM: MCFG table too small (%u bytes)\n", tableLen);
        ExFreePool(buf);
        return STATUS_INVALID_PARAMETER;
    }

    ULONG               numEntries = (tableLen - AVB_MCFG_ENTRIES_OFFSET)
                                   / (ULONG)sizeof(AVB_MCFG_ENTRY);
    const AVB_MCFG_ENTRY *ent      = (const AVB_MCFG_ENTRY *)
                                     (tableData + AVB_MCFG_ENTRIES_OFFSET);

    for (ULONG i = 0; i < numEntries; i++) {
        if (BusNumber >= ent[i].StartBus && BusNumber <= ent[i].EndBus) {
            *EcamBase = ent[i].BaseAddress;
            *StartBus = ent[i].StartBus;
            DEBUGP(DL_WARN,
                   "?? ECAM: MCFG[%u] Base=0x%llX StartBus=%u EndBus=%u covers bus %u\n",
                   i, ent[i].BaseAddress, ent[i].StartBus, ent[i].EndBus, BusNumber);
            ExFreePool(buf);
            return STATUS_SUCCESS;
        }
    }

    DEBUGP(DL_WARN, "?? ECAM: No MCFG entry covers bus %u (%u entries)\n",
           BusNumber, numEntries);
    ExFreePool(buf);
    return STATUS_NOT_FOUND;
}

/*
 * Read one DWORD from PCI config space via ECAM MMIO.
 * EcamBase / StartBus must be pre-resolved by AvbGetEcamBaseForBus.
 * Maps exactly 4 bytes; safe to call from PASSIVE_LEVEL.
 */
static NTSTATUS
AvbReadPciConfigDwordEcamDirect(
    _In_  ULONG               BusNumber,
    _In_  AVB_PCI_SLOT_NUMBER  Slot,
    _In_  ULONG               Offset,
    _Out_ ULONG              *Value,
    _In_  ULONGLONG            EcamBase,
    _In_  UCHAR                StartBus)
{
    *Value = 0xFFFFFFFFU;

    ULONG device   = Slot.bits.DeviceNumber;
    ULONG function = Slot.bits.FunctionNumber;

    /* ECAM: Base + ((Bus-Start)<<20) | (Dev<<15) | (Fn<<12) | Offset */
    ULONGLONG cfgPhys = EcamBase
        + (((ULONGLONG)(BusNumber - (ULONG)StartBus)) << 20)
        + ((ULONGLONG)device   << 15)
        + ((ULONGLONG)function << 12)
        + (ULONGLONG)(Offset & 0xFFFU);

    PHYSICAL_ADDRESS pa;
    pa.QuadPart = (LONGLONG)cfgPhys;

    PULONG va = (PULONG)MmMapIoSpace(pa, sizeof(ULONG), MmNonCached);
    if (!va) {
        DEBUGP(DL_WARN, "?? ECAM: MmMapIoSpace failed PA=0x%llX\n", pa.QuadPart);
        return STATUS_UNSUCCESSFUL;
    }

    *Value = READ_REGISTER_ULONG(va);
    MmUnmapIoSpace(va, sizeof(ULONG));

    DEBUGP(DL_WARN, "?? ECAM: bus=%u dev=%u fn=%u off=0x%03X -> 0x%08X\n",
           BusNumber, device, function, Offset, *Value);
    return STATUS_SUCCESS;
}

// Helper: known BAR lengths per Intel device, based on Intel datasheets (I210/I219/I225/I226)
static
ULONG
AvbGetIntelBarLengthByDeviceId(USHORT deviceId)
{
    switch (deviceId) {
        // I210 family (Datasheet 333016): 128KB CSR space
        case INTEL_DEV_I210_AT: // I210-AT
        case INTEL_DEV_I210_IS: // I210-IS
        case INTEL_DEV_I210_IT: // I210-IT
        case INTEL_DEV_I210_CS: // I210-CS/CL
        case INTEL_DEV_I210_FLASHLESS: // Flash-less variant
            return INTEL_BAR0_SIZE_128KB; // 128KB

        // I217/I219 family (PCH integrated MAC/PHY) � e1000e defines 128KB BAR
        case INTEL_DEV_I217_LM: // I217-LM
        case INTEL_DEV_I217_V: // I217-V
        case INTEL_DEV_I219_LM: // I219-LM
        case INTEL_DEV_I219_V: // I219-V
        case INTEL_DEV_I219_D0:
        case INTEL_DEV_I219_D1:
        case INTEL_DEV_I219_D2:
        case INTEL_DEV_I219_LM_DC7:
        case INTEL_DEV_I219_V6:
        case INTEL_DEV_I219_LM6:
            return INTEL_BAR0_SIZE_128KB; // 128KB

        // I225/I226 (Spec update/product briefs): 128KB CSR space
        case INTEL_DEV_I225_V: // I225
        case INTEL_DEV_I225_IT:
        case INTEL_DEV_I225_VARIANT:
        case INTEL_DEV_I226_LM: // I226
        case INTEL_DEV_I226_V:
        case INTEL_DEV_I226_IT:
            return INTEL_BAR0_SIZE_128KB; // 128KB

        default:
            return INTEL_BAR0_SIZE_128KB; // conservative default
    }
}

/*
 * Internal completion routine for synchronous PnP IRP dispatch.
 * Returns STATUS_MORE_PROCESSING_REQUIRED to keep the IRP alive until the
 * synchronous caller has read the final status and called IoFreeIrp.
 */
static NTSTATUS
AvbSyncIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PVOID          Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* GUID for BUS_INTERFACE_STANDARD (from wdm.h / initguid.h).
 * Declared here to avoid INITGUID-in-header link issues in kernel builds. */
static const GUID AVB_GUID_BUS_INTERFACE_STANDARD = {
    0x496B8280L, 0x6F25, 0x11D0,
    { 0xBE, 0xAF, 0x08, 0x00, 0x2B, 0xE2, 0x09, 0x2F }
};

/*
 * Read one DWORD from PCI config space via BUS_INTERFACE_STANDARD.
 *
 * Queries the interface fresh on each call (self-contained; this is only
 * used during initialization so the overhead is acceptable).
 *
 * The PCI bus driver services this for every PCIe function, including
 * PCH-integrated devices (I219/I217) where HalGetBusDataByOffset (CF8/CFC)
 * and ACPI MCFG access are blocked or unavailable.
 *
 * Must be called at PASSIVE_LEVEL.  The PDO must be referenced by the caller.
 */
static NTSTATUS
AvbReadPciConfigViaBusInterface(
    _In_  PDEVICE_OBJECT Pdo,
    _In_  ULONG          Offset,
    _Out_ ULONG         *Value)
{
    KEVENT                 event;
    BUS_INTERFACE_STANDARD busIf  = { 0 };
    NTSTATUS               status;
    PIO_STACK_LOCATION     ioStack;

    *Value = 0xFFFFFFFFU;

    KeInitializeEvent(&event, SynchronizationEvent, FALSE);

    PIRP irp = IoAllocateIrp(Pdo->StackSize, FALSE);
    if (!irp) return STATUS_INSUFFICIENT_RESOURCES;

    irp->IoStatus.Status      = STATUS_NOT_SUPPORTED;
    irp->IoStatus.Information = 0;

    IoSetCompletionRoutine(irp, AvbSyncIrpCompletion, &event, TRUE, TRUE, TRUE);

    ioStack = IoGetNextIrpStackLocation(irp);
    ioStack->MajorFunction = IRP_MJ_PNP;
    ioStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    ioStack->Parameters.QueryInterface.InterfaceType         = (LPGUID)&AVB_GUID_BUS_INTERFACE_STANDARD;
    ioStack->Parameters.QueryInterface.Size                  = sizeof(BUS_INTERFACE_STANDARD);
    ioStack->Parameters.QueryInterface.Version               = 1;
    ioStack->Parameters.QueryInterface.Interface             = (PINTERFACE)&busIf;
    ioStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    status = IoCallDriver(Pdo, irp);
    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
    }
    status = irp->IoStatus.Status;
    IoFreeIrp(irp);

    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_WARN, "?? BusIf: QueryInterface -> 0x%08X\n", status);
        return status;
    }

    ULONG n = busIf.GetBusData(busIf.Context,
                               PCI_WHICHSPACE_CONFIG,
                               Value,
                               Offset,
                               sizeof(ULONG));
    busIf.InterfaceDereference(busIf.Context);

    if (n != sizeof(ULONG)) {
        DEBUGP(DL_WARN, "?? BusIf: GetBusData at 0x%02X read %u byte(s)\n", Offset, n);
        return STATUS_UNSUCCESSFUL;
    }
    DEBUGP(DL_WARN, "?? BusIf: config[0x%02X] = 0x%08X\n", Offset, *Value);
    return STATUS_SUCCESS;
}

/**
 * Discover Intel controller hardware resources (BAR0) for MMIO mapping in LWF.
 * Strategy:
 *  1) Open the miniport device by name (AttachParameters->BaseMiniportName).
 *  2) Query bus number and device/function from PnP properties on that device.
 *  3) Read PCI config Vendor/Device IDs and BAR0 using HalGetBusDataByOffset.
 *  4) Return BAR0 physical address and a conservative MMIO size (128KB or per device ID).
 */
NTSTATUS
AvbDiscoverIntelControllerResources(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    ULONG busNumber = 0;
    ULONG address = 0;
    ULONG len = 0;
    AVB_PCI_SLOT_NUMBER slot = { 0 };
    ULONG id = 0, bar0lo = 0, bar0hi = 0;

    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResources (REAL HARDWARE BAR0 DISCOVERY)\n");

    if (!FilterModule || !Bar0Address || !Bar0Length) {
        DEBUGP(DL_ERROR, "AvbDiscoverIntelControllerResources: Invalid parameters\n");
        return STATUS_INVALID_PARAMETER;
    }

    Bar0Address->QuadPart = 0;
    *Bar0Length = 0;

    DEBUGP(DL_INFO, "?? STEP 1: Resolving PDO from filter module...\n");
    status = AvbGetPdoFromFilter(FilterModule, &pdo);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? STEP 1 FAILED: Failed to resolve PDO: 0x%x\n", status);
        return status;
    }
    DEBUGP(DL_INFO, "? STEP 1 SUCCESS: PDO resolved\n");

    DEBUGP(DL_INFO, "?? STEP 2: Reading PnP bus number...\n");
    status = IoGetDeviceProperty(pdo,
                                 DevicePropertyBusNumber,
                                 sizeof(busNumber),
                                 &busNumber,
                                 &len);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? STEP 2 FAILED: IoGetDeviceProperty(DevicePropertyBusNumber) failed: 0x%x\n", status);
        ObDereferenceObject(pdo);
        return status;
    }
    DEBUGP(DL_INFO, "? STEP 2 SUCCESS: Bus Number = %lu\n", busNumber);

    DEBUGP(DL_INFO, "?? STEP 3: Reading PnP device address...\n");
    len = 0;
    status = IoGetDeviceProperty(pdo,
                                 DevicePropertyAddress,
                                 sizeof(address),
                                 &address,
                                 &len);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? STEP 3 FAILED: IoGetDeviceProperty(DevicePropertyAddress) failed: 0x%x\n", status);
        ObDereferenceObject(pdo);
        return status;
    }

    /* Keep pdo referenced until BAR0 config reads complete.
     * BUS_INTERFACE_STANDARD fallback (added below) needs it for STEP 4-5. */
    slot.bits.DeviceNumber   = (address >> 16) & INTEL_MASK_16BIT;
    slot.bits.FunctionNumber = (address & INTEL_MASK_16BIT) & 0x7;
    DEBUGP(DL_WARN, "?? STEP 3 SUCCESS: bus=%lu dev=%lu fn=%lu\n",
           busNumber, slot.bits.DeviceNumber, slot.bits.FunctionNumber);

    /* Step 4: Read VID/DID.
     *  1st attempt: HAL (CF8/CFC) -- fast but blocked for PCH-integrated MACs.
     *  2nd attempt: ECAM direct MMIO via ACPI MCFG table.
     *  3rd attempt: BUS_INTERFACE_STANDARD PnP IRP -- always works via PCI driver. */
    ULONGLONG ecamBase  = 0;
    UCHAR     ecamStart = 0;
    BOOLEAN   useEcam   = FALSE;
    BOOLEAN   useBusIf  = FALSE;

    DEBUGP(DL_INFO, "?? STEP 4: Reading PCI Vendor/Device ID (HAL)...\n");
    status = AvbReadPciConfigDword(busNumber, slot, 0x00, &id);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_WARN, "?? STEP 4: HAL failed (0x%08X) -- trying ECAM\n", status);
        NTSTATUS ecSt = AvbGetEcamBaseForBus(busNumber, &ecamBase, &ecamStart);
        if (NT_SUCCESS(ecSt)) {
            status = AvbReadPciConfigDwordEcamDirect(busNumber, slot, 0x00, &id,
                                                     ecamBase, ecamStart);
            if (NT_SUCCESS(status)) {
                useEcam = TRUE;
            } else {
                DEBUGP(DL_ERROR, "? STEP 4 FAILED: ECAM VID read -> 0x%08X\n", status);
                ObDereferenceObject(pdo);
                return status;
            }
        } else {
            /* ECAM unavailable -- last resort: BUS_INTERFACE_STANDARD via PnP IRP */
            DEBUGP(DL_WARN, "?? STEP 4: ECAM unavail (0x%08X) -- trying BUS_INTERFACE_STANDARD\n", ecSt);
            status = AvbReadPciConfigViaBusInterface(pdo, 0x00, &id);
            if (NT_SUCCESS(status)) {
                useBusIf = TRUE;
            } else {
                DEBUGP(DL_ERROR, "? STEP 4 FAILED: HAL/ECAM/BusIf all failed\n");
                ObDereferenceObject(pdo);
                return STATUS_UNSUCCESSFUL;
            }
        }
    }

    USHORT ven = (USHORT)(id & INTEL_MASK_16BIT);
    USHORT dev = (USHORT)((id >> 16) & INTEL_MASK_16BIT);
    DEBUGP(DL_WARN, "? STEP 4 SUCCESS (%s): VID=0x%04X DID=0x%04X\n",
           useEcam ? "ECAM" : "HAL", ven, dev);

    if (ven != INTEL_VENDOR_ID) {
        DEBUGP(DL_ERROR, "? VALIDATION FAILED: Not an Intel device: VEN=0x%04x, DEV=0x%04x\n", ven, dev);
        ObDereferenceObject(pdo);
        return STATUS_DEVICE_NOT_READY;
    }

    /* Step 5: Read BAR0 (and BAR1 for 64-bit BARs) via the same method. */
    DEBUGP(DL_INFO, "?? STEP 5: Reading BAR0 (%s)...\n",
           useEcam ? "ECAM" : useBusIf ? "BusIf" : "HAL");
    if (useEcam) {
        status = AvbReadPciConfigDwordEcamDirect(busNumber, slot, 0x10, &bar0lo,
                                                 ecamBase, ecamStart);
    } else if (useBusIf) {
        status = AvbReadPciConfigViaBusInterface(pdo, 0x10, &bar0lo);
    } else {
        status = AvbReadPciConfigDword(busNumber, slot, 0x10, &bar0lo);
    }
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "? STEP 5 FAILED: Failed to read BAR0: 0x%x\n", status);
        ObDereferenceObject(pdo);
        return status;
    }

    if (bar0lo & 0x1) {
        DEBUGP(DL_ERROR, "? VALIDATION FAILED: BAR0 indicates I/O space, not MMIO: 0x%08x\n", bar0lo);
        ObDereferenceObject(pdo);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    ULONGLONG phys = (ULONGLONG)(bar0lo & ~0xFUL);

    /* Bits [2:1] == 0b10 -> 64-bit memory BAR (PCI spec s6.2.5.1) */
    if ((bar0lo & 0x6) == 0x4) {
        DEBUGP(DL_INFO, "?? STEP 5a: Reading BAR1 (high 32 bits) for 64-bit BAR...\n");
        if (useEcam) {
            status = AvbReadPciConfigDwordEcamDirect(busNumber, slot, 0x14, &bar0hi,
                                                     ecamBase, ecamStart);
        } else if (useBusIf) {
            status = AvbReadPciConfigViaBusInterface(pdo, 0x14, &bar0hi);
        } else {
            status = AvbReadPciConfigDword(busNumber, slot, 0x14, &bar0hi);
        }
        if (!NT_SUCCESS(status)) {
            DEBUGP(DL_ERROR, "? STEP 5a FAILED: Failed to read BAR1 (high 32 bits): 0x%x\n", status);
            ObDereferenceObject(pdo);
            return status;
        }
        phys |= ((ULONGLONG)bar0hi) << 32;
        DEBUGP(DL_WARN, "? STEP 5 SUCCESS: 64-bit BAR PA=0x%llX\n", phys);
    } else {
        DEBUGP(DL_WARN, "? STEP 5 SUCCESS: 32-bit BAR PA=0x%llX\n", phys);
    }

    Bar0Address->QuadPart = phys;
    *Bar0Length = AvbGetIntelBarLengthByDeviceId(dev);

    ObDereferenceObject(pdo);

    DEBUGP(DL_WARN,
           "?? DISCOVERY COMPLETE: VEN=0x%04X DID=0x%04X BAR0=0x%llX Len=0x%X (%s %s-bit)\n",
           ven, dev, Bar0Address->QuadPart, *Bar0Length,
           useEcam ? "ECAM" : useBusIf ? "BusIf" : "HAL",
           ((bar0lo & 0x6) == 0x4) ? "64" : "32");

    DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResources: SUCCESS\n");
    return STATUS_SUCCESS;
}

NTSTATUS
AvbDiscoverIntelControllerResourcesAlternative(
    _In_ PMS_FILTER FilterModule,
    _Out_ PPHYSICAL_ADDRESS Bar0Address,
    _Out_ PULONG Bar0Length
)
{
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    ULONG bufferSize = 0;
    PCM_RESOURCE_LIST resList = NULL;

    DEBUGP(DL_TRACE, "==>AvbDiscoverIntelControllerResourcesAlternative (PnP BootConfigTranslated)\n");

    if (!FilterModule || !Bar0Address || !Bar0Length) {
        return STATUS_INVALID_PARAMETER;
    }

    Bar0Address->QuadPart = 0;
    *Bar0Length = 0;

    /* Step 1: Get PDO from filter */
    status = AvbGetPdoFromFilter(FilterModule, &pdo);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "?? Alt STEP 1 FAILED: AvbGetPdoFromFilter: 0x%08X\n", status);
        return status;
    }

    /* Step 2: Query translated resource list size */
    (void)IoGetDeviceProperty(pdo,
                              DevicePropertyBootConfigurationTranslated,
                              0, NULL, &bufferSize);
    if (bufferSize == 0) {
        DEBUGP(DL_ERROR, "?? Alt STEP 2 FAILED: DevicePropertyBootConfigurationTranslated size=0\n");
        ObDereferenceObject(pdo);
        return STATUS_DEVICE_NOT_READY;
    }

    /* Step 3: Allocate and read the resource list */
    resList = (PCM_RESOURCE_LIST)ExAllocatePool2(POOL_FLAG_NON_PAGED, bufferSize, 'Lavb');
    if (!resList) {
        ObDereferenceObject(pdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IoGetDeviceProperty(pdo,
                                 DevicePropertyBootConfigurationTranslated,
                                 bufferSize, resList, &bufferSize);
    ObDereferenceObject(pdo);
    if (!NT_SUCCESS(status)) {
        DEBUGP(DL_ERROR, "?? Alt STEP 3 FAILED: IoGetDeviceProperty(BootConfigTranslated): 0x%08X\n", status);
        ExFreePool(resList);
        return status;
    }

    /* Step 4: Walk the resource list to find first memory range >= 64 KB (BAR0) */
    for (ULONG i = 0; i < resList->Count; i++) {
        PCM_FULL_RESOURCE_DESCRIPTOR full = &resList->List[i];
        for (ULONG j = 0; j < full->PartialResourceList.Count; j++) {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR partial =
                &full->PartialResourceList.PartialDescriptors[j];
            if (partial->Type == CmResourceTypeMemory &&
                partial->u.Memory.Length >= (64 * 1024)) {
                Bar0Address->QuadPart = partial->u.Memory.Start.QuadPart;
                *Bar0Length           = partial->u.Memory.Length;
                DEBUGP(DL_INFO, "?? Alt SUCCESS: BAR0 via PnP: PA=0x%llx Len=0x%x\n",
                       Bar0Address->QuadPart, *Bar0Length);
                ExFreePool(resList);
                DEBUGP(DL_TRACE, "<==AvbDiscoverIntelControllerResourcesAlternative: SUCCESS\n");
                return STATUS_SUCCESS;
            }
        }
    }

    DEBUGP(DL_WARN, "?? Alt STEP 4: No memory resource >= 64 KB found in PnP resource list\n");
    ExFreePool(resList);
    return STATUS_DEVICE_NOT_READY;
}

NTSTATUS
AvbInitializeDeviceWithBar0Discovery(
    _In_ PMS_FILTER FilterModule,
    _Out_ PAVB_DEVICE_CONTEXT *AvbContext
)
{
    NTSTATUS status;
    PHYSICAL_ADDRESS bar0 = { 0 };
    ULONG barLen = 0;

    DEBUGP(DL_TRACE, "==>AvbInitializeDeviceWithBar0Discovery\n");

    if (!AvbContext || !FilterModule) return STATUS_INVALID_PARAMETER;
    *AvbContext = NULL;

    PAVB_DEVICE_CONTEXT ctx = (PAVB_DEVICE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(AVB_DEVICE_CONTEXT),
        FILTER_ALLOC_TAG);
    if (!ctx) {
        DEBUGP(DL_ERROR, "AVB ctx allocation failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ctx, sizeof(AVB_DEVICE_CONTEXT));
    ctx->initialized = FALSE;
    ctx->filter_instance = FilterModule;
    ctx->hw_access_enabled = FALSE;
    ctx->miniport_handle = FilterModule->FilterHandle;
    ctx->hardware_context = NULL;
    ctx->intel_device.private_data = ctx;
    ctx->intel_device.pci_vendor_id = 0;
    ctx->intel_device.pci_device_id = 0;
    ctx->intel_device.device_type = INTEL_DEVICE_UNKNOWN;
    ctx->intel_device.capabilities = 0; // Will be set based on device type
    AVB_SET_HW_STATE(ctx, AVB_HW_BOUND); // Initial state

    // Try to discover PCI IDs first (needed for capability assignment)
    USHORT vendorId = 0, deviceId = 0;
    BOOLEAN gotPciIds = FALSE;
    
    // Attempt to get PCI IDs via PDO query
    do {
        PDEVICE_OBJECT pdo = NULL;
        ULONG busNumber = 0, address = 0, len = 0;
        AVB_PCI_SLOT_NUMBER slot = { 0 };
        ULONG id = 0;
        NTSTATUS st;

        st = AvbGetPdoFromFilter(FilterModule, &pdo);
        if (!NT_SUCCESS(st)) break;
        st = IoGetDeviceProperty(pdo, DevicePropertyBusNumber, sizeof(busNumber), &busNumber, &len);
        if (NT_SUCCESS(st)) {
            len = 0;
            st = IoGetDeviceProperty(pdo, DevicePropertyAddress, sizeof(address), &address, &len);
        }
        ObDereferenceObject(pdo);
        if (!NT_SUCCESS(st)) break;

        slot.bits.DeviceNumber = (address >> 16) & INTEL_MASK_16BIT;
        slot.bits.FunctionNumber = (address & INTEL_MASK_16BIT) & 0x7;
        st = AvbReadPciConfigDword(busNumber, slot, 0x00, &id);
        if (!NT_SUCCESS(st)) break;
        
        vendorId = (USHORT)(id & INTEL_MASK_16BIT);
        deviceId = (USHORT)((id >> 16) & INTEL_MASK_16BIT);
        gotPciIds = TRUE;
        
        ctx->intel_device.pci_vendor_id = vendorId;
        ctx->intel_device.pci_device_id = deviceId;
        ctx->intel_device.device_type = AvbGetIntelDeviceType(deviceId);
        
        DEBUGP(DL_INFO, "PCI IDs: VEN=0x%04x DEV=0x%04x Type=%d\n", 
               vendorId, deviceId, ctx->intel_device.device_type);
    } while (0);
    
    // CRITICAL FIX: Always set realistic baseline capabilities based on device type
    // This must happen even if BAR0 discovery fails to ensure enumeration works
    if (gotPciIds) {
        ULONG baseline_caps = 0;
        switch (ctx->intel_device.device_type) {
            case INTEL_DEVICE_I210:
                baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO;
                break;
            case INTEL_DEVICE_I217:
                baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_MMIO | INTEL_CAP_MDIO;
                break;
            case INTEL_DEVICE_I219:
                baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_MMIO | INTEL_CAP_MDIO;
                break;
            case INTEL_DEVICE_I225:
                baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                               INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO;
                break;
            case INTEL_DEVICE_I226:
                baseline_caps = INTEL_CAP_BASIC_1588 | INTEL_CAP_ENHANCED_TS | INTEL_CAP_TSN_TAS | 
                               INTEL_CAP_TSN_FP | INTEL_CAP_PCIe_PTM | INTEL_CAP_2_5G | INTEL_CAP_MMIO | INTEL_CAP_EEE;
                break;
            default:
                baseline_caps = INTEL_CAP_MMIO;
                break;
        }
        ctx->intel_device.capabilities = baseline_caps;
        DEBUGP(DL_INFO, "? Set baseline capabilities 0x%08X for device type %d\n", 
               baseline_caps, ctx->intel_device.device_type);
    } else {
        DEBUGP(DL_WARN, "? Could not determine PCI IDs - capabilities will remain 0\n");
    }

    // Discover BAR0 and map MMIO (optional - not required for enumeration)
    status = AvbDiscoverIntelControllerResources(FilterModule, &bar0, &barLen);
    if (NT_SUCCESS(status)) {
        NTSTATUS m = AvbMapIntelControllerMemory(ctx, bar0, barLen);
        if (NT_SUCCESS(m)) {
            ctx->hw_access_enabled = TRUE;
            AVB_SET_HW_STATE(ctx, AVB_HW_BAR_MAPPED);
            DEBUGP(DL_INFO, "MMIO mapped: BAR0=0x%llx, Len=0x%x, hw_state=%s\n", 
                   bar0.QuadPart, barLen, AvbHwStateName(ctx->hw_state));
        } else {
            DEBUGP(DL_WARN, "MmMapIoSpace failed: 0x%x (capabilities still valid for enumeration)\n", m);
        }
    } else {
        DEBUGP(DL_WARN, "BAR0 discovery failed: 0x%x (capabilities still valid for enumeration)\n", status);
    }

    ctx->initialized = TRUE;
    *AvbContext = ctx;

    DEBUGP(DL_TRACE, "<==AvbInitializeDeviceWithBar0Discovery (HW=%s)\n", ctx->hw_access_enabled ? "ENABLED" : "DISABLED");
    return STATUS_SUCCESS;
}

/**
 * @brief Map Intel controller MMIO register space (BAR0) into system virtual address space
 * @param AvbContext Device context
 * @param PhysicalAddress Physical BAR0 base address
 * @param Length Length (bytes) to map
 * @return NTSTATUS STATUS_SUCCESS if mapped successfully
 */
NTSTATUS
AvbMapIntelControllerMemory(
    _In_ PAVB_DEVICE_CONTEXT AvbContext,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length
)
{
    PINTEL_HARDWARE_CONTEXT hwContext;
    
    DEBUGP(DL_TRACE, "==>AvbMapIntelControllerMemory: PA=0x%llx, Length=0x%x\n", 
           PhysicalAddress.QuadPart, Length);
    
    if (AvbContext == NULL) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: Invalid context\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    if (PhysicalAddress.QuadPart == 0 || Length == 0) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: Invalid physical address or length\n");
        return STATUS_INVALID_PARAMETER;
    }
    
    // Allocate hardware context
    hwContext = (PINTEL_HARDWARE_CONTEXT)ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(INTEL_HARDWARE_CONTEXT),
        FILTER_ALLOC_TAG
    );
    
    if (hwContext == NULL) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: Failed to allocate hardware context\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // Initialize hardware context
    RtlZeroMemory(hwContext, sizeof(INTEL_HARDWARE_CONTEXT));

    // Store physical address and length
    hwContext->physical_address = PhysicalAddress;
    hwContext->mmio_length = Length;
    hwContext->mapped = FALSE;
    
    // Map the MMIO region using Windows kernel memory manager
    hwContext->mmio_base = (PUCHAR)MmMapIoSpace(
        PhysicalAddress,
        Length,
        MmNonCached
    );
    
    if (hwContext->mmio_base == NULL) {
        DEBUGP(DL_ERROR, "AvbMapIntelControllerMemory: MmMapIoSpace failed\n");
        ExFreePoolWithTag(hwContext, FILTER_ALLOC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    hwContext->mapped = TRUE;
    AvbContext->hardware_context = hwContext;
    
    // CRITICAL FIX: Also set mmio_base in intel_private for Intel library
    if (AvbContext->intel_device.private_data) {
        struct intel_private *priv = (struct intel_private *)AvbContext->intel_device.private_data;
        priv->mmio_base = hwContext->mmio_base;
        priv->mmio_size = Length;
        DEBUGP(DL_INFO, "AvbMapIntelControllerMemory: Updated intel_private mmio_base=%p size=0x%x\n",
               priv->mmio_base, priv->mmio_size);
    }
    
    DEBUGP(DL_INFO, "AvbMapIntelControllerMemory: Success - PA=0x%llx mapped to VA=0x%p\n", 
           PhysicalAddress.QuadPart, hwContext->mmio_base);
    DEBUGP(DL_TRACE, "<==AvbMapIntelControllerMemory: SUCCESS\n");
    
    return STATUS_SUCCESS;
}

/**
 * @brief Unmap previously mapped Intel controller MMIO space
 * @param AvbContext Device context
 */
VOID
AvbUnmapIntelControllerMemory(
    _In_ PAVB_DEVICE_CONTEXT AvbContext
)
{
    PINTEL_HARDWARE_CONTEXT hwContext;
    
    DEBUGP(DL_TRACE, "==>AvbUnmapIntelControllerMemory\n");
    
    if (AvbContext == NULL) {
        return;
    }
    
    hwContext = AvbContext->hardware_context;
    if (hwContext == NULL) {
        return;
    }
    
    // Unmap MMIO if it was mapped
    if (hwContext->mapped && hwContext->mmio_base != NULL) {
        MmUnmapIoSpace(hwContext->mmio_base, hwContext->mmio_length);
        DEBUGP(DL_INFO, "AvbUnmapIntelControllerMemory: Unmapped MMIO at VA=0x%p\n", 
               hwContext->mmio_base);
    }
    
    // Free hardware context
    ExFreePoolWithTag(hwContext, FILTER_ALLOC_TAG);
    AvbContext->hardware_context = NULL;
    
    DEBUGP(DL_TRACE, "<==AvbUnmapIntelControllerMemory\n");
}