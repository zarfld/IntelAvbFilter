/*++

Module Name:

    intel_kernel_real.c

Abstract:

    Real Intel AVB library function implementations for Windows kernel mode
    These functions provide the Intel library API but use our NDIS platform operations
    for actual hardware access instead of stubs.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel_private.h"

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
 * @brief Initialize Intel device (real hardware access)
 * Datasheets: See device-specific datasheets in external/intel_avb/spec
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
int intel_init(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>intel_init (real hardware)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // Use our platform operations for real initialization
    if (ndis_platform_ops.init) {
        NTSTATUS result = ndis_platform_ops.init(dev);
        if (!NT_SUCCESS(result)) {
            DEBUGP(DL_ERROR, "Platform init failed: 0x%x\n", result);
            return -1;
        }
    }
    
    DEBUGP(DL_TRACE, "<==intel_init: Success\n");
    return 0;
}

/**
 * @brief Detach from Intel device (real hardware access)  
 * @param dev Device handle
 * @return 0 on success, <0 on error
 */
int intel_detach(device_t *dev)
{
    DEBUGP(DL_TRACE, "==>intel_detach (real hardware)\n");
    
    if (dev == NULL) {
        return -1;
    }
    
    // Use our platform operations for real cleanup
    if (ndis_platform_ops.cleanup) {
        ndis_platform_ops.cleanup(dev);
    }
    
    DEBUGP(DL_TRACE, "<==intel_detach: Success\n");
    return 0;
}

/**
 * @brief Get device information (real hardware access)
 * @param dev Device handle
 * @param info_buffer Output buffer
 * @param buffer_size Buffer size
 * @return 0 on success, <0 on error
 */
int intel_get_device_info(device_t *dev, char *info_buffer, size_t buffer_size)
{
    PAVB_DEVICE_CONTEXT context;
    
    DEBUGP(DL_TRACE, "==>intel_get_device_info (real hardware)\n");
    
    if (dev == NULL || info_buffer == NULL || buffer_size == 0) {
        return -1;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        return -1;
    }
    
    // Create device info string based on real device type
    const char *device_name = "Unknown Intel Device";
    switch (context->intel_device.device_type) {
        case INTEL_DEVICE_I210:
            device_name = "Intel I210 Gigabit Ethernet - Full TSN Support";
            break;
        case INTEL_DEVICE_I219:
            device_name = "Intel I219 Ethernet Connection - IEEE 1588 + MDIO";
            break;
        case INTEL_DEVICE_I225:
            device_name = "Intel I225 2.5G Ethernet - Advanced TSN";
            break;
        case INTEL_DEVICE_I226:
            device_name = "Intel I226 2.5G Ethernet - Advanced TSN";
            break;
        default:
            break;
    }
    
    // Safe string copy
    size_t name_len = strlen(device_name);
    if (name_len >= buffer_size) {
        name_len = buffer_size - 1;
    }
    
    RtlCopyMemory(info_buffer, device_name, name_len);
    info_buffer[name_len] = '\0';
    
    DEBUGP(DL_TRACE, "<==intel_get_device_info: %s\n", device_name);
    return 0;
}

/**
 * @brief Read register (real hardware access)
 * @param dev Device handle
 * @param offset Register offset (SSOT)
 * @param value Output value
 * @return 0 on success, <0 on error
 */
