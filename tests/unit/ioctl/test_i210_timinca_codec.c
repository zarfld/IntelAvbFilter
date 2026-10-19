/*
 * test_i210_timinca_codec.c
 * Offline golden-vector tests for the I210 TIMINCA codec (devices/intel_i210_timinca.h).
 *
 * Reference: Intel I210 Datasheet 333016 v3.7, sections 7.8.3.2 and 8.15.11:
 *     INC_TIME = 8 ns +/- TIMINCA.Incvalue * 2^-32 ns      (every 8 ns / 125 MHz cycle)
 *     Incvalue = bits 30:0, ISGN = bit 31 (1b = subtract), reset value 0 (= exactly 8 ns).
 * The expected values below were derived INDEPENDENTLY of the C implementation from that
 * formula with exact integer arithmetic (INC_TIME in 2^-32 ns = 8*2^32 +/- Incvalue).
 *
 * Also characterizes the DEFECT: the driver used to program 0x08000000 as the "8 ns nominal"
 * (an (ns<<24)|frac24 model).  By the datasheet that is 8 ns + 2^27 * 2^-32 ns = 8.03125 ns per
 * cycle, i.e. +3906.25 ppm - exactly the offset measured on the HIL I210.
 *
 * Authorized public API range (requirement #3, +/-999,999 ppb) is a SEPARATE, tighter check
 * from physical representability (+/-0.5 ns of 8 ns, about +/-6.25 percent).
 *
 * Verifies: #3 (REQ-F-PTP-002), #149, #21 / #124 (register SSOT), #319 / #296
 * No driver, no hardware.
 *
 * Build: cl /nologo /W4 /Zi -I include -I external/intel_avb/lib tests\unit\ioctl\test_i210_timinca_codec.c
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../../../devices/intel_i210_timinca.h"

static int g_run = 0, g_fail = 0;
#define CHECK(label, cond) do { g_run++; if (cond) printf("  [PASS] %s\n", (label)); \
    else { g_fail++; printf("  [FAIL] %s\n", (label)); } } while (0)

typedef struct {
    const char *name;
    avb_u32 ns, frac;
    int     representable;
    avb_u32 raw;            /* expected when representable */
} VEC;

/* Expected raw values: independent derivation, see header comment. */
static const VEC vectors[] = {
    { "nominal 8 ns (reset value)",          8, 0u,            1, 0x00000000u },
    { "+1 unit (minimum positive)",          8, 1u,            1, 0x00000001u },
    { "-1 unit (minimum negative)",          7, 0xFFFFFFFFu,   1, 0x80000001u },
    { "maximum positive (2^31-1 units)",     8, 0x7FFFFFFFu,   1, 0x7FFFFFFFu },
    { "over positive (2^31 units)",          8, 0x80000000u,   0, 0u },
    { "maximum negative (-(2^31-1) units)",  7, 0x80000001u,   1, 0xFFFFFFFFu },
    { "over negative (-2^31 units)",         7, 0x80000000u,   0, 0u },
    { "+1 ppb",                              8, 34u,           1, 0x00000022u },
    { "+100,000 ppb",                        8, 3435974u,      1, 0x00346DC6u },
    { "-100,000 ppb",                        7, 4291531322u,   1, 0x80346DC6u },
    { "+999,999 ppb",                        8, 34359704u,     1, 0x020C4998u },
    { "-999,999 ppb",                        7, 4260607592u,   1, 0x820C4998u },
    { "ns = 0 (clock frozen)",               0, 0u,            0, 0u },
    { "ns = 6",                              6, 0u,            0, 0u },
    { "ns = 9",                              9, 0u,            0, 0u },
    { "ns = 15 (old model max)",            15, 0u,            0, 0u },
    { "ns = 16",                            16, 0u,            0, 0u },
};

