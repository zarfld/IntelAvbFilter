# Fix-SSOT-Violations.ps1
# Replace all SSOT magic number violations in src/ and devices/ with named constants

$ErrorActionPreference = 'Stop'

param([string]$RepoRoot = (Get-Location).Path)

Set-Location $RepoRoot

function Fix-File {
    param([string]$Path, [hashtable]$Replacements)
    $content = Get-Content $Path -Raw -Encoding UTF8
    foreach ($old in $Replacements.Keys) {
        $content = $content.Replace($old, $Replacements[$old])
    }
    Set-Content $Path -Value $content -Encoding UTF8 -NoNewline
    Write-Host "Fixed: $Path"
}

# === avb_bar0_discovery.c ===
$disc_replacements = [ordered]@{
    '0x1533' = 'INTEL_DEV_I210_AT'
    '0x1534' = 'INTEL_DEV_I210_AT2'
    '0x1535' = 'INTEL_DEV_I210_FIBER'
    '0x1536' = 'INTEL_DEV_I210_IS'
    '0x1537' = 'INTEL_DEV_I210_IT'
    '0x1538' = 'INTEL_DEV_I210_CS'
    '0x1539' = 'INTEL_DEV_I211_AT'
    '0x157B' = 'INTEL_DEV_I210_FLASHLESS'
    '0x157C' = 'INTEL_DEV_I210_FLASHLESS2'
    '0x153A' = 'INTEL_DEV_I217_LM'
    '0x153B' = 'INTEL_DEV_I217_V'
    '0x15A0' = 'INTEL_DEV_I219_LM_A0'
    '0x15A1' = 'INTEL_DEV_I219_V_A0'
    '0x15A2' = 'INTEL_DEV_I219_LM_A1'
    '0x15A3' = 'INTEL_DEV_I219_V_A1'
    '0x15B7' = 'INTEL_DEV_I219_LM'
    '0x15B8' = 'INTEL_DEV_I219_V'
    '0x15B9' = 'INTEL_DEV_I219_LM3'
    '0x15BB' = 'INTEL_DEV_I219_LM4'
    '0x15BC' = 'INTEL_DEV_I219_V4'
    '0x15BD' = 'INTEL_DEV_I219_LM5'
    '0x15BE' = 'INTEL_DEV_I219_V5'
    '0x15D6' = 'INTEL_DEV_I219_D0'
    '0x15D7' = 'INTEL_DEV_I219_D1'
    '0x15D8' = 'INTEL_DEV_I219_D2'
    '0x0DC7' = 'INTEL_DEV_I219_LM_DC7'
    '0x1570' = 'INTEL_DEV_I219_V6'
    '0x15E3' = 'INTEL_DEV_I219_LM6'
    '0x15F2' = 'INTEL_DEV_I225_V'
    '0x15F3' = 'INTEL_DEV_I225_IT'
    '0x15F4' = 'INTEL_DEV_I225_LM'
    '0x15F5' = 'INTEL_DEV_I225_K'
    '0x15F6' = 'INTEL_DEV_I225_K2'
    '0x15F7' = 'INTEL_DEV_I225_LMVP'
    '0x15F8' = 'INTEL_DEV_I225_VB'
    '0x15F9' = 'INTEL_DEV_I225_IT2'
    '0x15FA' = 'INTEL_DEV_I225_LM2'
    '0x15FB' = 'INTEL_DEV_I225_LM3'
    '0x15FC' = 'INTEL_DEV_I225_V2'
    '0x0D9F' = 'INTEL_DEV_I225_VARIANT'
    '0x125B' = 'INTEL_DEV_I226_LM'
    '0x125C' = 'INTEL_DEV_I226_V'
    '0x125D' = 'INTEL_DEV_I226_IT'
    'return 0x20000;' = 'return INTEL_BAR0_SIZE_128KB;'
    '& 0xFFFF)' = '& INTEL_MASK_16BIT)'
    '& 0xFFFF;' = '& INTEL_MASK_16BIT;'
    '& 0xFFFF &' = '& INTEL_MASK_16BIT &'
}
Fix-File 'src\avb_bar0_discovery.c' $disc_replacements