int intel_read_reg(device_t *dev, ULONG offset, ULONG *value)
{
    DEBUGP(DL_TRACE, "==>intel_read_reg (real hardware): offset=0x%x\n", offset);
    
    if (dev == NULL || value == NULL) {
        return -1;
    }
    
    // Use our platform operations for real MMIO access
    if (ndis_platform_ops.mmio_read) {
        int result = ndis_platform_ops.mmio_read(dev, offset, value);
        DEBUGP(DL_TRACE, "<==intel_read_reg: offset=0x%x, value=0x%x, result=%d\n", 
               offset, *value, result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_read_reg: No MMIO read function available\n");
    return -1;
}

/**
 * @brief Write register (real hardware access)
 * Ensure offsets and value mask conform to SSOT/datasheet.
 * @param dev Device handle
 * @param offset Register offset (SSOT)
 * @param value Value to write
 * @return 0 on success, <0 on error
 */
int intel_write_reg(device_t *dev, ULONG offset, ULONG value)
{
    DEBUGP(DL_TRACE, "==>intel_write_reg (real hardware): offset=0x%x, value=0x%x\n", offset, value);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Use our platform operations for real MMIO access
    if (ndis_platform_ops.mmio_write) {
        int result = ndis_platform_ops.mmio_write(dev, offset, value);
        DEBUGP(DL_TRACE, "<==intel_write_reg: result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_write_reg: No MMIO write function available\n");
    return -1;
}

/**
 * @brief Get time (real hardware access)
 * @param dev Device handle
 * @param clk_id Clock id (unused for now)
 * @param curtime Output current PTP time
 * @param system_time Optional system time
 * @return 0 on success, <0 on error
 */
int intel_gettime(device_t *dev, clockid_t clk_id, ULONGLONG *curtime, struct timespec *system_time)
{
    LARGE_INTEGER currentTime;
    
    DEBUGP(DL_TRACE, "==>intel_gettime (real hardware): clk_id=%d\n", clk_id);
    
    if (dev == NULL || curtime == NULL) {
        return -1;
    }
    
    // Try to read hardware timestamp using real hardware access
    if (ndis_platform_ops.read_timestamp) {
        int result = ndis_platform_ops.read_timestamp(dev, curtime);
        if (result == 0) {
            // Also fill system_time if requested
            if (system_time) {
                KeQuerySystemTime(&currentTime);
                // Convert 100ns units to seconds and nanoseconds
                system_time->tv_sec = (LONG)(currentTime.QuadPart / 10000000LL);
                system_time->tv_nsec = (LONG)((currentTime.QuadPart % 10000000LL) * 100);
            }
            DEBUGP(DL_TRACE, "<==intel_gettime: hardware timestamp=0x%llx\n", *curtime);
            return 0;
        }
    }
    
    // Fallback to system time
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
 * @brief Program SYSTIM registers (IEEE 1588 time of day)
 * Datasheet: I210 PTP block (e.g., 333016), SYSTIML/H: 0x0B600/0x0B604
 * @param dev Device handle
 * @param systime 64-bit time value
 * @return 0 on success, <0 on error
 */
int intel_set_systime(device_t *dev, ULONGLONG systime)
{
    PAVB_DEVICE_CONTEXT context;
    ULONG ts_low, ts_high;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_set_systime (real hardware): systime=0x%llx\n", systime);
    
    if (dev == NULL) {
        return -1;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        return -1;
    }
    
    // Split timestamp into low and high parts
    ts_low = (ULONG)(systime & 0xFFFFFFFF);
    ts_high = (ULONG)((systime >> 32) & 0xFFFFFFFF);
    
    switch (context->intel_device.device_type) {
        case INTEL_DEVICE_I210:
            result = ndis_platform_ops.mmio_write(dev, I210_SYSTIML, ts_low);
            if (result != 0) return result;
            result = ndis_platform_ops.mmio_write(dev, I210_SYSTIMH, ts_high);
            if (result != 0) return result;
            break;
        case INTEL_DEVICE_I217:
            // SSOT marks SYSTIM as read-only on I217; do not write via MMIO
            DEBUGP(DL_ERROR, "intel_set_systime: I217 SYSTIM is RO per SSOT; use device-specific method if any\n");
            return -ENOTSUP;
        case INTEL_DEVICE_I219:
#ifndef AVB_I219_SYSTIM_TEST
            // SSOT has no SYSTIM regs for I219; use MDIO/device-specific API instead
            DEBUGP(DL_ERROR, "intel_set_systime: I219 SYSTIM MMIO not defined in SSOT; use MDIO path\n");
            return -ENOTSUP;
#else
            result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIML, ts_low);
            if (result != 0) return result;
            result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIMH, ts_high);
            if (result != 0) return result;
            break;
#endif
        case INTEL_DEVICE_I225:
        case INTEL_DEVICE_I226:
            result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIML, ts_low);
            if (result != 0) return result;
            result = ndis_platform_ops.mmio_write(dev, INTEL_REG_SYSTIMH, ts_high);
            if (result != 0) return result;
            break;
        default:
            DEBUGP(DL_ERROR, "Unsupported device type for timestamp write: %d\n", 
                   context->intel_device.device_type);
            return -1;
    }
    
    DEBUGP(DL_TRACE, "<==intel_set_systime: Hardware timestamp written successfully\n");
    return 0;
}

/*
 * Note: TAS/FP/PTM offsets used below are not yet provided by SSOT i225/i226 headers.
 * They must be validated against Intel datasheets before upstreaming into SSOT.
 */

/**
 * @brief Setup Time Aware Shaper (real hardware access)
 * Implements IEEE 802.1Qbv Time-Aware Shaper for I225/I226
 */
int intel_setup_time_aware_shaper(device_t *dev, struct tsn_tas_config *config)
{
    PAVB_DEVICE_CONTEXT context;
    ULONG regValue;
    int result;
    UINT16 i;
    
    DEBUGP(DL_TRACE, "==>intel_setup_time_aware_shaper (REAL I225/I226 IMPLEMENTATION)\n");
    
    if (dev == NULL || config == NULL) {
        DEBUGP(DL_ERROR, "intel_setup_time_aware_shaper: Invalid parameters\n");
        return -1;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_ERROR, "intel_setup_time_aware_shaper: No device context\n");
        return -1;
    }
    
    // Validate device supports TAS (I225/I226 only)
    if (context->intel_device.device_type != INTEL_DEVICE_I225 && 
        context->intel_device.device_type != INTEL_DEVICE_I226) {
        DEBUGP(DL_ERROR, "intel_setup_time_aware_shaper: Device does not support TAS\n");
        return -1;
    }
    
    DEBUGP(DL_INFO, "TAS Config: base_time=%llu:%lu, cycle_time=%lu:%lu\n",
           config->base_time_s, config->base_time_ns,
           config->cycle_time_s, config->cycle_time_ns);
    
    // Disable TAS during configuration
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        result = ndis_platform_ops.mmio_read(dev, I225_TAS_CTRL, &regValue);
    } else {
        result = ndis_platform_ops.mmio_read(dev, I226_TAS_CTRL, &regValue);
    }
    if (result != 0) {
        DEBUGP(DL_ERROR, "Failed to read TAS_CTRL\n");
        return result;
    }
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        regValue &= ~(unsigned long)I225_TAS_CTRL_EN_MASK;
        result = ndis_platform_ops.mmio_write(dev, I225_TAS_CTRL, regValue);
    } else {
        regValue &= ~(unsigned long)I226_TAS_CTRL_EN_MASK;
        result = ndis_platform_ops.mmio_write(dev, I226_TAS_CTRL, regValue);
    }
    if (result != 0) {
        DEBUGP(DL_ERROR, "Failed to disable TAS\n");
        return result;
    }
    
    // Compose 64-bit base time in nanoseconds
    ULONGLONG baseTimeNs = (ULONGLONG)config->base_time_s * 1000000000ULL + (ULONGLONG)config->base_time_ns;
    ULONG baseTimeLow = (ULONG)(baseTimeNs & 0xFFFFFFFF);
    ULONG baseTimeHigh = (ULONG)((baseTimeNs >> 32) & 0xFFFFFFFF);
    
    // Program Base Time using SSOT config registers
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        result = ndis_platform_ops.mmio_write(dev, I225_TAS_CONFIG0, baseTimeLow);
        if (result != 0) return result;
        result = ndis_platform_ops.mmio_write(dev, I225_TAS_CONFIG1, baseTimeHigh);
        if (result != 0) return result;
    } else {
        result = ndis_platform_ops.mmio_write(dev, I226_TAS_CONFIG0, baseTimeLow);
        if (result != 0) return result;
        result = ndis_platform_ops.mmio_write(dev, I226_TAS_CONFIG1, baseTimeHigh);
        if (result != 0) return result;
    }
    
    // Program Gate Control List (simplified: pack state and duration per entry)
    for (i = 0; i < 8 && config->gate_durations[i] > 0; i++) {
        ULONG entry = ((ULONG)(config->gate_states[i] & 0xFF) << 24) |
                      (config->gate_durations[i] & 0x00FFFFFF);
        if (context->intel_device.device_type == INTEL_DEVICE_I225) {
            result = ndis_platform_ops.mmio_write(dev, I225_TAS_GATE_LIST + (i * 4), entry);
        } else {
            result = ndis_platform_ops.mmio_write(dev, I226_TAS_GATE_LIST + (i * 4), entry);
        }
        if (result != 0) {
            DEBUGP(DL_ERROR, "Failed to write gate entry %u\n", i);
            return result;
        }
    }
    
    // Enable TAS and mark gate list/base time as valid
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        result = ndis_platform_ops.mmio_read(dev, I225_TAS_CTRL, &regValue);
        if (result != 0) return result;
        regValue |= (ULONG)(I225_TAS_CTRL_EN_MASK | I225_TAS_CTRL_GATE_LIST_MASK | I225_TAS_CTRL_BASE_TIME_MASK);
        result = ndis_platform_ops.mmio_write(dev, I225_TAS_CTRL, regValue);
    } else {
        result = ndis_platform_ops.mmio_read(dev, I226_TAS_CTRL, &regValue);
        if (result != 0) return result;
        regValue |= (ULONG)(I226_TAS_CTRL_EN_MASK | I226_TAS_CTRL_GATE_LIST_MASK | I226_TAS_CTRL_BASE_TIME_MASK);
        result = ndis_platform_ops.mmio_write(dev, I226_TAS_CTRL, regValue);
    }
    if (result != 0) {
        DEBUGP(DL_ERROR, "Failed to enable TAS\n");
        return result;
    }
    
    DEBUGP(DL_TRACE, "<==intel_setup_time_aware_shaper: SUCCESS\n");
    return 0;
}

