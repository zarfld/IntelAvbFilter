/*
 * test_i210_device_ops.c
 * Offline behavior tests for the I210 device operations of the PRODUCTION device implementation
 * (devices/intel_i210_impl.c, compiled unchanged with the test-only host shim in tests/unit/host_shim).
 * Drives i210_ops against a fake register file behind the platform ops.
 *
 *   F-xxx  frequency operations: validate_frequency_request / adjust_frequency (TIMINCA encoding)
 *   I-xxx  initialization: single TIMINCA owner, no repeated SYSTIM seeding, honest failure propagation
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
 * Initialization contract tested here (issue #343):
 *   - init() is the single owner of the I210 TIMINCA/TSAUXC/SYSTIM/timestamp-control programming; it does
 *     NOT route through the generic platform init (which would write a second, contradictory TIMINCA).
 *   - A device whose configuration is already in place is NOT touched again (no SYSTIM re-seed): repeated
 *     initialization performs no writes.  TIMINCA == 0 is the I210 NOMINAL value, never "uninitialized".
 *   - A fresh or power-cycled device (SYSTIM disabled, or both timestamp controls off) is fully initialized.
 *   - A configured device with a configuration defect is repaired minimally, without a re-seed.
 *   - Every mandatory step failure, and a timestamp control that does not latch, fails init (non-zero).
 *   - If the configuration cannot be read, init fails and writes nothing.
 *
 * NOT covered here (kernel-only, reported as a coverage gap): the generic lifecycle that calls init, PHC
 * health (separate iteration), and anything needing the WDK build or hardware.
 *
 * Verifies: #3 (REQ-F-PTP-002), #149, #326, #343, #40 / #126 (device abstraction), #21 / #124 (register SSOT).
 * No driver, no hardware.
 *
 * Build (see tools/build/Build-Tests.ps1): cl /nologo /W4 -I tests/unit/host_shim -I . -I include
 *     -I external/intel_avb/lib -I intel-ethernet-regs/gen tests\unit\ioctl\test_i210_device_ops.c devices\intel_i210_impl.c
 */
#include "precomp.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "external/intel_avb/lib/intel_windows.h"
#include "devices/intel_device_interface.h"
#include "src/avb_hw_snapshot.h"   /* QUERY_HW_SNAPSHOT builder (I-010..I-014) */

extern const intel_device_ops_t i210_ops;

/* ---- fake driver for the HIL-test helpers -----------------------------------------------------------
 * tests/common/avb_test_common.h talks to the driver through DeviceIoControl.  Redirect it to a fake that
 * routes IOCTL_AVB_ADJUST_FREQUENCY through the PRODUCTION I210 ops with the same error mapping the real
 * handler applies (device -EINVAL -> STATUS_INVALID_PARAMETER -> Win32 ERROR_INVALID_PARAMETER; ARCH-008
 * pins that mapping structurally), so the helper logic can be verified offline. */
static device_t g_fake_dev;
static int      g_fake_cfg_fail;         /* GET_CLOCK_CONFIG fails */
static int      g_fake_cfg_fail_after;   /* GET_CLOCK_CONFIG fails after N successful calls (-1 = never) */
static int      g_fake_cfg_calls;
static DWORD    g_fake_cfg_gle;          /* Win32 error returned when GET_CLOCK_CONFIG fails (0 = ERROR_GEN_FAILURE) */
static int      g_fake_adjust_fail;      /* adjust fails with a generic device error */
static int      g_fake_adjust_swallow;   /* adjust reports success but never writes the register (buggy driver) */
static DWORD    g_fake_reject_gle;       /* override the Win32 error of a rejection (0 = natural) */
static int      g_fake_corrupt_on_reject;/* a (buggy) driver that changes TIMINCA even though it rejects */
static int      g_fake_cfg_bad_status;   /* GET_CLOCK_CONFIG "succeeds" but reports a failing status field */
static BOOL WINAPI FakeDeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_sz, LPVOID out, DWORD out_sz,
                                       LPDWORD ret, LPOVERLAPPED ov);
/* The helpers print verdict tags for the HIL tests.  Here several negative cases are deliberate, so de-tag the
 * output ("[FAIL]" -> "<FAIL>") to keep real tags reserved for this test's own CHECK results. */
/* Captured helper output, so cases can assert what the helper REPORTED (not only what it returned). */
static char   g_helper_log[4096];
static size_t g_helper_len;
#define HELPER_LOG_RESET()   (g_helper_len = 0, g_helper_log[0] = '\0')
#define HELPER_LOG_HAS(s)    (strstr(g_helper_log, (s)) != NULL)
static void HostHelperLog(const char *fmt, ...)
{
    char buf[512];
    char *p;
    size_t n;
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    for (p = buf; *p; p++) { if (*p == '[') *p = '<'; else if (*p == ']') *p = '>'; }
    printf("      helper: %s", buf);
    n = strlen(buf);
    if (g_helper_len + n < sizeof(g_helper_log)) { memcpy(g_helper_log + g_helper_len, buf, n + 1); g_helper_len += n; }
}
#define AVB_TEST_LOG HostHelperLog
#define DeviceIoControl FakeDeviceIoControl
#include "tests/common/avb_test_common.h"   /* HIL-test I210 oracle + helpers, cross-validated below */
#undef DeviceIoControl


static int g_run = 0, g_fail = 0;
#define CHECK(label, cond) do { g_run++; if (cond) printf("  [PASS] %s\n", (label)); \
    else { g_fail++; printf("  [FAIL] %s\n", (label)); } } while (0)

/* Synthetic PHC/QPC window for the rate verdict: 10 MHz QPC sampled every 0.5 s, PHC = rate * elapsed plus
 * deterministic uniform noise in [-noise_ns, +noise_ns], fitted by the helper under test. */
static void RATE_WIN(AvbRateFit *f, double rate, int noise_ns, int n, unsigned seed)
{
    unsigned long long phc[256], qpc[256];
    const unsigned long long qf = 10000000ULL;
    unsigned s = seed * 2654435761u + 1u;
    int i;
    if (n > 256) n = 256;
    for (i = 0; i < n; i++) {
        double e = 0.0;
        s = s * 1664525u + 1013904223u;
        if (noise_ns) e = ((double)(s >> 8) / 16777216.0 * 2.0 - 1.0) * (double)noise_ns;
        qpc[i] = 123456789ULL + (unsigned long long)i * (qf / 2ULL);
        phc[i] = (unsigned long long)(1000000000000LL + llround(rate * ((double)i * 0.5e9) + e));
    }
    AvbRateFitWindow(phc, qpc, n, qf, f);
}

/* ---- fake register file behind the platform ops ---------------------------------------------------- */
typedef struct { uint32_t off, val; int failed; } WLOG;

static struct {
    uint32_t tsauxc, timinca, systiml, systimh, tx, rx;
    int      reads, writes;
    int      fail_read, fail_write;            /* any register */
    int      have_fail_read_off, have_fail_write_off;
    uint32_t fail_read_off, fail_write_off;    /* one register */
    int      tx_latches, rx_latches;           /* does writing the EN bit stick? */
    int      platform_init_calls;
    uint32_t last_write_off;
    WLOG     log[64];
    int      nlog;
} hw;

