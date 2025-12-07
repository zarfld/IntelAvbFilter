#!/usr/bin/env python3
"""
Generate Doxygen Documentation - Template Script

TEMPLATE: This script can be used for any project using Doxygen.
Customize the paths and project-specific settings as needed.

Phase: 08-transition (Documentation)
Standards: ISO/IEC/IEEE 12207:2017 (Transition Process)

Usage:
    python scripts/generate-doxygen.py [--check] [--clean] [--coverage]
    
Options:
    --check     Check prerequisites only (Doxygen, Graphviz)
    --clean     Remove previous documentation before generating
    --coverage  Generate documentation coverage report
    --doxyfile  Specify custom Doxyfile path (default: Doxyfile)
"""

import os
import sys
import subprocess
import shutil
import argparse
from pathlib import Path

def find_doxygen():
    """Find Doxygen executable"""
    doxygen_paths = [
        "doxygen",  # In PATH
        r"C:\Program Files\doxygen\bin\doxygen.exe",
        r"C:\Program Files (x86)\doxygen\bin\doxygen.exe",
        "/usr/bin/doxygen",
        "/usr/local/bin/doxygen"
    ]
    
    for path in doxygen_paths:
        if shutil.which(path):
            return path
        if Path(path).exists():
            return path
    
    return None

def find_dot():
    """Find Graphviz dot executable"""
    dot_paths = [
        "dot",  # In PATH
        r"C:\Program Files\Graphviz\bin\dot.exe",
        r"C:\Program Files (x86)\Graphviz\bin\dot.exe",
        "/usr/bin/dot",
        "/usr/local/bin/dot"
    ]
    
    for path in dot_paths:
        if shutil.which(path):
            return path
        if Path(path).exists():
            return path
    
    return None

def check_prerequisites():
    """Check if required tools are available"""
    print("Checking prerequisites...")
    
    # Check Doxygen
    doxygen = find_doxygen()
    if not doxygen:
        print("ERROR: Doxygen not found!")
        print("Please install Doxygen:")
        print("  Windows: Download from https://www.doxygen.nl/download.html")
        print("  Linux: sudo apt-get install doxygen")
        print("  macOS: brew install doxygen")
        return False
    
    print(f"✓ Found Doxygen: {doxygen}")
    
    # Check version
    try:
        result = subprocess.run([doxygen, "--version"], 
                              capture_output=True, text=True, check=True)
        version = result.stdout.strip()
        print(f"  Version: {version}")
    except subprocess.CalledProcessError:
        print("  WARNING: Could not determine Doxygen version")
    
    # Check Graphviz (optional but recommended)
    dot = find_dot()
    if dot:
        print(f"✓ Found Graphviz: {dot}")
        try:
            result = subprocess.run([dot, "-V"], 
                                  capture_output=True, text=True, check=True)
            version = result.stderr.strip()  # dot writes version to stderr
            print(f"  Version: {version}")
        except subprocess.CalledProcessError:
            print("  WARNING: Could not determine Graphviz version")
    else:
        print("⚠ Graphviz not found (diagrams will not be generated)")
        print("  Install Graphviz for better documentation:")
        print("  Windows: Download from https://graphviz.org/download/")
        print("  Linux: sudo apt-get install graphviz")
        print("  macOS: brew install graphviz")
    
    return True

def clean_output(output_dir):
    """Clean previous documentation output"""
    if output_dir.exists():
        print(f"Cleaning previous output: {output_dir}")
        shutil.rmtree(output_dir)

def generate_docs(doxyfile, clean=False):
    """Generate documentation using Doxygen"""
    # Check prerequisites
    if not check_prerequisites():
        return False
    
    # Validate Doxyfile exists
    doxyfile_path = Path(doxyfile)
    if not doxyfile_path.exists():
        print(f"ERROR: Doxyfile not found: {doxyfile}")
        return False
    
    print(f"\nUsing configuration: {doxyfile_path}")
    
    # Clean if requested
    output_dir = Path("docs/doxygen")
    if clean:
        clean_output(output_dir)
    
    # Generate documentation
    print("\nGenerating documentation...")
    doxygen = find_doxygen()
    
    try:
        result = subprocess.run(
            [doxygen, str(doxyfile_path)],
            capture_output=True,
            text=True,
            check=True
        )
        
        # Print Doxygen output
        if result.stdout:
            print(result.stdout)
        if result.stderr:
            print(result.stderr, file=sys.stderr)
        
        print("\n✓ Documentation generated successfully!")
        
        # Check for warnings
        warnings_log = output_dir / "warnings.log"
        if warnings_log.exists():
            warnings_content = warnings_log.read_text()
            if warnings_content.strip():
                print(f"\n⚠ Warnings found (see {warnings_log}):")
                print(warnings_content[:1000])  # First 1000 chars
                if len(warnings_content) > 1000:
                    print("... (more warnings in log file)")
        
        # Print output locations
        html_dir = output_dir / "html" / "index.html"
        xml_dir = output_dir / "xml"
        
        print(f"\nDocumentation output:")
        print(f"  HTML: {html_dir}")
        if xml_dir.exists():
            print(f"  XML:  {xml_dir}")
        
        # Open in browser (optional)
        if html_dir.exists():
            print(f"\nTo view documentation, open: {html_dir.absolute()}")
            print("Or run: python -m webbrowser file:///" + str(html_dir.absolute()))
        
        return True
        
    except subprocess.CalledProcessError as e:
        print(f"\nERROR: Doxygen failed with exit code {e.returncode}")
        if e.stdout:
            print("STDOUT:", e.stdout)
        if e.stderr:
            print("STDERR:", e.stderr, file=sys.stderr)
        return False
    except Exception as e:
        print(f"\nERROR: {e}")
        return False

