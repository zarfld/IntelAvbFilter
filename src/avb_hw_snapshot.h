/* avb_hw_snapshot.h - IOCTL_AVB_QUERY_HW_SNAPSHOT builder (HIL restoration verification, Q 2026-10-22).
 *
 * Family-independent: the MMIO offset of every AVB_HWREG_* id comes from the family's hil_register_offset op
 * (intel-ethernet-regs/gen through the device HAL); this file contains no register offsets.  Read-only: it only calls
 * the supplied MMIO reader (the kernel passes ndis_platform_ops.mmio_read; host unit tests pass a fake register file).
 * Every id 1..AVB_HWREG_COUNT gets one entry, in id order, with an explicit state - nothing is skipped or guessed.
 * Writes version, count and entries only: net_luid, vendor_id, device_id and status belong to the caller.
 * Verified offline by tests/unit/ioctl/test_i210_device_ops.c I-010..I-018 (version 2: AvbFillHwSnapshotV2 below, #503).
 */
#ifndef AVB_HW_SNAPSHOT_H
#define AVB_HW_SNAPSHOT_H

#include "include/avb_ioctl.h"                 /* AVB_HW_SNAPSHOT_REQUEST, AVB_HWREG_* (#pragma once) */
#include "devices/intel_device_interface.h"    /* intel_device_ops_t */

static void AvbFillHwSnapshot(const intel_device_ops_t *ops, device_t *dev, uint32_t capabilities,
                              int (*mmio_read)(device_t *dev, uint32_t offset, uint32_t *value),
                              PAVB_HW_SNAPSHOT_REQUEST out)
{
    uint32_t id;
    AVB_HW_SNAPSHOT_REQUEST snapshot;

    if (out == NULL) {
        return;
    }

    RtlZeroMemory(&snapshot, sizeof(snapshot));
    snapshot.version = AVB_HW_SNAPSHOT_VERSION;
    snapshot.count = AVB_HWREG_COUNT;

#ifdef INTEL_WIN32_KERNEL_MODE
    __try {
        for (id = 1u; id <= AVB_HWREG_COUNT; id++) {
            AVB_HWREG_ENTRY *e = &snapshot.entries[id - 1u];
            uint32_t off = 0u, val = 0u;
            e->id = id; e->offset = 0u; e->value = 0u;
            if (ops == NULL || ops->hil_register_offset == NULL) { e->state = AVB_HWREG_STATE_NO_HAL; continue; }
            if (!(capabilities & INTEL_CAP_MMIO))               { e->state = AVB_HWREG_STATE_NO_MMIO; continue; }
            if (ops->hil_register_offset(id, &off) != 0)         { e->state = AVB_HWREG_STATE_NOT_IN_SSOT; continue; }
            e->offset = off;
            if (mmio_read == NULL || dev == NULL || mmio_read(dev, off, &val) != 0) { e->state = AVB_HWREG_STATE_READ_FAILED; continue; }
            e->value = val;
            e->state = AVB_HWREG_STATE_VALID;
        }
        out->version = snapshot.version;
        out->count = snapshot.count;
        RtlCopyMemory(out->entries, snapshot.entries, sizeof(snapshot.entries));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        __try {
            RtlZeroMemory(out, sizeof(*out));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            /* fail closed: do not dereference a poisoned or user-mode pointer again */
        }
    }
#else
    for (id = 1u; id <= AVB_HWREG_COUNT; id++) {
        AVB_HWREG_ENTRY *e = &snapshot.entries[id - 1u];
        uint32_t off = 0u, val = 0u;
        e->id = id; e->offset = 0u; e->value = 0u;
        if (ops == NULL || ops->hil_register_offset == NULL) { e->state = AVB_HWREG_STATE_NO_HAL; continue; }
        if (!(capabilities & INTEL_CAP_MMIO))               { e->state = AVB_HWREG_STATE_NO_MMIO; continue; }
        if (ops->hil_register_offset(id, &off) != 0)         { e->state = AVB_HWREG_STATE_NOT_IN_SSOT; continue; }
        e->offset = off;
        if (mmio_read == NULL || dev == NULL || mmio_read(dev, off, &val) != 0) { e->state = AVB_HWREG_STATE_READ_FAILED; continue; }
        e->value = val;
        e->state = AVB_HWREG_STATE_VALID;
    }
    out->version = snapshot.version;
    out->count = snapshot.count;
    RtlCopyMemory(out->entries, snapshot.entries, sizeof(snapshot.entries));
#endif
}

