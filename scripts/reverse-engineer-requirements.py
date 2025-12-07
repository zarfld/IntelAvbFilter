#!/usr/bin/env python3
"""
Intel AVB Filter Driver - Code to Requirements Reverse Engineering

Analyzes existing C/C++ kernel-mode driver code to extract:
- Functional requirements from IOCTL handlers
- Device-specific implementations
- Test specifications from test tools
- Architecture decisions from implementation patterns

Standards: ISO/IEC/IEEE 29148:2018 (Requirements Engineering)
"""

import os
import re
from pathlib import Path
from typing import List, Dict, Tuple
from datetime import datetime

# Supported controller families from codebase
INTEL_CONTROLLERS = ['i210', 'i217', 'i219', 'i225', 'i226', 'i350', '82575', '82576', '82580']

def analyze_ioctl_handler(file_path: Path) -> List[Dict]:
    """
    Extract functional requirements from IOCTL handler code
    
    Returns requirement data for GitHub Issue creation
    """
    content = file_path.read_text(encoding='utf-8', errors='ignore')
    requirements = []
    
    # Find IOCTL case statements
    ioctl_pattern = r'case\s+(IOCTL_AVB_\w+):\s*{([^}]+)}'
    matches = re.finditer(ioctl_pattern, content, re.DOTALL)
    
    for match in matches:
        ioctl_code = match.group(1)
        ioctl_body = match.group(2)
        
        # Extract validation checks
        validations = []
        if 'NULL' in ioctl_body and '==' in ioctl_body:
            validations.append('NULL pointer validation')
        if 'sizeof' in ioctl_body:
            validations.append('Buffer size validation')
        if 'STATUS_' in ioctl_body or 'ERROR_' in ioctl_body:
            validations.append('Error code handling')
        
        # Extract hardware operations
        hw_ops = []
        if 'MMIO' in ioctl_body or 'MmMapIoSpace' in ioctl_body:
            hw_ops.append('MMIO register access')
        if 'MDIO' in ioctl_body:
            hw_ops.append('MDIO PHY access')
        if 'SYSTIM' in ioctl_body or 'PTP' in ioctl_body:
            hw_ops.append('PTP clock operations')
        
        requirements.append({
            'ioctl_code': ioctl_code,
            'file': str(file_path),
            'validations': validations,
            'hw_operations': hw_ops,
            'priority': 'priority:p1'  # Default priority
        })
    
    return requirements

def analyze_device_implementation(file_path: Path) -> Dict:
    """
    Extract device-specific implementation details
    
    Returns device capability data
    """
    content = file_path.read_text(encoding='utf-8', errors='ignore')
    
    # Detect controller family from filename
    controller = None
    for ctrl in INTEL_CONTROLLERS:
        if ctrl in file_path.stem.lower():
            controller = ctrl.upper()
            break
    
    if not controller:
        return None
    
    # Find supported features
    features = {
        'ptp': 'SYSTIM' in content or 'ptp_init' in content,
        'tas': 'TAS' in content or 'TQAVCTRL' in content,
        'cbs': 'CBS' in content or 'QAV' in content,
        'frame_preemption': 'FP' in content or 'preempt' in content.lower(),
        'mdio': 'MDIO' in content or 'mdio_read' in content
    }
    
    # Find initialization sequence
    init_found = 'init(' in content or 'init_ptp' in content
    cleanup_found = 'cleanup(' in content
    
    return {
        'controller': controller,
        'file': str(file_path),
        'features': features,
        'has_init': init_found,
        'has_cleanup': cleanup_found
    }

