/*
 * i226_phc_seed.h - the SYSTIM writes of the I226, the seed of its initialization (#357) and the set of set_systime (#350), and what they
 * report about the clock (#352 R1 L10).
 *
 * Pure: the register access is a callback, shared by devices/intel_i226_impl.c and the offline cases TC-MOCK-069 and -074 in
 * tests/unit/test_tas_restore_contract.c. Passing those cases says nothing about hardware.
 */
#pragma once

#include "../include/avb_ioctl.h"
#include "../src/avb_tas_retained.h"   /* AVB_PHC_EFFECT_* */
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

/*
 * The SYSTIM set sequence of set_systime (unchanged order and values) with the report of intel_device_ops_t.set_systime_reported (#350): what
 * was issued, apart from the result. effect (may be NULL): AVB_PHC_EFFECT_*, only raised.
 * - A SYSTIM write that went through is ISSUED, also when a later write or the restore fails.
 * - A TSAUXC write alone, the first one or a restore attempt, is UNKNOWN: what it did to the clock is not claimed.
 * - NONE only when no write went through at all.
 * The result is as before: the failure of a SYSTIM write; the TSAUXC writes have never decided it.
 */
static __inline void i226_phc_effect(int *effect, int value)
{
    if (effect != NULL && *effect < value) {
        *effect = value;
    }
}

static __inline int i226_phc_set_control(int (*write)(void *ctx, avb_u32 offset, avb_u32 value), void *ctx, avb_u32 value, int *effect)
{
    int result = write(ctx, I226_TSAUXC, value);

    if (result == 0) {
        i226_phc_effect(effect, AVB_PHC_EFFECT_UNKNOWN);
    }
    return result;
}

static __inline int i226_phc_set(int (*read)(void *ctx, avb_u32 offset, avb_u32 *value), int (*write)(void *ctx, avb_u32 offset, avb_u32 value),
                                 void *ctx, avb_u32 sec, avb_u32 nsec, int *effect)
{
    avb_u32 tsauxc = 0;
    int result;

    if (effect != NULL) {
        *effect = AVB_PHC_EFFECT_NONE;
    }
    /* Step 1: TSAUXC (0xB640) bit 31 around the two SYSTIM writes. Note: the SSOT header labels this bit PLLLOCKED; this sequence has always
     * used it as "Disable systime". A failure is not fatal, as before */
    (void)read(ctx, I226_TSAUXC, &tsauxc);
    (void)i226_phc_set_control(write, ctx, (avb_u32)(tsauxc | (1UL << 31)), effect);
    /* Step 2: nanoseconds */
    result = write(ctx, I226_SYSTIML, nsec);
    if (result != 0) {
        (void)i226_phc_set_control(write, ctx, tsauxc, effect);   /* restore */
        return result;
    }
    i226_phc_effect(effect, AVB_PHC_EFFECT_ISSUED);
    /* Step 3: seconds */
    result = write(ctx, I226_SYSTIMH, sec);
    if (result != 0) {
        (void)i226_phc_set_control(write, ctx, tsauxc, effect);   /* restore */
        return result;
    }
    /* Step 4 */
    (void)i226_phc_set_control(write, ctx, (avb_u32)(tsauxc & ~(1UL << 31)), effect);
    return 0;
}