/* Version 2 (#503, opt-in): the same rules for ids 1..AVB_HWREG_COUNT_V2 (adds SYSTIML/SYSTIMH, 17/18) into the
 * 312-byte version-2 request.  Separate from the version-1 builder above, which stays unchanged for existing callers.
 * Writes version, count and entries only: net_luid, vendor_id, device_id and status belong to the caller.  Consecutive
 * register reads are not one coherent PHC sample. */
static void AvbFillHwSnapshotV2(const intel_device_ops_t *ops, device_t *dev, uint32_t capabilities,
                                int (*mmio_read)(device_t *dev, uint32_t offset, uint32_t *value),
                                PAVB_HW_SNAPSHOT_REQUEST_V2 out)
{
    uint32_t id;
    AVB_HW_SNAPSHOT_REQUEST_V2 snapshot;

    if (out == NULL) {
        return;
    }

    RtlZeroMemory(&snapshot, sizeof(snapshot));
    snapshot.version = AVB_HW_SNAPSHOT_VERSION_2;
    snapshot.count = AVB_HWREG_COUNT_V2;

#ifdef INTEL_WIN32_KERNEL_MODE
    __try {
        for (id = 1u; id <= AVB_HWREG_COUNT_V2; id++) {
            AVB_HWREG_ENTRY *e = &snapshot.entries[id - 1u];
            uint32_t off = 0u, val = 0u;
            e->id = id; e->offset = 0u; e->value = 0u;
            if (ops == NULL || ops->hil_register_offset == NULL) { e->state = AVB_HWREG_STATE_NO_HAL; continue; }
            if (!(capabilities & INTEL_CAP_MMIO))               { e->state = AVB_HWREG_STATE_NO_MMIO; continue; }
            if (ops->hil_register_offset(id, &off) != 0)         { e->state = AVB_HWREG_STATE_NOT_IN_SSOT; continue; }
            e->offset = off;
            if (mmio_read == NULL || dev == NULL || mmio_read(dev, off, &val) != 0) { e->state = AVB_HWREG_STATE_READ_FAILED; continue; }
            e->value = val;
            e->state = AVB_HWREG_STATE_VALID;
        }
        out->version = snapshot.version;
        out->count = snapshot.count;
        RtlCopyMemory(out->entries, snapshot.entries, sizeof(snapshot.entries));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        __try {
            RtlZeroMemory(out, sizeof(*out));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            /* fail closed: do not dereference a poisoned or user-mode pointer again */
        }
    }
#else
    for (id = 1u; id <= AVB_HWREG_COUNT_V2; id++) {
        AVB_HWREG_ENTRY *e = &snapshot.entries[id - 1u];
        uint32_t off = 0u, val = 0u;
        e->id = id; e->offset = 0u; e->value = 0u;
        if (ops == NULL || ops->hil_register_offset == NULL) { e->state = AVB_HWREG_STATE_NO_HAL; continue; }
        if (!(capabilities & INTEL_CAP_MMIO))               { e->state = AVB_HWREG_STATE_NO_MMIO; continue; }
        if (ops->hil_register_offset(id, &off) != 0)         { e->state = AVB_HWREG_STATE_NOT_IN_SSOT; continue; }
        e->offset = off;
        if (mmio_read == NULL || dev == NULL || mmio_read(dev, off, &val) != 0) { e->state = AVB_HWREG_STATE_READ_FAILED; continue; }
        e->value = val;
        e->state = AVB_HWREG_STATE_VALID;
    }
    out->version = snapshot.version;
    out->count = snapshot.count;
    RtlCopyMemory(out->entries, snapshot.entries, sizeof(snapshot.entries));
#endif
}

#endif /* AVB_HW_SNAPSHOT_H */
