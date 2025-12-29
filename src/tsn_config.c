/*++
 *
 * TSN Configuration Templates and Helpers - Implementation
 *
--*/

#include "precomp.h"
#include "tsn_config.h"

// Example TAS configuration for audio streaming
// 8 traffic classes with guaranteed bandwidth for audio (TC 6-7)
const struct tsn_tas_config AVB_TAS_CONFIG_AUDIO = {
    .base_time_s = 0,
    .base_time_ns = 0,
    .cycle_time_s = 0,
    .cycle_time_ns = 125000,  // 125us cycle for audio
    .gate_states = {0xC0, 0xFF, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00},  // Audio priority gates
    .gate_durations = {31250, 62500, 31250, 0, 0, 0, 0, 0}             // Durations for each gate
};

// Example TAS configuration for video streaming  
// Higher bandwidth allocation for video traffic
const struct tsn_tas_config AVB_TAS_CONFIG_VIDEO = {
    .base_time_s = 0,
    .base_time_ns = 0,
    .cycle_time_s = 0,
    .cycle_time_ns = 250000,  // 250us cycle for video
    .gate_states = {0xE0, 0xFF, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00},  // Video priority gates
    .gate_durations = {125000, 100000, 25000, 0, 0, 0, 0, 0}           // Durations for each gate
};

// Example TAS configuration for industrial control
// Ultra-low latency with strict timing
const struct tsn_tas_config AVB_TAS_CONFIG_INDUSTRIAL = {
    .base_time_s = 0,
    .base_time_ns = 0,
    .cycle_time_s = 0,
    .cycle_time_ns = 62500,   // 62.5us cycle for industrial
    .gate_states = {0x80, 0xC0, 0xFF, 0x7F, 0x3F, 0x00, 0x00, 0x00},  // Industrial priority gates
    .gate_durations = {12500, 12500, 25000, 6250, 6250, 0, 0, 0}        // Durations for each gate
};

// Mixed best effort and AVB configuration
const struct tsn_tas_config AVB_TAS_CONFIG_MIXED = {
    .base_time_s = 0,
    .base_time_ns = 0,
    .cycle_time_s = 0,
    .cycle_time_ns = 1000000, // 1ms cycle for mixed traffic
    .gate_states = {0xE0, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},   // Mixed priority gates
    .gate_durations = {200000, 800000, 0, 0, 0, 0, 0, 0}                // Durations for each gate
};

/**
 * @brief Get default TAS configuration
 */