# === avb_bar0_enhanced.c ===
$enh_replacements = [ordered]@{
    '0x1533' = 'INTEL_DEV_I210_AT'
    '0x1536' = 'INTEL_DEV_I210_IS'
    '0x1537' = 'INTEL_DEV_I210_IT'
    '0x1538' = 'INTEL_DEV_I210_CS'
    '0x157B' = 'INTEL_DEV_I210_FLASHLESS'
    '0x15B7' = 'INTEL_DEV_I219_LM'
    '0x15B8' = 'INTEL_DEV_I219_V'
    '0x15D6' = 'INTEL_DEV_I219_D0'
    '0x15D7' = 'INTEL_DEV_I219_D1'
    '0x15D8' = 'INTEL_DEV_I219_D2'
    '0x15F2' = 'INTEL_DEV_I225_V'
    '0x15F3' = 'INTEL_DEV_I225_IT'
    '0x0D9F' = 'INTEL_DEV_I225_VARIANT'
    '0x125B' = 'INTEL_DEV_I226_LM'
    '0x125C' = 'INTEL_DEV_I226_V'
    '0x125D' = 'INTEL_DEV_I226_IT'
    '& 0xFFFF)' = '& INTEL_MASK_16BIT)'
    '& 0xFFFF;' = '& INTEL_MASK_16BIT;'
    '& 0xFFFFFFF0' = '& INTEL_PCI_BAR_MMIO_MASK'
    '== 0xFFFFFFF0)' = '== INTEL_PCI_BAR_MMIO_MASK)'
    '0x8086' = 'INTEL_VENDOR_ID'
}
Fix-File 'src\avb_bar0_enhanced.c' $enh_replacements

# === tsn_config.c ===
$tsn_replacements = [ordered]@{
    '0x15F2' = 'INTEL_DEV_I225_V'
    '0x15F3' = 'INTEL_DEV_I225_IT'
    '0x15F4' = 'INTEL_DEV_I225_LM'
    '0x15F5' = 'INTEL_DEV_I225_K'
    '0x15F6' = 'INTEL_DEV_I225_K2'
    '0x15F7' = 'INTEL_DEV_I225_LMVP'
    '0x15F8' = 'INTEL_DEV_I225_VB'
    '0x15F9' = 'INTEL_DEV_I225_IT2'
    '0x15FA' = 'INTEL_DEV_I225_LM2'
    '0x15FB' = 'INTEL_DEV_I225_LM3'
    '0x15FC' = 'INTEL_DEV_I225_V2'
    '0x125B' = 'INTEL_DEV_I226_LM'
    '0x125C' = 'INTEL_DEV_I226_V'
    '0x125D' = 'INTEL_DEV_I226_IT'
    '0x1533' = 'INTEL_DEV_I210_AT'
    '0x1539' = 'INTEL_DEV_I211_AT'
    '0x157B' = 'INTEL_DEV_I210_FLASHLESS'
    '0x157C' = 'INTEL_DEV_I210_FLASHLESS2'
    '0x15A0' = 'INTEL_DEV_I219_LM_A0'
    '0x15A1' = 'INTEL_DEV_I219_V_A0'
    '0x15A2' = 'INTEL_DEV_I219_LM_A1'
    '0x15A3' = 'INTEL_DEV_I219_V_A1'
    '0x15B7' = 'INTEL_DEV_I219_LM'
    '0x15B8' = 'INTEL_DEV_I219_V'
    '0x15B9' = 'INTEL_DEV_I219_LM3'
    '0x15BB' = 'INTEL_DEV_I219_LM4'
    '0x15BC' = 'INTEL_DEV_I219_V4'
    '0x15BD' = 'INTEL_DEV_I219_LM5'
    '0x15BE' = 'INTEL_DEV_I219_V5'
}
Fix-File 'src\tsn_config.c' $tsn_replacements