def analyze_test_tool(file_path: Path) -> Dict:
    """
    Extract test scenarios from user-mode test tools
    
    Returns test data for TEST issue creation
    """
    content = file_path.read_text(encoding='utf-8', errors='ignore')
    
    # Find test name
    test_name = file_path.stem
    
    # Find IOCTLs being tested
    ioctl_pattern = r'DeviceIoControl\([^,]+,\s*(IOCTL_AVB_\w+)'
    tested_ioctls = re.findall(ioctl_pattern, content)
    
    # Find test scenarios (functions with "test" in name)
    test_func_pattern = r'(?:void|int|NTSTATUS)\s+(\w*[Tt]est\w*)\s*\('
    test_functions = re.findall(test_func_pattern, content)
    
    # Find register access patterns
    has_register_reads = 'RegReq.offset' in content or 'AVB_REGISTER_REQUEST' in content
    has_clock_tests = 'IOCTL_AVB_GET_TIMESTAMP' in content or 'SYSTIM' in content
    has_tas_tests = 'IOCTL_AVB_SETUP_TAS' in content
    
    return {
        'test_name': test_name,
        'file': str(file_path),
        'tested_ioctls': list(set(tested_ioctls)),
        'test_functions': test_functions,
        'test_types': {
            'register_access': has_register_reads,
            'clock': has_clock_tests,
            'tas': has_tas_tests
        }
    }

def generate_functional_req_issue_body(req_data: Dict, ioctl_defines: Dict) -> str:
    """Generate GitHub Issue body for functional requirement from IOCTL handler"""
    
    ioctl_code = req_data['ioctl_code']
    ioctl_value = ioctl_defines.get(ioctl_code, '(undefined)')
    
    validations_md = '\n'.join(f"- {v}" for v in req_data['validations']) if req_data['validations'] else "- No validations detected"
    hw_ops_md = '\n'.join(f"- {op}" for op in req_data['hw_operations']) if req_data['hw_operations'] else "- No hardware operations detected"
    
    # Generate requirement ID
    ioctl_short = ioctl_code.replace('IOCTL_AVB_', '').replace('_', '-')
    req_id = f"REQ-F-IOCTL-{ioctl_short}-001"
    
    issue_body = f"""## Description
IOCTL handler for {ioctl_code} operations

**Source**: Reverse-engineered from `{req_data['file']}`  
**IOCTL Code**: `{ioctl_value}`

## Functional Requirements

### {req_id}: {ioctl_code} Handler

**Input**: User-mode IOCTL request via `DeviceIoControl`

**Validation Rules**:
{validations_md}

**Hardware Operations**:
{hw_ops_md}

**Output**: 
- Success: `STATUS_SUCCESS` / `ERROR_SUCCESS`
- Failure: Appropriate error code (`STATUS_INVALID_PARAMETER`, `ERROR_INVALID_FUNCTION`, etc.)

## Acceptance Criteria

```gherkin
Scenario: Successful {ioctl_code} request
  Given driver is loaded and device is initialized
  And user-mode application has valid device handle
  When user sends {ioctl_code} request with valid parameters
  Then request is processed successfully
  And appropriate hardware operations are performed
  And success status is returned

Scenario: Invalid input handling
  Given user sends {ioctl_code} with invalid parameters
  Then request is rejected with clear error code
  And no hardware operations are performed
  And system remains stable

Scenario: Unsupported controller
  Given {ioctl_code} is not supported on detected controller
  Then ERROR_INVALID_FUNCTION is returned
  And clear error message indicates feature not supported
```

## Implementation

**Current File**: `{req_data['file']}`

**Traceability Comment** (add to code):
```c
/**
 * Implements: #N ({req_id}: {ioctl_code})
 * 
 * TODO: Create GitHub Issue for this requirement
 * Issue body generated by reverse-engineering script
 */
case {ioctl_code}:
```

## Validation Required

⚠️ **This requirement was reverse-engineered from code and needs validation:**
- [ ] Verify input validation is complete (all edge cases covered)
- [ ] Confirm hardware operations are safe and correct
- [ ] Check error handling is comprehensive
- [ ] Validate behavior on all supported controllers (I210, I219, I225, I226)
- [ ] Review with stakeholders

## Traceability
- **Traces to**: StR-HWAC-001 (Intel NIC AVB/TSN Feature Access)
- **Depends on**: (TBD - device initialization requirements)
- **Verified by**: (TBD - create TEST issue for this IOCTL)

## Priority
{req_data['priority']}
"""
    
    return issue_body

