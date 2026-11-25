/*
 * i226_phc_seed.h - the PHC seed of the I226 initialization and whether it reset the clock (#357, #352 R1 L10).
 *
 * Pure: the register write is a callback, shared by devices/intel_i226_impl.c and the offline case TC-MOCK-069 in
 * tests/unit/test_tas_restore_contract.c. Passing that case says nothing about hardware.
 */
#pragma once

#include "../include/avb_ioctl.h"
#include "../intel-ethernet-regs/gen/i226_regs.h"

typedef int (*I226_PHC_WRITE)(void *ctx, avb_u32 offset, avb_u32 value);

/* SYSTIM := 1 ns (writing 0 might not start the clock). Returns 0 on success, -1 when a write failed. *invalidated (may be NULL) becomes 1
 * as soon as one SYSTIM write was performed: the clock is not what it was, also when a later step fails. It is never cleared here */
static __inline int i226_phc_seed(I226_PHC_WRITE write, void *ctx, int *invalidated)
{
    if (write(ctx, I226_SYSTIML, 1) != 0) {
        return -1;
    }
    if (invalidated != NULL) {
        *invalidated = 1;
    }
    return write(ctx, I226_SYSTIMH, 0) != 0 ? -1 : 0;
}