# === avb_integration_fixed.c ===
$int_replacements = [ordered]@{
    # PCI Device IDs (in switch at bottom)
    '0x1533' = 'INTEL_DEV_I210_AT'
    '0x1534' = 'INTEL_DEV_I210_AT2'
    '0x1535' = 'INTEL_DEV_I210_FIBER'
    '0x1536' = 'INTEL_DEV_I210_IS'
    '0x1537' = 'INTEL_DEV_I210_IT'
    '0x1538' = 'INTEL_DEV_I210_CS'
    '0x153A' = 'INTEL_DEV_I217_LM'
    '0x153B' = 'INTEL_DEV_I217_V'
    '0x15B7' = 'INTEL_DEV_I219_LM'
    '0x15B8' = 'INTEL_DEV_I219_V'
    '0x15D6' = 'INTEL_DEV_I219_D0'
    '0x15D7' = 'INTEL_DEV_I219_D1'
    '0x15D8' = 'INTEL_DEV_I219_D2'
    '0x0DC7' = 'INTEL_DEV_I219_LM_DC7'
    '0x1570' = 'INTEL_DEV_I219_V6'
    '0x15E3' = 'INTEL_DEV_I219_LM6'
    '0x15F2' = 'INTEL_DEV_I225_V'
    '0x125B' = 'INTEL_DEV_I226_LM'
    # Register offsets (direct MMIO calls)
    ', 0xB644,' = ', INTEL_REG_TRGTTIML0,'
    ', 0xB648,' = ', INTEL_REG_TRGTTIMH0,'
    ', 0xB64C,' = ', INTEL_REG_TRGTTIML1,'
    ', 0xB650,' = ', INTEL_REG_TRGTTIMH1,'
    ', 0xB66C,' = ', INTEL_REG_TSICR,'
    ', 0xB640,' = ', INTEL_REG_TSAUXC,'
    ', 0xB674,' = ', INTEL_REG_TSIM,'
    '? 0xB644 :' = '? INTEL_REG_TRGTTIML0 :'
    '? 0xB648 :' = '? INTEL_REG_TRGTTIMH0 :'
    ': 0xB64C;' = ': INTEL_REG_TRGTTIML1;'
    ': 0xB650;' = ': INTEL_REG_TRGTTIMH1;'
    # RXPBSIZE register
    ', 0x2404,' = ', INTEL_REG_RXPBSIZE,'
    # SRRCTL base
    '0x0C00C +' = 'INTEL_REG_SRRCTL0 +'
    # 32-bit mask / invalid register sentinel
    'ctrl = 0xFFFFFFFF' = 'ctrl = INTEL_MASK_32BIT'
    'ctrl == 0xFFFFFFFF' = 'ctrl == INTEL_MASK_32BIT'
    'ctrl != 0xFFFFFFFF' = 'ctrl != INTEL_MASK_32BIT'
    'ring_id == 0xFFFFFFFF' = 'ring_id == INTEL_MASK_32BIT'
    # VLAN ID 0xFFFF sentinels (subscription)
    '0xFFFF,  // No VLAN (TX timestamps' = 'INTEL_MASK_16BIT,  // No VLAN (TX timestamps'
    'vlan_filter != 0xFFFF' = 'vlan_filter != INTEL_MASK_16BIT'
    '0xFFFF,  /* vlan_id - not applicable */' = 'INTEL_MASK_16BIT,  /* vlan_id - not applicable */'
    # TSAUXC bit masks (local const ULONG assignments)
    'BIT31_DISABLE_SYSTIM0 = 0x80000000;' = 'BIT31_DISABLE_SYSTIM0 = INTEL_TSAUXC_DISABLE_SYSTIM;'
    'BIT29_DISABLE_SYSTIM3 = 0x20000000;' = 'BIT29_DISABLE_SYSTIM3 = INTEL_TSAUXC_DISABLE_SYSTIM3;'
    'BIT28_DISABLE_SYSTIM2 = 0x10000000;' = 'BIT28_DISABLE_SYSTIM2 = INTEL_TSAUXC_DISABLE_SYSTIM2;'
    'BIT27_DISABLE_SYSTIM1 = 0x08000000;' = 'BIT27_DISABLE_SYSTIM1 = INTEL_TSAUXC_DISABLE_SYSTIM1;'
    # TSAUXC bit31 check / clear at step 0
    'tsauxc_value & 0x80000000' = 'tsauxc_value & INTEL_TSAUXC_DISABLE_SYSTIM'
    '&= 0x7FFFFFFF;  // Clear bit 31' = '&= INTEL_TSYNC_TS_MASK;  // Clear bit 31'
    # TIMINCA default
    '= 0x18000000;  // Standard value for 25MHz clock' = '= INTEL_TIMINCA_DEFAULT;  // Standard value for 25MHz clock'
    # cfg tsauxc check in clock config
    '(cfg->tsauxc & 0x80000000)' = '(cfg->tsauxc & INTEL_TSAUXC_DISABLE_SYSTIM)'
    # TIMINCA sub-ns mask
    '& 0xFFFFFF);' = '& INTEL_TIMINCA_SUBNS_MASK);'
    # MDIO page/reg 16-bit masks
    '(mdio->page & 0xFFFF),' = '(mdio->page & INTEL_MASK_16BIT),'
    '(mdio->reg  & 0xFFFF),' = '(mdio->reg  & INTEL_MASK_16BIT),'
    '(mdio->page  & 0xFFFF),' = '(mdio->page  & INTEL_MASK_16BIT),'
    '(mdio->reg   & 0xFFFF),' = '(mdio->reg   & INTEL_MASK_16BIT),'
    # Debug DEADBEEF ring_id sentinel
    'ring_id == 0xDEADBEEF' = 'ring_id == INTEL_SENTINEL_DEAD_BEEF'
}
Fix-File 'src\avb_integration_fixed.c' $int_replacements