def generate_coverage_report():
    """Generate documentation coverage report"""
    print("\nGenerating documentation coverage report...")
    
    xml_dir = Path("docs/doxygen/xml")
    if not xml_dir.exists():
        print("ERROR: XML output not found. Run with Doxygen first.")
        return False
    
    # Parse XML files to count documented vs undocumented items
    try:
        import xml.etree.ElementTree as ET
        
        stats = {
            'classes': {'total': 0, 'documented': 0},
            'functions': {'total': 0, 'documented': 0},
            'variables': {'total': 0, 'documented': 0},
            'enums': {'total': 0, 'documented': 0}
        }
        
        for xml_file in xml_dir.glob("*.xml"):
            if xml_file.name == "index.xml":
                continue
            
            try:
                tree = ET.parse(xml_file)
                root = tree.getroot()
                
                # Count classes/structs
                for compound in root.findall(".//compounddef[@kind='class']") + \
                              root.findall(".//compounddef[@kind='struct']"):
                    stats['classes']['total'] += 1
                    if compound.find("briefdescription") is not None:
                        stats['classes']['documented'] += 1
                
                # Count functions
                for member in root.findall(".//memberdef[@kind='function']"):
                    stats['functions']['total'] += 1
                    if member.find("briefdescription") is not None:
                        stats['functions']['documented'] += 1
                
                # Count variables
                for member in root.findall(".//memberdef[@kind='variable']"):
                    stats['variables']['total'] += 1
                    if member.find("briefdescription") is not None:
                        stats['variables']['documented'] += 1
                
                # Count enums
                for member in root.findall(".//memberdef[@kind='enum']"):
                    stats['enums']['total'] += 1
                    if member.find("briefdescription") is not None:
                        stats['enums']['documented'] += 1
                        
            except Exception as e:
                print(f"Warning: Could not parse {xml_file.name}: {e}")
                continue
        
        # Print report
        print("\n" + "="*60)
        print("Documentation Coverage Report")
        print("="*60)
        
        total_items = 0
        total_documented = 0
        
        for category, counts in stats.items():
            if counts['total'] > 0:
                percentage = (counts['documented'] / counts['total']) * 100
                print(f"{category.capitalize()}: {counts['documented']}/{counts['total']} ({percentage:.1f}%)")
                total_items += counts['total']
                total_documented += counts['documented']
        
        if total_items > 0:
            overall_percentage = (total_documented / total_items) * 100
            print("-"*60)
            print(f"Overall: {total_documented}/{total_items} ({overall_percentage:.1f}%)")
        
        print("="*60)
        
        return True
        
    except ImportError:
        print("ERROR: xml.etree.ElementTree not available")
        return False
    except Exception as e:
        print(f"ERROR: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(
        description="Generate Doxygen documentation for IEEE 1588-2019 PTP Library"
    )
    parser.add_argument(
        "--doxyfile",
        default="Doxyfile",
        help="Path to Doxyfile (default: Doxyfile)"
    )
    parser.add_argument(
        "--clean",
        action="store_true",
        help="Clean output directory before generating"
    )
    parser.add_argument(
        "--coverage",
        action="store_true",
        help="Generate documentation coverage report"
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Only check prerequisites, don't generate"
    )
    
    args = parser.parse_args()
    
    # Check prerequisites only
    if args.check:
        if check_prerequisites():
            print("\n✓ All prerequisites met")
            return 0
        else:
            print("\n✗ Prerequisites not met")
            return 1
    
    # Generate documentation
    if not generate_docs(args.doxyfile, args.clean):
        return 1
    
    # Generate coverage report if requested
    if args.coverage:
        if not generate_coverage_report():
            print("\n⚠ Coverage report generation failed")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
