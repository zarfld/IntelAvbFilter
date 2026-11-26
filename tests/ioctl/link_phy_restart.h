/* link_phy_restart.h - the guarded auto-negotiation restart through IOCTL_AVB_MDIO_WRITE (#472 option B, approved 2026-11-25; for
 * TC-TASACT-META-007 approved 2026-11-26). One write of the BMCR value read plus the self-clearing restart bit. No PHY reset, no power-down,
 * no other register write.
 *
 * COPY, unchanged in values and rules, of the helpers ev_mdio, ev_phy_snap, ev_phy_allows_restart and the comparison of ev_compare_phy in
 * tests/hardware/test_tas_query_clean.c (TC-TASAPI-021). That file is left untouched in this step so the #472 regression keeps its tested
 * binary; joining the two is a later change with a hardware run of both tests.
 *
 * PHY access (sources compared 2026-11-25, see TC-TASAPI-021): clause 22 at PHY address 0 through MDIC, confirmed per run by a plausible,
 * stable identity. The driver's MDIC access takes no PHY semaphore (#152), so every read is made twice and must agree, no MDIO access is
 * made while the link changes, and an unexpected value never leads to a write.
 * Needs <windows.h>, include/avb_ioctl.h and tests/common/avb_ntstatus.h before it.
 */
#ifndef LINK_PHY_RESTART_H
#define LINK_PHY_RESTART_H

#include <stdio.h>

#define LP_PHY_ADDR            0u
#define LP_REG_BMCR            0u
#define LP_REG_BMSR            1u
#define LP_REG_PHYID1          2u
#define LP_REG_PHYID2          3u
#define LP_REG_ANAR            4u
#define LP_REG_CTRL1000        9u
#define LP_BMCR_ANRESTART      0x0200u  /* self-clearing command */
#define LP_BMCR_ANENABLE       0x1000u
#define LP_BMCR_COMMANDS       0x8200u  /* reset, restart auto-negotiation: self-clearing, must read 0 before and after */
#define LP_BMCR_MODES          0x4C80u  /* loopback, power-down, isolate, collision test: must read 0, else nothing is written */
#define LP_BMCR_RESERVED       0x003Fu  /* unidirectional enable and reserved bits: must read 0, else nothing is written */
#define LP_BMCR_CONFIG         0x5CBFu  /* compared after the event */
#define LP_BMCR_REPORTED       0x2140u  /* speed selection and duplex: without effect under auto-negotiation; reported, not judged */
#define LP_BMSR_LINK           0x0004u
#define LP_BMSR_ABLE           0x0008u
#define LP_BMSR_COMPLETE       0x0020u
#define LP_ANAR_COMPARED       0xBFFFu  /* bit 14 is reserved */
#define LP_CTRL1000_COMPARED   0xFF00u  /* bits 7:0 are reserved */

typedef struct { BOOL ok; avb_u16 bmcr, bmsr, id1, id2, anar, ctrl1000; } LP_PHY;

static __inline BOOL lp_mdio(HANDLE h, BOOL write, avb_u32 reg, avb_u16 *value, const char *label)
{
    AVB_MDIO_REQUEST m;
    DWORD got = 0;
    LONG st;
    BOOL ok;

    ZeroMemory(&m, sizeof(m));
    m.page = LP_PHY_ADDR;
    m.reg = reg;
    if (write) {
        m.value = *value;
    }
    m.status = 0xFFFFFFFFu;   /* never a success the driver did not write */
    st = avb_ioctl_ntstatus(h, write ? IOCTL_AVB_MDIO_WRITE : IOCTL_AVB_MDIO_READ, &m, sizeof(m), &m, sizeof(m), &got);
    ok = st == 0 && got == sizeof(m) && m.status == 0;
    printf("    [EVIDENCE] MDIO %s %s: addr=%u reg=%u value=0x%04X ntstatus=0x%08lX information=%lu reply=0x%08X%s\n",
           write ? "write" : "read", label, LP_PHY_ADDR, reg, m.value, (unsigned long)st, got, m.status, ok ? "" : " NOT VALID");
    fflush(stdout);
    if (!write) {
        *value = ok ? m.value : 0;
    }
    return ok;
}

