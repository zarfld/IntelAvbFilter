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
    .base_time = 0,
    .cycle_time = 125000,  // 125us cycle for audio
    .cycle_extension = 0,
    .num_entries = 4,
    .entries = {
        { .gate_states = 0xC0, .time_interval = 31250 },  // TC 6-7 (audio) - 25% 
        { .gate_states = 0xFF, .time_interval = 62500 },  // All classes - 50%
        { .gate_states = 0x3F, .time_interval = 31250 },  // TC 0-5 - 25%
        { .gate_states = 0x00, .time_interval = 0 }       // Guard band
    }
};

// Example TAS configuration for video streaming  
// Higher bandwidth allocation for video traffic
const struct tsn_tas_config AVB_TAS_CONFIG_VIDEO = {
    .base_time = 0,
    .cycle_time = 250000,  // 250us cycle for video
    .cycle_extension = 0,
    .num_entries = 3,
    .entries = {
        { .gate_states = 0xE0, .time_interval = 125000 }, // TC 5-7 (video/audio) - 50%
        { .gate_states = 0xFF, .time_interval = 100000 }, // All classes - 40%
        { .gate_states = 0x1F, .time_interval = 25000 }   // TC 0-4 - 10%
    }
};

// Example TAS configuration for industrial control
// Ultra-low latency with strict timing
const struct tsn_tas_config AVB_TAS_CONFIG_INDUSTRIAL = {
    .base_time = 0,
    .cycle_time = 62500,   // 62.5us cycle for industrial
    .cycle_extension = 0,
    .num_entries = 6,
    .entries = {
        { .gate_states = 0x80, .time_interval = 12500 },  // TC 7 (critical) - 20%
        { .gate_states = 0xC0, .time_interval = 12500 },  // TC 6-7 - 20%
        { .gate_states = 0xFF, .time_interval = 25000 },  // All classes - 40%
        { .gate_states = 0x7F, .time_interval = 6250 },   // TC 0-6 - 10%
        { .gate_states = 0x3F, .time_interval = 6250 },   // TC 0-5 - 10%
        { .gate_states = 0x00, .time_interval = 0 }       // Guard band
    }
};

// Mixed best effort and AVB configuration
const struct tsn_tas_config AVB_TAS_CONFIG_MIXED = {
    .base_time = 0,
    .cycle_time = 1000000, // 1ms cycle for mixed traffic
    .cycle_extension = 10000,
    .num_entries = 2,
    .entries = {
        { .gate_states = 0xE0, .time_interval = 200000 }, // AVB classes - 20%
        { .gate_states = 0xFF, .time_interval = 800000 }  // All classes - 80%
    }
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
    config->preemptible_queues = 0xC0;  // TC 6-7 can be preempted
    config->express_queues = 0x3F;      // TC 0-5 are express (non-preemptible)
    config->additional_fragment_size = 64;
    config->verify_disable_timeout = 128000; // 128ms
    config->verify_enable_timeout = 128000;  // 128ms

    return STATUS_SUCCESS;
}

/**
 * @brief Get default PTM configuration
 */
NTSTATUS AvbGetDefaultPtmConfig(_Out_ struct tsn_ptm_config* config)
{
    if (config == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    config->enable = TRUE;
    config->root_select = FALSE;  // Auto-select root
    config->local_clock_granularity = 4; // 16ns granularity
    config->effective_granularity = 0;   // Auto-calculate

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
        case 0x3100: // I226-LM
        case 0x3101: // I226-V
        case 0x3102: // I226-K
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
        case 0x3100: // I226-LM
        case 0x3101: // I226-V
        case 0x3102: // I226-K
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
        case 0x3100: // I226-LM
        case 0x3101: // I226-V
        case 0x3102: // I226-K
            return 8;  // 8 traffic classes
        case 0x1533: // I210-T1
        case 0x1539: // I211-AT
        case 0x157B: // I210-T1
        case 0x157C: // I210-IS
            return 4;  // 4 traffic classes
        case 0x15B7: // I219-LM
        case 0x15B8: // I219-V
        case 0x15B9: // I219-LM
        case 0x15BB: // I219-LM
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
        case 0x3100: // I226-LM
        case 0x3101: // I226-V
        case 0x3102: // I226-K
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
    if (tasConfig->cycle_time == 0) {
        return STATUS_INVALID_PARAMETER;
    }

    // Validate number of entries
    UINT16 maxEntries = AvbGetMaxGateControlEntries(deviceId);
    if (tasConfig->num_entries > maxEntries) {
        return STATUS_INVALID_PARAMETER;
    }

    // Validate that total time intervals don't exceed cycle time
    UINT64 totalTime = 0;
    for (UINT32 i = 0; i < tasConfig->num_entries; i++) {
        totalTime += tasConfig->entries[i].time_interval;
    }

    if (totalTime > tasConfig->cycle_time) {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}
