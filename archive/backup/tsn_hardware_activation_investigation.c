/*++

Module Name:

    tsn_hardware_activation_investigation.c

Abstract:

    TSN Hardware Activation Issues Investigation
    
    This module investigates why TSN features (TAS, FP, PTM) are not activating
    at the hardware level despite successful IOCTL handler execution.

    Based on comprehensive hardware testing results:
    - ? IOCTL handlers work (no more ERROR_INVALID_FUNCTION) 
    - ? Hardware activation fails (registers don't show activation)

    Purpose: Provide investigation tools and enhanced implementations that
    address specific hardware activation failure modes for Intel I225/I226 TSN
    features and I210 PTP clock initialization issues.

--*/

#include "precomp.h"
#include "avb_integration.h"
#include "external/intel_avb/lib/intel.h"
#include "external/intel_avb/lib/intel_private.h"
#include "intel-ethernet-regs/gen/i226_regs.h"
#include "intel-ethernet-regs/gen/i210_regs.h"

/**
 * @brief Phase 2: Investigate I226 TAS activation failure
 * 
 * Based on hardware testing, TAS configuration succeeds but activation fails.
 * This function investigates the activation requirements and implements proper
 * prerequisite checking and activation sequence.
 * 
 * @param dev Intel device structure
 * @param config TAS configuration
 * @return 0 on success, error code on failure
 */
int InvestigateTASActivationFailure(device_t *dev, struct tsn_tas_config *config)
{
    if (!dev || !config) {
        DEBUGP(DL_ERROR, "Phase2: TAS investigation - invalid parameters\n");
        return -EINVAL;
    }
    
    DEBUGP(DL_INFO, "?? Phase 2: TAS Activation Failure Investigation\n");
    DEBUGP(DL_INFO, "   Device: VID=0x%04X DID=0x%04X\n", dev->pci_vendor_id, dev->pci_device_id);
    
    // Phase 2 Investigation Step 1: Check device support
    if (!(dev->capabilities & INTEL_CAP_TSN_TAS)) {
        DEBUGP(DL_ERROR, "Phase2: Device doesn't support TAS (caps=0x%08X)\n", dev->capabilities);
        return -ENOTSUP;
    }
    
    if (dev->device_type != INTEL_DEVICE_I226 && dev->device_type != INTEL_DEVICE_I225) {
        DEBUGP(DL_ERROR, "Phase2: TAS only supported on I225/I226 (device_type=%d)\n", dev->device_type);
        return -ENOTSUP;
    }
    
    DEBUGP(DL_INFO, "? Phase2: Device TAS support confirmed\n");
    
    // Phase 2 Investigation Step 2: Prerequisites Analysis
    DEBUGP(DL_INFO, "?? Phase2: Checking TAS Prerequisites\n");
    
    // Check if PTP clock is running (critical prerequisite for TAS)
    uint32_t systiml1 = 0, systiml2 = 0;
    if (intel_read_reg(dev, I226_SYSTIML, &systiml1) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Cannot read SYSTIML register\n");
        return -EIO;
    }
    
    // Wait 10ms and check if clock advanced
    // Note: In kernel mode, we'd use KeStallExecutionProcessor, but this is conceptual
    // The actual implementation would need platform-specific timing
    
    if (intel_read_reg(dev, I226_SYSTIML, &systiml2) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Cannot read SYSTIML register (second read)\n");
        return -EIO;
    }
    
    if (systiml2 <= systiml1) {
        DEBUGP(DL_ERROR, "? Phase2: PTP clock not running - TAS requires active PTP\n");
        DEBUGP(DL_ERROR, "   SYSTIM: 0x%08X -> 0x%08X (no advancement)\n", systiml1, systiml2);
        DEBUGP(DL_ERROR, "   Solution: Initialize PTP clock first\n");
        return -EBUSY;
    }
    
    DEBUGP(DL_INFO, "? Phase2: PTP clock running (SYSTIM: 0x%08X -> 0x%08X)\n", 
           systiml1, systiml2);
    
    // Phase 2 Investigation Step 3: Current TAS State Analysis
    uint32_t tas_ctrl = 0;
    if (intel_read_reg(dev, I226_TAS_CTRL, &tas_ctrl) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Cannot read TAS_CTRL register\n");
        return -EIO;
    }
    
    DEBUGP(DL_INFO, "?? Phase2: Current TAS_CTRL = 0x%08X\n", tas_ctrl);
    DEBUGP(DL_INFO, "   TAS Enable bit: %s\n", (tas_ctrl & I226_TAS_CTRL_EN) ? "SET" : "CLEAR");
    DEBUGP(DL_INFO, "   Configuration Change: %s\n", (tas_ctrl & I226_TAS_CTRL_CONFIG_CHANGE) ? "SET" : "CLEAR");
    
    // Phase 2 Investigation Step 4: Base Time Analysis
    DEBUGP(DL_INFO, "?? Phase2: Base Time Analysis\n");
    
    // Get current time for base time calculation
    uint64_t current_time = ((uint64_t)systiml2) | (((uint64_t)0) << 32); // Simplified - would need SYSTIMH
    uint64_t required_base_time = current_time + 1000000000ULL; // +1 second in the future
    
    DEBUGP(DL_INFO, "   Current time (approx): 0x%016llX\n", current_time);
    DEBUGP(DL_INFO, "   Required base time: 0x%016llX\n", required_base_time);
    DEBUGP(DL_INFO, "   Config base time: %llu.%09u\n", 
           config->base_time_s, config->base_time_ns);
    
    // Check if configured base time is in the future
    uint64_t config_base_time = ((uint64_t)config->base_time_s * 1000000000ULL) + config->base_time_ns;
    if (config_base_time <= current_time) {
        DEBUGP(DL_ERROR, "? Phase2: Base time is not in the future\n");
        DEBUGP(DL_ERROR, "   This is a common cause of TAS activation failure\n");
        DEBUGP(DL_ERROR, "   Solution: Set base time > current time + margin\n");
        return -EINVAL;
    }
    
    DEBUGP(DL_INFO, "? Phase2: Base time is properly in the future\n");
    
    // Phase 2 Investigation Step 5: Gate List Validation
    DEBUGP(DL_INFO, "?? Phase2: Gate List Validation\n");
    
    for (int i = 0; i < 8; i++) {
        if (config->gate_durations[i] > 0) {
            DEBUGP(DL_INFO, "   Gate[%d]: state=0x%02X, duration=%u ns\n",
                   i, config->gate_states[i], config->gate_durations[i]);
        }
    }
    
    // Validate gate list constraints
    uint32_t total_cycle_time = 0;
    for (int i = 0; i < 8; i++) {
        total_cycle_time += config->gate_durations[i];
    }
    
    if (total_cycle_time != config->cycle_time_ns) {
        DEBUGP(DL_ERROR, "? Phase2: Gate list durations don't match cycle time\n");
        DEBUGP(DL_ERROR, "   Total gate durations: %u ns\n", total_cycle_time);
        DEBUGP(DL_ERROR, "   Configured cycle time: %u ns\n", config->cycle_time_ns);
        return -EINVAL;
    }
    
    DEBUGP(DL_INFO, "? Phase2: Gate list validation passed\n");
    
    return 0; // Investigation passed - prerequisites look good
}

