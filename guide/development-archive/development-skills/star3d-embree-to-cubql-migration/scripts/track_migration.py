#!/usr/bin/env python3
"""
Migration Tracking Script for Star-3D Embree to cuBQL Migration

Maintains a JSON database of function migration status.
"""

import json
import argparse
import sys
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Optional

# Migration status constants
STATUS_UNMIGRATED = "UNMIGRATED"
STATUS_MIGRATED = "MIGRATED"
STATUS_SKIPPED_NO_EMBREE = "SKIPPED_NO_EMBREE"
STATUS_LSP_UNAVAILABLE = "LSP_UNAVAILABLE"
STATUS_DOCS_UNAVAILABLE = "DOCS_UNAVAILABLE"
STATUS_REWRITE_FAILED = "REWRITE_FAILED"

VALID_STATUSES = {
    STATUS_UNMIGRATED,
    STATUS_MIGRATED,
    STATUS_SKIPPED_NO_EMBREE,
    STATUS_LSP_UNAVAILABLE,
    STATUS_DOCS_UNAVAILABLE,
    STATUS_REWRITE_FAILED
}

DB_FILE = Path(__file__).parent.parent / "migration_status.json"


def load_db() -> Dict:
    """Load migration status database."""
    if not DB_FILE.exists():
        return {"functions": {}, "modules": {}, "last_updated": None}
    
    with open(DB_FILE, 'r', encoding='utf-8') as f:
        return json.load(f)


def save_db(db: Dict):
    """Save migration status database."""
    db["last_updated"] = datetime.now().isoformat()
    with open(DB_FILE, 'w', encoding='utf-8') as f:
        json.dump(db, f, indent=2, ensure_ascii=False)


def list_unmigrated(db: Dict, module: Optional[str] = None):
    """List all unmigrated functions."""
    functions = db.get("functions", {})
    
    unmigrated = []
    for func_name, info in functions.items():
        if info["status"] == STATUS_UNMIGRATED:
            if module is None or info.get("module") == module:
                unmigrated.append((func_name, info))
    
    if not unmigrated:
        print("✓ All functions migrated!")
        return
    
    print(f"Unmigrated functions ({len(unmigrated)}):")
    print("-" * 80)
    for func_name, info in sorted(unmigrated):
        module_name = info.get("module", "unknown")
        file_name = info.get("file", "unknown")
        print(f"  {func_name:<40} [{module_name}] in {file_name}")


def mark_migrated(db: Dict, function: str, status: str, 
                  file: Optional[str] = None, line: Optional[int] = None,
                  notes: Optional[str] = None):
    """Mark a function as migrated with given status."""
    if status not in VALID_STATUSES:
        print(f"✗ Invalid status: {status}")
        print(f"  Valid statuses: {', '.join(VALID_STATUSES)}")
        sys.exit(1)
    
    functions = db.get("functions", {})
    
    if function not in functions:
        functions[function] = {
            "status": STATUS_UNMIGRATED,
            "module": "unknown",
            "file": file or "unknown",
            "line": line or 0
        }
    
    functions[function]["status"] = status
    functions[function]["migrated_at"] = datetime.now().isoformat()
    
    if file:
        functions[function]["file"] = file
    if line:
        functions[function]["line"] = line
    if notes:
        functions[function]["notes"] = notes
    
    db["functions"] = functions
    save_db(db)
    
    print(f"✓ Marked {function} as {status}")


