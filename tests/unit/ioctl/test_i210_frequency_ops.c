/*
 * test_i210_frequency_ops.c
 * Offline behavior tests for the I210 frequency operations of the PRODUCTION device implementation
 * (devices/intel_i210_impl.c, compiled unchanged with the test-only host shim in tests/unit/host_shim).
 * Drives i210_ops.validate_frequency_request / adjust_frequency against a fake register file and checks
 * the raw TIMINCA values the device writes.
 *
 * Reference: Intel I210 Datasheet 333016 v3.7, sections 7.8.3.2 and 8.15.11:
 *     INC_TIME = 8 ns +/- TIMINCA.Incvalue * 2^-32 ns   (every 8 ns / 125 MHz cycle)
 *     Incvalue = bits 30:0, ISGN = bit 31 (1b = subtract), reset value 0 (= exactly 8 ns).
 * The expected raw values were derived INDEPENDENTLY of the C implementation from that formula with exact
 * integer arithmetic (INC_TIME in 2^-32 ns = 8*2^32 +/- Incvalue).
 *
 * Range policy: physical representability (datasheet window about +/-6.25 percent) is separate from the
 * PROVISIONAL public API range of +/-100,000 ppb (#149, which lists #3 as superseded; the conflict with
 * #3's +/-999,999 ppb is recorded in #326).  The latter is a policy, not a hardware limit.
 *
 * Also pins: public ABI semantics of current_increment (raw 32-bit TIMINCA read before the change),
 * validation performs no MMIO, rejection writes nothing, fault propagation.
 *
 * Verifies: #3 (REQ-F-PTP-002), #149, #326, #40 / #126 (device abstraction), #21 / #124 (register SSOT).
 * No driver, no hardware.
 *
 * Build (see tools/build/Build-Tests.ps1): cl /nologo /W4 -I tests/unit/host_shim -I . -I include
 *     -I external/intel_avb/lib -I intel-ethernet-regs/gen tests\unit\ioctl\test_i210_frequency_ops.c devices\intel_i210_impl.c
 */
#include "precomp.h"
#include <stdio.h>
#include "external/intel_avb/lib/intel_windows.h"
#include "devices/intel_device_interface.h"

extern const intel_device_ops_t i210_ops;

static int g_run = 0, g_fail = 0;
#define CHECK(label, cond) do { g_run++; if (cond) printf("  [PASS] %s\n", (label)); \
    else { g_fail++; printf("  [FAIL] %s\n", (label)); } } while (0)

/* ---- fake register file behind the platform ops ---------------------------------------------------- */
static struct {
    uint32_t timinca;
    int      reads, writes;
    int      fail_read, fail_write;
    uint32_t last_write_off;
} hw;

#ifdef INTEL_WIN32_KERNEL_MODE
typedef uint32_t pci_u32;
#else
typedef DWORD pci_u32;
#endif

static int f_init(device_t *d)                                   { (void)d; return 0; }
static void f_cleanup(device_t *d)                               { (void)d; }
static int f_pci_r(device_t *d, pci_u32 o, pci_u32 *v)           { (void)d; (void)o; (void)v; return -1; }
static int f_pci_w(device_t *d, pci_u32 o, pci_u32 v)            { (void)d; (void)o; (void)v; return -1; }
static int f_mmio_r(device_t *d, uint32_t off, uint32_t *v)
{
    (void)d; hw.reads++;
    if (hw.fail_read) return -1;
    *v = (off == I210_TIMINCA) ? hw.timinca : 0u;
    return 0;
}
static int f_mmio_w(device_t *d, uint32_t off, uint32_t val)
{
    (void)d; hw.writes++; hw.last_write_off = off;
    if (hw.fail_write) return -1;
    if (off == I210_TIMINCA) hw.timinca = val;
    return 0;
}
static int f_mdio_r(device_t *d, uint16_t p, uint16_t r, uint16_t *v) { (void)d; (void)p; (void)r; (void)v; return -1; }
static int f_mdio_w(device_t *d, uint16_t p, uint16_t r, uint16_t v)  { (void)d; (void)p; (void)r; (void)v; return -1; }
static int f_ts(device_t *d, uint64_t *t)                              { (void)d; (void)t; return -1; }

const struct platform_ops ndis_platform_ops = {
    f_init, f_cleanup, f_pci_r, f_pci_w, f_mmio_r, f_mmio_w, f_mdio_r, f_mdio_w, f_ts
};

static void hw_reset(uint32_t timinca)
{
    memset(&hw, 0, sizeof(hw));
    hw.timinca = timinca;
}