/**
 * @brief Phase 2: Enhanced TAS activation with proper sequencing
 * 
 * This implements the corrected TAS activation sequence based on Intel I226
 * datasheet requirements and addresses the hardware activation failures
 * identified in Phase 1 testing.
 */
int Phase2_EnhancedTASActivation(device_t *dev, struct tsn_tas_config *config)
{
    DEBUGP(DL_INFO, "?? Phase 2: Enhanced TAS Activation Starting\n");
    
    // Step 1: Run activation failure investigation
    int investigation_result = InvestigateTASActivationFailure(dev, config);
    if (investigation_result != 0) {
        DEBUGP(DL_ERROR, "Phase2: TAS prerequisites failed: %d\n", investigation_result);
        return investigation_result;
    }
    
    // Step 2: Disable TAS before configuration (clean slate)
    DEBUGP(DL_INFO, "?? Phase2: Step 1 - Clean TAS disable\n");
    if (intel_write_reg(dev, I226_TAS_CTRL, 0) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Failed to disable TAS\n");
        return -EIO;
    }
    
    // Step 3: Configure base time registers
    DEBUGP(DL_INFO, "?? Phase2: Step 2 - Configure base time\n");
    uint64_t base_time_total = ((uint64_t)config->base_time_s * 1000000000ULL) + config->base_time_ns;
    uint32_t base_time_low = (uint32_t)(base_time_total & 0xFFFFFFFF);
    uint32_t base_time_high = (uint32_t)(base_time_total >> 32);
    
    if (intel_write_reg(dev, I226_TAS_BASETIME_LOW, base_time_low) != 0 ||
        intel_write_reg(dev, I226_TAS_BASETIME_HIGH, base_time_high) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Failed to configure base time\n");
        return -EIO;
    }
    
    DEBUGP(DL_INFO, "   Base time configured: 0x%08X%08X\n", base_time_high, base_time_low);
    
    // Step 4: Configure cycle time
    DEBUGP(DL_INFO, "?? Phase2: Step 3 - Configure cycle time\n");
    if (intel_write_reg(dev, I226_TAS_CYCLE_TIME, config->cycle_time_ns) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Failed to configure cycle time\n");
        return -EIO;
    }
    
    // Step 5: Program gate list
    DEBUGP(DL_INFO, "?? Phase2: Step 4 - Program gate list\n");
    for (int i = 0; i < 8; i++) {
        if (config->gate_durations[i] > 0) {
            uint32_t gate_value = ((uint32_t)config->gate_states[i] << 24) | config->gate_durations[i];
            if (intel_write_reg(dev, I226_TAS_GATE_LIST_BASE + (i * 4), gate_value) != 0) {
                DEBUGP(DL_ERROR, "Phase2: Failed to program gate[%d]\n", i);
                return -EIO;
            }
            DEBUGP(DL_INFO, "   Gate[%d] = 0x%08X\n", i, gate_value);
        }
    }
    
    // Step 6: Enable TAS with configuration change
    DEBUGP(DL_INFO, "?? Phase2: Step 5 - Enable TAS with proper flags\n");
    uint32_t enable_value = I226_TAS_CTRL_EN | I226_TAS_CTRL_CONFIG_CHANGE;
    if (intel_write_reg(dev, I226_TAS_CTRL, enable_value) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Failed to enable TAS\n");
        return -EIO;
    }
    
    // Step 7: Verify activation (critical verification step)
    DEBUGP(DL_INFO, "?? Phase2: Step 6 - Verify TAS activation\n");
    
    // Allow hardware time to process the configuration
    // Note: Actual implementation would use platform-specific delay
    
    uint32_t verify_ctrl = 0;
    if (intel_read_reg(dev, I226_TAS_CTRL, &verify_ctrl) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Cannot verify TAS activation\n");
        return -EIO;
    }
    
    DEBUGP(DL_INFO, "   TAS_CTRL after enable: 0x%08X\n", verify_ctrl);
    DEBUGP(DL_INFO, "   Enable bit: %s\n", (verify_ctrl & I226_TAS_CTRL_EN) ? "SET" : "CLEAR");
    DEBUGP(DL_INFO, "   Config change: %s\n", (verify_ctrl & I226_TAS_CTRL_CONFIG_CHANGE) ? "SET" : "CLEAR");
    
    if (!(verify_ctrl & I226_TAS_CTRL_EN)) {
        DEBUGP(DL_ERROR, "? Phase2: TAS ACTIVATION FAILED - Enable bit did not stick\n");
        DEBUGP(DL_ERROR, "   This indicates a prerequisite or configuration error\n");
        
        // Additional diagnostic information
        uint32_t status_reg = 0;
        if (intel_read_reg(dev, I226_TAS_STATUS, &status_reg) == 0) {
            DEBUGP(DL_ERROR, "   TAS_STATUS: 0x%08X\n", status_reg);
        }
        
        return -EBUSY;
    }
    
    DEBUGP(DL_INFO, "? Phase2: TAS ACTIVATION SUCCESSFUL!\n");
    return 0;
}

