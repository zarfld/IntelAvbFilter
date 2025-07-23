/*++
 *
 * TSN Configuration Templates and Helpers
 * 
 * This file provides example configurations and helper functions
 * for Time-Sensitive Networking (TSN) features.
 *
--*/

#pragma once

#include "precomp.h"

// TSN Configuration Templates

/**
 * @brief Get default Time-Aware Shaper (TAS) configuration for I225/I226
 * 
 * This provides a basic TAS configuration suitable for audio/video streaming
 * with 4 traffic classes.
 */
NTSTATUS AvbGetDefaultTasConfig(_Out_ struct tsn_tas_config* config);

/**
 * @brief Get default Frame Preemption (FP) configuration
 * 
 * This enables frame preemption for low-latency traffic on express queues.
 */
NTSTATUS AvbGetDefaultFpConfig(_Out_ struct tsn_fp_config* config);

/**
 * @brief Get default PCIe Precision Time Measurement (PTM) configuration
 * 
 * This enables PTM for improved timestamp accuracy across PCIe hierarchy.
 */
NTSTATUS AvbGetDefaultPtmConfig(_Out_ struct tsn_ptm_config* config);

/**
 * @brief Validate TSN configuration for specific Intel controller
 * 
 * @param deviceId PCI device ID of Intel controller
 * @param tasConfig TAS configuration to validate
 * @return NTSTATUS indicating validation result
 */
NTSTATUS AvbValidateTsnConfig(
    _In_ UINT16 deviceId,
    _In_ const struct tsn_tas_config* tasConfig
);

/**
 * @brief Example configurations for different use cases
 */

// Audio streaming optimized (low latency, guaranteed bandwidth)
extern const struct tsn_tas_config AVB_TAS_CONFIG_AUDIO;

// Video streaming optimized (higher bandwidth, moderate latency)
extern const struct tsn_tas_config AVB_TAS_CONFIG_VIDEO;

// Industrial control optimized (ultra-low latency, deterministic)
extern const struct tsn_tas_config AVB_TAS_CONFIG_INDUSTRIAL;

// Best effort + AVB mixed (backward compatibility)
extern const struct tsn_tas_config AVB_TAS_CONFIG_MIXED;

/**
 * @brief Helper functions for TSN feature detection
 */

/**
 * @brief Check if controller supports Time-Aware Shaper
 */
BOOLEAN AvbSupportsTas(_In_ UINT16 deviceId);

/**
 * @brief Check if controller supports Frame Preemption
 */
BOOLEAN AvbSupportsFp(_In_ UINT16 deviceId);

/**
 * @brief Check if controller supports PCIe PTM
 */
BOOLEAN AvbSupportsPtm(_In_ UINT16 deviceId);

/**
 * @brief Get maximum number of traffic classes supported
 */
UINT8 AvbGetMaxTrafficClasses(_In_ UINT16 deviceId);

/**
 * @brief Get maximum gate control list entries for TAS
 */
UINT16 AvbGetMaxGateControlEntries(_In_ UINT16 deviceId);
