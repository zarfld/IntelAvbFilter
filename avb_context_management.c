/*++

Module Name:

    avb_context_management.c

Abstract:

    Global context management for multi-adapter support in Intel AVB Filter Driver.
    Provides atomic context switching between different Intel adapters.

--*/

#include "precomp.h"
#include "avb_integration.h"

/*========================================================================
 * Global context for multi-adapter support - CRITICAL FIX
 *=======================================================================*/
static PAVB_DEVICE_CONTEXT g_ActiveAvbContext = NULL;
static FILTER_LOCK g_ActiveContextLock;
static BOOLEAN g_ContextLockInitialized = FALSE;

/**
 * @brief Initialize the global context management system.
 * Called once during driver initialization.
 */
VOID AvbInitializeGlobalContext(VOID) {
    if (!g_ContextLockInitialized) {
        FILTER_INIT_LOCK(&g_ActiveContextLock);
        g_ActiveAvbContext = NULL;
        g_ContextLockInitialized = TRUE;
        DEBUGP(DL_INFO, "?? Global AVB context management initialized\n");
    }
}

/**
 * @brief Cleanup the global context management system.
 * Called during driver unload.
 */
VOID AvbCleanupGlobalContext(VOID) {
    if (g_ContextLockInitialized) {
        BOOLEAN bFalse = FALSE;
        FILTER_ACQUIRE_LOCK(&g_ActiveContextLock, bFalse);
        g_ActiveAvbContext = NULL;
        FILTER_RELEASE_LOCK(&g_ActiveContextLock, bFalse);
        FILTER_FREE_LOCK(&g_ActiveContextLock);
        g_ContextLockInitialized = FALSE;
        DEBUGP(DL_INFO, "?? Global AVB context management cleaned up\n");
    }
}

/**
 * @brief Set the active AVB context for subsequent IOCTL operations.
 * @param AvbContext Device context to make active (NULL to clear).
 */
VOID AvbSetActiveContext(PAVB_DEVICE_CONTEXT AvbContext) {
    if (!g_ContextLockInitialized) {
        AvbInitializeGlobalContext();
    }
    
    BOOLEAN bFalse = FALSE;
    FILTER_ACQUIRE_LOCK(&g_ActiveContextLock, bFalse);
    
    PAVB_DEVICE_CONTEXT oldContext = g_ActiveAvbContext;
    g_ActiveAvbContext = AvbContext;
    
    if (oldContext != AvbContext) {
        if (AvbContext) {
            DEBUGP(DL_INFO, "?? CONTEXT SWITCH: Active context -> VID=0x%04X DID=0x%04X (%s)\n",
                   AvbContext->intel_device.pci_vendor_id,
                   AvbContext->intel_device.pci_device_id,
                   AvbHwStateName(AvbContext->hw_state));
        } else {
            DEBUGP(DL_INFO, "?? CONTEXT SWITCH: Active context -> NULL\n");
        }
    }
    
    FILTER_RELEASE_LOCK(&g_ActiveContextLock, bFalse);
}

/**
 * @brief Get the currently active AVB context.
 * @return Current active context or NULL if none selected.
 */
PAVB_DEVICE_CONTEXT AvbGetActiveContext(VOID) {
    if (!g_ContextLockInitialized) {
        return NULL;
    }
    
    BOOLEAN bFalse = FALSE;
    FILTER_ACQUIRE_LOCK(&g_ActiveContextLock, bFalse);
    PAVB_DEVICE_CONTEXT context = g_ActiveAvbContext;
    FILTER_RELEASE_LOCK(&g_ActiveContextLock, bFalse);
    
    return context;
}

/**
 * @brief Find filter module by device ID for multi-adapter support.
 * @param VendorId PCI vendor ID (e.g., 0x8086 for Intel).
 * @param DeviceId PCI device ID (e.g., 0x1533 for I210).
 * @return Filter module pointer or NULL if not found.
 */
PMS_FILTER AvbFindFilterByDeviceId(USHORT VendorId, USHORT DeviceId) {
    BOOLEAN bFalse = FALSE;
    PMS_FILTER result = NULL;
    
    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    PLIST_ENTRY Link = FilterModuleList.Flink;
    
    while (Link != &FilterModuleList) {
        PMS_FILTER cand = CONTAINING_RECORD(Link, MS_FILTER, FilterModuleLink);
        Link = Link->Flink;
        
        FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
        
        USHORT ven = 0, dev = 0;
        if (AvbIsSupportedIntelController(cand, &ven, &dev) && 
            ven == VendorId && dev == DeviceId) {
            DEBUGP(DL_INFO, "AvbFindFilterByDeviceId: Found match %wZ for VID=0x%04X DID=0x%04X\n",
                   &cand->MiniportFriendlyName, VendorId, DeviceId);
            result = cand;
            break;
        }
        
        FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    }
    
    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);
    
    if (result == NULL) {
        DEBUGP(DL_WARN, "AvbFindFilterByDeviceId: No filter found for VID=0x%04X DID=0x%04X\n", VendorId, DeviceId);
    }
    
    return result;
}