def generate_device_capability_issue_body(dev_data: Dict) -> str:
    """Generate GitHub Issue body for device-specific capability requirement"""
    
    controller = dev_data['controller']
    req_id = f"REQ-F-DEV-{controller}-001"
    
    features_md = ""
    for feature, supported in dev_data['features'].items():
        status = "✅ Supported" if supported else "❌ Not Detected"
        features_md += f"- **{feature.upper()}**: {status}\n"
    
    issue_body = f"""## Description
Device-specific implementation for Intel {controller} Ethernet Controller

**Source**: Reverse-engineered from `{dev_data['file']}`

## Functional Requirements

### {req_id}: {controller} Hardware Support

The driver shall provide device-specific support for Intel {controller} controllers.

**Feature Support**:
{features_md}

**Lifecycle Operations**:
- **Initialization**: {"✅ Implemented" if dev_data['has_init'] else "❌ Missing"}
- **Cleanup**: {"✅ Implemented" if dev_data['has_cleanup'] else "❌ Missing"}

## Acceptance Criteria

```gherkin
Scenario: {controller} detection and initialization
  Given system has Intel {controller} adapter installed
  When driver loads
  Then adapter is detected as supported device
  And device-specific initialization completes successfully
  And feature capabilities are accurately reported

Scenario: Feature availability reporting
  Given {controller} is initialized
  When user queries device capabilities
  Then accurate feature support is reported
  And unsupported features return clear error codes
```

## Implementation

**Current File**: `{dev_data['file']}`

**Operations Table**:
| Operation | Function | Status |
|-----------|----------|--------|
| Initialize | `init(device_t *dev)` | {'✅' if dev_data['has_init'] else '⚠️ Needs Review'} |
| Cleanup | `cleanup(device_t *dev)` | {'✅' if dev_data['has_cleanup'] else '⚠️ Needs Review'} |
| PTP Init | `init_ptp(device_t *dev)` | {'✅' if dev_data['features']['ptp'] else 'N/A'} |

## Validation Required

⚠️ **This requirement was reverse-engineered from code and needs validation:**
- [ ] Verify feature support on actual {controller} hardware
- [ ] Test initialization sequence (success and failure paths)
- [ ] Validate cleanup prevents resource leaks
- [ ] Test on multiple {controller} hardware variants
- [ ] Document any hardware-specific quirks discovered

## Traceability
- **Traces to**: StR-HWAC-002 (Reliable Hardware Detection)
- **Depends on**: REQ-F-DEV-001 (Generic Device Discovery)
- **Verified by**: (TBD - create TEST issue for {controller} hardware validation)

## Priority
priority:p1
"""
    
    return issue_body

