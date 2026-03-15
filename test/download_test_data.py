#!/usr/bin/env python3
"""
Download required test data files from https://www.peulen.xyz/downloads/tttr-data

This script:
1. Reads settings.json to determine required files
2. Uses pooch for downloading with caching, progress bars, and hash verification
3. Supports resuming interrupted downloads
4. Verifies file integrity if hashes are provided

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
from typing import List, Dict, Any

import pooch

SETTINGS_FILE = Path(__file__).parent / "settings.json"


def load_settings() -> dict:
    """Load test settings from settings.json"""
    if not SETTINGS_FILE.exists():
        print(f"ERROR: {SETTINGS_FILE} not found")
        sys.exit(1)
    
    with open(SETTINGS_FILE) as f:
        return json.load(f)


def get_base_url(settings: dict) -> str:
    """Get base URL from settings"""
    return settings.get("base_url", "https://www.peulen.xyz/downloads/tttr-data/")


def get_file_hashes(settings: dict) -> Dict[str, str]:
    """Get file hashes from settings"""
    return settings.get("file_hashes", {})


def get_required_files(settings: dict) -> List[str]:
    """Extract all required test data files from settings.json"""
    required_files = set()
    
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
    
    if "test_files" in settings:
        for test_file in settings["test_files"]:
            if isinstance(test_file, list) and len(test_file) > 0:
                required_files.add(test_file[0])

    for extra in settings.get("required_files", []):
        if extra:
            required_files.add(extra)

    return sorted(required_files)


def get_output_dir(settings: dict) -> Path:
    """Get output directory from env var or settings"""
    env_dir = os.getenv("TTTRLIB_DATA")
    if env_dir:
        return Path(env_dir)
    
    data_root = settings.get("data_root", "tttr-data")
    
    if os.path.isabs(data_root):
        return Path(data_root)
    
    return Path(__file__).parent / data_root


def create_pooch_client(output_dir: Path, settings: dict) -> pooch.Pooch:
    """Create a pooch client with registry from settings"""
    base_url = get_base_url(settings)
    file_hashes = get_file_hashes(settings)
    required_files = get_required_files(settings)
    
    registry = {}
    for f in required_files:
        if f in file_hashes:
            registry[f] = file_hashes[f]
        else:
            registry[f] = None
    
    return pooch.create(
        path=output_dir,
        base_url=base_url,
        registry=registry,
    )


def download_file(pup: pooch.Pooch, file_path: str, show_progress: bool = False) -> bool:
    """
    Download a single file using pooch
    
    Args:
        pup: Pooch instance
        file_path: Relative path to download
        show_progress: Show progress bar
        
    Returns:
        True if successful, False otherwise
    """
    try:
        output_path = pup.path / file_path
        
        if output_path.exists():
            print(f"  [OK] Already exists: {file_path}")
            return True
        
        print(f"  [DL] Downloading: {file_path}")
        
        downloaded_path = pup.fetch(
            fname=file_path,
            progressbar=show_progress,
        )
        
        print(f"  [OK] Downloaded: {file_path}")
        return True
        
    except Exception as e:
        print(f"  [FAIL] Failed to download {file_path}: {e}")
        return False


def list_required_files(settings: dict, verbose: bool = True) -> List[str]:
    """List all required test data files"""
    files = get_required_files(settings)
    file_hashes = get_file_hashes(settings)
    
    if verbose:
        print("\nRequired test data files:")
        print("-" * 60)
        for f in files:
            hash_info = " [hashed]" if f in file_hashes else ""
            print(f"  {f}{hash_info}")
        print("-" * 60)
        print(f"Total: {len(files)} files")
        print(f"Hashed: {sum(1 for f in files if f in file_hashes)} files\n")
    
    return list(files)


def download_all(settings: dict, output_dir: Path, verbose: bool = True) -> bool:
    """Download all required test data files"""
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    pup = create_pooch_client(output_dir, settings)
    files = get_required_files(settings)
    
    if verbose:
        print(f"\nDownloading test data to: {output_dir}")
        print(f"Base URL: {get_base_url(settings)}")
        print("-" * 60)
    
    success_count = 0
    fail_count = 0
    
    for file_path in files:
        if download_file(pup, file_path, show_progress=verbose):
            success_count += 1
        else:
            fail_count += 1
    
    print("-" * 60)
    print(f"\nDownload Summary:")
    print(f"  [OK] Successful: {success_count}")
    print(f"  [FAIL] Failed: {fail_count}")
    print(f"  Total: {len(files)}\n")
    
    return fail_count == 0


def compute_hashes(output_dir: Path, settings: dict, verbose: bool = True) -> Dict[str, str]:
    """
    Download all files and compute SHA256 hashes.
    Returns dict of filename -> hash.
    """
    import hashlib
    
    output_dir = Path(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    pup = create_pooch_client(output_dir, settings)
    files = get_required_files(settings)
    
    hashes = {}
    
    if verbose:
        print(f"\nComputing hashes for: {len(files)} files")
        print("-" * 60)
    
    for i, file_path in enumerate(files):
        try:
            downloaded_path = pup.fetch(fname=file_path, progressbar=False)
            
            sha256_hash = hashlib.sha256()
            with open(downloaded_path, "rb") as f:
                for chunk in iter(lambda: f.read(8192), b""):
                    sha256_hash.update(chunk)
            
            hashes[file_path] = f"sha256:{sha256_hash.hexdigest()}"
            
            if verbose:
                print(f"  [{i+1}/{len(files)}] {file_path}")
                
        except Exception as e:
            if verbose:
                print(f"  [FAIL] {file_path}: {e}")
    
    if verbose:
        print("-" * 60)
        print(f"Computed {len(hashes)} hashes\n")
    
    return hashes


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
    parser.add_argument(
        "--compute-hashes",
        action="store_true",
        help="Download all files and compute SHA256 hashes, then print JSON to stdout"
    )
    
    args = parser.parse_args()
    
    settings = load_settings()
    output_dir = Path(args.output_dir) if args.output_dir else get_output_dir(settings)
    
    if args.compute_hashes:
        hashes = compute_hashes(output_dir, settings, verbose=not args.quiet)
        print(json.dumps(hashes, indent=2))
        return 0
    
    if args.list_only:
        list_required_files(settings, verbose=not args.quiet)
        return 0
    
    success = download_all(settings, output_dir, verbose=not args.quiet)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
