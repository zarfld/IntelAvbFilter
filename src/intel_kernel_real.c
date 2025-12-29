/*++

Module Name:

    intel_kernel_real.c

Abstract:

    Real Intel AVB library function implementations for Windows kernel mode.
    CLEAN HAL: No device-specific register definitions or logic.
    All device-specific operations delegated to device implementations.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "devices/intel_device_interface.h"  // Fix path

// Platform operations structure (matches what Intel library expects)
struct platform_ops {
    int (*init)(device_t *dev);
    void (*cleanup)(device_t *dev);
    int (*pci_read_config)(device_t *dev, ULONG offset, ULONG *value);
    int (*pci_write_config)(device_t *dev, ULONG offset, ULONG value);
    int (*mmio_read)(device_t *dev, ULONG offset, ULONG *value);
    int (*mmio_write)(device_t *dev, ULONG offset, ULONG value);
    int (*mdio_read)(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT *value);
    int (*mdio_write)(device_t *dev, USHORT phy_addr, USHORT reg_addr, USHORT value);
    int (*read_timestamp)(device_t *dev, ULONGLONG *timestamp);
};

// External reference to our platform operations (defined in avb_integration.c)
extern const struct platform_ops ndis_platform_ops;

/**
 * @brief Get device context and validate
 * 
 * CRITICAL FIX: dev->private_data points to struct intel_private, NOT AVB_DEVICE_CONTEXT
 * The AVB context is stored in priv->platform_data (see avb_integration.c line 251)
 * 
 * @param dev Device handle
 * @param context_out Output context
 * @param device_type_out Output device type
 * @return 0 on success, <0 on error
 */
static int get_device_context(device_t *dev, PAVB_DEVICE_CONTEXT *context_out, intel_device_type_t *device_type_out)
{
    struct intel_private *priv;
    PAVB_DEVICE_CONTEXT context;
    
    if (dev == NULL) {
        return -1;
    }
    
    // CRITICAL: dev->private_data is struct intel_private, not AVB context
    priv = (struct intel_private *)dev->private_data;
    if (priv == NULL) {
        return -1;
    }
    
    // AVB context is stored in priv->platform_data (set in avb_integration.c)
    context = (PAVB_DEVICE_CONTEXT)priv->platform_data;
    if (context == NULL) {
        return -1;
    }
    
    if (context_out) {
        *context_out = context;
    }
    
    if (device_type_out) {
        *device_type_out = priv->device_type;  // Use device_type from priv, not context
    }
    
    return 0;
}

/**
 * @brief Initialize Intel device using device-specific implementation
 * CLEAN HAL: Pure dispatch to device-specific init
 * @param dev Device handle
 * @return 0 on success, <0 on error * 
 * Implements: #40 (REQ-F-DEVICE-ABS-003: Register Access via Device Abstraction Layer) */