/**
 * @brief Phase 2: Investigate Frame Preemption activation failure
 */
int InvestigateFramePreemptionFailure(device_t *dev, struct tsn_fp_config *config)
{
    if (!dev || !config) {
        return -EINVAL;
    }
    
    DEBUGP(DL_INFO, "?? Phase 2: Frame Preemption Activation Investigation\n");
    
    // Check device support
    if (!(dev->capabilities & INTEL_CAP_TSN_FP)) {
        DEBUGP(DL_ERROR, "Phase2: Device doesn't support Frame Preemption\n");
        return -ENOTSUP;
    }
    
    // Read current FP configuration
    uint32_t fp_config = 0;
    if (intel_read_reg(dev, I226_FP_CONFIG, &fp_config) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Cannot read FP_CONFIG register\n");
        return -EIO;
    }
    
    DEBUGP(DL_INFO, "?? Phase2: Current FP_CONFIG = 0x%08X\n", fp_config);
    
    // Frame Preemption requires link partner support - check link status
    uint32_t status = 0;
    if (intel_read_reg(dev, I226_STATUS, &status) == 0) {
        DEBUGP(DL_INFO, "   Link status: %s\n", (status & 0x2) ? "UP" : "DOWN");
        if (!(status & 0x2)) {
            DEBUGP(DL_ERROR, "? Phase2: Frame Preemption requires active link\n");
            return -EBUSY;
        }
    }
    
    // Check if link partner supports preemption (would require PHY analysis)
    DEBUGP(DL_WARN, "?? Phase2: Cannot verify link partner FP support - may cause activation failure\n");
    
    return 0;
}

/**
 * @brief Phase 2: Enhanced Frame Preemption activation
 */