typedef enum { ACCEPT, API_REJECT, UNREPRESENTABLE } KIND;
typedef struct { const char *name; uint32_t ns, frac; KIND kind; uint32_t raw; } VEC;

/* Expected raw values: independent derivation, see header comment. */
static const VEC vectors[] = {
    { "nominal 8 ns (reset value)",               8, 0u,          ACCEPT,          0x00000000u },
    { "+1 unit (minimum positive)",               8, 1u,          ACCEPT,          0x00000001u },
    { "-1 unit (minimum negative)",               7, 0xFFFFFFFFu, ACCEPT,          0x80000001u },
    { "+1 ppb",                                   8, 34u,         ACCEPT,          0x00000022u },
    { "+100,000 ppb (provisional API limit)",     8, 3435974u,    ACCEPT,          0x00346DC6u },
    { "-100,000 ppb (provisional API limit)",     7, 4291531322u, ACCEPT,          0x80346DC6u },
    { "one unit beyond +100,000 ppb",             8, 3435975u,    API_REJECT,      0u },
    { "one unit beyond -100,000 ppb",             7, 4291531321u, API_REJECT,      0u },
    { "+100,001 ppb",                             8, 3436008u,    API_REJECT,      0u },
    { "+999,999 ppb (the #3 limit)",              8, 34359704u,   API_REJECT,      0u },
    { "-999,999 ppb (the #3 limit)",              7, 4260607592u, API_REJECT,      0u },
    { "maximum positive (2^31-1 units)",          8, 0x7FFFFFFFu, API_REJECT,      0u },
    { "maximum negative (-(2^31-1) units)",       7, 0x80000001u, API_REJECT,      0u },
    { "over positive (2^31 units)",               8, 0x80000000u, UNREPRESENTABLE, 0u },
    { "over negative (-2^31 units)",              7, 0x80000000u, UNREPRESENTABLE, 0u },
    { "ns = 0 (clock frozen)",                    0, 0u,          UNREPRESENTABLE, 0u },
    { "ns = 6",                                   6, 0u,          UNREPRESENTABLE, 0u },
    { "ns = 9",                                   9, 0u,          UNREPRESENTABLE, 0u },
    { "ns = 15 (old model maximum)",             15, 0u,          UNREPRESENTABLE, 0u },
    { "ns = 16",                                 16, 0u,          UNREPRESENTABLE, 0u },
};