def generate_test_issue_body(test_data: Dict) -> str:
    """Generate TEST issue body from user-mode test tool"""
    
    test_name = test_data['test_name']
    test_id = f"TEST-{test_name.upper().replace('_', '-')}-001"
    
    ioctls_md = '\n'.join(f"- `{ioctl}`" for ioctl in test_data['tested_ioctls']) if test_data['tested_ioctls'] else "- (No IOCTLs detected)"
    funcs_md = '\n'.join(f"- `{func}()`" for func in test_data['test_functions'][:10]) if test_data['test_functions'] else "- (No test functions detected)"
    
    test_types_md = ""
    for test_type, present in test_data['test_types'].items():
        status = "✅ Present" if present else "❌ Not Present"
        test_types_md += f"- **{test_type.replace('_', ' ').title()}**: {status}\n"
    
    issue_body = f"""## Description
User-mode test tool for validating driver functionality

**Source**: `{test_data['file']}`

## Test Coverage

### IOCTLs Tested
{ioctls_md}

### Test Functions
{funcs_md}
{f"... and {len(test_data['test_functions']) - 10} more" if len(test_data['test_functions']) > 10 else ""}

### Test Types
{test_types_md}

## Verifies Requirements
- **TBD**: Link to requirement issues being verified
- Suggested: REQ-F-IOCTL-* for IOCTL handlers tested

## Test Scenarios

Based on code analysis, this test tool appears to cover:

1. **Device Access**: Opening device handle to `\\\\.\IntelAvbFilter`
2. **IOCTL Communication**: Sending IOCTL requests and validating responses
3. **Hardware Validation**: {'Register read/write operations' if test_data['test_types']['register_access'] else 'Basic functionality'}

## Test Implementation

**Test File**: `{test_data['file']}`

**Traceability**: Add to test file header:
```c
/**
 * {test_id}: {test_name} Test Suite
 * 
 * Verifies: (add requirement issue numbers)
 * - REQ-F-IOCTL-* (IOCTL handlers)
 * - REQ-F-DEV-* (Device-specific functionality)
 */
```

## Test Execution

**Build**:
```powershell
# Use existing build scripts from project
.\\tools\\vs_compile.ps1 -BuildCmd "cl /nologo /W4 /Zi {test_data['file']} /Fe:{test_name}.exe"
```

**Run**:
```powershell
.\\{test_name}.exe
```

**Expected Output**: (TBD - document expected success criteria)

## Coverage Gaps

⚠️ **Potential gaps to address:**
- [ ] Add negative test cases (invalid inputs, error paths)
- [ ] Test on all supported controllers (I210, I219, I225, I226)
- [ ] Add performance/stress testing scenarios
- [ ] Validate error code handling
- [ ] Add traceability comments linking to requirements

## Validation Required

- [ ] Run test on actual hardware
- [ ] Verify all assertions pass
- [ ] Document expected vs. actual results
- [ ] Link to requirement issues being verified

## Priority
priority:p1
"""
    
    return issue_body

def extract_ioctl_definitions(file_path: Path) -> Dict[str, str]:
    """Extract IOCTL code definitions from header file"""
    content = file_path.read_text(encoding='utf-8', errors='ignore')
    
    # Find #define IOCTL_AVB_* _NDIS_CONTROL_CODE(...)
    pattern = r'#define\s+(IOCTL_AVB_\w+)\s+_NDIS_CONTROL_CODE\((\d+),\s*METHOD_BUFFERED\)'
    matches = re.findall(pattern, content)
    
    return {name: f"0x{int(code):08X}" for name, code in matches}

def scan_driver_codebase(base_dir: str = '.'):
    """
    Scan Intel AVB Filter Driver codebase for requirements
    """
    base_path = Path(base_dir)
    
    print("Intel AVB Filter Driver - Code-to-Requirements Analysis")
    print("=" * 70)
    print()
    
    # Extract IOCTL definitions
    print("[1/4] Extracting IOCTL definitions...")
    ioctl_header = base_path / 'include' / 'avb_ioctl.h'
    ioctl_defines = {}
    if ioctl_header.exists():
        ioctl_defines = extract_ioctl_definitions(ioctl_header)
        print(f"    Found {len(ioctl_defines)} IOCTL definitions")
    
    # Scan for IOCTL handlers
    print("[2/4] Scanning IOCTL handlers...")
    ioctl_files = list(base_path.glob('device.c')) + list(base_path.glob('filter.c'))
    requirements = []
    for file_path in ioctl_files:
        reqs = analyze_ioctl_handler(file_path)
        requirements.extend(reqs)
    print(f"    Found {len(requirements)} IOCTL handlers")
    
    # Scan device implementations
    print("[3/4] Scanning device-specific implementations...")
    device_dir = base_path / 'devices'
    device_impls = []
    if device_dir.exists():
        impl_files = list(device_dir.glob('intel_*_impl.c'))
        for file_path in impl_files:
            dev_data = analyze_device_implementation(file_path)
            if dev_data:
                device_impls.append(dev_data)
    print(f"    Found {len(device_impls)} device implementations")
    
    # Scan test tools
    print("[4/4] Scanning test tools...")
    test_files = (list(base_path.glob('avb_test_*.c')) + 
                  list(base_path.glob('tools/avb_test/*.c')) +
                  list(base_path.glob('*_test.c')))
    tests = []
    for file_path in test_files:
        test_data = analyze_test_tool(file_path)
        if test_data:
            tests.append(test_data)
    print(f"    Found {len(tests)} test tools")
    
    print()
    return requirements, device_impls, tests, ioctl_defines