int main(void)
{
    size_t i;
    printf("=== I210 TIMINCA codec - datasheet golden vectors (offline) ===\n");
    printf("Verifies: #3, #149, #21, #124, #319, #296\n");

    printf("\n[C-001] SSOT field definitions (intel-ethernet-regs/gen/i210_regs.h)\n");
    CHECK("INCVALUE is bits 30:0", I210_TIMINCA_INCVALUE_SHIFT == 0 && I210_TIMINCA_INCVALUE_MASK == 0x7FFFFFFFULL);
    CHECK("ISGN is bit 31",        I210_TIMINCA_ISGN_SHIFT == 31 && I210_TIMINCA_ISGN_MASK == 0x80000000ULL);
    CHECK("TIMINCA offset 0xB608", I210_TIMINCA == 0x0B608);

    printf("\n[C-002] encode: golden vectors\n");
    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        avb_u32 raw = 0xDEADBEEFu;
        int rc = i210_timinca_encode(vectors[i].ns, vectors[i].frac, &raw);
        char label[96];
        if (vectors[i].representable) {
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%s -> 0x%08X", vectors[i].name, vectors[i].raw);
            CHECK(label, rc == I210_TIMINCA_OK && raw == vectors[i].raw);
        } else {
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%s -> rejected, output untouched", vectors[i].name);
            CHECK(label, rc == I210_TIMINCA_E_RANGE && raw == 0xDEADBEEFu);
        }
    }

    printf("\n[C-003] decode and round trip\n");
    {
        avb_u32 ns = 99, frac = 99;
        i210_timinca_decode(0x00000000u, &ns, &frac);
        CHECK("decode(0) = 8 ns exactly", ns == 8u && frac == 0u);
        i210_timinca_decode(0x80000001u, &ns, &frac);
        CHECK("decode(0x80000001) = 7 + 0xFFFFFFFF/2^32", ns == 7u && frac == 0xFFFFFFFFu);
        i210_timinca_decode(0x7FFFFFFFu, &ns, &frac);
        CHECK("decode(0x7FFFFFFF) = 8 + 0x7FFFFFFF/2^32", ns == 8u && frac == 0x7FFFFFFFu);
        i210_timinca_decode(0xFFFFFFFFu, &ns, &frac);
        CHECK("decode(0xFFFFFFFF) = 7 + 0x80000001/2^32", ns == 7u && frac == 0x80000001u);
        i210_timinca_decode(0x80000000u, &ns, &frac);
        CHECK("decode(0x80000000) (-0) normalises to nominal", ns == 8u && frac == 0u);

        /* Defect characterization: the value the driver used to call "8 ns nominal". */
        i210_timinca_decode(0x08000000u, &ns, &frac);
        CHECK("DEFECT: old nominal 0x08000000 decodes to 8 ns + 2^27/2^32 ns (8.03125 ns, +3906.25 ppm)",
              ns == 8u && frac == 0x08000000u);
        CHECK("DEFECT: 0x08000000 is NOT the nominal encoding", i210_timinca_nominal() != 0x08000000u);
    }
    {
        /* every representable vector decodes back to the same (ns, frac) */
        int ok = 1;
        for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
            avb_u32 raw, ns, frac;
            if (!vectors[i].representable) continue;
            if (i210_timinca_encode(vectors[i].ns, vectors[i].frac, &raw) != I210_TIMINCA_OK) { ok = 0; break; }
            i210_timinca_decode(raw, &ns, &frac);
            if (ns != vectors[i].ns || frac != vectors[i].frac) { ok = 0; break; }
        }
        CHECK("decode(encode(x)) == x for all representable vectors", ok);
    }

    printf("\n[C-004] nominal\n");
    CHECK("nominal raw is the reset value 0 (datasheet 8.15.11)", i210_timinca_nominal() == 0u);
    {
        avb_u32 raw = 1;
        CHECK("encode(8 ns, 0) == nominal", i210_timinca_encode(I210_TIMINCA_CLOCK_PERIOD_NS, 0u, &raw) == I210_TIMINCA_OK
              && raw == i210_timinca_nominal());
    }

    printf("\n[C-005] authorized public API range vs physical representability\n");
    CHECK("API limit constant is 999,999 ppb (requirement #3)", AVB_I210_FREQ_API_MAX_ABS_PPB == 999999u);
    CHECK("max units for 1 ppb = 34",           i210_timinca_max_units_for_ppb(1u) == 34u);
    CHECK("max units for 100,000 ppb = 3435974", i210_timinca_max_units_for_ppb(100000u) == 3435974u);
    CHECK("max units for 999,999 ppb = 34359704", i210_timinca_max_units_for_ppb(999999u) == 34359704u);
    {
        avb_u32 raw;
        i210_timinca_encode(8u, 34359704u, &raw);
        CHECK("+999,999 ppb is inside the API range", i210_timinca_within_api_range(raw, AVB_I210_FREQ_API_MAX_ABS_PPB));
        i210_timinca_encode(7u, 4260607592u, &raw);
        CHECK("-999,999 ppb is inside the API range", i210_timinca_within_api_range(raw, AVB_I210_FREQ_API_MAX_ABS_PPB));
        i210_timinca_encode(8u, 34359705u, &raw);
        CHECK("one unit beyond +999,999 ppb is outside the API range", !i210_timinca_within_api_range(raw, AVB_I210_FREQ_API_MAX_ABS_PPB));
        /* +1,000,000 ppb is physically representable (about 0.1 percent of +/-6.25 percent) but not authorized */
        i210_timinca_encode(8u, 34359738u, &raw);
        CHECK("+1,000,000 ppb: representable but NOT authorized", i210_timinca_within_api_range(raw, AVB_I210_FREQ_API_MAX_ABS_PPB) == 0);
        i210_timinca_encode(8u, 0x7FFFFFFFu, &raw);
        CHECK("physical maximum (+6.25 percent) is outside the API range", !i210_timinca_within_api_range(raw, AVB_I210_FREQ_API_MAX_ABS_PPB));
        CHECK("nominal is inside the API range", i210_timinca_within_api_range(0u, AVB_I210_FREQ_API_MAX_ABS_PPB));
    }

    printf("\n PASS: %d / %d\n FAIL: %d / %d\n", g_run - g_fail, g_run, g_fail, g_run);
    if (g_fail) { printf("[RESULT] FAILURE\n"); return 1; }
    printf("[RESULT] ALL I210 TIMINCA CODEC TESTS PASSED\n");
    return 0;
}
