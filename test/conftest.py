"""
Pytest configuration and fixtures for tttrlib tests.

Environment Variables:
    TTTRLIB_DATA: Path to test data directory (e.g., V:\tttr-data or /path/to/tttr-data)
                  If not set, uses data_root from settings.json
                  
Usage:
    # Run tests with custom data location
    export TTTRLIB_DATA=/path/to/test/data
    pytest test/
    
    # Or on Windows
    set TTTRLIB_DATA=V:\tttr-data
    pytest test/
"""

import os
import json
from pathlib import Path


def get_test_data_root():
    """
    Get the test data root directory.
    
    Priority:
    1. TTTRLIB_DATA environment variable (if set)
    2. data_root from settings.json
    3. ./tttr-data (relative to repo root)
    
    Returns:
        Path: Absolute path to test data directory
    """
    # Check environment variable first
    env_root = os.getenv("TTTRLIB_DATA")
    if env_root:
        env_root = env_root.strip().strip('\'"')
        return Path(os.path.abspath(env_root))
    
    # Load settings.json
    settings_path = os.path.join(os.path.dirname(__file__), "settings.json")
    try:
        settings = json.load(open(settings_path))
        data_root_str = settings.get("data_root", "./tttr-data")
    except (FileNotFoundError, json.JSONDecodeError):
        data_root_str = "./tttr-data"
    
    # Handle absolute vs relative paths
    if os.path.isabs(data_root_str):
        return Path(data_root_str)
    
    # Resolve relative to repo root
    repo_root = Path(__file__).resolve().parents[1]
    return Path(os.path.abspath(str(repo_root / data_root_str)))


def pytest_configure(config):
    """
    Pytest hook to configure test environment.
    
    Prints the test data root path for debugging.
    """
    data_root = get_test_data_root()
    available = data_root.is_dir()
    status = "[OK]" if available else "[NOT FOUND]"
    print(f"\n{status} Test data root: {data_root}")
    if not available:
        print(f"  WARNING: Test data directory not found!")
        print(f"  Set TTTRLIB_DATA environment variable to specify location")
