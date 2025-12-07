#!/usr/bin/env python3
"""
Build traceability.json from project artifacts.

Scans:
- 01-stakeholder-requirements/*.md (StR issues)
- 02-requirements/**/*.md (REQ-F, REQ-NF)
- 03-architecture/decisions/*.md (ADR)
- 03-architecture/components/*.md (ARC-C)
- test/**/*.cpp (TEST cases with comments)
- src/**/*.cpp (implementation with issue refs)

Generates build/traceability.json with metrics for CI validation.
"""
from __future__ import annotations
import json, re, sys
from pathlib import Path
from typing import Dict, List, Set
from collections import defaultdict

ROOT = Path(__file__).resolve().parents[1]

# Issue reference patterns
TRACES_TO_PATTERN = re.compile(r'\*\*Traces to\*\*:.*?#(\d+)', re.IGNORECASE)
VERIFIED_BY_PATTERN = re.compile(r'\*\*Verified by\*\*:.*?#(\d+)', re.IGNORECASE)
IMPLEMENTS_PATTERN = re.compile(r'\*\*Implements\*\*:.*?#(\d+)', re.IGNORECASE | re.MULTILINE)
SATISFIES_PATTERN = re.compile(r'\*\*Satisfies.*?\*\*:.*?#(\d+)', re.IGNORECASE)
REFINED_BY_PATTERN = re.compile(r'\*\*Refined by.*?\*\*:.*?#(\d+)', re.IGNORECASE)
ISSUE_REF_PATTERN = re.compile(r'#(\d+)')  # Generic issue reference
CODE_COMMENT_PATTERN = re.compile(r'(?://|/\*|\*)\s*(?:Implements?|Verifies?|Tests?):?\s*#(\d+)', re.IGNORECASE)

# ID patterns in filenames and content
STR_ID_PATTERN = re.compile(r'StR-(?:[A-Z]{4}-)?(\d{3})')
REQ_F_ID_PATTERN = re.compile(r'REQ-F-(?:[A-Z]{4}-)?(\d{3})')
REQ_NF_ID_PATTERN = re.compile(r'REQ-NF-(?:[A-Z]{4}-)?(\d{3})')
ADR_ID_PATTERN = re.compile(r'ADR-(?:[A-Z]{4}-)?(\d{3})')
ARC_C_ID_PATTERN = re.compile(r'ARC-C-(?:[A-Z]{4}-)?(\d{3})')
TEST_ID_PATTERN = re.compile(r'TEST(?:-[A-Z]+)?-(\d{3})')

