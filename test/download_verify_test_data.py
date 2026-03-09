#!/usr/bin/env python3
"""
Verify presence of required tttrlib test data files.

- Determines required files from test/settings.json
- Resolves data root using TTTRLIB_DATA env var or settings.json:data_root
- Prints a short summary and missing files, returns non‑zero if anything is missing

Usage:
    python -m test.verify_test_data
    python test/download_verify_test_data.py [--list-only]

Exit codes:
    0 = all required files are present
    1 = one or more required files are missing or an unexpected error occurred
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import List

HERE = Path(__file__).parent

try:
    from test.download_test_data import get_required_files, get_output_dir, load_settings
except Exception:
    sys.path.insert(0, str(HERE))
    from download_test_data import get_required_files, get_output_dir, load_settings


def list_existing_files(root: Path, max_lines: int = 200) -> List[Path]:
    files: List[Path] = []
    try:
        for p in sorted(root.rglob('*')):
            if p.is_file():
                files.append(p)
                if len(files) >= max_lines:
                    break
    except Exception:
        pass
    return files


def main(argv: List[str]) -> int:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--list-only', action='store_true')
    parser.add_argument('--output-dir', type=str, default=None)
    args = parser.parse_args(argv)
    
    settings = load_settings()
    
    if args.output_dir:
        data_root = Path(args.output_dir)
    else:
        data_root = get_output_dir(settings)
    
    required_rel_paths = list(get_required_files(settings))

    print(f"TTTRLIB_DATA resolved to: {data_root}")
    print("Listing tttr-data top-level (if exists):")
    try:
        if data_root.exists():
            for entry in sorted(data_root.iterdir()):
                print("  ", entry.name)
        else:
            print("  (directory does not exist)")
    except Exception as e:
        print(f"  (failed to list top-level: {e})")

    print("Listing tttr-data recursively (first 200 files):")
    for p in list_existing_files(data_root, max_lines=200):
        try:
            print("  ", p.relative_to(data_root))
        except Exception:
            print("  ", p)

    if args.list_only:
        print("--list-only specified; not checking for required files.")
        return 0

    print("Checking for required files from settings.json:")
    missing = 0
    for rel in required_rel_paths:
        abs_path = data_root / rel
        if not abs_path.is_file():
            print(f"MISSING: {abs_path}")
            missing += 1
        else:
            print(f"FOUND:   {abs_path}")

    if missing:
        print(f"One or more required files are missing ({missing}).")
        print("You can download them with: python test/download_test_data.py --output-dir ./tttr-data")
        return 1

    print("All required test data files are present.")
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