int intel_init(device_t *dev)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_init (clean HAL dispatch)\n");
    
    result = get_device_context(dev, &context, &device_type);
    if (result != 0) {
        DEBUGP(DL_ERROR, "intel_init: Invalid device context\n");
        return result;
    }
    
    ops = intel_get_device_ops(device_type);
    if (ops && ops->init) {
        result = ops->init(dev);
        DEBUGP(DL_TRACE, "<==intel_init: device-specific init result=%d\n", result);
        return result;
    }
    
    // Default platform init if no device-specific init
    if (ndis_platform_ops.init) {
        NTSTATUS nt_result = ndis_platform_ops.init(dev);
        result = NT_SUCCESS(nt_result) ? 0 : -1;
        DEBUGP(DL_TRACE, "<==intel_init: platform init result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_TRACE, "<==intel_init: no init function (success by default)\n");
    return 0;
}

/**
 * @brief Detach from Intel device
 * CLEAN HAL: Pure dispatch to device-specific cleanup
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
int intel_detach(device_t *dev)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    
    DEBUGP(DL_TRACE, "==>intel_detach (clean HAL dispatch)\n");
    
    if (get_device_context(dev, &context, &device_type) == 0) {
        ops = intel_get_device_ops(device_type);
        if (ops && ops->cleanup) {
            ops->cleanup(dev);
        }
    }
    
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==intel_detach: Success\n");
    return 0;
}

/**
 * @brief Get device information using device-specific implementation
 * CLEAN HAL: Pure dispatch to device-specific get_info
 * @param dev Device handle
 * @param info_buffer Output buffer
 * @param buffer_size Buffer size
 * @return 0 on success, <0 on error
 */
int intel_get_device_info(device_t *dev, char *info_buffer, size_t buffer_size)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_get_device_info (clean HAL dispatch)\n");
    
    if (info_buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    result = get_device_context(dev, &context, &device_type);
    if (result != 0) {
        return result;
    }
    
    ops = intel_get_device_ops(device_type);
    if (ops && ops->get_info) {
        result = ops->get_info(dev, info_buffer, (ULONG)buffer_size);
        DEBUGP(DL_TRACE, "<==intel_get_device_info: device-specific result=%d\n", result);
        return result;
    }
    
    // Fallback using device name from ops
    if (ops && ops->device_name) {
        size_t name_len = strlen(ops->device_name);
        if (name_len >= buffer_size) {
            name_len = buffer_size - 1;
        }
        RtlCopyMemory(info_buffer, ops->device_name, name_len);
        info_buffer[name_len] = '\0';
        DEBUGP(DL_TRACE, "<==intel_get_device_info: fallback result=%s\n", info_buffer);
        return 0;
    }
    
    DEBUGP(DL_ERROR, "No device info available for device type %d\n", device_type);
    return -1;
}

/**
 * @brief Read register using platform operations
 * CLEAN HAL: Generic register access, no device-specific logic
 * @param dev Device handle
 * @param offset Register offset
 * @param value Output value
 * @return 0 on success, <0 on error
 */
int intel_read_reg(device_t *dev, ULONG offset, ULONG *value)
{
    DEBUGP(DL_TRACE, "==>intel_read_reg: offset=0x%x\n", offset);
    
    if (dev == NULL || value == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.mmio_read) {
        int result = ndis_platform_ops.mmio_read(dev, offset, value);
        DEBUGP(DL_TRACE, "<==intel_read_reg: offset=0x%x, value=0x%x, result=%d\n", 
               offset, result == 0 ? *value : 0, result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_read_reg: No MMIO read function available\n");
    return -1;
}

/**
 * @brief Write register using platform operations  
 * CLEAN HAL: Generic register access, no device-specific logic
 * @param dev Device handle
 * @param offset Register offset
 * @param value Value to write
 * @return 0 on success, <0 on error
 */
int intel_write_reg(device_t *dev, ULONG offset, ULONG value)
{
    DEBUGP(DL_TRACE, "==>intel_write_reg: offset=0x%x, value=0x%x\n", offset, value);
    
    if (dev == NULL) {
        return -1;
    }
    
    if (ndis_platform_ops.mmio_write) {
        int result = ndis_platform_ops.mmio_write(dev, offset, value);
        DEBUGP(DL_TRACE, "<==intel_write_reg: result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_write_reg: No MMIO write function available\n");
    return -1;
}

/**
 * @brief Get time using device-specific or platform implementation
 * CLEAN HAL: Attempts device-specific first, then platform fallback
 * @param dev Device handle
 * @param clk_id Clock id (unused for now)
 * @param curtime Output current PTP time
 * @param system_time Optional system time
 * @return 0 on success, <0 on error
 */
int intel_gettime(device_t *dev, clockid_t clk_id, ULONGLONG *curtime, struct timespec *system_time)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    LARGE_INTEGER currentTime;
    int result;
    
    UNREFERENCED_PARAMETER(clk_id);
    DEBUGP(DL_TRACE, "==>intel_gettime (clean HAL dispatch)\n");
    
    if (dev == NULL || curtime == NULL) {
        return -1;
    }
    
    // Try device-specific timestamp read first
    if (get_device_context(dev, &context, &device_type) == 0) {
        ops = intel_get_device_ops(device_type);
        if (ops && ops->get_systime) {
            uint64_t device_time = 0;
            result = ops->get_systime(dev, &device_time);
            if (result == 0) {
                *curtime = device_time;
                if (system_time) {
                    KeQuerySystemTime(&currentTime);
                    system_time->tv_sec = (LONG)(currentTime.QuadPart / 10000000LL);
                    system_time->tv_nsec = (LONG)((currentTime.QuadPart % 10000000LL) * 100);
                }
                DEBUGP(DL_TRACE, "<==intel_gettime: device-specific timestamp=0x%llx\n", *curtime);
                return 0;
            }
        }
    }
    
    // Fallback to platform timestamp function
    if (ndis_platform_ops.read_timestamp) {
        result = ndis_platform_ops.read_timestamp(dev, curtime);
        if (result == 0) {
            if (system_time) {
                KeQuerySystemTime(&currentTime);
                system_time->tv_sec = (LONG)(currentTime.QuadPart / 10000000LL);
                system_time->tv_nsec = (LONG)((currentTime.QuadPart % 10000000LL) * 100);
            }
            DEBUGP(DL_TRACE, "<==intel_gettime: platform timestamp=0x%llx\n", *curtime);
            return 0;
        }
    }
    
    // Final fallback to system time
    KeQuerySystemTime(&currentTime);
    *curtime = currentTime.QuadPart;
    
    if (system_time) {
        system_time->tv_sec = (LONG)(currentTime.QuadPart / 10000000LL);
        system_time->tv_nsec = (LONG)((currentTime.QuadPart % 10000000LL) * 100);
    }
    
    DEBUGP(DL_TRACE, "<==intel_gettime: fallback timestamp=0x%llx\n", *curtime);
    return 0;
}

/**
 * @brief Set system time using device-specific implementation  
 * CLEAN HAL: Pure dispatch to device-specific set_systime
 * @param dev Device handle
 * @param systime 64-bit time value
 * @return 0 on success, <0 on error
 */
int intel_set_systime(device_t *dev, ULONGLONG systime)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_set_systime (clean HAL dispatch): systime=0x%llx\n", systime);
    
    result = get_device_context(dev, &context, &device_type);
    if (result != 0) {
        DEBUGP(DL_ERROR, "intel_set_systime: Invalid device context\n");
        return result;
    }
    
    ops = intel_get_device_ops(device_type);
    if (ops && ops->set_systime) {
        result = ops->set_systime(dev, systime);
        DEBUGP(DL_TRACE, "<==intel_set_systime: device-specific result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "No SYSTIME implementation for device type %d\n", device_type);
    return -ENOTSUP;
}

/**
 * @brief Setup Time Aware Shaper using device-specific implementation
 * CLEAN HAL: Pure dispatch to device-specific setup_tas
 * @param dev Device handle
 * @param config TAS configuration
 * @return 0 on success, <0 on error
 */
int intel_setup_time_aware_shaper(device_t *dev, struct tsn_tas_config *config)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_setup_time_aware_shaper (clean HAL dispatch)\n");
    
    if (config == NULL) {
        return -1;
    }
    
    result = get_device_context(dev, &context, &device_type);
    if (result != 0) {
        DEBUGP(DL_ERROR, "intel_setup_time_aware_shaper: Invalid device context\n");
        return result;
    }
    
    ops = intel_get_device_ops(device_type);
    if (ops && ops->setup_tas) {
        result = ops->setup_tas(dev, config);
        DEBUGP(DL_TRACE, "<==intel_setup_time_aware_shaper: device-specific result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "TAS not supported on device type %d\n", device_type);
    return -ENOTSUP;
}

/**
 * @brief Setup Frame Preemption using device-specific implementation
 * CLEAN HAL: Pure dispatch to device-specific setup_frame_preemption
 * @param dev Device handle
 * @param config FP configuration
 * @return 0 on success, <0 on error
 */
int intel_setup_frame_preemption(device_t *dev, struct tsn_fp_config *config)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_setup_frame_preemption (clean HAL dispatch)\n");
    
    if (config == NULL) {
        return -1;
    }
    
    result = get_device_context(dev, &context, &device_type);
    if (result != 0) {
        DEBUGP(DL_ERROR, "intel_setup_frame_preemption: Invalid device context\n");
        return result;
    }
    
    ops = intel_get_device_ops(device_type);
    if (ops && ops->setup_frame_preemption) {
        result = ops->setup_frame_preemption(dev, config);
        DEBUGP(DL_TRACE, "<==intel_setup_frame_preemption: device-specific result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "Frame Preemption not supported on device type %d\n", device_type);
    return -ENOTSUP;
}

/**
 * @brief Setup PTM using device-specific implementation
 * CLEAN HAL: Pure dispatch to device-specific setup_ptm  
 * @param dev Device handle
 * @param config PTM configuration
 * @return 0 on success, <0 on error
 */
int intel_setup_ptm(device_t *dev, struct ptm_config *config)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_setup_ptm (clean HAL dispatch)\n");
    
    if (config == NULL) {
        return -1;
    }
    
    result = get_device_context(dev, &context, &device_type);
    if (result != 0) {
        DEBUGP(DL_ERROR, "intel_setup_ptm: Invalid device context\n");
        return result;
    }
    
    ops = intel_get_device_ops(device_type);
    if (ops && ops->setup_ptm) {
        result = ops->setup_ptm(dev, config);
        DEBUGP(DL_TRACE, "<==intel_setup_ptm: device-specific result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "PTM not supported on device type %d\n", device_type);
    return -ENOTSUP;
}

/**
 * @brief MDIO read using device-specific or platform implementation
 * CLEAN HAL: Tries device-specific first, then platform fallback
 * @param dev Device handle
 * @param page PHY page
 * @param reg Register address
 * @param value Output value
 * @return 0 on success, <0 on error
 */
int intel_mdio_read(device_t *dev, ULONG page, ULONG reg, USHORT *value)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    
    DEBUGP(DL_TRACE, "==>intel_mdio_read: page=%lu, reg=%lu\n", page, reg);
    
    if (dev == NULL || value == NULL) {
        return -1;
    }
    
    // Try device-specific MDIO first
    if (get_device_context(dev, &context, &device_type) == 0) {
        ops = intel_get_device_ops(device_type);
        if (ops && ops->mdio_read) {
            int result = ops->mdio_read(dev, (uint16_t)page, (uint16_t)reg, value);
            if (result == 0) {
                DEBUGP(DL_TRACE, "<==intel_mdio_read: device-specific value=0x%x\n", *value);
                return 0;
            }
        }
    }
    
    // Fallback to platform MDIO
    if (ndis_platform_ops.mdio_read) {
        int result = ndis_platform_ops.mdio_read(dev, (USHORT)page, (USHORT)reg, value);
        DEBUGP(DL_TRACE, "<==intel_mdio_read: platform value=0x%x, result=%d\n", *value, result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_mdio_read: No MDIO read function available\n");
    return -1;
}

/**
 * @brief MDIO write using device-specific or platform implementation
 * CLEAN HAL: Tries device-specific first, then platform fallback
 * @param dev Device handle
 * @param page PHY page
 * @param reg Register address
 * @param value Value to write
 * @return 0 on success, <0 on error
 */
int intel_mdio_write(device_t *dev, ULONG page, ULONG reg, USHORT value)
{
    PAVB_DEVICE_CONTEXT context;
    intel_device_type_t device_type;
    const intel_device_ops_t *ops;
    
    DEBUGP(DL_TRACE, "==>intel_mdio_write: page=%lu, reg=%lu, value=0x%x\n", page, reg, value);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Try device-specific MDIO first
    if (get_device_context(dev, &context, &device_type) == 0) {
        ops = intel_get_device_ops(device_type);
        if (ops && ops->mdio_write) {
            int result = ops->mdio_write(dev, (uint16_t)page, (uint16_t)reg, value);
            if (result == 0) {
                DEBUGP(DL_TRACE, "<==intel_mdio_write: device-specific result=0\n");
                return 0;
            }
        }
    }
    
    // Fallback to platform MDIO
    if (ndis_platform_ops.mdio_write) {
        int result = ndis_platform_ops.mdio_write(dev, (USHORT)page, (USHORT)reg, value);
        DEBUGP(DL_TRACE, "<==intel_mdio_write: platform result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_mdio_write: No MDIO write function available\n");
    return -1;
}