#ifdef INTEL_WIN32_KERNEL_MODE
typedef uint32_t pci_u32;
#else
typedef DWORD pci_u32;
#endif

static int f_init(device_t *d)                                   { (void)d; hw.platform_init_calls++; return 0; }
static void f_cleanup(device_t *d)                               { (void)d; }
static int f_pci_r(device_t *d, pci_u32 o, pci_u32 *v)           { (void)d; (void)o; (void)v; return -1; }
static int f_pci_w(device_t *d, pci_u32 o, pci_u32 v)            { (void)d; (void)o; (void)v; return -1; }
static int f_mmio_r(device_t *d, uint32_t off, uint32_t *v)
{
    (void)d; hw.reads++;
    if (hw.fail_read || (hw.have_fail_read_off && hw.fail_read_off == off)) return -1;
    switch (off) {
    case I210_TIMINCA:    *v = hw.timinca; break;
    case I210_TSAUXC:     *v = hw.tsauxc;  break;
    case I210_SYSTIML:    *v = hw.systiml; break;
    case I210_SYSTIMH:    *v = hw.systimh; break;
    case I210_TSYNCTXCTL: *v = hw.tx;      break;
    case I210_TSYNCRXCTL: *v = hw.rx;      break;
    default:              *v = 0u;         break;
    }
    return 0;
}
static int f_mmio_w(device_t *d, uint32_t off, uint32_t val)
{
    int fail;
    (void)d; hw.writes++; hw.last_write_off = off;
    fail = hw.fail_write || (hw.have_fail_write_off && hw.fail_write_off == off);
    if (hw.nlog < 64) { hw.log[hw.nlog].off = off; hw.log[hw.nlog].val = val; hw.log[hw.nlog].failed = fail; hw.nlog++; }
    if (fail) return -1;
    switch (off) {
    case I210_TIMINCA:    hw.timinca = val; break;
    case I210_TSAUXC:     hw.tsauxc  = val; break;
    case I210_SYSTIML:    hw.systiml = val; break;
    case I210_SYSTIMH:    hw.systimh = val; break;
    case I210_TSYNCTXCTL: if (hw.tx_latches) hw.tx = val; break;
    case I210_TSYNCRXCTL: if (hw.rx_latches) hw.rx = val; break;
    default: break;
    }
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
    hw.tx_latches = 1;
    hw.rx_latches = 1;
}

static BOOL WINAPI FakeDeviceIoControl(HANDLE h, DWORD code, LPVOID in, DWORD in_sz, LPVOID out, DWORD out_sz,
                                       LPDWORD ret, LPOVERLAPPED ov)
{
    (void)h; (void)in_sz; (void)out_sz; (void)ov;
    if (ret) *ret = 0;
    if (code == IOCTL_AVB_ADJUST_FREQUENCY) {
        AVB_FREQUENCY_REQUEST *rq = (AVB_FREQUENCY_REQUEST *)in;
        AVB_FREQUENCY_REQUEST *rs = (AVB_FREQUENCY_REQUEST *)out;
        uint32_t prev = 0;
        int rc = i210_ops.validate_frequency_request(&g_fake_dev, rq->increment_ns, rq->increment_frac);
        if (rc == -ENOTSUP) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
        if (rc != 0) {
            if (g_fake_corrupt_on_reject) hw.timinca ^= 1u;
            SetLastError(g_fake_reject_gle ? g_fake_reject_gle : ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        if (g_fake_adjust_fail) { SetLastError(ERROR_GEN_FAILURE); return FALSE; }
        if (g_fake_adjust_swallow) { rs->current_increment = hw.timinca; rs->status = 0; return TRUE; }
        rc = i210_ops.adjust_frequency(&g_fake_dev, rq->increment_ns, rq->increment_frac, &prev);
        if (rc != 0) { SetLastError(ERROR_GEN_FAILURE); return FALSE; }
        rs->current_increment = prev;
        rs->status = 0;
        return TRUE;
    }
    if (code == IOCTL_AVB_GET_CLOCK_CONFIG) {
        AVB_CLOCK_CONFIG *c = (AVB_CLOCK_CONFIG *)out;
        g_fake_cfg_calls++;
        if (g_fake_cfg_fail || (g_fake_cfg_fail_after >= 0 && g_fake_cfg_calls > g_fake_cfg_fail_after)) { SetLastError(g_fake_cfg_gle ? g_fake_cfg_gle : ERROR_GEN_FAILURE); return FALSE; }
        c->timinca = hw.timinca;
        c->status = g_fake_cfg_bad_status ? 0xC0000001u : 0u;
        return TRUE;
    }
    if (code == IOCTL_AVB_GET_HW_STATE) {
        AVB_HW_STATE_QUERY *q = (AVB_HW_STATE_QUERY *)out;
        q->device_id = INTEL_DEV_I210_AT;
        return TRUE;
    }
    SetLastError(ERROR_INVALID_FUNCTION);
    return FALSE;
}


/* ---- init scenarios -------------------------------------------------------------------------------- */
#define TX_EN  ((uint32_t)I210_TSYNCTXCTL_SET(0, I210_TSYNCTXCTL_EN_MASK, I210_TSYNCTXCTL_EN_SHIFT, 1))
#define RX_EN  ((uint32_t)I210_TSYNCRXCTL_SET(I210_TSYNCRXCTL_SET(0, I210_TSYNCRXCTL_EN_MASK, I210_TSYNCRXCTL_EN_SHIFT, 1), \
                                              I210_TSYNCRXCTL_TYPE_MASK, I210_TSYNCRXCTL_TYPE_SHIFT, I210_TSYNCRXCTL_TYPE_ALL))