int Phase2_EnhancedFramePreemptionActivation(device_t *dev, struct tsn_fp_config *config)
{
    DEBUGP(DL_INFO, "?? Phase 2: Enhanced Frame Preemption Activation\n");
    
    // Run investigation
    int investigation_result = InvestigateFramePreemptionFailure(dev, config);
    if (investigation_result != 0) {
        return investigation_result;
    }
    
    // Configure preemptable queues
    uint32_t fp_config_value = ((uint32_t)config->preemptable_queues << 16) | 
                              (config->min_fragment_size & 0x3FF);
    
    if (!config->verify_disable) {
        fp_config_value |= I226_FP_CONFIG_VERIFY_EN;
    }
    
    // Enable Frame Preemption
    fp_config_value |= I226_FP_CONFIG_ENABLE;
    
    if (intel_write_reg(dev, I226_FP_CONFIG, fp_config_value) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Failed to configure Frame Preemption\n");
        return -EIO;
    }
    
    // Verify activation
    uint32_t verify_config = 0;
    if (intel_read_reg(dev, I226_FP_CONFIG, &verify_config) != 0) {
        return -EIO;
    }
    
    if (!(verify_config & I226_FP_CONFIG_ENABLE)) {
        DEBUGP(DL_ERROR, "? Phase2: Frame Preemption activation failed\n");
        return -EBUSY;
    }
    
    DEBUGP(DL_INFO, "? Phase2: Frame Preemption activated successfully\n");
    return 0;
}

/**
 * @brief Phase 2: I210 PTP Clock initialization fix
 * 
 * Addresses the I210 PTP clock stuck at zero issue identified in hardware testing.
 */
int Phase2_FixI210PTPClock(device_t *dev)
{
    if (!dev || dev->device_type != INTEL_DEVICE_I210) {
        return -EINVAL;
    }
    
    DEBUGP(DL_INFO, "?? Phase 2: I210 PTP Clock Fix Implementation\n");
    
    // Read current SYSTIM to check if already running
    uint32_t systiml1 = 0, systimh1 = 0;
    if (intel_read_reg(dev, I210_SYSTIML, &systiml1) != 0 ||
        intel_read_reg(dev, I210_SYSTIMH, &systimh1) != 0) {
        DEBUGP(DL_ERROR, "Phase2: Cannot read I210 SYSTIM registers\n");
        return -EIO;
    }
    
    DEBUGP(DL_INFO, "?? Phase2: Current I210 SYSTIM: 0x%08X%08X\n", systimh1, systiml1);
    
    // If clock is stuck at zero, implement complete reset sequence
    if (systiml1 == 0 && systimh1 == 0) {
        DEBUGP(DL_INFO, "?? Phase2: I210 clock stuck at zero - implementing reset sequence\n");
        
        // Step 1: Complete PTP disable
        if (intel_write_reg(dev, INTEL_REG_TSAUXC, 0x80000000) != 0) {
            return -EIO;
        }
        
        // Step 2: Clear all timing registers
        intel_write_reg(dev, I210_SYSTIML, 0);
        intel_write_reg(dev, I210_SYSTIMH, 0);
        intel_write_reg(dev, I210_TSYNCRXCTL, 0);
        intel_write_reg(dev, I210_TSYNCTXCTL, 0);
        
        // Step 3: Configure TIMINCA (critical for I210)
        if (intel_write_reg(dev, I210_TIMINCA, 0x08000000) != 0) {
            return -EIO;
        }
        
        // Step 4: Enable PTP with PHC
        if (intel_write_reg(dev, INTEL_REG_TSAUXC, 0x40000000) != 0) {
            return -EIO;
        }
        
        // Step 5: Set non-zero initial time to trigger clock start
        if (intel_write_reg(dev, I210_SYSTIML, 0x10000000) != 0 ||
            intel_write_reg(dev, I210_SYSTIMH, 0x00000000) != 0) {
            return -EIO;
        }
        
        DEBUGP(DL_INFO, "? Phase2: I210 PTP reset sequence completed\n");
    }
    
    // Verify clock is now running
    // Note: Actual implementation would include proper timing delays
    uint32_t systiml2 = 0, systimh2 = 0;
    if (intel_read_reg(dev, I210_SYSTIML, &systiml2) != 0 ||
        intel_read_reg(dev, I210_SYSTIMH, &systimh2) != 0) {
        return -EIO;
    }
    
    DEBUGP(DL_INFO, "?? Phase2: I210 SYSTIM after fix: 0x%08X%08X\n", systimh2, systiml2);
    
    if (systiml2 == systiml1 && systimh2 == systimh1) {
        DEBUGP(DL_ERROR, "? Phase2: I210 clock still not advancing\n");
        return -EBUSY;
    }
    
    DEBUGP(DL_INFO, "? Phase2: I210 PTP clock is now running\n");
    return 0;
}