int intel_setup_frame_preemption(device_t *dev, struct tsn_fp_config *config)
{
    PAVB_DEVICE_CONTEXT context;
    ULONG fp_cfg = 0;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_setup_frame_preemption (REAL I225/I226 IMPLEMENTATION)\n");
    
    if (dev == NULL || config == NULL) {
        DEBUGP(DL_ERROR, "intel_setup_frame_preemption: Invalid parameters\n");
        return -1;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_ERROR, "intel_setup_frame_preemption: No device context\n");
        return -1;
    }
    
    if (context->intel_device.device_type != INTEL_DEVICE_I225 && 
        context->intel_device.device_type != INTEL_DEVICE_I226) {
        DEBUGP(DL_ERROR, "intel_setup_frame_preemption: Unsupported device\n");
        return -1;
    }
    
    // Build FP_CONFIG value using SSOT masks
    if (config->verify_disable) {
        if (context->intel_device.device_type == INTEL_DEVICE_I225)
            fp_cfg |= (ULONG)I225_FP_CONFIG_VERIFY_DIS_MASK;
        else
            fp_cfg |= (ULONG)I226_FP_CONFIG_VERIFY_DIS_MASK;
    }
    // Preemptable queues in bits [15:8]
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        fp_cfg |= ((ULONG)(config->preemptable_queues & 0xFF) << I225_FP_CONFIG_PREEMPTABLE_QUEUES_SHIFT);
    } else {
        fp_cfg |= ((ULONG)(config->preemptable_queues & 0xFF) << I226_FP_CONFIG_PREEMPTABLE_QUEUES_SHIFT);
    }
    // Min fragment size in bits [23:16] encoded in 64B units
    ULONG minFragUnits = (config->min_fragment_size + 63) / 64;
    if (minFragUnits > 255) minFragUnits = 255;
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        fp_cfg |= ((ULONG)minFragUnits << I225_FP_CONFIG_MIN_FRAGMENT_SIZE_SHIFT);
    } else {
        fp_cfg |= ((ULONG)minFragUnits << I226_FP_CONFIG_MIN_FRAGMENT_SIZE_SHIFT);
    }
    // Enable bit
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        fp_cfg |= (ULONG)I225_FP_CONFIG_EN_MASK;
        result = ndis_platform_ops.mmio_write(dev, I225_FP_CONFIG, fp_cfg);
    } else {
        fp_cfg |= (ULONG)I226_FP_CONFIG_EN_MASK;
        result = ndis_platform_ops.mmio_write(dev, I226_FP_CONFIG, fp_cfg);
    }
    if (result != 0) {
        DEBUGP(DL_ERROR, "Failed to write FP_CONFIG\n");
        return result;
    }
    
    // Read FP status (optional)
    ULONG fp_status;
    if (context->intel_device.device_type == INTEL_DEVICE_I225) {
        (void)ndis_platform_ops.mmio_read(dev, I225_FP_STATUS, &fp_status);
    } else {
        (void)ndis_platform_ops.mmio_read(dev, I226_FP_STATUS, &fp_status);
    }
    
    DEBUGP(DL_TRACE, "<==intel_setup_frame_preemption: SUCCESS\n");
    return 0;
}