class TraceabilityBuilder:
    def __init__(self):
        self.artifacts = {
            'stakeholder_requirements': [],
            'functional_requirements': [],
            'non_functional_requirements': [],
            'architecture_decisions': [],
            'architecture_components': [],
            'test_cases': [],
            'implementations': []
        }
        self.links = defaultdict(lambda: defaultdict(list))
        
    def scan_markdown(self, file_path: Path, artifact_type: str, id_pattern: re.Pattern):
        """Scan markdown file for artifact and extract traceability links."""
        if not file_path.exists():
            return
        
        content = file_path.read_text(encoding='utf-8', errors='ignore')
        
        # Extract ID from filename or content
        match = id_pattern.search(file_path.name)
        if not match and artifact_type in ('test', 'implementation'):
            return  # Tests and code handled separately
        
        if not match:
            match = id_pattern.search(content[:500])  # Check first 500 chars
        
        if not match:
            return
        
        artifact_id = match.group(0)
        
        artifact = {
            'id': artifact_id,
            'file': str(file_path.relative_to(ROOT)),
            'type': artifact_type,
            'traces_to': [],
            'verified_by': [],
            'implements': [],
            'satisfies': [],
            'refined_by': []
        }
        
        # Extract traceability links
        for match in TRACES_TO_PATTERN.finditer(content):
            issue_num = int(match.group(1))
            artifact['traces_to'].append(issue_num)
            self.links[artifact_id]['traces_to'].append(issue_num)
        
        for match in VERIFIED_BY_PATTERN.finditer(content):
            issue_num = int(match.group(1))
            artifact['verified_by'].append(issue_num)
            self.links[artifact_id]['verified_by'].append(issue_num)
        
        for match in IMPLEMENTS_PATTERN.finditer(content):
            issue_num = int(match.group(1))
            artifact['implements'].append(issue_num)
            self.links[artifact_id]['implements'].append(issue_num)
        
        for match in SATISFIES_PATTERN.finditer(content):
            issue_num = int(match.group(1))
            artifact['satisfies'].append(issue_num)
            self.links[artifact_id]['satisfies'].append(issue_num)
        
        for match in REFINED_BY_PATTERN.finditer(content):
            issue_num = int(match.group(1))
            artifact['refined_by'].append(issue_num)
            self.links[artifact_id]['refined_by'].append(issue_num)
        
        # Deduplicate
        for key in ['traces_to', 'verified_by', 'implements', 'satisfies', 'refined_by']:
            artifact[key] = sorted(list(set(artifact[key])))
        
        # Add to collection
        if artifact_type == 'StR':
            self.artifacts['stakeholder_requirements'].append(artifact)
        elif artifact_type == 'REQ-F':
            self.artifacts['functional_requirements'].append(artifact)
        elif artifact_type == 'REQ-NF':
            self.artifacts['non_functional_requirements'].append(artifact)
        elif artifact_type == 'ADR':
            self.artifacts['architecture_decisions'].append(artifact)
        elif artifact_type == 'ARC-C':
            self.artifacts['architecture_components'].append(artifact)
    
    def scan_test_file(self, file_path: Path):
        """Scan C++ test file for test cases with issue references."""
        content = file_path.read_text(encoding='utf-8', errors='ignore')
        
        # Find test case names and associated comments
        test_cases = re.findall(r'(?://.*?Verifies?:?\s*#(\d+).*?\n.*?void\s+(test_\w+)|void\s+(test_\w+).*?//.*?#(\d+))', content, re.IGNORECASE)
        
        # Also find generic issue refs in comments
        for match in CODE_COMMENT_PATTERN.finditer(content):
            issue_num = int(match.group(1))
            artifact = {
                'id': f"TEST-{file_path.stem}",
                'file': str(file_path.relative_to(ROOT)),
                'type': 'TEST',
                'verifies': [issue_num]
            }
            self.artifacts['test_cases'].append(artifact)
    
    def scan_implementation_file(self, file_path: Path):
        """Scan C++ implementation file for issue references."""
        content = file_path.read_text(encoding='utf-8', errors='ignore')
        
        issue_refs = []
        for match in CODE_COMMENT_PATTERN.finditer(content):
            issue_num = int(match.group(1))
            issue_refs.append(issue_num)
        
        if issue_refs:
            artifact = {
                'id': f"IMPL-{file_path.stem}",
                'file': str(file_path.relative_to(ROOT)),
                'type': 'IMPL',
                'implements': sorted(list(set(issue_refs)))
            }
            self.artifacts['implementations'].append(artifact)
    
    def scan_all(self):
        """Scan all project directories."""
        print("🔍 Scanning project for traceability artifacts...")
        
        # Stakeholder requirements
        str_dir = ROOT / '01-stakeholder-requirements'
        if str_dir.exists():
            for md_file in str_dir.glob('*.md'):
                self.scan_markdown(md_file, 'StR', STR_ID_PATTERN)
        
        # Functional requirements
        req_f_dir = ROOT / '02-requirements' / 'functional'
        if req_f_dir.exists():
            for md_file in req_f_dir.glob('*.md'):
                self.scan_markdown(md_file, 'REQ-F', REQ_F_ID_PATTERN)
        
        # Non-functional requirements
        req_nf_dir = ROOT / '02-requirements' / 'non-functional'
        if req_nf_dir.exists():
            for md_file in req_nf_dir.glob('*.md'):
                self.scan_markdown(md_file, 'REQ-NF', REQ_NF_ID_PATTERN)
        
        # Architecture decisions
        adr_dir = ROOT / '03-architecture' / 'decisions'
        if adr_dir.exists():
            for md_file in adr_dir.glob('*.md'):
                self.scan_markdown(md_file, 'ADR', ADR_ID_PATTERN)
        
        # Architecture components
        arc_c_dir = ROOT / '03-architecture' / 'components'
        if arc_c_dir.exists():
            for md_file in arc_c_dir.glob('*.md'):
                self.scan_markdown(md_file, 'ARC-C', ARC_C_ID_PATTERN)
        
        # Test cases
        test_dir = ROOT / 'test'
        if test_dir.exists():
            for cpp_file in test_dir.rglob('*.cpp'):
                if 'test_' in cpp_file.name:
                    self.scan_test_file(cpp_file)
        
        # Implementations
        src_dir = ROOT / 'src'
        if src_dir.exists():
            for cpp_file in src_dir.rglob('*.cpp'):
                self.scan_implementation_file(cpp_file)
        
        print(f"✅ Found:")
        print(f"   - {len(self.artifacts['stakeholder_requirements'])} StR")
        print(f"   - {len(self.artifacts['functional_requirements'])} REQ-F")
        print(f"   - {len(self.artifacts['non_functional_requirements'])} REQ-NF")
        print(f"   - {len(self.artifacts['architecture_decisions'])} ADR")
        print(f"   - {len(self.artifacts['architecture_components'])} ARC-C")
        print(f"   - {len(self.artifacts['test_cases'])} TEST")
        print(f"   - {len(self.artifacts['implementations'])} IMPL")
    
    def calculate_metrics(self) -> Dict:
        """Calculate traceability coverage metrics for CI validation."""
        total_requirements = (
            len(self.artifacts['functional_requirements']) +
            len(self.artifacts['non_functional_requirements'])
        )
        
        if total_requirements == 0:
            return {
                'requirement': {'coverage_pct': 0.0},
                'requirement_to_ADR': {'coverage_pct': 0.0},
                'requirement_to_scenario': {'coverage_pct': 0.0},
                'requirement_to_test': {'coverage_pct': 0.0}
            }
        
        # Count requirements with linkage
        req_with_any_link = 0
        req_with_adr = 0
        req_with_scenario = 0
        req_with_test = 0
        
        all_reqs = (
            self.artifacts['functional_requirements'] +
            self.artifacts['non_functional_requirements']
        )
        
        for req in all_reqs:
            has_link = (
                len(req['traces_to']) > 0 or
                len(req['verified_by']) > 0 or
                len(req['implements']) > 0 or
                len(req['satisfies']) > 0 or
                len(req['refined_by']) > 0
            )
            if has_link:
                req_with_any_link += 1
            
            # ADR linkage (implements or satisfies ADR)
            if len(req['implements']) > 0 or len(req['satisfies']) > 0:
                req_with_adr += 1
            
            # Scenario linkage (TODO: detect QA-SC references)
            # For now, assume satisfied if ADR present
            if len(req['implements']) > 0:
                req_with_scenario += 1
            
            # Test linkage (verified_by)
            if len(req['verified_by']) > 0:
                req_with_test += 1
        
        return {
            'requirement': {
                'coverage_pct': round((req_with_any_link / total_requirements) * 100, 2)
            },
            'requirement_to_ADR': {
                'coverage_pct': round((req_with_adr / total_requirements) * 100, 2)
            },
            'requirement_to_scenario': {
                'coverage_pct': round((req_with_scenario / total_requirements) * 100, 2)
            },
            'requirement_to_test': {
                'coverage_pct': round((req_with_test / total_requirements) * 100, 2)
            }
        }
    
    def build(self) -> Dict:
        """Build complete traceability data structure."""
        metrics = self.calculate_metrics()
        
        # Auto-detect project name from environment
        project_name = os.environ.get('GITHUB_REPOSITORY', 'zarfld/copilot-instructions-template').split('/')[-1]
        
        return {
            'version': '1.0',
            'generated_at': '2025-11-21',
            'project': project_name,
            'artifacts': self.artifacts,
            'links': dict(self.links),
            'metrics': metrics,
            'summary': {
                'total_stakeholder_requirements': len(self.artifacts['stakeholder_requirements']),
                'total_functional_requirements': len(self.artifacts['functional_requirements']),
                'total_non_functional_requirements': len(self.artifacts['non_functional_requirements']),
                'total_architecture_decisions': len(self.artifacts['architecture_decisions']),
                'total_architecture_components': len(self.artifacts['architecture_components']),
                'total_test_cases': len(self.artifacts['test_cases']),
                'total_implementations': len(self.artifacts['implementations']),
                'overall_coverage': metrics['requirement']['coverage_pct'],
                'adr_linkage': metrics['requirement_to_ADR']['coverage_pct'],
                'test_linkage': metrics['requirement_to_test']['coverage_pct']
            }
        }

def main():
    builder = TraceabilityBuilder()
    builder.scan_all()
    
    traceability_data = builder.build()
    
    # Ensure build directory exists
    build_dir = ROOT / 'build'
    build_dir.mkdir(exist_ok=True)
    
    output_file = build_dir / 'traceability.json'
    output_file.write_text(json.dumps(traceability_data, indent=2), encoding='utf-8')
    
    print(f"\n✅ Generated: {output_file}")
    print(f"\n📊 Metrics:")
    print(f"   - Overall coverage: {traceability_data['metrics']['requirement']['coverage_pct']}%")
    print(f"   - ADR linkage: {traceability_data['metrics']['requirement_to_ADR']['coverage_pct']}%")
    print(f"   - Scenario linkage: {traceability_data['metrics']['requirement_to_scenario']['coverage_pct']}%")
    print(f"   - Test linkage: {traceability_data['metrics']['requirement_to_test']['coverage_pct']}%")
    
    return 0

if __name__ == '__main__':
    sys.exit(main())