def generate_report(db: Dict):
    """Generate migration progress report."""
    functions = db.get("functions", {})
    
    if not functions:
        print("No migration data available.")
        return
    
    # Count by status
    status_counts = {}
    for info in functions.values():
        status = info["status"]
        status_counts[status] = status_counts.get(status, 0) + 1
    
    # Count by module
    module_counts = {}
    for info in functions.values():
        module = info.get("module", "unknown")
        if module not in module_counts:
            module_counts[module] = {
                STATUS_MIGRATED: 0,
                STATUS_UNMIGRATED: 0,
                "other": 0
            }
        
        status = info["status"]
        if status == STATUS_MIGRATED:
            module_counts[module][STATUS_MIGRATED] += 1
        elif status == STATUS_UNMIGRATED:
            module_counts[module][STATUS_UNMIGRATED] += 1
        else:
            module_counts[module]["other"] += 1
    
    # Print report
    print("=" * 80)
    print("STAR-3D EMBREE → cuBQL MIGRATION REPORT")
    print("=" * 80)
    print()
    
    print("Overall Progress:")
    print("-" * 80)
    total = len(functions)
    for status in sorted(status_counts.keys()):
        count = status_counts[status]
        pct = (count / total) * 100 if total > 0 else 0
        print(f"  {status:<30} {count:>5} ({pct:>5.1f}%)")
    print(f"  {'TOTAL':<30} {total:>5}")
    print()
    
    print("Progress by Module:")
    print("-" * 80)
    for module in sorted(module_counts.keys()):
        counts = module_counts[module]
        total_module = counts[STATUS_MIGRATED] + counts[STATUS_UNMIGRATED] + counts["other"]
        migrated = counts[STATUS_MIGRATED]
        pct = (migrated / total_module) * 100 if total_module > 0 else 0
        
        print(f"  {module:<30} {migrated:>3}/{total_module:<3} ({pct:>5.1f}%) migrated")
    print()
    
    # Failed functions
    failed = []
    for func_name, info in functions.items():
        if info["status"] in [STATUS_LSP_UNAVAILABLE, STATUS_DOCS_UNAVAILABLE, STATUS_REWRITE_FAILED]:
            failed.append((func_name, info))
    
    if failed:
        print("Failed Functions (Require Attention):")
        print("-" * 80)
        for func_name, info in sorted(failed):
            status = info["status"]
            file_name = info.get("file", "unknown")
            notes = info.get("notes", "")
            print(f"  {func_name:<40} {status}")
            print(f"    File: {file_name}")
            if notes:
                print(f"    Note: {notes}")
        print()
    
    last_updated = db.get("last_updated")
    if last_updated:
        print(f"Last updated: {last_updated}")
    print("=" * 80)


def init_from_hierarchy(db: Dict, hierarchy_file: Path):
    """Initialize database from function-hierarchy.md."""
    if not hierarchy_file.exists():
        print(f"✗ Hierarchy file not found: {hierarchy_file}")
        sys.exit(1)
    
    print(f"Initializing from {hierarchy_file}...")
    
    # Parse function-hierarchy.md
    # This is a simplified parser - extend as needed
    functions = db.get("functions", {})
    
    with open(hierarchy_file, 'r', encoding='utf-8') as f:
        content = f.read()
    
    # Extract function tables (simplified)
    # Real implementation would parse markdown tables properly
    import re
    
    # Find function declarations in tables
    # Pattern: | `function_name` | description | file |
    pattern = r'\|\s*`([a-z0-9_]+)`\s*\|.*?\|\s*`([a-z0-9_.]+)`\s*\|'
    matches = re.findall(pattern, content, re.MULTILINE)
    
    count = 0
    for func_name, file_name in matches:
        if func_name not in functions:
            functions[func_name] = {
                "status": STATUS_UNMIGRATED,
                "module": "auto-detected",
                "file": file_name,
                "line": 0
            }
            count += 1
    
    db["functions"] = functions
    save_db(db)
    
    print(f"✓ Initialized {count} functions from hierarchy file")


def main():
    parser = argparse.ArgumentParser(description="Track Star-3D migration progress")
    parser.add_argument("--list-unmigrated", action="store_true",
                        help="List all unmigrated functions")
    parser.add_argument("--module", type=str,
                        help="Filter by module")
    parser.add_argument("--mark-migrated", type=str, metavar="FUNCTION",
                        help="Mark function as migrated")
    parser.add_argument("--status", type=str, choices=list(VALID_STATUSES),
                        help="Migration status")
    parser.add_argument("--file", type=str,
                        help="File path")
    parser.add_argument("--line", type=int,
                        help="Line number")
    parser.add_argument("--notes", type=str,
                        help="Additional notes")
    parser.add_argument("--report", action="store_true",
                        help="Generate migration report")
    parser.add_argument("--init-from-hierarchy", type=str, metavar="FILE",
                        help="Initialize from function-hierarchy.md")
    
    args = parser.parse_args()
    
    db = load_db()
    
    if args.init_from_hierarchy:
        init_from_hierarchy(db, Path(args.init_from_hierarchy))
    elif args.list_unmigrated:
        list_unmigrated(db, args.module)
    elif args.mark_migrated:
        if not args.status:
            print("✗ --status required when marking function")
            sys.exit(1)
        mark_migrated(db, args.mark_migrated, args.status,
                      args.file, args.line, args.notes)
    elif args.report:
        generate_report(db)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
