/*++

Module Name:

    intel_device_interface.h

Abstract:

    Common interface definition for Intel device-specific implementations.
    Provides clean separation and extensibility for future Intel adapters.

--*/

#pragma once

#include "precomp.h"
#include "external/intel_avb/lib/intel_private.h"

// Forward declarations
typedef struct _device_t device_t;
struct tsn_tas_config;
struct tsn_fp_config;
struct ptm_config;

/**
 * @brief Intel device-specific operations interface
 * 
 * Each Intel device family implements this interface to provide
 * device-specific hardware access and feature support.
 * 
 * This design prevents cross-contamination between device implementations
 * and allows easy addition of new Intel adapter families.
 * 
 * Implements: #40 (REQ-F-DEVICE-ABS-003: Register Access via Device Abstraction Layer)
 */
typedef struct _intel_device_ops {
    // Device identification and capabilities
    const char* device_name;
    uint32_t supported_capabilities;
    
    // Basic device operations
    int (*init)(device_t *dev);
    int (*cleanup)(device_t *dev);
    int (*get_info)(device_t *dev, char *buffer, ULONG size);  // Use ULONG for kernel compatibility
    
    // PTP/IEEE 1588 operations
    int (*set_systime)(device_t *dev, uint64_t systime);
    int (*get_systime)(device_t *dev, uint64_t *systime);
    int (*init_ptp)(device_t *dev);
    int (*enable_packet_timestamping)(device_t *dev, int enable);  // Enable TSYNCRXCTL/TSYNCTXCTL
    
    // Target time operations (Issue #13 Task 7, IOCTLs 43/44)
    int (*set_target_time)(device_t *dev, uint8_t timer_index, uint64_t target_time_ns, int enable_interrupt);
    int (*get_target_time)(device_t *dev, uint8_t timer_index, uint64_t *target_time_ns);
    int (*check_autt_flags)(device_t *dev, uint8_t *autt_flags);  // Returns AUTT0/AUTT1 status bits
    int (*clear_autt_flag)(device_t *dev, uint8_t timer_index);   // Clear AUTT0 or AUTT1 flag
    
    // Auxiliary timestamp operations (Issue #7, IOCTL 44)
    int (*get_aux_timestamp)(device_t *dev, uint8_t aux_index, uint64_t *aux_timestamp_ns);
    int (*clear_aux_timestamp_flag)(device_t *dev, uint8_t aux_index);  // Clear AUTT flag for aux timestamp
    
    // TSN operations (optional - can be NULL for basic devices)
    int (*setup_tas)(device_t *dev, struct tsn_tas_config *config);
    int (*setup_frame_preemption)(device_t *dev, struct tsn_fp_config *config);
    int (*setup_ptm)(device_t *dev, struct ptm_config *config);
    
    // Device-specific register access (optional overrides)
    int (*read_register)(device_t *dev, uint32_t offset, uint32_t *value);
    int (*write_register)(device_t *dev, uint32_t offset, uint32_t value);
    
    // MDIO operations (for devices that support it)
    int (*mdio_read)(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t *value);
    int (*mdio_write)(device_t *dev, uint16_t phy_addr, uint16_t reg_addr, uint16_t value);
    
    // Advanced features (device-specific extensions)
    int (*enable_advanced_features)(device_t *dev, uint32_t feature_mask);
    int (*validate_configuration)(device_t *dev);
    
} intel_device_ops_t;

/**
 * @brief Get device-specific operations for an Intel device
 * @param device_type Intel device type identifier
 * @return Pointer to device operations structure, or NULL if unsupported
 */
const intel_device_ops_t* intel_get_device_ops(intel_device_type_t device_type);

/**
 * @brief Register a device-specific implementation
 * @param device_type Device type to register
 * @param ops Operations structure for this device type
 * @return 0 on success, <0 on error
 */
int intel_register_device_ops(intel_device_type_t device_type, const intel_device_ops_t *ops);

// Device-specific operation declarations (implemented in device-specific files)
extern const intel_device_ops_t i210_ops;
extern const intel_device_ops_t i217_ops;
extern const intel_device_ops_t i219_ops;
extern const intel_device_ops_t i225_ops;
extern const intel_device_ops_t i226_ops;
extern const intel_device_ops_t i350_ops;
extern const intel_device_ops_t e810_ops;