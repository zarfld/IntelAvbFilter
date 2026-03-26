# Issue #207 - CORRUPTED CONTENT BACKUP (2025-12-30)

**Issue URL**: https://github.com/zarfld/IntelAvbFilter/issues/207  
**Title**: [TEST] TEST-CBS-CONFIG-001: Credit-Based Shaper Configuration Verification  
**Status**: Open (Corrupted with AVDECC content)  
**Created**: 2025-12-19T10:44:17Z  
**Last Updated**: 2025-12-22T02:45:26Z

---

## ⚠️ CURRENT CORRUPTED CONTENT (AVDECC Discovery Test - WRONG!)

## Test Description

Verify AVDECC entity discovery cross-vendor.

## Test Objectives

- Test discovery with multiple vendors
- Verify entity enumeration
- Validate capability reporting

## Preconditions

- Multi-vendor AVDECC network
- Test entities from 3+ vendors

## Test Steps

1. Enable AVDECC discovery
2. Enumerate all entities
3. Verify each vendor's entities found
4. Check capability reporting accuracy

## Expected Results

- All vendor entities discovered
- Capabilities correctly reported
- Discovery time <5 seconds

## Acceptance Criteria

- ✅ 3+ vendor entities discovered
- ✅ Capabilities validated
- ✅ Discovery performance met

## Test Type

- **Type**: Compatibility, Integration
- **Priority**: P1
- **Automation**: Automated

## Traceability

Traces to: #233 (TEST-PLAN-001)
Traces to: #60 (REQ-NF-COMPATIBILITY-001)

---

## 🔍 ANALYSIS

**Problem**: Issue #207 title says "Credit-Based Shaper Configuration" but content is about AVDECC entity discovery (completely unrelated topic).

**Root Cause**: Content was overwritten during recent edit (last update 2025-12-22T02:45:26Z).

**Expected Content**: Should contain comprehensive CBS (IEEE 802.1Qav) test specification with:
- 10 unit tests for CBS algorithm
- 3 integration tests
- 2 V&V tests
- CBS register configuration (i225)
- Performance targets and acceptance criteria

**Action Required**: Restore original CBS test specification from diff provided by user.

---

## 📝 LABELS (Current)

- `priority:p1` - High priority - should have
- `status:backlog` - Issue in backlog, not yet prioritized
- `phase:07-verification-validation` - Phase 07: Verification & Validation
- `type:test-case` - Test case (TEST) - verification and validation
- `test-type:compatibility` - Compatibility testing

**Expected Labels**: Should include `feature:qav`, `test-type:functional`, `test-type:integration` for CBS testing.

---

## 🔗 COMMENTS (8 total)

Issue has 8 comments - may contain original CBS specification or discussion about the corruption.

---

**Backup Created**: 2025-12-30  
**Next Steps**: Restore original CBS test specification using the diff provided by user.