def generate_report(requirements: List[Dict], device_impls: List[Dict], 
                   tests: List[Dict], ioctl_defines: Dict):
    """Generate markdown report with GitHub Issue bodies"""
    
    report = f"""# Intel AVB Filter Driver - Reverse Engineering Report

**Date**: {datetime.now().strftime('%Y-%m-%d')}  
**Purpose**: Extract functional requirements from existing implementation  
**Standards**: ISO/IEC/IEEE 29148:2018 (Requirements Engineering)

---

## Executive Summary

**Functional Requirements Found**: {len(requirements)} IOCTL handlers  
**Device Implementations Found**: {len(device_impls)} Intel controllers  
**Test Tools Found**: {len(tests)}

This report contains GitHub Issue bodies ready for creation. Each issue represents a functional requirement reverse-engineered from code.

⚠️ **Validation Required**: All requirements must be validated with stakeholders and tested on actual hardware before being considered authoritative.

---

## 📋 Section 1: IOCTL Handler Requirements

The following functional requirements were extracted from IOCTL handler code:

"""
    
    for i, req in enumerate(requirements):
        issue_body = generate_functional_req_issue_body(req, ioctl_defines)
        ioctl_short = req['ioctl_code'].replace('IOCTL_AVB_', '').replace('_', '-')
        
        report += f"""### {i+1}. {req['ioctl_code']}

**Title**: `REQ-F-IOCTL-{ioctl_short}-001: {req['ioctl_code']} Handler`  
**Labels**: `type:requirement:functional`, `phase:02-requirements`, `priority:p1`  
**Source File**: `{req['file']}`

**Body**:
```markdown
{issue_body}
```

**Create Issue Command**:
```bash
gh issue create \\
  --label "type:requirement:functional,phase:02-requirements,priority:p1" \\
  --title "REQ-F-IOCTL-{ioctl_short}-001: {req['ioctl_code']} Handler" \\
  --body-file issue-bodies/req-ioctl-{i+1}.md
```

---

"""
    
    report += "\n## 🖥️ Section 2: Device-Specific Requirements\n\n"
    
    for i, dev in enumerate(device_impls):
        issue_body = generate_device_capability_issue_body(dev)
        
        report += f"""### {i+1}. Intel {dev['controller']} Support

**Title**: `REQ-F-DEV-{dev['controller']}-001: {dev['controller']} Hardware Support`  
**Labels**: `type:requirement:functional`, `phase:02-requirements`, `priority:p1`, `controller:{dev['controller'].lower()}`  
**Source File**: `{dev['file']}`

**Body**:
```markdown
{issue_body}
```

**Create Issue Command**:
```bash
gh issue create \\
  --label "type:requirement:functional,phase:02-requirements,priority:p1,controller:{dev['controller'].lower()}" \\
  --title "REQ-F-DEV-{dev['controller']}-001: {dev['controller']} Hardware Support" \\
  --body-file issue-bodies/req-dev-{i+1}.md
```

---

"""
    
    report += "\n## 🧪 Section 3: Test Coverage\n\n"
    
    for i, test in enumerate(tests):
        issue_body = generate_test_issue_body(test)
        test_id = test['test_name'].upper().replace('_', '-')
        
        report += f"""### {i+1}. {test['test_name']} Test Suite

**Title**: `TEST-{test_id}-001: {test['test_name']} Validation`  
**Labels**: `type:test`, `phase:07-verification-validation`, `priority:p1`  
**Source File**: `{test['file']}`

**Body**:
```markdown
{issue_body}
```

**Create Issue Command**:
```bash
gh issue create \\
  --label "type:test,phase:07-verification-validation,priority:p1" \\
  --title "TEST-{test_id}-001: {test['test_name']} Validation" \\
  --body-file issue-bodies/test-{i+1}.md
```

---

"""
    
    report += f"""
## 📊 Summary Statistics

| Category | Count | Status |
|----------|-------|--------|
| IOCTL Handlers | {len(requirements)} | Ready for GitHub Issue creation |
| Device Implementations | {len(device_impls)} | Ready for GitHub Issue creation |
| Test Tools | {len(tests)} | Ready for GitHub Issue creation |
| **Total Issues to Create** | **{len(requirements) + len(device_impls) + len(tests)}** | - |

---

## 🚀 Next Steps

### 1. Review Generated Issues
- Validate each requirement with project owner
- Verify requirements match stakeholder needs (see `01-stakeholder-requirements/STAKEHOLDER-REQUIREMENTS.md`)
- Add missing edge cases or scenarios

### 2. Create GitHub Issues
```bash
# Create all requirements at once (bulk creation)
cd issue-bodies
for file in req-*.md; do
  gh issue create --label "type:requirement:functional,phase:02-requirements,priority:p1" \\
    --title "$(head -n1 $file | sed 's/## Description//')" \\
    --body-file "$file"
done
```

### 3. Add Traceability to Code
- Add `Implements: #N (REQ-F-*)` comments to IOCTL handlers
- Add `Verifies: #N` comments to test files
- Link issues bidirectionally (parent/child relationships)

### 4. Validate on Hardware
- Run tests on all supported controllers (I210, I219, I225, I226)
- Document actual vs. expected behavior
- Update requirement status (Working / Experimental / Broken)

---

## ⚠️ Important Reminders

1. **Not Authoritative**: These requirements are reverse-engineered from code and may not represent correct or intended behavior
2. **Validation Required**: All requirements must be validated with stakeholders before being treated as truth
3. **Hardware Testing**: Code analysis alone cannot prove features work on hardware
4. **Traceability**: Link all requirements to stakeholder requirements (StR-*) in Phase 01

---

## 📚 Related Documents

- **Project Charter**: `01-stakeholder-requirements/PROJECT-CHARTER.md`
- **Stakeholder Requirements**: `01-stakeholder-requirements/STAKEHOLDER-REQUIREMENTS.md`
- **System Requirements**: `02-requirements/` (next phase)
- **GitHub Issue Templates**: `.github/ISSUE_TEMPLATE/`

---

*Generated by code-to-requirements reverse engineering script*  
*Standards: ISO/IEC/IEEE 29148:2018*
"""
    
    return report

if __name__ == '__main__':
    import sys
    
    base_dir = sys.argv[1] if len(sys.argv) > 1 else '.'
    
    print(f"Analyzing codebase in: {base_dir}")
    print()
    
    requirements, device_impls, tests, ioctl_defines = scan_driver_codebase(base_dir)
    
    print()
    print("Generating report...")
    report = generate_report(requirements, device_impls, tests, ioctl_defines)
    
    # Save report
    report_file = Path(base_dir) / 'reverse-engineering-report.md'
    report_file.write_text(report, encoding='utf-8')
    
    print(f"✅ Report generated: {report_file}")
    print()
    print("Next steps:")
    print("1. Review report: reverse-engineering-report.md")
    print("2. Validate requirements with stakeholders")
    print("3. Create GitHub Issues using provided commands")
    print("4. Add traceability comments to code files")
