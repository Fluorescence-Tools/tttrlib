"""Optional C++-level integration checks.

This repository historically carried standalone scripts under `test/` that were
useful for local debugging but not suitable for automated pytest runs.

These checks are now:
- pytest-friendly (no sys.exit / top-level execution)
- skipped automatically if the required artifacts are not present
"""

from __future__ import annotations

import ctypes
import os
from pathlib import Path

import pytest


@pytest.fixture(scope="session")
def tttrlib_dll() -> ctypes.CDLL:
    if os.name != "nt":
        pytest.skip("DLL integration test is Windows-only")

    # Try environment variable first (user can override)
    dll_path = os.environ.get("TTTRLIB_DLL_PATH")
    if dll_path:
        p = Path(dll_path)
    else:
        # Search in common build directories relative to repo root
        test_dir = Path(__file__).parent
        repo_root = test_dir.parent
        possible_paths = [
            repo_root / "build-release" / "Release" / "tttrlib.dll",
            repo_root / "build" / "Release" / "tttrlib.dll",
            repo_root / "build" / "bin" / "Release" / "tttrlib.dll",
            repo_root / "build" / "bin" / "tttrlib.dll",
        ]
        p = None
        for candidate in possible_paths:
            if candidate.is_file():
                p = candidate
                break
        
        if p is None:
            # If not found, set to first expected location for error message
            p = repo_root / "build-release" / "Release" / "tttrlib.dll"

    if not p.is_file():
        pytest.skip(f"tttrlib.dll not available at {p} (set TTTRLIB_DLL_PATH environment variable to override)")

    return ctypes.CDLL(str(p))


@pytest.mark.integration
def test_dll_loads(tttrlib_dll: ctypes.CDLL):
    assert tttrlib_dll is not None