int intel_setup_ptm(device_t *dev, struct ptm_config *config)
{
    PAVB_DEVICE_CONTEXT context;
    ULONG regValue;
    int result;
    
    DEBUGP(DL_TRACE, "==>intel_setup_ptm (REAL PCIe PTM IMPLEMENTATION)\n");
    
    if (dev == NULL || config == NULL) {
        DEBUGP(DL_ERROR, "intel_setup_ptm: Invalid parameters\n");
        return -1;
    }
    
    context = (PAVB_DEVICE_CONTEXT)dev->private_data;
    if (context == NULL) {
        DEBUGP(DL_ERROR, "intel_setup_ptm: No device context\n");
        return -1;
    }
    
    DEBUGP(DL_INFO, "PTM Config: enabled=%d, clock_granularity=%lu\n",
           config->enabled, config->clock_granularity);
    
    // Locate PTM Capability (ID 0x001F) in PCIe extended config space
    ULONG ptmCapOffset = 0;
    for (ULONG offset = 0x100; offset < 0x200; offset += 4) {
        result = ndis_platform_ops.pci_read_config(dev, offset, &regValue);
        if (result != 0) continue;
        if ((regValue & 0xFFFF) == 0x001F) { // PTM Cap ID
            ptmCapOffset = offset;
            break;
        }
    }
    if (ptmCapOffset == 0) {
        DEBUGP(DL_ERROR, "PTM capability not found\n");
        return -1;
    }
    
    // Read PTM Capability Register
    result = ndis_platform_ops.pci_read_config(dev, ptmCapOffset + 0x04, &regValue);
    if (result != 0) return result;
    BOOLEAN ptmRoot = (regValue & 0x00000004) != 0;
    
    // Configure PTM Control Register
    result = ndis_platform_ops.pci_read_config(dev, ptmCapOffset + 0x08, &regValue);
    if (result != 0) return result;
    if (config->enabled) {
        regValue |= 0x00000001; // Enable PTM
        if (ptmRoot) regValue |= 0x00000002; // Root select if supported
        regValue &= ~0x0000FF00;
        regValue |= ((config->clock_granularity & 0xFF) << 8);
    } else {
        regValue &= ~0x00000001;
    }
    result = ndis_platform_ops.pci_write_config(dev, ptmCapOffset + 0x08, regValue);
    if (result != 0) return result;
    
    // Remove non-SSOT MMIO PTM writes; rely on PCIe PTM capability only
    
    // Verify PTM status
    result = ndis_platform_ops.pci_read_config(dev, ptmCapOffset + 0x08, &regValue);
    if (result == 0) {
        BOOLEAN ptmEnabled = (regValue & 0x00000001) != 0;
        UINT8 effectiveGranularity = (UINT8)((regValue >> 8) & 0xFF);
        DEBUGP(DL_INFO, "PTM Status: enabled=%s, granularity=%u\n",
               ptmEnabled ? "YES" : "NO", effectiveGranularity);
        if (config->enabled && !ptmEnabled) {
            DEBUGP(DL_ERROR, "PTM enable failed - check link partner support\n");
            return -1;
        }
    }
    
    DEBUGP(DL_TRACE, "<==intel_setup_ptm: SUCCESS\n");
    return 0;
}