static void hw_fresh(void)        /* power-on / power-cycled: SYSTIM disabled, controls off, nominal TIMINCA */
{
    hw_reset(0u);
    hw.tsauxc = INTEL_TSAUXC_DISABLE_SYSTIM;
}
static void hw_configured(void)   /* running, configured: what a successful init leaves behind */
{
    hw_reset(0u);
    hw.tsauxc = 0u;
    hw.tx = TX_EN;
    hw.rx = RX_EN;
    hw.systimh = 5u;
    hw.systiml = 123456u;
}
static int writes_to(uint32_t off)
{
    int i, n = 0;
    for (i = 0; i < hw.nlog; i++) if (hw.log[i].off == off) n++;
    return n;
}
static int first_write_index(uint32_t off)
{
    int i;
    for (i = 0; i < hw.nlog; i++) if (hw.log[i].off == off) return i;
    return -1;
}
static int known_register(uint32_t off)
{
    return off == I210_TSAUXC || off == I210_TIMINCA || off == I210_SYSTIML || off == I210_SYSTIMH ||
           off == I210_TSYNCTXCTL || off == I210_TSYNCRXCTL;
}
static int writes_outside_known_registers(void)
{
    int i, n = 0;
    for (i = 0; i < hw.nlog; i++) if (!known_register(hw.log[i].off)) n++;
    return n;
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

    printf("=== I210 device operations - production device implementation, fake register file ===\n");
    printf("Verifies: #3, #149, #326, #343, #40, #126, #21, #124\n");

    printf("\n[F-000] register layout originates from the register SSOT (intel-ethernet-regs/gen/i210_regs.h)\n");
    CHECK("TIMINCA offset 0xB608",  I210_TIMINCA == 0x0B608);
    CHECK("INCVALUE is bits 30:0",  I210_TIMINCA_INCVALUE_SHIFT == 0 && I210_TIMINCA_INCVALUE_MASK == 0x7FFFFFFFULL);
    CHECK("ISGN is bit 31",         I210_TIMINCA_ISGN_SHIFT == 31 && I210_TIMINCA_ISGN_MASK == 0x80000000ULL);

    printf("\n[F-001] the device layer exposes both frequency operations\n");
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

    /* ======================= HIL test oracle (tests/common/avb_test_common.h) ======================= */
    printf("\n[O-001] test-side I210 TIMINCA oracle agrees with the datasheet golden table\n");
    {
        /* ppb -> raw, derived by hand: units = round(|ppb| * 8 * 2^32 / 1e9) = round(|ppb| * 34.359738368) */
        static const struct { long long ppb; unsigned raw; } golden[] = {
            {       0LL, 0x00000000u }, {       1LL, 0x00000022u }, {      50LL, 0x000006B6u }, {     -50LL, 0x800006B6u },
            {     100LL, 0x00000D6Cu }, {    1000LL, 0x00008638u }, {  100000LL, 0x00346DC6u }, { -100000LL, 0x80346DC6u },
            {  100001LL, 0x00346DE8u }, {  999999LL, 0x020C4998u }, { -999999LL, 0x820C4998u },
        };
        size_t g;
        int all_raw = 1, all_req = 1, all_prod = 1;
        for (g = 0; g < sizeof(golden) / sizeof(golden[0]); g++) {
            unsigned ns = 0, frac = 0, dn = 0, df = 0, rt = 0;
            int in_policy = (golden[g].ppb >= -100000LL && golden[g].ppb <= 100000LL);
            if (AvbTestI210RawFromPpb(golden[g].ppb) != golden[g].raw) all_raw = 0;
            AvbTestI210RequestFromPpb(golden[g].ppb, &ns, &frac);
            AvbTestI210RawToRequest(golden[g].raw, &dn, &df);
            if (ns != dn || frac != df) all_req = 0;                       /* encode(request) and decode(raw) agree */
            hw_reset(0x0A5A5A5Au);
            {
                int vrc = i210_ops.validate_frequency_request(&dev, ns, frac);
                int arc = i210_ops.adjust_frequency(&dev, ns, frac, &rt);
                /* in policy: the PRODUCTION op writes exactly the oracle's raw; outside: production rejects it and the oracle agrees */
                if (in_policy) { if (vrc != 0 || arc != 0 || hw.timinca != golden[g].raw || !AvbTestI210RawInPolicy(golden[g].raw)) all_prod = 0; }
                else           { if (vrc != -EINVAL || arc != -EINVAL || hw.timinca != 0x0A5A5A5Au || AvbTestI210RawInPolicy(golden[g].raw)) all_prod = 0; }
            }
        }
        CHECK("oracle raw == hand-derived golden raw for every ppb (incl. +/-1, +/-50, +/-100000, +/-999999)", all_raw);
        CHECK("oracle request(ppb) == oracle decode(raw) (restore reproduces the same request)", all_req);
        CHECK("PRODUCTION ops write exactly the oracle raw inside +/-100,000 ppb and reject everything outside", all_prod);
    }
    {
        unsigned n = 0, f = 0;
        AvbTestI210RawToRequest(0x08000000u, &n, &f);
        CHECK("legacy 0x08000000 decodes to 8 ns + 2^27 units (the +3906.25 ppm defect)", n == 8u && f == 134217728u);
        CHECK("legacy 0x08000000 is +3906250 ppb and outside the policy", AvbTestI210RawToPpb(0x08000000u) == 3906250LL && !AvbTestI210RawInPolicy(0x08000000u));
        AvbTestI210RawToRequest(0x00000000u, &n, &f);
        CHECK("nominal raw 0 restores to exactly (8 ns, 0)", n == 8u && f == 0u && AvbTestI210RawInPolicy(0u));
        AvbTestI210RawToRequest(0x80000000u, &n, &f);
        CHECK("negative zero (0x80000000) restores to nominal", n == 8u && f == 0u);
    }
    CHECK("AvbTestI210RawEquivalent: negative zero == nominal; equal magnitudes with different sign are NOT equivalent; equal raws are",
          AvbTestI210RawEquivalent(0u, 0x80000000u) && AvbTestI210RawEquivalent(0x6B6u, 0x6B6u) && !AvbTestI210RawEquivalent(0x6B6u, 0x800006B6u) && !AvbTestI210RawEquivalent(0x6B6u, 0x6B7u));    CHECK("oracle DID set == the six DIDs the driver maps to the I210 family (and not I211)",
          AvbTestIsI210Did(INTEL_DEV_I210_AT) && AvbTestIsI210Did(INTEL_DEV_I210_AT2) && AvbTestIsI210Did(INTEL_DEV_I210_FIBER) &&
          AvbTestIsI210Did(INTEL_DEV_I210_IS) && AvbTestIsI210Did(INTEL_DEV_I210_IT) && AvbTestIsI210Did(INTEL_DEV_I210_CS) &&
          !AvbTestIsI210Did(INTEL_DEV_I211_AT) && !AvbTestIsI210Did(INTEL_DEV_I226_LM) && !AvbTestIsI210Did(INTEL_DEV_I219_LM));

    printf("\n[O-002] HIL-test helpers behave correctly against a fake driver backed by the PRODUCTION I210 ops\n");
    {
        HANDLE fh = (HANDLE)(ULONG_PTR)1;   /* opaque: the fake ignores it */
        unsigned before, st = 0, prev = 0, raw = 0;
        DWORD gle = 0;
        unsigned short did = 0;
        g_fake_cfg_fail_after = -1;

        hw_reset(0x6B6u);
        CHECK("AvbTestAdjustFrequency: accepted request reports the raw previous value", AvbTestAdjustFrequency(fh, 8u, 0u, &prev, &st, &gle) && prev == 0x6B6u && st == 0u && hw.timinca == 0u);
        CHECK("AvbTestReadTimincaRaw reads the raw register through GET_CLOCK_CONFIG", AvbTestReadTimincaRaw(fh, &raw) && raw == 0u);
        CHECK("AvbTestGetBoundDid + AvbTestIsI210Did identify the bound I210", AvbTestGetBoundDid(fh, &did) && AvbTestIsI210Did(did));

        /* rejection + unchanged register */
        hw_reset(0x22u); before = hw.timinca;
        CHECK("ExpectRejectedUnchanged: 6 ns is rejected (87), register unchanged -> PASS (1)", AvbTestI210ExpectRejectedUnchanged(fh, 6u, 0u, "6 ns") == 1 && hw.timinca == before);
        CHECK("ExpectRejectedUnchanged: one unit beyond +100,000 ppb is rejected -> PASS", AvbTestI210ExpectRejectedUnchanged(fh, 8u, 3435975u, "beyond +100000 ppb") == 1);
        {
            unsigned n999 = 0, f999 = 0;
            AvbTestI210RequestFromPpb(999999LL, &n999, &f999);
            CHECK("ExpectRejectedUnchanged: +999,999 ppb (the #3 limit) is rejected by the provisional policy -> PASS",
                  AvbTestI210ExpectRejectedUnchanged(fh, n999, f999, "+999999 ppb") == 1);
        }
        hw_reset(0x22u);
        CHECK("ExpectRejectedUnchanged: an ACCEPTED in-range request is a FAIL (0), not a pass", AvbTestI210ExpectRejectedUnchanged(fh, 8u, 34u, "accepted") == 0);
        hw_reset(0x22u); g_fake_corrupt_on_reject = 1;
        CHECK("ExpectRejectedUnchanged: a driver that changes the register while rejecting -> FAIL (0)", AvbTestI210ExpectRejectedUnchanged(fh, 6u, 0u, "corrupting driver") == 0);
        g_fake_corrupt_on_reject = 0;
        hw_reset(0x22u); g_fake_reject_gle = ERROR_NOT_SUPPORTED;
        CHECK("ExpectRejectedUnchanged: rejection with the wrong error (NOT_SUPPORTED instead of INVALID_PARAMETER) -> FAIL (0)", AvbTestI210ExpectRejectedUnchanged(fh, 6u, 0u, "wrong error") == 0);
        g_fake_reject_gle = 0;
        hw_reset(0x22u); g_fake_cfg_fail = 1;
        CHECK("ExpectRejectedUnchanged: GET_CLOCK_CONFIG broken (generic error) -> FAIL (0), NO request is sent (an implementation failure is never BLOCKED)",
              AvbTestI210ExpectRejectedUnchanged(fh, 8u, 34u, "broken config ioctl") == 0 && hw.timinca == 0x22u && hw.writes == 0);
        g_fake_cfg_gle = ERROR_NOT_READY;
        CHECK("ExpectRejectedUnchanged: device not ready (STATUS_DEVICE_NOT_READY) -> BLOCKED (-1), NO request is sent",
              AvbTestI210ExpectRejectedUnchanged(fh, 8u, 34u, "device not ready") == -1 && hw.timinca == 0x22u && hw.writes == 0);
        g_fake_cfg_gle = 0;
        g_fake_cfg_fail = 0;
        hw_reset(0x22u); g_fake_cfg_calls = 0; g_fake_cfg_fail_after = 1;
        CHECK("ExpectRejectedUnchanged: post-state unreadable -> INCONCLUSIVE (-2)", AvbTestI210ExpectRejectedUnchanged(fh, 6u, 0u, "no post-state") == -2);
        g_fake_cfg_fail_after = -1;

        /* verified safe restore */
        hw_reset(0x08000000u);   /* legacy defect left by the old driver: must restore to NOMINAL, not re-apply it */
        CHECK("RestoreVerified: legacy 0x08000000 (+3906 ppm) is restored to nominal 0", AvbTestI210RestoreVerified(fh, 0x08000000u, "legacy") == 1 && hw.timinca == 0u);
        {
            int r;
            hw_reset(0u);   /* clock currently at nominal; the test saved 0x6B6 earlier (a servo had set +50 ppb) */
            r = AvbTestI210RestoreVerified(fh, 0x6B6u, "servo");
            CHECK("RestoreVerified: an in-policy servo value 0x6B6 saved earlier is restored EXACTLY", r == 1 && hw.timinca == 0x6B6u);
            hw_reset(0u);
            r = AvbTestI210RestoreVerified(fh, 0x346DC6u, "edge");
            CHECK("RestoreVerified: the +100,000 ppb policy edge is restored exactly", r == 1 && hw.timinca == 0x346DC6u);
            hw_reset(0u);
            r = AvbTestI210RestoreVerified(fh, 0x80346DC6u, "edge-");
            CHECK("RestoreVerified: the -100,000 ppb policy edge is restored exactly", r == 1 && hw.timinca == 0x80346DC6u);
        }
        hw_reset(0x6B6u);
        CHECK("RestoreVerified: negative zero (0x80000000) and nominal restore to 0", AvbTestI210RestoreVerified(fh, 0x80000000u, "neg0") == 1 && hw.timinca == 0u);
        hw_reset(0x6B6u); g_fake_adjust_fail = 1;
        CHECK("RestoreVerified: restore that fails is reported as FAIL (0), never silent", AvbTestI210RestoreVerified(fh, 0u, "failing restore") == 0);
        g_fake_adjust_fail = 0;
        hw_reset(0x08000000u); g_fake_adjust_swallow = 1;
        CHECK("RestoreVerified: a driver that reports success but does not write the register is caught by the readback -> FAIL (0)", AvbTestI210RestoreVerified(fh, 0u, "swallowed restore") == 0);
        g_fake_adjust_swallow = 0;
    }

    /* FullSuite 2026-10-19 finding: the restore reported "now 0x00000000" although the readback never ran (the
     * driver refuses GET_CLOCK_CONFIG on a handle without OPEN_ADAPTER).  The helper must PASS only on an actual,
     * successful readback that matches exactly, and must never report an unread value. */
    printf("\n[O-003] RestoreVerified reports only what it actually read back\n");
    {
        HANDLE fh = (HANDLE)(ULONG_PTR)1;
        int r;
        g_fake_cfg_fail_after = -1;

        hw_reset(0x22u); g_fake_adjust_swallow = 1; g_fake_cfg_fail = 1; HELPER_LOG_RESET();
        r = AvbTestI210RestoreVerified(fh, 0u, "unread restore");
        CHECK("RestoreVerified: readback refused -> FAIL (0) and the report says UNREAD, never a register value",
              r == 0 && HELPER_LOG_HAS("UNREAD") && !HELPER_LOG_HAS("now 0x00000000"));
        g_fake_adjust_swallow = 0; g_fake_cfg_fail = 0;

        hw_reset(0x22u); g_fake_adjust_fail = 1; HELPER_LOG_RESET();
        r = AvbTestI210RestoreVerified(fh, 0u, "failed adjust");
        CHECK("RestoreVerified: restore IOCTL failed -> FAIL (0) and the report carries the register value ACTUALLY read (0x00000022)",
              r == 0 && HELPER_LOG_HAS("0x00000022"));
        g_fake_adjust_fail = 0;

        hw_reset(0x22u); g_fake_cfg_bad_status = 1; HELPER_LOG_RESET();
        r = AvbTestI210RestoreVerified(fh, 0u, "bad readback status");
        CHECK("RestoreVerified: GET_CLOCK_CONFIG returns TRUE with a failing status -> not a readback -> FAIL (0), reported UNREAD",
              r == 0 && HELPER_LOG_HAS("UNREAD"));
        g_fake_cfg_bad_status = 0;

        hw_reset(0x22u); HELPER_LOG_RESET();
        r = AvbTestI210RestoreVerified(fh, 0u, "good restore");
        CHECK("RestoreVerified: adjust OK + readback OK + exact match -> PASS (1), report carries the read value",
              r == 1 && hw.timinca == 0u && HELPER_LOG_HAS("restored TIMINCA 0x00000000 (verified by readback)"));
    }

    /* Exact-adapter targeting for MUTATING HIL tests (FullSuite 2026-10-19: "adapter 0" was the I210 in the Debug
     * pass and the I226 in the Release pass).  Selection is explicit, unique, and never defaults to index 0. */
    printf("\n[T-001] exact target selection for mutating HIL tests\n");
    {
        const unsigned short two[2]  = { 0x15BB, 0x1533 };          /* I219 enumerated first, I210 second */
        const unsigned short dup[2]  = { 0x1533, 0x1533 };          /* two NICs with the same DID */
        const unsigned short one[1]  = { 0x1533 };
        int idx;
        idx = -7; CHECK("no target requested (NULL) -> NOT_REQUESTED, index untouched (never a silent index 0)",
                        AvbTestSelectTargetIndex(two, 2, NULL, &idx) == AVB_TARGET_NOT_REQUESTED && idx == -7);
        idx = -7; CHECK("no target requested (\"\") -> NOT_REQUESTED, even with a single adapter",
                        AvbTestSelectTargetIndex(one, 1, "", &idx) == AVB_TARGET_NOT_REQUESTED && idx == -7);
        idx = -7; CHECK("\"0x1533\" -> OK, index 1 (not the first enumerated adapter)",
                        AvbTestSelectTargetIndex(two, 2, "0x1533", &idx) == AVB_TARGET_OK && idx == 1);
        idx = -7; CHECK("\"1533\" (hex without prefix) and \" 0X15bb \" (spaces, case) are accepted",
                        AvbTestSelectTargetIndex(two, 2, "1533", &idx) == AVB_TARGET_OK && idx == 1 &&
                        AvbTestSelectTargetIndex(two, 2, " 0X15bb ", &idx) == AVB_TARGET_OK && idx == 0);
        idx = -7; CHECK("duplicate DID -> AMBIGUOUS (a DID alone cannot identify the adapter), index untouched",
                        AvbTestSelectTargetIndex(dup, 2, "0x1533", &idx) == AVB_TARGET_AMBIGUOUS && idx == -7);
        idx = -7; CHECK("requested DID not present -> ABSENT, index untouched",
                        AvbTestSelectTargetIndex(two, 2, "0x125B", &idx) == AVB_TARGET_ABSENT && idx == -7);
        idx = -7; CHECK("malformed values (\"zz\", \"0x\", \"0x1533x\", \"0x12345\", \"0\") -> BAD_VALUE",
                        AvbTestSelectTargetIndex(two, 2, "zz", &idx) == AVB_TARGET_BAD_VALUE &&
                        AvbTestSelectTargetIndex(two, 2, "0x", &idx) == AVB_TARGET_BAD_VALUE &&
                        AvbTestSelectTargetIndex(two, 2, "0x1533x", &idx) == AVB_TARGET_BAD_VALUE &&
                        AvbTestSelectTargetIndex(two, 2, "0x12345", &idx) == AVB_TARGET_BAD_VALUE &&
                        AvbTestSelectTargetIndex(two, 2, "0", &idx) == AVB_TARGET_BAD_VALUE && idx == -7);
        idx = -7; CHECK("zero adapters with a target -> ABSENT", AvbTestSelectTargetIndex(two, 0, "0x1533", &idx) == AVB_TARGET_ABSENT && idx == -7);
        {
            AvbAdapterInfo list[2];
            ZeroMemory(list, sizeof(list));
            list[0].vendor_id = 0x8086; list[0].device_id = 0x15BB; list[0].global_index = 0;
            list[1].vendor_id = 0x8086; list[1].device_id = 0x1533; list[1].global_index = 1;
            SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, NULL);
            idx = -7; CHECK("env wrapper: variable unset -> NOT_REQUESTED", AvbTestSelectTarget(list, 2, &idx) == AVB_TARGET_NOT_REQUESTED && idx == -7);
            SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, "0x1533");
            idx = -7; CHECK("env wrapper: AVB_HIL_TARGET_DID=0x1533 -> OK, index 1", AvbTestSelectTarget(list, 2, &idx) == AVB_TARGET_OK && idx == 1);
            SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, NULL);
        }
    }

    printf("\n[T-002] shared enumerator exact-target filter (AvbEnumerateAdapters + AVB_HIL_TARGET_DID)\n");
    {
        AvbAdapterInfo list[3];
        int n;
        #define T002_FILL() do { ZeroMemory(list, sizeof(list)); \
            list[0].vendor_id = 0x8086; list[0].device_id = 0x15BB; list[0].global_index = 0; \
            list[1].vendor_id = 0x8086; list[1].device_id = 0x125B; list[1].global_index = 1; \
            list[2].vendor_id = 0x8086; list[2].device_id = 0x1533; list[2].global_index = 2; } while (0)
        T002_FILL(); SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, NULL);
        n = AvbTestFilterToTarget(list, 3);
        CHECK("no target requested -> list and count unchanged", n == 3 && list[0].device_id == 0x15BB && list[2].device_id == 0x1533);
        T002_FILL(); SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, "0x1533");
        n = AvbTestFilterToTarget(list, 3);
        CHECK("target 0x1533 -> exactly one adapter, and it keeps its driver index 2 (OPEN_ADAPTER binds the right NIC)",
              n == 1 && list[0].device_id == 0x1533 && list[0].global_index == 2);
        T002_FILL(); SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, "0x1539");
        n = AvbTestFilterToTarget(list, 3);
        CHECK("target absent -> -1 (caller must fail), list untouched", n == -1 && list[0].device_id == 0x15BB && list[2].device_id == 0x1533);
        T002_FILL(); list[1].device_id = 0x1533; SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, "0x1533");
        n = AvbTestFilterToTarget(list, 3);
        CHECK("duplicate target DID -> -1 (ambiguous)", n == -1);
        T002_FILL(); SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, "zz");
        n = AvbTestFilterToTarget(list, 3);
        CHECK("malformed target value -> -1", n == -1);
        SetEnvironmentVariableA(AVB_TEST_TARGET_ENV, NULL);
        #undef T002_FILL
    }

    /* ======================= physical rate verdict (issue #343, F) ======================= */
    printf("\n[R-001] PHC/QPC rate fit + ABA verdict on synthetic samples (independent QPC timebase)\n");
    {
        AvbRateFit nom[3], wp, wm;
        AvbRateEval ev;
        const double qoff = 23e-6;                 /* PHC crystal vs host QPC: +23 ppm, cancels in the ABA ratio */
        const double r0 = 1.0 + qoff;
        RATE_WIN(&nom[0], r0, 2000, 60, 1u); RATE_WIN(&nom[1], r0, 2000, 60, 2u); RATE_WIN(&nom[2], r0, 2000, 60, 3u);

        {   /* the fit itself */
            AvbRateFit f;
            RATE_WIN(&f, 1.0 + 1e-4, 0, 60, 9u);
            CHECK("noiseless window: LS slope recovered to < 1e-12, residual < 1 ns, monotonic",
                  f.ok && fabs(f.rate - (1.0 + 1e-4)) < 1e-12 && f.resid_rms_ns < 1.0 && f.monotonic && f.n == 60);
        }
        RATE_WIN(&wp, r0 * (1.0 + 100000e-9), 2000, 60, 4u);
        RATE_WIN(&wm, r0 * (1.0 - 100000e-9), 2000, 60, 5u);
        AvbRateEvaluate(nom, 3, &wp, 100000, 1, &ev);
        CHECK("+100,000 ppb commanded and realised (2 us sample noise) -> PASS", ev.verdict == AVB_RATE_PASS && fabs(ev.delta_ppb - 100000.0) < 500.0);
        AvbRateEvaluate(nom, 3, &wm, -100000, 1, &ev);
        CHECK("-100,000 ppb commanded and realised -> PASS", ev.verdict == AVB_RATE_PASS && fabs(ev.delta_ppb + 100000.0) < 500.0);
        AvbRateEvaluate(nom, 3, &wm, 100000, 1, &ev);
        CHECK("sign error (+100,000 commanded, -100,000 realised) -> FAIL", ev.verdict == AVB_RATE_FAIL);
        AvbRateEvaluate(nom, 3, &nom[1], 100000, 1, &ev);
        CHECK("adjustment without effect (rate unchanged) -> FAIL", ev.verdict == AVB_RATE_FAIL);
        AvbRateEvaluate(nom, 3, &wp, 100000, 0, &ev);
        CHECK("perfect data but a sample not REGISTER_DERIVED (mixed provenance) -> INCONCLUSIVE", ev.verdict == AVB_RATE_INCONCLUSIVE);
        {
            AvbRateFit legacy[3], lp;
            const double rl = r0 * (1.0 + 3906.25e-6);      /* legacy nominal raw 0x08000000 = +3906.25 ppm */
            RATE_WIN(&legacy[0], rl, 2000, 60, 6u); RATE_WIN(&legacy[1], rl, 2000, 60, 7u); RATE_WIN(&legacy[2], rl, 2000, 60, 8u);
            RATE_WIN(&lp, rl * (1.0 + 100000e-9), 2000, 60, 10u);
            AvbRateEvaluate(legacy, 3, &lp, 100000, 1, &ev);
            CHECK("legacy +3906.25 ppm nominal (even with a correct relative step) -> FAIL", ev.verdict == AVB_RATE_FAIL);
        }
        {
            AvbRateFit fz[3];
            RATE_WIN(&fz[0], 0.0, 0, 60, 11u); RATE_WIN(&fz[1], 0.0, 0, 60, 12u); RATE_WIN(&fz[2], 0.0, 0, 60, 13u);
            AvbRateEvaluate(fz, 3, &fz[1], 100000, 1, &ev);
            CHECK("frozen PHC (no advance) -> FAIL", ev.verdict == AVB_RATE_FAIL);
        }
        {
            AvbRateFit nz[3], np;
            RATE_WIN(&nz[0], r0, 2000, 60, 14u); RATE_WIN(&nz[1], r0 * (1.0 + 800e-9), 2000, 60, 15u); RATE_WIN(&nz[2], r0, 2000, 60, 16u);
            RATE_WIN(&np, r0 * (1.0 + 100000e-9), 2000, 60, 17u);
            AvbRateEvaluate(nz, 3, &np, 100000, 1, &ev);
            CHECK("nominal windows 800 ppb apart, commanded step realised -> INCONCLUSIVE (cannot certify)", ev.verdict == AVB_RATE_INCONCLUSIVE && ev.spread_ppb > 500.0);
            AvbRateEvaluate(nz, 3, &nz[0], 100000, 1, &ev);
            CHECK("same noise, but no step at all (error far beyond 5x spread) -> FAIL, not INCONCLUSIVE", ev.verdict == AVB_RATE_FAIL);
        }
        {
            AvbRateFit few;
            RATE_WIN(&few, r0 * (1.0 + 100000e-9), 2000, 10, 18u);
            AvbRateEvaluate(nom, 3, &few, 100000, 1, &ev);
            CHECK("window with fewer than AVB_RATE_MIN_SAMPLES samples -> INCONCLUSIVE", ev.verdict == AVB_RATE_INCONCLUSIVE);
        }
        {
            AvbRateFit q[3], b1, b2;
            RATE_WIN(&q[0], r0, 0, 60, 19u); RATE_WIN(&q[1], r0, 0, 60, 20u); RATE_WIN(&q[2], r0, 0, 60, 21u);
            RATE_WIN(&b1, r0 * (1.0 + 101900e-9), 0, 60, 22u);
            RATE_WIN(&b2, r0 * (1.0 + 102100e-9), 0, 60, 23u);
            AvbRateEvaluate(q, 3, &b1, 100000, 1, &ev);
            CHECK("tolerance: 1,900 ppb off with a quiet baseline -> PASS (min tolerance 2,000 ppb)", ev.verdict == AVB_RATE_PASS && fabs(ev.tol_ppb - 2000.0) < 1e-6);
            AvbRateEvaluate(q, 3, &b2, 100000, 1, &ev);
            CHECK("tolerance: 2,100 ppb off -> FAIL", ev.verdict == AVB_RATE_FAIL);
        }
    }

    /* ======================= initialization (issue #343) ======================= */
    printf("\n[I-001] init is exposed through the device table\n");
    CHECK("init and init_ptp are implemented", i210_ops.init != NULL && i210_ops.init_ptp != NULL);
    if (!i210_ops.init || !i210_ops.init_ptp) {
        printf("\n PASS: %d / %d\n FAIL: %d / %d\n[RESULT] FAILURE (init missing)\n", g_run - g_fail, g_run, g_fail, g_run);
        return 1;
    }

    printf("\n[I-002] fresh / power-cycled device: full initialization, one owner, no generic platform init\n");
    {
        int rc, iT, iS, iSH;
        hw_fresh();
        rc = i210_ops.init(&dev);
        CHECK("init succeeds", rc == 0);
        CHECK("the generic platform init is NOT invoked (it would write a second, contradictory TIMINCA)", hw.platform_init_calls == 0);
        CHECK("SYSTIM enabled: TSAUXC written to 0", hw.tsauxc == 0u && writes_to(I210_TSAUXC) == 1);
        CHECK("TIMINCA programmed at most once, nominal (0)", writes_to(I210_TIMINCA) <= 1 && hw.timinca == 0u);
        CHECK("SYSTIM seeded exactly once (SYSTIML then SYSTIMH)", writes_to(I210_SYSTIML) == 1 && writes_to(I210_SYSTIMH) == 1 &&
              first_write_index(I210_SYSTIML) < first_write_index(I210_SYSTIMH));
        iT = first_write_index(I210_TSAUXC); iS = first_write_index(I210_SYSTIML); iSH = first_write_index(I210_SYSTIMH);
        CHECK("SYSTIM is enabled BEFORE it is seeded", iT >= 0 && iT < iS && iT < iSH);
        CHECK("seed value is sec=0, ns=1", hw.systiml == 1u && hw.systimh == 0u);
        CHECK("TX timestamping control enabled and read back", (hw.tx & TX_EN) == TX_EN && writes_to(I210_TSYNCTXCTL) == 1);
        CHECK("RX timestamping control enabled (type ALL) and read back", hw.rx == RX_EN && writes_to(I210_TSYNCRXCTL) == 1);
        CHECK("no register outside the I210 PTP set is written", writes_outside_known_registers() == 0);
    }

    printf("\n[I-003] repeated initialization of an initialized device performs NO writes (no re-seed)\n");
    {
        int rc, before;
        hw_fresh();
        (void)i210_ops.init(&dev);
        before = hw.writes;
        rc = i210_ops.init(&dev);
        CHECK("second init succeeds", rc == 0);
        CHECK("second init writes nothing (in particular no SYSTIM re-seed)", hw.writes == before);
        rc = i210_ops.init(&dev);
        CHECK("third init succeeds and still writes nothing", rc == 0 && hw.writes == before);
        rc = i210_ops.init_ptp(&dev);
        CHECK("init_ptp (explicit PTP init op) on an initialized device also writes nothing", rc == 0 && hw.writes == before);
        hw_configured();
        rc = i210_ops.init(&dev);
        CHECK("nominal TIMINCA 0 on a running, configured clock is NOT 'uninitialized': no writes",
              rc == 0 && hw.writes == 0 && hw.systiml == 123456u && hw.systimh == 5u);
    }

    printf("\n[I-004] configured device with a configuration defect: minimal repair, never a re-seed\n");
    {
        int rc;
        hw_configured(); hw.timinca = 0x08000000u;   /* what the old driver left behind: +3906 ppm */
        rc = i210_ops.init(&dev);
        CHECK("legacy 0x08000000 is rewritten to nominal", rc == 0 && hw.timinca == 0u && writes_to(I210_TIMINCA) == 1);
        CHECK("... with no other write at all (no TSAUXC clear, no re-seed, controls untouched)", hw.writes == 1 && hw.systiml == 123456u);
        hw_configured(); hw.timinca = 0x6B6u;        /* +50 ppb adjusted by ADJUST_FREQUENCY: inside the policy */
        rc = i210_ops.init(&dev);
        CHECK("an in-policy frequency adjustment is preserved (no writes)", rc == 0 && hw.writes == 0 && hw.timinca == 0x6B6u);
        hw_configured(); hw.tx = 0u;
        rc = i210_ops.init(&dev);
        CHECK("TX control off -> only TSYNCTXCTL written", rc == 0 && hw.writes == 1 && writes_to(I210_TSYNCTXCTL) == 1 && (hw.tx & TX_EN) == TX_EN);
        hw_configured(); hw.rx = 0u;
        rc = i210_ops.init(&dev);
        CHECK("RX control off -> only TSYNCRXCTL written", rc == 0 && hw.writes == 1 && writes_to(I210_TSYNCRXCTL) == 1 && hw.rx == RX_EN);
        hw_configured(); hw.rx = TX_EN | 0x20u;      /* EN set but type != ALL */
        rc = i210_ops.init(&dev);
        CHECK("RX control with a type other than ALL is corrected", rc == 0 && hw.rx == RX_EN && writes_to(I210_TSYNCRXCTL) == 1);
    }

    printf("\n[I-005] SYSTIM disabled on an otherwise configured device is a recovery reason (full re-initialization)\n");
    {
        int rc;
        hw_configured(); hw.tsauxc = INTEL_TSAUXC_DISABLE_SYSTIM;
        rc = i210_ops.init(&dev);
        CHECK("frozen clock: TSAUXC cleared and SYSTIM re-seeded", rc == 0 && hw.tsauxc == 0u && hw.systiml == 1u && hw.systimh == 0u);

        /* Power-cycled device whose DisableSystime reads back clear: both timestamp controls are off, which is
         * how a fresh/power-cycled device is recognised (S3/S4 resume clears them), so it is fully initialized. */
        hw_reset(0u); hw.tsauxc = 0u; hw.systiml = 777u; hw.systimh = 9u;
        rc = i210_ops.init(&dev);
        CHECK("both timestamp controls off (SYSTIM enabled) -> full initialization incl. SYSTIM seed",
              rc == 0 && hw.systiml == 1u && hw.systimh == 0u && writes_to(I210_TSAUXC) == 1 &&
              (hw.tx & TX_EN) == TX_EN && hw.rx == RX_EN);
    }

    printf("\n[I-006] every mandatory step failure fails init and stops further writes\n");
    {
        struct { const char *name; uint32_t off; } steps[] = {
            { "TSAUXC write",        I210_TSAUXC },
            { "TIMINCA write",       I210_TIMINCA },
            { "SYSTIML seed write",  I210_SYSTIML },
            { "SYSTIMH seed write",  I210_SYSTIMH },
            { "TSYNCTXCTL write",    I210_TSYNCTXCTL },
            { "TSYNCRXCTL write",    I210_TSYNCRXCTL },
        };
        for (i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
            int rc, idx;
            char label[128];
            hw_fresh(); hw.timinca = 0x08000000u;      /* forces every step, including a TIMINCA rewrite */
            hw.have_fail_write_off = 1; hw.fail_write_off = steps[i].off;
            rc = i210_ops.init(&dev);
            idx = first_write_index(steps[i].off);
            _snprintf_s(label, sizeof(label), _TRUNCATE, "%s fails -> init fails, no write after the failing one", steps[i].name);
            CHECK(label, rc != 0 && idx >= 0 && hw.log[idx].failed && hw.nlog == idx + 1);
        }
    }

    printf("\n[I-007] timestamp-capture enable must be verified by readback\n");
    {
        int rc;
        hw_fresh(); hw.tx_latches = 0;
        rc = i210_ops.init(&dev);
        CHECK("TX enable does not latch -> init fails", rc != 0);
        hw_fresh(); hw.rx_latches = 0;
        rc = i210_ops.init(&dev);
        CHECK("RX enable does not latch -> init fails", rc != 0);
    }

    printf("\n[I-008] configuration that cannot be read: fail closed, never mutate\n");
    {
        uint32_t offs[] = { I210_TSAUXC, I210_TIMINCA, I210_TSYNCTXCTL, I210_TSYNCRXCTL };
        for (i = 0; i < sizeof(offs) / sizeof(offs[0]); i++) {
            int rc;
            char label[96];
            hw_configured(); hw.tsauxc = INTEL_TSAUXC_DISABLE_SYSTIM;   /* a state that WOULD be mutated if it could be read */
            hw.have_fail_read_off = 1; hw.fail_read_off = offs[i];
            rc = i210_ops.init(&dev);
            _snprintf_s(label, sizeof(label), _TRUNCATE, "read of 0x%05X fails -> init fails with zero writes", (unsigned)offs[i]);
            CHECK(label, rc != 0 && hw.writes == 0);
        }
    }

    printf("\n[I-009] NULL device\n");
    hw_fresh();
    CHECK("init(NULL) fails without MMIO", i210_ops.init(NULL) != 0 && hw.reads == 0 && hw.writes == 0);

    /* I-010..I-014: QUERY_HW_SNAPSHOT (Q 2026-10-22) - Release-available, read-only observation of every register HIL
     * tests modify, with the production I210 SSOT offset map.  Synthetic register values (host shim), not hardware. */
    printf("\n[I-010] HW snapshot: SSOT offsets, values, zero writes\n");
    CHECK("hil_register_offset is implemented", i210_ops.hil_register_offset != NULL);
    if (i210_ops.hil_register_offset) {
        AVB_HW_SNAPSHOT_REQUEST s;
        hw_configured(); hw.tx = 0x00000010u; hw.rx = 0x0000001Au; hw.timinca = 0x08000000u; hw.tsauxc = 0x00000004u;
        memset(&s, 0xA5, sizeof(s));
        AvbFillHwSnapshot(&i210_ops, &dev, INTEL_CAP_MMIO, ndis_platform_ops.mmio_read, &s);
        CHECK("count == AVB_HWREG_COUNT, entries in id order",
              s.count == AVB_HWREG_COUNT && s.entries[0].id == AVB_HWREG_TIMINCA && s.entries[AVB_HWREG_COUNT - 1].id == AVB_HWREG_RAH0);
        CHECK("TSYNCTXCTL VALID at SSOT 0xB614 with the register value",
              s.entries[AVB_HWREG_TSYNCTXCTL - 1].state == AVB_HWREG_STATE_VALID && s.entries[AVB_HWREG_TSYNCTXCTL - 1].offset == I210_TSYNCTXCTL &&
              s.entries[AVB_HWREG_TSYNCTXCTL - 1].value == 0x00000010u);
        CHECK("TSYNCRXCTL / TIMINCA / TSAUXC VALID with their values",
              s.entries[AVB_HWREG_TSYNCRXCTL - 1].value == 0x0000001Au && s.entries[AVB_HWREG_TIMINCA - 1].value == 0x08000000u &&
              s.entries[AVB_HWREG_TSAUXC - 1].value == 0x00000004u && s.entries[AVB_HWREG_TSAUXC - 1].offset == I210_TSAUXC);
        CHECK("RXPBSIZE, TRGTTIML0, TQAVCTRL, RAL0 at their I210 SSOT offsets",
              s.entries[AVB_HWREG_RXPBSIZE - 1].offset == I210_RXPBSIZE && s.entries[AVB_HWREG_TRGTTIML0 - 1].offset == I210_TRGTTIML0 &&
              s.entries[AVB_HWREG_TQAVCTRL - 1].offset == I210_TQAVCTRL && s.entries[AVB_HWREG_RAL0 - 1].offset == I210_RAL0);
        CHECK("SRRCTL0..3 NOT_IN_SSOT (intel-ethernet-regs defines no SRRCTL for I210), offset 0, value 0",
              s.entries[AVB_HWREG_SRRCTL0 - 1].state == AVB_HWREG_STATE_NOT_IN_SSOT && s.entries[AVB_HWREG_SRRCTL3 - 1].state == AVB_HWREG_STATE_NOT_IN_SSOT &&
              s.entries[AVB_HWREG_SRRCTL0 - 1].offset == 0 && s.entries[AVB_HWREG_SRRCTL0 - 1].value == 0);
        CHECK("snapshot is read-only: zero register writes", hw.writes == 0);

        printf("\n[I-011] one register unreadable -> only that entry READ_FAILED\n");
        hw_configured(); hw.have_fail_read_off = 1; hw.fail_read_off = I210_TSYNCRXCTL;
        AvbFillHwSnapshot(&i210_ops, &dev, INTEL_CAP_MMIO, ndis_platform_ops.mmio_read, &s);
        CHECK("TSYNCRXCTL READ_FAILED with its offset, TSYNCTXCTL still VALID, zero writes",
              s.entries[AVB_HWREG_TSYNCRXCTL - 1].state == AVB_HWREG_STATE_READ_FAILED && s.entries[AVB_HWREG_TSYNCRXCTL - 1].offset == I210_TSYNCRXCTL &&
              s.entries[AVB_HWREG_TSYNCTXCTL - 1].state == AVB_HWREG_STATE_VALID && hw.writes == 0);

        printf("\n[I-012] adapter without MMIO access -> every entry NO_MMIO, no register touched\n");
        hw_configured();
        AvbFillHwSnapshot(&i210_ops, &dev, 0u, ndis_platform_ops.mmio_read, &s);
        CHECK("all NO_MMIO, zero reads, zero writes",
              s.entries[0].state == AVB_HWREG_STATE_NO_MMIO && s.entries[AVB_HWREG_COUNT - 1].state == AVB_HWREG_STATE_NO_MMIO && hw.reads == 0 && hw.writes == 0);

        printf("\n[I-013] family without an SSOT offset map -> every entry NO_HAL\n");
        {
            intel_device_ops_t noHal = i210_ops; noHal.hil_register_offset = NULL;
            hw_configured();
            AvbFillHwSnapshot(&noHal, &dev, INTEL_CAP_MMIO, ndis_platform_ops.mmio_read, &s);
            CHECK("all NO_HAL, zero reads", s.entries[0].state == AVB_HWREG_STATE_NO_HAL && s.entries[AVB_HWREG_COUNT - 1].state == AVB_HWREG_STATE_NO_HAL && hw.reads == 0);
        }

        printf("\n[I-014] unknown register id -> not defined\n");
        {
            uint32_t off = 0x12345u;
            CHECK("id 0 and id AVB_HWREG_COUNT+1 are not defined, offset untouched",
                  i210_ops.hil_register_offset(0u, &off) != 0 && i210_ops.hil_register_offset(AVB_HWREG_COUNT + 1u, &off) != 0 && off == 0x12345u);
        }
    }

    printf("\n PASS: %d / %d\n FAIL: %d / %d\n", g_run - g_fail, g_run, g_fail, g_run);
    if (g_fail) { printf("[RESULT] FAILURE\n"); return 1; }
    printf("[RESULT] ALL I210 DEVICE OPERATION TESTS PASSED\n");
    return 0;
}