Write-Host "All src/ files processed."

# ============================================================
# Devices files: intel_i210_impl.c, intel_i217_impl.c, intel_i219_impl.c
# Common IGB-family patterns (TSYNCRXCTL/TSYNCTXCTL local vars, bit masks)
# ============================================================
$igb_common = [ordered]@{
    # Local const variable initializers (enable_packet_timestamping)
    'TSYNCRXCTL = 0x0B344;' = 'TSYNCRXCTL = INTEL_REG_TSYNCTXCTL;'
    'TSYNCTXCTL = 0x0B348;' = 'TSYNCTXCTL = INTEL_REG_TSYNCRXCTL;'
    'RXTT_ENABLE = 0x80000000;  // Bit 31' = 'RXTT_ENABLE = INTEL_TSYNC_VALID;  // Bit 31'
    'TXTT_ENABLE = 0x80000000;  // Bit 31' = 'TXTT_ENABLE = INTEL_TSYNC_VALID;  // Bit 31'
    'TYPE_ALL = 0x0E000000;     // Bits 27-25' = 'TYPE_ALL = INTEL_TIMINCA_INCPERIOD;     // Bits 27-25'
    'TYPE_MASK = 0x0E000000;' = 'TYPE_MASK = INTEL_TIMINCA_INCPERIOD;'
    # Timestamp valid bit checks
    'txstmph_val & 0x80000000' = 'txstmph_val & INTEL_TSYNC_VALID'
    'txstmph_val & 0x7FFFFFFF' = 'txstmph_val & INTEL_TSYNC_TS_MASK'
    # 32-bit systime extraction
    'systime & 0xFFFFFFFF' = 'systime & INTEL_MASK_32BIT'
    '(systime >> 32) & 0xFFFFFFFF' = '(systime >> 32) & INTEL_MASK_32BIT'
}

foreach ($f in @('devices\intel_i210_impl.c', 'devices\intel_i217_impl.c', 'devices\intel_i219_impl.c')) {
    Fix-File $f $igb_common
}

# i210-specific: direct MMIO register offset literals + tsauxc bit clear
$i210_specific = [ordered]@{
    'dev, 0x0B600, ts_low);   // SYSTIML' = 'dev, INTEL_REG_SYSTIML, ts_low);   // SYSTIML'
    'dev, 0x0B604, ts_high);  // SYSTIMH' = 'dev, INTEL_REG_SYSTIMH, ts_high);  // SYSTIMH'
    'dev, 0x0B600, &ts_low);   // SYSTIML' = 'dev, INTEL_REG_SYSTIML, &ts_low);   // SYSTIML'
    'dev, 0x0B604, &ts_high);  // SYSTIMH' = 'dev, INTEL_REG_SYSTIMH, &ts_high);  // SYSTIMH'
    'dev, 0x0B640, &tsauxc);  // TSAUXC' = 'dev, INTEL_REG_TSAUXC, &tsauxc);  // TSAUXC'
    'dev, 0x0B640, tsauxc);' = 'dev, INTEL_REG_TSAUXC, tsauxc);'
    'tsauxc &= ~0x80000000;' = 'tsauxc &= ~INTEL_TSAUXC_DISABLE_SYSTIM;'
}
Fix-File 'devices\intel_i210_impl.c' $i210_specific