int main(void)
{
    size_t i;
    device_t dev;
    memset(&dev, 0, sizeof(dev));

    printf("=== I210 frequency operations - production device implementation, fake register file ===\n");
    printf("Verifies: #3, #149, #326, #40, #126, #21, #124\n");

    printf("\n[F-000] register layout originates from the register SSOT (intel-ethernet-regs/gen/i210_regs.h)\n");
    CHECK("TIMINCA offset 0xB608",  I210_TIMINCA == 0x0B608);
    CHECK("INCVALUE is bits 30:0",  I210_TIMINCA_INCVALUE_SHIFT == 0 && I210_TIMINCA_INCVALUE_MASK == 0x7FFFFFFFULL);
    CHECK("ISGN is bit 31",         I210_TIMINCA_ISGN_SHIFT == 31 && I210_TIMINCA_ISGN_MASK == 0x80000000ULL);

    printf("\n[F-001] the device layer exposes both operations\n");
    CHECK("validate_frequency_request is implemented", i210_ops.validate_frequency_request != NULL);
    CHECK("adjust_frequency is implemented",           i210_ops.adjust_frequency != NULL);
    if (!i210_ops.validate_frequency_request || !i210_ops.adjust_frequency) {
        printf("\n PASS: %d / %d\n FAIL: %d / %d\n[RESULT] FAILURE (operations missing)\n", g_run - g_fail, g_run, g_fail, g_run);
        return 1;
    }

    printf("\n[F-002] adjust_frequency: golden vectors -> raw TIMINCA written (datasheet 8.15.11)\n");
    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        uint32_t prev = 0xCAFEBABEu;
        int rc;
        char label[128];
        hw_reset(0x0A5A5A5Au);   /* arbitrary previous value */
        rc = i210_ops.adjust_frequency(&dev, vectors[i].ns, vectors[i].frac, &prev);
        if (vectors[i].kind == ACCEPT) {
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%s -> writes 0x%08X to TIMINCA", vectors[i].name, vectors[i].raw);
            CHECK(label, rc == 0 && hw.writes == 1 && hw.last_write_off == I210_TIMINCA && hw.timinca == vectors[i].raw);
        } else {
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%s -> -EINVAL, no register touched, previous untouched", vectors[i].name);
            CHECK(label, rc == -EINVAL && hw.writes == 0 && hw.reads == 0 && hw.timinca == 0x0A5A5A5Au && prev == 0xCAFEBABEu);
        }
    }

    printf("\n[F-003] validate_frequency_request: same classification, ZERO register access\n");
    for (i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        int rc;
        char label[128];
        hw_reset(0x0A5A5A5Au);
        rc = i210_ops.validate_frequency_request(&dev, vectors[i].ns, vectors[i].frac);
        _snprintf_s(label, sizeof(label), _TRUNCATE, "%s -> %s, no MMIO", vectors[i].name,
                    vectors[i].kind == ACCEPT ? "accepted" : "-EINVAL");
        CHECK(label, (vectors[i].kind == ACCEPT ? rc == 0 : rc == -EINVAL) && hw.reads == 0 && hw.writes == 0);
    }
    hw_reset(0);
    CHECK("validate does not need a device handle (pure)", i210_ops.validate_frequency_request(NULL, 8u, 0u) == 0 && hw.reads == 0 && hw.writes == 0);

    printf("\n[F-004] current_increment ABI semantics: raw TIMINCA read BEFORE the change\n");
    {
        uint32_t prev = 0;
        hw_reset(0x00123456u);
        CHECK("previous raw value is returned unmodified", i210_ops.adjust_frequency(&dev, 8u, 0u, &prev) == 0 && prev == 0x00123456u && hw.timinca == 0u);
        hw_reset(0x08000000u);   /* what the old driver programmed as "8 ns" */
        CHECK("the legacy 0x08000000 is reported raw, not reinterpreted as logical ns",
              i210_ops.adjust_frequency(&dev, 8u, 34u, &prev) == 0 && prev == 0x08000000u && hw.timinca == 0x22u);
        hw_reset(0u);
        CHECK("nominal previous value 0 is a valid previous value (not 'uninitialized')",
              i210_ops.adjust_frequency(&dev, 8u, 34u, &prev) == 0 && prev == 0u);
        CHECK("read_timinca op still returns the raw register", i210_ops.read_timinca != NULL && i210_ops.read_timinca(&dev, &prev) == 0 && prev == 0x22u);
    }

    printf("\n[F-005] fault propagation and argument checks\n");
    {
        uint32_t prev = 0xCAFEBABEu;
        int rc;
        hw_reset(0x77u); hw.fail_read = 1;
        rc = i210_ops.adjust_frequency(&dev, 8u, 34u, &prev);
        CHECK("read fault -> -EIO, nothing written, previous untouched", rc == -EIO && hw.writes == 0 && prev == 0xCAFEBABEu);
        hw_reset(0x77u); hw.fail_write = 1;
        rc = i210_ops.adjust_frequency(&dev, 8u, 34u, &prev);
        CHECK("write fault -> -EIO, previous reported (the read succeeded), register unchanged", rc == -EIO && prev == 0x77u && hw.timinca == 0x77u);
        hw_reset(0x77u);
        CHECK("NULL previous pointer -> -EINVAL, no MMIO", i210_ops.adjust_frequency(&dev, 8u, 34u, NULL) == -EINVAL && hw.reads == 0 && hw.writes == 0);
        CHECK("NULL device -> -EINVAL, no MMIO", i210_ops.adjust_frequency(NULL, 8u, 34u, &prev) == -EINVAL && hw.reads == 0 && hw.writes == 0);
    }

    printf("\n[F-006] defect characterization (independent datasheet arithmetic, not the implementation)\n");
    {
        /* 0x08000000 = Incvalue 2^27 -> INC_TIME = 8 ns + 2^27 * 2^-32 ns = 8.03125 ns -> +3906.25 ppm. */
        const double ppm = (134217728.0 / 4294967296.0) / 8.0 * 1e6;
        CHECK("legacy 0x08000000 is +3906.25 ppm, i.e. NOT nominal", ppm > 3906.2 && ppm < 3906.3);
        hw_reset(0x08000000u);
        CHECK("a request equal to the legacy +3906 ppm value is rejected by the provisional +/-100,000 ppb API range",
              i210_ops.validate_frequency_request(&dev, 8u, 134217728u) == -EINVAL);
    }

    printf("\n PASS: %d / %d\n FAIL: %d / %d\n", g_run - g_fail, g_run, g_fail, g_run);
    if (g_fail) { printf("[RESULT] FAILURE\n"); return 1; }
    printf("[RESULT] ALL I210 FREQUENCY OPERATION TESTS PASSED\n");
    return 0;
}
