/*
 * intel_i210_timinca.h - I210 TIMINCA (increment attributes) codec. Pure arithmetic, no I/O.
 *
 * Reference: Intel I210 Datasheet 333016 v3.7, sections 7.8.3.2 and 8.15.11 (TIMINCA, 0xB608):
 *
 *     SYSTIM advances every 8 ns clock cycle (125 MHz) by
 *         INC_TIME = 8 ns +/- Incvalue * 2^-32 ns
 *     Incvalue = bits 30:0     (SSOT: I210_TIMINCA_INCVALUE_*)
 *     ISGN     = bit 31        (SSOT: I210_TIMINCA_ISGN_*; 1b = subtract)
 *     reset value 0           => exactly 8 ns per cycle: this IS the nominal encoding.
 *
 * Register layout comes from the register SSOT (intel-ethernet-regs, requirement #21 / ADR #124);
 * this header only adds the device arithmetic on top of it and defines no register bits itself.
 *
 * The public IOCTL ABI (increment_ns + increment_frac, 2^-32 ns units) is unchanged. Its logical
 * increment L = increment_ns + increment_frac / 2^32 ns is physically representable only when
 * |L - 8 ns| <= (2^31 - 1) * 2^-32 ns (about +/-0.5 ns, +/-6.25 percent), i.e. increment_ns is 7 or 8.
 * Physical representability and the authorized API range are separate checks:
 *     i210_timinca_encode()            -> physical (datasheet)
 *     i210_timinca_within_api_range()  -> authorized public range (requirement #3)
 *
 * Replaces the earlier (ns << 24) | frac24 model, whose "8 ns nominal" 0x08000000 is by the datasheet
 * 8.03125 ns per cycle (+3906.25 ppm).
 *
 * Verifies: #3 (REQ-F-PTP-002), #149; offline tests: tests/unit/ioctl/test_i210_timinca_codec.c
 */
#pragma once

#include "../include/avb_ioctl.h"                  /* avb_u32 / avb_u64 (same typedefs in kernel and user mode) */
#include "../intel-ethernet-regs/gen/i210_regs.h"   /* I210_TIMINCA, I210_TIMINCA_INCVALUE_*, I210_TIMINCA_ISGN_* (SSOT) */

#define I210_TIMINCA_CLOCK_PERIOD_NS   8u   /* datasheet 7.8.3.2: one 125 MHz clock cycle */
#define I210_TIMINCA_FRAC_BITS         32u  /* Incvalue resolution: 2^-32 ns */

#define I210_TIMINCA_OK                0
#define I210_TIMINCA_E_RANGE           (-1) /* logical increment not representable in TIMINCA */

/* Authorized public API range for ADJUST_FREQUENCY on I210: requirement #3 (REQ-F-PTP-002).
 * #149 states a narrower +/-100,000 ppb (called a "hardware limit", which the datasheet window of
 * +/-6.25 percent contradicts); reconciliation is a pending decision, see the I210 TIMINCA bug issue. */
#define AVB_I210_FREQ_API_MAX_ABS_PPB  999999u

/* Raw TIMINCA value that gives exactly 8 ns per cycle (the reset value). */
static __inline avb_u32 i210_timinca_nominal(void)
{
    return 0u;
}

/*
 * Encode the logical increment (increment_ns + increment_frac / 2^32 ns) into a TIMINCA value.
 * Returns I210_TIMINCA_OK, or I210_TIMINCA_E_RANGE (and leaves *raw untouched) when the increment
 * cannot be represented.
 */
static __inline int i210_timinca_encode(avb_u32 increment_ns, avb_u32 increment_frac, avb_u32 *raw)
{
    const avb_u64 nominal = (avb_u64)I210_TIMINCA_CLOCK_PERIOD_NS << I210_TIMINCA_FRAC_BITS;
    const avb_u64 total   = ((avb_u64)increment_ns << I210_TIMINCA_FRAC_BITS) | (avb_u64)increment_frac;
    avb_u64 magnitude;
    avb_u32 sign = 0u;

    if (total >= nominal) {
        magnitude = total - nominal;
    } else {
        magnitude = nominal - total;
        sign = (avb_u32)I210_TIMINCA_ISGN_MASK;
    }
    if (magnitude > (avb_u64)I210_TIMINCA_INCVALUE_MASK) {
        return I210_TIMINCA_E_RANGE;
    }
    *raw = sign | (avb_u32)((magnitude << I210_TIMINCA_INCVALUE_SHIFT) & I210_TIMINCA_INCVALUE_MASK);
    return I210_TIMINCA_OK;
}

/* Decode a TIMINCA value into the logical increment (increment_ns, increment_frac in 2^-32 ns). */
static __inline void i210_timinca_decode(avb_u32 raw, avb_u32 *increment_ns, avb_u32 *increment_frac)
{
    const avb_u64 nominal   = (avb_u64)I210_TIMINCA_CLOCK_PERIOD_NS << I210_TIMINCA_FRAC_BITS;
    const avb_u64 magnitude = ((avb_u64)raw & I210_TIMINCA_INCVALUE_MASK) >> I210_TIMINCA_INCVALUE_SHIFT;
    const avb_u64 total     = (raw & (avb_u32)I210_TIMINCA_ISGN_MASK) ? (nominal - magnitude)
                                                                       : (nominal + magnitude);
    *increment_ns   = (avb_u32)(total >> I210_TIMINCA_FRAC_BITS);
    *increment_frac = (avb_u32)(total & 0xFFFFFFFFULL);
}

/* Largest |Incvalue| (2^-32 ns units) a +/-ppb adjustment may use: round(ppb * 8 ns * 2^32 / 1e9). */
static __inline avb_u32 i210_timinca_max_units_for_ppb(avb_u32 ppb)
{
    const avb_u64 period_units = (avb_u64)I210_TIMINCA_CLOCK_PERIOD_NS << I210_TIMINCA_FRAC_BITS;
    return (avb_u32)(((avb_u64)ppb * period_units + 500000000ULL) / 1000000000ULL);
}

/* Non-zero when |Incvalue| of `raw` is within the authorized +/- max_ppb public range. */
static __inline int i210_timinca_within_api_range(avb_u32 raw, avb_u32 max_ppb)
{
    const avb_u32 magnitude = (avb_u32)(((avb_u64)raw & I210_TIMINCA_INCVALUE_MASK) >> I210_TIMINCA_INCVALUE_SHIFT);
    return magnitude <= i210_timinca_max_units_for_ppb(max_ppb);
}
