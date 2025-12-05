/*++

Module Name:

    intel_i225_kernel_wrapper.c

Abstract:

    Kernel-mode wrapper for Intel I225/I226 initialization.
    Provides kernel-mode compatible versions that work with our architecture
    where dev->private_data points to PAVB_DEVICE_CONTEXT.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "devices/intel_device_interface.h"

/**
 * @brief I225 initialization - delegates to device operations
 */
int intel_i225_init(device_t *dev)
{
    PAVB_DEVICE_CONTEXT ctx;
    const intel_device_ops_t *ops;
    
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    ctx = (PAVB_DEVICE_CONTEXT)dev->private_data;
    
    // Get device operations for I225
    ops = intel_get_device_ops(INTEL_DEVICE_I225);
    if (!ops || !ops->init) {
        DEBUGP(DL_WARN, "intel_i225_init: No device operations available\n");
        return 0; // Not fatal, just no TSN
    }
    
    // Call device-specific init
    int result = ops->init(dev);
    DEBUGP(DL_INFO, "intel_i225_init: Device init result=%d\n", result);
    
    return result;
}

/**
 * @brief I226 initialization - delegates to device operations
 */
int intel_i226_init(device_t *dev)
{
    PAVB_DEVICE_CONTEXT ctx;
    const intel_device_ops_t *ops;
    
    if (!dev || !dev->private_data) {
        return -1;
    }
    
    ctx = (PAVB_DEVICE_CONTEXT)dev->private_data;
    
    // Get device operations for I226
    ops = intel_get_device_ops(INTEL_DEVICE_I226);
    if (!ops || !ops->init) {
        DEBUGP(DL_WARN, "intel_i226_init: No device operations available\n");
        return 0; // Not fatal, just no TSN
    }
    
    // Call device-specific init
    int result = ops->init(dev);
    DEBUGP(DL_INFO, "intel_i226_init: Device init result=%d\n", result);
    
    return result;
}

/**
 * @brief I225 cleanup
 */
void intel_i225_cleanup(device_t *dev)
{
    const intel_device_ops_t *ops;
    
    if (!dev) {
        return;
    }
    
    ops = intel_get_device_ops(INTEL_DEVICE_I225);
    if (ops && ops->cleanup) {
        ops->cleanup(dev);
    }
}

/**
 * @brief I226 cleanup
 */
void intel_i226_cleanup(device_t *dev)
{
    const intel_device_ops_t *ops;
    
    if (!dev) {
        return;
    }
    
    ops = intel_get_device_ops(INTEL_DEVICE_I226);
    if (ops && ops->cleanup) {
        ops->cleanup(dev);
    }
}
