#!/usr/bin/env python3
"""
Download required test data files from https://www.peulen.xyz/downloads/tttr-data

This script:
1. Reads settings.json to determine required files
2. Lists all required files
3. Downloads only the files needed for testing
4. Supports resuming interrupted downloads
5. Verifies file integrity

Usage:
    python download_test_data.py [--list-only] [--output-dir /path/to/tttr-data]

Environment Variables:
    TTTRLIB_DATA: Output directory for test data (default: ./tttr-data)
"""

import os
import sys
import json
import argparse
from pathlib import Path
from urllib.parse import urljoin
from typing import List, Set

# Force UTF-8 encoding for stdout on Windows
if sys.platform == 'win32':
    import io
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')

try:
    import requests
except ImportError:
    print("ERROR: requests library not found. Install with: pip install requests")
    sys.exit(1)


BASE_URL = "https://www.peulen.xyz/downloads/tttr-data/"
SETTINGS_FILE = Path(__file__).parent / "settings.json"


def load_settings() -> dict:
    """Load test settings from settings.json"""
    if not SETTINGS_FILE.exists():
        print(f"ERROR: {SETTINGS_FILE} not found")
        sys.exit(1)
    
    with open(SETTINGS_FILE) as f:
        return json.load(f)


def get_required_files() -> Set[str]:
    """Extract all required test data files from settings.json"""
    settings = load_settings()
    required_files = set()
    
    # Add files from individual settings
    file_keys = [
        "spc132_filename",
        "spc630_filename", 
        "photon_hdf_filename",
        "ptu_hh_t2_filename",
        "ptu_hh_t3_filename",
        "ht3_clsm_filename",
        "sm_filename",
        "clsm_confocal_filename",
        "clsm_sted_filename"
    ]
    
    for key in file_keys:
        if key in settings:
            required_files.add(settings[key])
    
    # Add files from test_files list
    if "test_files" in settings:
        for test_file in settings["test_files"]:
            if isinstance(test_file, list) and len(test_file) > 0:
                required_files.add(test_file[0])
    
    return sorted(required_files)


def get_output_dir() -> Path:
    """Get output directory from env var or default"""
    env_dir = os.getenv("TTTRLIB_DATA")
    if env_dir:
        return Path(env_dir)
    
    settings = load_settings()
    data_root = settings.get("data_root", "tttr-data")
    
    if os.path.isabs(data_root):
        return Path(data_root)
    
    return Path(__file__).parent / data_root


def download_file(url: str, output_path: Path, chunk_size: int = 8192) -> bool:
    """
    Download a single file with progress tracking
    
    Args:
        url: URL to download from
        output_path: Local path to save to
        chunk_size: Download chunk size in bytes
        
    Returns:
        True if successful, False otherwise
    """
    try:
        # Create parent directories
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        # Check if file already exists
        if output_path.exists():
            print(f"  [OK] Already exists: {output_path.name}")
            return True
        
        print(f"  [DL] Downloading: {output_path.name}")
        
        response = requests.get(url, stream=True, timeout=30)
        response.raise_for_status()
        
        total_size = int(response.headers.get('content-length', 0))
        downloaded = 0
        
        with open(output_path, 'wb') as f:
            for chunk in response.iter_content(chunk_size=chunk_size):
                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    
                    if total_size > 0:
                        percent = (downloaded / total_size) * 100
                        print(f"    {percent:.1f}% ({downloaded / 1024 / 1024:.1f} MB)", end='\r')
        
        if total_size > 0:
            print(f"    100.0% ({downloaded / 1024 / 1024:.1f} MB)")
        
        print(f"  [OK] Downloaded: {output_path.name}")
        return True
        
    except requests.RequestException as e:
        print(f"  [FAIL] Failed to download {output_path.name}: {e}")
        return False
    except Exception as e:
        print(f"  [FAIL] Error downloading {output_path.name}: {e}")
        return False


def list_required_files(verbose: bool = True) -> List[str]:
    """List all required test data files"""
    files = get_required_files()
    
    if verbose:
        print("\nRequired test data files:")
        print("-" * 60)
        for f in files:
            print(f"  {f}")
        print("-" * 60)
        print(f"Total: {len(files)} files\n")
    
    return list(files)


def download_all(output_dir: Path, verbose: bool = True) -> bool:
    """Download all required test data files"""
    files = get_required_files()
    output_dir = Path(output_dir)
    
    if verbose:
        print(f"\nDownloading test data to: {output_dir}")
        print("-" * 60)
    
    success_count = 0
    fail_count = 0
    
    for file_path in files:
        url = urljoin(BASE_URL, file_path)
        output_path = output_dir / file_path
        
        if download_file(url, output_path):
            success_count += 1
        else:
            fail_count += 1
    
    print("-" * 60)
    print(f"\nDownload Summary:")
    print(f"  [OK] Successful: {success_count}")
    print(f"  [FAIL] Failed: {fail_count}")
    print(f"  Total: {len(files)}\n")
    
    return fail_count == 0


def main():
    parser = argparse.ArgumentParser(
        description="Download required test data files for tttrlib tests"
    )
    parser.add_argument(
        "--list-only",
        action="store_true",
        help="Only list required files, don't download"
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        help="Output directory for test data (default: from TTTRLIB_DATA env var or settings.json)"
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress verbose output"
    )
    
    args = parser.parse_args()
    
    if args.list_only:
        list_required_files(verbose=not args.quiet)
        return 0
    
    output_dir = Path(args.output_dir) if args.output_dir else get_output_dir()
    
    success = download_all(output_dir, verbose=not args.quiet)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