/** Every register twice; both reads valid and, except the latching BMSR, equal. BMSR: the second read counts (IEEE 802.3 22.2.4.2) */
static __inline BOOL lp_phy_snap(HANDLE h, const char *label, BOOL with_bmsr, LP_PHY *p, char *why, size_t size)
{
    static const avb_u32 regs[] = { LP_REG_PHYID1, LP_REG_PHYID2, LP_REG_BMCR, LP_REG_ANAR, LP_REG_CTRL1000, LP_REG_BMSR };
    unsigned i;

    ZeroMemory(p, sizeof(*p));
    for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++) {
        avb_u16 a, b;
        if (regs[i] == LP_REG_BMSR && !with_bmsr) {
            continue;
        }
        if (!lp_mdio(h, FALSE, regs[i], &a, label) || !lp_mdio(h, FALSE, regs[i], &b, label)) {
            sprintf_s(why, size, "PHY register %u could not be read (see the MDIO evidence)", regs[i]);
            return FALSE;
        }
        if (regs[i] != LP_REG_BMSR && a != b) {
            sprintf_s(why, size, "PHY register %u read 0x%04X then 0x%04X: not stable (a concurrent MDIC access is possible)", regs[i], a, b);
            return FALSE;
        }
        switch (regs[i]) {
        case LP_REG_PHYID1:   p->id1 = b; break;
        case LP_REG_PHYID2:   p->id2 = b; break;
        case LP_REG_BMCR:     p->bmcr = b; break;
        case LP_REG_ANAR:     p->anar = b; break;
        case LP_REG_CTRL1000: p->ctrl1000 = b; break;
        default:              p->bmsr = b; break;
        }
    }
    p->ok = TRUE;
    return TRUE;
}

/** Everything that must hold before the one write; the first missing condition is the reason */
static __inline BOOL lp_phy_allows_restart(const LP_PHY *p, char *why, size_t size)
{
    if (p->id1 == 0 || p->id1 == 0xFFFFu || p->id2 == 0xFFFFu) {
        sprintf_s(why, size, "no plausible PHY identity at address %u (0x%04X 0x%04X)", LP_PHY_ADDR, p->id1, p->id2);
    } else if ((p->bmcr & LP_BMCR_ANENABLE) == 0) {
        sprintf_s(why, size, "auto-negotiation is not enabled (BMCR 0x%04X); it is not enabled by this test", p->bmcr);
    } else if ((p->bmcr & (LP_BMCR_COMMANDS | LP_BMCR_MODES | LP_BMCR_RESERVED)) != 0) {
        sprintf_s(why, size, "BMCR 0x%04X has a command, mode or reserved bit set: writing it back could trigger it again", p->bmcr);
    } else if (p->bmsr == 0xFFFFu || (p->bmsr & (LP_BMSR_ABLE | LP_BMSR_COMPLETE | LP_BMSR_LINK)) != (LP_BMSR_ABLE | LP_BMSR_COMPLETE | LP_BMSR_LINK)) {
        sprintf_s(why, size, "BMSR 0x%04X does not report auto-negotiation ability, completion and link, although the OS reports a connected "
                  "link: this PHY address is not established as the adapter's PHY", p->bmsr);
    } else {
        return TRUE;
    }
    return FALSE;
}

typedef enum { LP_SAME = 0, LP_VIOLATION, LP_UNTRUSTED } LP_COMPARE;

/** After the event, by the rules of ev_compare_phy: identity first, then the compared configuration. why: every difference */
static __inline LP_COMPARE lp_compare_phy(const LP_PHY *a, const LP_PHY *b, char *why, size_t size)
{
    LP_COMPARE r = LP_SAME;

    why[0] = '\0';
    printf("    [EVIDENCE] PHY before/after: BMCR 0x%04X/0x%04X (compared 0x%04X, reported only 0x%04X) ANAR 0x%04X/0x%04X (0x%04X) "
           "CTRL1000 0x%04X/0x%04X (0x%04X); 2.5G and EEE advertisement (clause 45) not read\n", a->bmcr, b->bmcr, LP_BMCR_CONFIG,
           LP_BMCR_REPORTED, a->anar, b->anar, LP_ANAR_COMPARED, a->ctrl1000, b->ctrl1000, LP_CTRL1000_COMPARED);
    if (a->id1 != b->id1 || a->id2 != b->id2) {
        sprintf_s(why, size, "PHY identity 0x%04X 0x%04X, before 0x%04X 0x%04X: not trusted", b->id1, b->id2, a->id1, a->id2);
        return LP_UNTRUSTED;
    }
    if ((b->bmcr & LP_BMCR_COMMANDS) != 0) {
        sprintf_s(why + strlen(why), size - strlen(why), "BMCR 0x%04X: a self-clearing command bit still set; ", b->bmcr);
        r = LP_VIOLATION;
    }
    if (((a->bmcr ^ b->bmcr) & LP_BMCR_CONFIG) != 0) {
        sprintf_s(why + strlen(why), size - strlen(why), "BMCR configuration 0x%04X -> 0x%04X; ", a->bmcr, b->bmcr);
        r = LP_VIOLATION;
    }
    if (((a->anar ^ b->anar) & LP_ANAR_COMPARED) != 0) {
        sprintf_s(why + strlen(why), size - strlen(why), "advertisement 0x%04X -> 0x%04X; ", a->anar, b->anar);
        r = LP_VIOLATION;
    }
    if (((a->ctrl1000 ^ b->ctrl1000) & LP_CTRL1000_COMPARED) != 0) {
        sprintf_s(why + strlen(why), size - strlen(why), "1000BASE-T control 0x%04X -> 0x%04X; ", a->ctrl1000, b->ctrl1000);
        r = LP_VIOLATION;
    }
    return r;
}

#endif /* LINK_PHY_RESTART_H */
