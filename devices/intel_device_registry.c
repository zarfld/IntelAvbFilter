/*++

Module Name:

    intel_device_registry.c

Abstract:

    Device-specific operations registry and dispatcher.
    Provides clean separation between device implementations and prevents cross-contamination.

--*/

#include "precomp.h"
#include "intel_device_interface.h"
#define INTEL_DEVICE_TYPE_MAX 16  // Adjust as needed for maximum device types

// External declarations for all device implementations
extern const intel_device_ops_t i210_ops;
extern const intel_device_ops_t i217_ops;
extern const intel_device_ops_t i219_ops;
extern const intel_device_ops_t i226_ops;
extern const intel_device_ops_t i350_ops;
extern const intel_device_ops_t e82575_ops;
extern const intel_device_ops_t e82576_ops;
extern const intel_device_ops_t e82580_ops;

// Device operations registry
static const intel_device_ops_t* device_registry[INTEL_DEVICE_TYPE_MAX] = { 0 };

/**
 * @brief Initialize device registry with all supported device implementations
 */
static void initialize_device_registry(void)
{
    static BOOLEAN initialized = FALSE;
    
    if (!initialized) {
        // Register all supported device implementations
        
        // Modern Intel devices (I-series)
        device_registry[INTEL_DEVICE_I210] = &i210_ops;
        device_registry[INTEL_DEVICE_I217] = &i217_ops;
        device_registry[INTEL_DEVICE_I219] = &i219_ops;
        device_registry[INTEL_DEVICE_I226] = &i226_ops;
        device_registry[INTEL_DEVICE_I350] = &i350_ops;
        device_registry[INTEL_DEVICE_I354] = &i350_ops;  // I354 uses same ops as I350
        
        // Legacy IGB devices (82xxx series)
        device_registry[INTEL_DEVICE_82575] = &e82575_ops;
        device_registry[INTEL_DEVICE_82576] = &e82576_ops;
        device_registry[INTEL_DEVICE_82580] = &e82580_ops;
        
        // Additional devices can be registered here as needed:
        // device_registry[INTEL_DEVICE_I225] = &i225_ops;
        // device_registry[INTEL_DEVICE_E810] = &e810_ops;
        
        initialized = TRUE;
        DEBUGP(DL_INFO, "Intel device registry initialized with full IGB support:\n");
        DEBUGP(DL_INFO, "  Modern: I210, I217, I219, I226, I350, I354\n");
        DEBUGP(DL_INFO, "  Legacy: 82575, 82576, 82580\n");
    }
}

/**
 * @brief Get device-specific operations for an Intel device
 * @param device_type Intel device type identifier
 * @return Pointer to device operations structure, or NULL if unsupported
 */
const intel_device_ops_t* intel_get_device_ops(intel_device_type_t device_type)
{
    initialize_device_registry();
    
    if (device_type >= INTEL_DEVICE_TYPE_MAX) {
        DEBUGP(DL_ERROR, "Invalid device type: %d\n", device_type);
        return NULL;
    }
    
    const intel_device_ops_t* ops = device_registry[device_type];
    if (ops == NULL) {
        DEBUGP(DL_WARN, "No implementation registered for device type %d\n", device_type);
    }
    
    return ops;
}

/**
 * @brief Register a device-specific implementation
 * @param device_type Device type to register
 * @param ops Operations structure for this device type
 * @return 0 on success, <0 on error
 */
int intel_register_device_ops(intel_device_type_t device_type, const intel_device_ops_t *ops)
{
    if (device_type >= INTEL_DEVICE_TYPE_MAX || ops == NULL) {
        return -1;
    }
    
    initialize_device_registry();
    
    device_registry[device_type] = ops;
    DEBUGP(DL_INFO, "Registered device implementation for type %d: %s\n", 
           device_type, ops->device_name ? ops->device_name : "Unknown");
    
    return 0;
}