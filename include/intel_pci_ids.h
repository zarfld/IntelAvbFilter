/******************************************************************************
 *
 * Intel Ethernet PCI Device ID Constants
 *
 * Single Source of Truth (SSOT) for all Intel PCI device IDs used in the
 * IntelAvbFilter driver. All device ID comparisons and switch statements
 * in src/ and devices/ must reference these constants.
 *
 * Implements: REQ-NF-REGS-001 (Eliminate Magic Numbers / SSOT)
 * See also:   external/intel_avb/lib/intel.h  (INTEL_VENDOR_ID = 0x8086)
 *             external/intel_avb/lib/intel_private.h  (register offsets)
 *
 ******************************************************************************/
#ifndef _INTEL_PCI_IDS_H_
#define _INTEL_PCI_IDS_H_

/* --------------------------------------------------------------------------
 * Intel I210 family (Gigabit Ethernet – no TSN)
 * -------------------------------------------------------------------------- */
#define INTEL_DEV_I210_AT           0x1533U  /* I210-AT  Copper       */
#define INTEL_DEV_I210_AT2          0x1534U  /* I210 Copper OEM1      */
#define INTEL_DEV_I210_FIBER        0x1535U  /* I210 Copper IT        */
#define INTEL_DEV_I210_IS           0x1536U  /* I210-IS  Fiber        */
#define INTEL_DEV_I210_IT           0x1537U  /* I210-IT  Serdes       */
#define INTEL_DEV_I210_CS           0x1538U  /* I210-CS  SGMII        */
#define INTEL_DEV_I211_AT           0x1539U  /* I211-AT  (flash-less I210) */
#define INTEL_DEV_I210_FLASHLESS    0x157BU  /* I210 flash-less       */
#define INTEL_DEV_I210_FLASHLESS2   0x157CU  /* I210 flash-less IS    */

/* --------------------------------------------------------------------------
 * Intel I217 family (PCH integrated, MDIO interface)
 * -------------------------------------------------------------------------- */
#define INTEL_DEV_I217_LM           0x153AU  /* I217-LM               */
#define INTEL_DEV_I217_V            0x153BU  /* I217-V                */

/* --------------------------------------------------------------------------
 * Intel I219 family (PCH integrated, MDIO interface)
 * -------------------------------------------------------------------------- */
#define INTEL_DEV_I219_LM_A0        0x15A0U  /* I219-LM (A0)          */
#define INTEL_DEV_I219_V_A0         0x15A1U  /* I219-V  (A0)          */
#define INTEL_DEV_I219_LM_A1        0x15A2U  /* I219-LM (A1)          */
#define INTEL_DEV_I219_V_A1         0x15A3U  /* I219-V  (A1)          */
#define INTEL_DEV_I219_LM           0x15B7U  /* I219-LM               */
#define INTEL_DEV_I219_V            0x15B8U  /* I219-V                */
#define INTEL_DEV_I219_LM3          0x15B9U  /* I219-LM3              */
#define INTEL_DEV_I219_LM4          0x15BBU  /* I219-LM4              */
#define INTEL_DEV_I219_V4           0x15BCU  /* I219-V4               */
#define INTEL_DEV_I219_LM5          0x15BDU  /* I219-LM5              */
#define INTEL_DEV_I219_V5           0x15BEU  /* I219-V5               */
#define INTEL_DEV_I219_D0           0x15D6U  /* I219-V  (Skylake)     */
#define INTEL_DEV_I219_D1           0x15D7U  /* I219-LM (Skylake)     */
#define INTEL_DEV_I219_D2           0x15D8U  /* I219-LM (Kaby Lake)   */
#define INTEL_DEV_I219_LM_DC7       0x0DC7U  /* I219-LM (variant DC7) */
#define INTEL_DEV_I219_V6           0x1570U  /* I219-V  (Broadwell)   */
#define INTEL_DEV_I219_LM6          0x15E3U  /* I219-LM (Broadwell)   */

/* --------------------------------------------------------------------------
 * Intel I225 family (2.5GbE, TSN capable)
 * -------------------------------------------------------------------------- */
#define INTEL_DEV_I225_V            0x15F2U  /* I225-V   2.5G         */
#define INTEL_DEV_I225_IT           0x15F3U  /* I225-IT  2.5G         */
#define INTEL_DEV_I225_LM           0x15F4U  /* I225-LM  2.5G         */
#define INTEL_DEV_I225_K            0x15F5U  /* I225-K   2.5G         */
#define INTEL_DEV_I225_K2           0x15F6U  /* I225-K2  2.5G         */
#define INTEL_DEV_I225_LMVP         0x15F7U  /* I225-LMvP 2.5G        */
#define INTEL_DEV_I225_VB           0x15F8U  /* I225-VB  2.5G         */
#define INTEL_DEV_I225_IT2          0x15F9U  /* I225-IT2 2.5G         */
#define INTEL_DEV_I225_LM2          0x15FAU  /* I225-LM2 2.5G         */
#define INTEL_DEV_I225_LM3          0x15FBU  /* I225-LM3 2.5G         */
#define INTEL_DEV_I225_V2           0x15FCU  /* I225-V2  2.5G         */
#define INTEL_DEV_I225_VARIANT      0x0D9FU  /* I225 variant           */

/* --------------------------------------------------------------------------
 * Intel I226 family (2.5GbE, TSN capable, Foxville successor)
 * -------------------------------------------------------------------------- */
#define INTEL_DEV_I226_LM           0x125BU  /* I226-LM               */
#define INTEL_DEV_I226_V            0x125CU  /* I226-V                */
#define INTEL_DEV_I226_IT           0x125DU  /* I226-IT               */

#endif /* _INTEL_PCI_IDS_H_ */
