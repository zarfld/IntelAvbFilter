# ISSUE-223-CORRUPTED-BACKUP.md

**Purpose**: Backup of corrupted issue #223 content before restoration (32nd corruption in continuous range #192-223).

**Corruption Date**: 2025-12-22 (batch update failure)

**Analysis**: 
- **Title**: Correct - "TEST-EEE-001: Energy Efficient Ethernet (IEEE 802.3az) Verification" (assumed)
- **Body**: COMPLETELY WRONG - Generic Secure Boot compatibility test instead of comprehensive IEEE 802.3az Energy Efficient Ethernet verification
- **Expected**: 15 test cases (10 unit + 3 integration + 2 V&V) covering EEE capability advertisement (TLV in LLDPDU supported=1 enabled=1), LPI mode negotiation (autoneg exchange, EEE capability bits register 7.60), Tx LPI entry (<10µs after 200µs idle), Tx LPI exit (<30µs for 1000BASE-T refresh signal), Rx LPI detection (symbols, statistics), wake time configuration (Tw_sys 16.5µs @ 1Gbps, 30µs @ 100Mbps), LPI timer management (entry 200µs, exit track wake time), power measurement API (baseline vs EEE >20% reduction), EEE disable/enable runtime IOCTL, non-EEE interoperability fallback, EEE under variable load (LPI entry/exit <50µs latency penalty), EEE with TSN traffic (Class A/B unaffected best-effort uses LPI), multi-adapter EEE coordination (independent LPI per port), 24-hour power efficiency monitoring (LPI time ≥60% power reduction ≥20% no packet loss), EEE interoperability matrix (EEE and non-EEE switches fallback)
- **Actual**: Simple Secure Boot compatibility test - test driver on Secure Boot systems Win10/Win11, verify signature validation, confirm UEFI compatibility, no warnings/errors
- **Priority**: P0 (Critical) - WRONG, should be P2 (Medium - Power Efficiency)
- **Traceability**: #233 (TEST-PLAN-001), #61 (REQ-NF-SECURITY-001) - WRONG, should be #1 (StR-CORE-001), #58 (REQ-NF-POWER-001: Power Management and Efficiency), #14, #60
- **Missing labels**: test-type:unit, test-type:integration, test-type:v&v, feature:eee, feature:power-management, feature:energy-efficiency, feature:lpi

**Pattern**: 32nd corruption in continuous range #192-223 (same systematic replacement: comprehensive IEEE standard specifications → generic simple tests)

---

## CORRUPTED CONTENT (AS FOUND 2025-12-31):

## Test Description

Verify secure boot compatibility.

## Test Objectives

- Test driver on Secure Boot systems
- Verify signature validation
- Confirm UEFI compatibility

## Preconditions

- Secure Boot enabled systems
- Signed driver binaries

## Test Steps

1. Install driver on Secure Boot Win10
2. Verify signature validation
3. Test on Secure Boot Win11
4. Confirm no warnings/errors

## Expected Results

- Driver loads successfully
- Signature validated correctly
- No Secure Boot violations

## Acceptance Criteria

- ✅ Win10 Secure Boot verified
- ✅ Win11 Secure Boot verified
- ✅ Signature validation passed

## Test Type

- **Type**: Security, Compliance
- **Priority**: P0
- **Automation**: Manual verification

## Traceability

Traces to: #233 (TEST-PLAN-001)
Traces to: #61 (REQ-NF-SECURITY-001)

---

**Recovery Action**: Replace with original comprehensive Energy Efficient Ethernet (IEEE 802.3az) verification specification (15 test cases, LPI mode, power savings ≥20%, wake time 16.5µs @ 1Gbps, P2 Medium - Power Efficiency, correct traceability #1/#58/#14/#60).