/**
 * @brief MDIO read (real hardware access)
 */
int intel_mdio_read(device_t *dev, ULONG page, ULONG reg, USHORT *value)
{
    DEBUGP(DL_TRACE, "==>intel_mdio_read (real hardware): page=%lu, reg=%lu\n", page, reg);
    
    if (dev == NULL || value == NULL) {
        return -1;
    }
    
    // Use our platform operations for real MDIO access
    if (ndis_platform_ops.mdio_read) {
        int result = ndis_platform_ops.mdio_read(dev, (USHORT)page, (USHORT)reg, value);
        DEBUGP(DL_TRACE, "<==intel_mdio_read: value=0x%x, result=%d\n", *value, result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_mdio_read: No MDIO read function available\n");
    return -1;
}

/**
 * @brief MDIO write (real hardware access)
 */
int intel_mdio_write(device_t *dev, ULONG page, ULONG reg, USHORT value)
{
    DEBUGP(DL_TRACE, "==>intel_mdio_write (real hardware): page=%lu, reg=%lu, value=0x%x\n", page, reg, value);
    
    if (dev == NULL) {
        return -1;
    }
    
    // Use our platform operations for real MDIO access
    if (ndis_platform_ops.mdio_write) {
        int result = ndis_platform_ops.mdio_write(dev, (USHORT)page, (USHORT)reg, value);
        DEBUGP(DL_TRACE, "<==intel_mdio_write: result=%d\n", result);
        return result;
    }
    
    DEBUGP(DL_ERROR, "intel_mdio_write: No MDIO write function available\n");
    return -1;
}