NTSTATUS AvbGetDefaultTasConfig(_Out_ struct tsn_tas_config* config)
{
    if (config == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    // Use audio configuration as default
    RtlCopyMemory(config, &AVB_TAS_CONFIG_AUDIO, sizeof(*config));
    return STATUS_SUCCESS;
}

/**
 * @brief Get default Frame Preemption configuration
 */
NTSTATUS AvbGetDefaultFpConfig(_Out_ struct tsn_fp_config* config)
{
    if (config == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    // Enable preemption for TC 0-5 (express), disable for TC 6-7 (preemptible)
    config->preemptable_queues = 0xC0;  // TC 6-7 can be preempted
    config->min_fragment_size = 64;     // Minimum fragment size
    config->verify_disable = 0;        // Enable verification

    return STATUS_SUCCESS;
}

/**
 * @brief Get default PTM configuration
 */
NTSTATUS AvbGetDefaultPtmConfig(_Out_ struct ptm_config* config)
{
    if (config == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    config->enabled = 1;                // Enable PTM
    config->clock_granularity = 4;     // 16ns granularity

    return STATUS_SUCCESS;
}

/**
 * @brief Check TSN feature support based on device ID
 */
BOOLEAN AvbSupportsTas(_In_ UINT16 deviceId)
{
    switch (deviceId) {
        case 0x15F2: // I225-LM 
        case 0x15F3: // I225-V
        case 0x15F4: // I225-K
        case 0x15F5: // I225-K
        case 0x15F6: // I225-IT
        case 0x15F7: // I225-LMvP
        case 0x15F8: // I225-V
        case 0x15F9: // I225-IT
        case 0x15FA: // I225-LM
        case 0x15FB: // I225-LM
        case 0x15FC: // I225-V
        case 0x125B: // I226-LM
        case 0x125C: // I226-V
        case 0x125D: // I226-IT
            return TRUE;
        default:
            return FALSE;
    }
}

BOOLEAN AvbSupportsFp(_In_ UINT16 deviceId)
{
    // Same controllers that support TAS also support Frame Preemption
    return AvbSupportsTas(deviceId);
}

BOOLEAN AvbSupportsPtm(_In_ UINT16 deviceId)
{
    switch (deviceId) {
        case 0x15F2: // I225-LM 
        case 0x15F3: // I225-V
        case 0x15F4: // I225-K
        case 0x15F5: // I225-K
        case 0x15F6: // I225-IT
        case 0x15F7: // I225-LMvP
        case 0x15F8: // I225-V
        case 0x15F9: // I225-IT
        case 0x15FA: // I225-LM
        case 0x15FB: // I225-LM
        case 0x15FC: // I225-V
        case 0x125B: // I226-LM
        case 0x125C: // I226-V
        case 0x125D: // I226-IT
        case 0x1533: // I210-T1
        case 0x1539: // I211-AT
        case 0x157B: // I210-T1
        case 0x157C: // I210-IS
            return TRUE;
        default:
            return FALSE;
    }
}

UINT8 AvbGetMaxTrafficClasses(_In_ UINT16 deviceId)
{
    switch (deviceId) {
        case 0x15F2: // I225-LM 
        case 0x15F3: // I225-V
        case 0x15F4: // I225-K
        case 0x15F5: // I225-K
        case 0x15F6: // I225-IT
        case 0x15F7: // I225-LMvP
        case 0x15F8: // I225-V
        case 0x15F9: // I225-IT
        case 0x15FA: // I225-LM
        case 0x15FB: // I225-LM
        case 0x15FC: // I225-V
        case 0x125B: // I226-LM
        case 0x125C: // I226-V
        case 0x125D: // I226-IT
            return 8;  // 8 traffic classes
        case 0x1533: // I210-T1
        case 0x1539: // I211-AT
        case 0x157B: // I210-T1
        case 0x157C: // I210-IS
            return 4;  // 4 traffic classes
        case 0x15A0: // I219-LM
        case 0x15A1: // I219-V
        case 0x15A2: // I219-LM
        case 0x15A3: // I219-LM
        case 0x15B7: // I219-LM
        case 0x15B8: // I219-V
        case 0x15B9: // I219-LM
        case 0x15BB: // I219-LM
        case 0x15BC: // I219-LM
        case 0x15BD: // I219-LM
        case 0x15BE: // I219-LM
            return 2;  // Limited traffic class support
        default:
            return 1;  // Single queue
    }
}

UINT16 AvbGetMaxGateControlEntries(_In_ UINT16 deviceId)
{
    switch (deviceId) {
        case 0x15F2: // I225-LM 
        case 0x15F3: // I225-V
        case 0x15F4: // I225-K
        case 0x15F5: // I225-K
        case 0x15F6: // I225-IT
        case 0x15F7: // I225-LMvP
        case 0x15F8: // I225-V
        case 0x15F9: // I225-IT
        case 0x15FA: // I225-LM
        case 0x15FB: // I225-LM
        case 0x15FC: // I225-V
        case 0x125B: // I226-LM
        case 0x125C: // I226-V
        case 0x125D: // I226-IT
            return 1024; // Large gate control list
        default:
            return 0;    // TAS not supported
    }
}

/**
 * @brief Validate TSN configuration
 */
NTSTATUS AvbValidateTsnConfig(
    _In_ UINT16 deviceId,
    _In_ const struct tsn_tas_config* tasConfig
)
{
    if (tasConfig == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    if (!AvbSupportsTas(deviceId)) {
        return STATUS_NOT_SUPPORTED;
    }

    // Validate cycle time (must be > 0)
    if (tasConfig->cycle_time_ns == 0 && tasConfig->cycle_time_s == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // Calculate total cycle time in nanoseconds
    UINT64 totalCycleTime = (UINT64)tasConfig->cycle_time_s * 1000000000ULL + tasConfig->cycle_time_ns;

    // Validate that gate durations don't exceed cycle time
    UINT64 totalGateTime = 0;
    for (UINT32 i = 0; i < 8; i++) {
        if (tasConfig->gate_durations[i] > 0) {
            totalGateTime += tasConfig->gate_durations[i];
        }
    }

    if (totalGateTime > totalCycleTime) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}