# i217-specific: TIMINCA init value
$i217_specific = [ordered]@{
    'timinca = 0x08000001; // Basic 8ns increment for I217' = 'timinca = INTEL_TIMINCA_I217_INIT; // Basic 8ns increment for I217'
}
Fix-File 'devices\intel_i217_impl.c' $i217_specific

# ============================================================
# Devices: intel_82580_impl.c and intel_i350_impl.c
# Same IGB common patterns + device-specific TIMINCA
# ============================================================
$legacy_common = [ordered]@{
    'TSYNCRXCTL = 0x0B344;' = 'TSYNCRXCTL = INTEL_REG_TSYNCTXCTL;'
    'TSYNCTXCTL = 0x0B348;' = 'TSYNCTXCTL = INTEL_REG_TSYNCRXCTL;'
    'RXTT_ENABLE = 0x80000000;  // Bit 31' = 'RXTT_ENABLE = INTEL_TSYNC_VALID;  // Bit 31'
    'TXTT_ENABLE = 0x80000000;  // Bit 31' = 'TXTT_ENABLE = INTEL_TSYNC_VALID;  // Bit 31'
    'TYPE_ALL = 0x0E000000;     // Bits 27-25' = 'TYPE_ALL = INTEL_TIMINCA_INCPERIOD;     // Bits 27-25'
    'TYPE_MASK = 0x0E000000;' = 'TYPE_MASK = INTEL_TIMINCA_INCPERIOD;'
    'txstmph_val & 0x80000000' = 'txstmph_val & INTEL_TSYNC_VALID'
    'txstmph_val & 0x7FFFFFFF' = 'txstmph_val & INTEL_TSYNC_TS_MASK'
    'systime & 0xFFFFFFFF' = 'systime & INTEL_MASK_32BIT'
    '(systime >> 32) & 0xFFFFFFFF' = '(systime >> 32) & INTEL_MASK_32BIT'
}
Fix-File 'devices\intel_82580_impl.c' $legacy_common
Fix-File 'devices\intel_i350_impl.c'  $legacy_common

$f82 = 'devices\intel_82580_impl.c'
$c82 = Get-Content $f82 -Raw -Encoding UTF8
$c82 = $c82.Replace('timinca = 0x80000006;  // Enhanced 6ns increment for 82580', 'timinca = INTEL_TIMINCA_82580_INIT;  // Enhanced 6ns increment for 82580')
Set-Content $f82 -Value $c82 -Encoding UTF8 -NoNewline

$f35 = 'devices\intel_i350_impl.c'
$c35 = Get-Content $f35 -Raw -Encoding UTF8
$c35 = $c35.Replace('timinca = 0x80000008;  // 8ns increment for I350', 'timinca = INTEL_TIMINCA_I350_INIT;  // 8ns increment for I350')
Set-Content $f35 -Value $c35 -Encoding UTF8 -NoNewline

# ============================================================
# Device: intel_i226_impl.c
# ============================================================
$i226_specific = [ordered]@{
    'timinca = 0x18000000;  // 24ns per cycle (I226 default)' = 'timinca = INTEL_TIMINCA_DEFAULT;  // 24ns per cycle (I226 default)'
    'target_time_ns & 0xFFFFFFFF' = 'target_time_ns & INTEL_MASK_32BIT'
    'PTP EtherType=0x88F7)' = 'PTP EtherType=ETH_P_1588)'
}
Fix-File 'devices\intel_i226_impl.c' $i226_specific

Write-Host "All devices/ files processed."
Write-Host "SSOT migration complete."
