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
 *
 * get_systime() return-code contract:
 *   SYSTIME_RC_HW_OK (0): hardware SYSTIM register successfully read; value is hardware-derived.
 *   SYSTIME_RC_SW_OK (1): software fallback; value is valid (correct ns domain, advancing)
 *                         but derived from the wall clock, NOT from the hardware counter.
 *   <0:                   error; *systime is indeterminate.
 * Callers that need hardware provenance must check rc == SYSTIME_RC_HW_OK.
 * Callers that accept any valid value must check rc >= 0 (and *systime != 0).
 */
#define SYSTIME_RC_HW_OK  0    /* hardware SYSTIM read; hardware-provenance */
#define SYSTIME_RC_SW_OK  1    /* software-clock fallback; correct ns domain but not hardware */
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
    
    // PTP register access operations (eliminates magic numbers in src/)
    int (*read_tx_timestamp)(device_t *dev, uint64_t *timestamp_ns);   // Read TXSTMPL/H
    int (*read_rx_timestamp)(device_t *dev, uint64_t *timestamp_ns);   // Read RXSTMPL/H  
    int (*poll_tx_timestamp_fifo)(device_t *dev, uint64_t *timestamp_ns);  // Poll TX FIFO (returns 0=empty, 1=valid)
    int (*read_timinca)(device_t *dev, uint32_t *timinca_value);       // Read TIMINCA register
    int (*write_timinca)(device_t *dev, uint32_t timinca_value);       // Write TIMINCA register
    int (*read_tsauxc)(device_t *dev, uint32_t *tsauxc_value);         // Read TSAUXC register
    int (*write_tsauxc)(device_t *dev, uint32_t tsauxc_value);         // Write TSAUXC register (for bit operations)
    
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
        /* disable_tas: clear the hardware TSN transmit mode bit (TQAVCTRL.TRANSMIT_MODE_TSN on I226).
         * Returns 0 on success, -ENOTSUP if hardware has no TAS disable path, <0 on hardware error.
         * NULL means device never had TAS hardware (e.g. I219, I210). */
        int (*disable_tas)(device_t *dev);
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

    /* ---- PHC frequency adjustment (IOCTL_AVB_ADJUST_FREQUENCY) --------------------------------
     * The generic layer only validates the IOCTL buffer, routes, and translates the result to
     * NTSTATUS / ETW.  Everything family-specific (TIMINCA encoding, representability, the applicable
     * range, the register transaction) is owned by the device implementation behind these two ops.
     *
     * Logical request: increment = increment_ns + increment_frac / 2^32 nanoseconds per clock cycle.
     *
     * Return codes (shared by both ops):
     *    0          accepted / applied
     *   -EINVAL     request not acceptable for this family (not representable, or outside the family's
     *               API range): STATUS_INVALID_PARAMETER.  No register was touched.
     *   -ENOTSUP    the HARDWARE has no frequency-adjust path: STATUS_NOT_SUPPORTED.
     *   other <0    register access failed (-EIO): STATUS_UNSUCCESSFUL.
     * A NULL op pointer means the DRIVER has no implementation for this family (migration state):
     * the generic layer then uses its legacy path, and at the end state returns STATUS_NOT_IMPLEMENTED.
     */

    /* Side-effect free: no MMIO, no clock mutation, no implicit initialization. */
    int (*validate_frequency_request)(device_t *dev, uint32_t increment_ns, uint32_t increment_frac);

    /* Explicitly mutating.  Re-validates first (rejection touches no register), reads the previous raw
     * TIMINCA value into *previous_increment (the value reported as AVB_FREQUENCY_REQUEST.current_increment;
     * per family: raw register, except I219 which keeps its documented logical_ns << 24 normalisation),
     * then encodes and writes.  *previous_increment is set only once the read succeeded. */
    int (*adjust_frequency)(device_t *dev, uint32_t increment_ns, uint32_t increment_frac,
                            uint32_t *previous_increment);

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