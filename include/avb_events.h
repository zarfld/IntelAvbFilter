/**
 * @file avb_events.h
 * @brief SSOT for AVB Driver Event Log IDs
 *
 * Single Source of Truth for all Windows Event Log IDs emitted by the
 * IntelAvbFilter driver.  Every event ID in the driver and all tests MUST be
 * defined here.  Hardcoded integer literals for event IDs are forbidden.
 *
 * Implements: #289 (TEST-EVENT-ID-SSOT-001: Event ID SSOT)
 * Traces to:  #21  (REQ-NF-REGS-001: Eliminate Magic Numbers)
 *
 * Range allocation:
 *   17300 - 17399  Hardware Abstraction Layer (HAL) events
 *   17400 - 17499  IOCTL / NDIS filter events
 *   17500 - 17599  PTP / timestamping events
 *   17600 - 17699  TSN / scheduler events
 *
 * @note This header contains only preprocessor definitions so it is safe
 *       to include from both kernel-mode and user-mode translation units.
 */

#ifndef AVB_EVENTS_H
#define AVB_EVENTS_H

/* -------------------------------------------------------------------------
 * HAL Events  17300 - 17399
 * Source of record: src/avb_hal.c, test_hal_errors.c
 * ------------------------------------------------------------------------- */
#define AVB_EVENT_UNSUPPORTED_DEVICE        17301  /**< Unknown/unsupported PCI Device ID   */
#define AVB_EVENT_NULL_OPERATION            17302  /**< HAL ops table contains NULL pointer  */
#define AVB_EVENT_CAPABILITY_MISMATCH       17303  /**< Feature not supported by hardware    */
#define AVB_EVENT_INVALID_REGISTER_OFFSET   17304  /**< BAR0 register offset out of bounds   */
#define AVB_EVENT_HARDWARE_INIT_FAILED      17305  /**< BAR0 mapping or HW init failed       */
/** 17306 reserved */
#define AVB_EVENT_VERSION_MISMATCH          17307  /**< HAL ops table version mismatch       */
/** 17308-17309 reserved */
#define AVB_EVENT_OPERATION_NOT_IMPLEMENTED 17310  /**< HAL operation not implemented        */

/* -------------------------------------------------------------------------
 * IOCTL / Filter Events  17400 - 17499
 * (reserved — populate as driver emits these)
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * PTP / Timestamp Events  17500 - 17599
 * (reserved — populate as driver emits these)
 * ------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * TSN / Scheduler Events  17600 - 17699
 * (reserved — populate as driver emits these)
 * ------------------------------------------------------------------------- */

#endif /* AVB_EVENTS_H */
