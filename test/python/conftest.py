"""
Pytest configuration and fixtures for tttrlib tests.

This module delegates all test data configuration to test/test_settings.py,
which loads settings.json and resolves data paths. settings.json is the single
management point for file locations and test parameters.

Environment Variables:
    TTTRLIB_DATA: Path to test data directory (e.g., V:\tttr-data or /path/to/tttr-data)
                  If set, overrides settings.json:data_root.
"""
from __future__ import annotations

import importlib.machinery
import sys
from pathlib import Path
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore

# A stale or broken tttrlib install in site-packages (e.g. a namespace package
# left over from a partial pip install) can shadow the SWIG-built module in
# build/ext. Put the build output first so every test — including those that
# spawn subprocesses inheriting sys.path — sees the development build.
#
# Only when that build is for *this* interpreter. `build/ext` outlives the
# Python it was built against: a wrapper beside a `_tttrlib.cpython-310-*.so`
# shadows a perfectly good install under 3.12 and every test then errors at
# collection with `No module named '_tttrlib'`, which reads as a broken install
# rather than a stale build directory.
_REPO = Path(__file__).resolve().parent.parent.parent
_BUILD_EXT = _REPO / "build" / "ext"
_BUILT_HERE = any(
    (_BUILD_EXT / f"_tttrlib{suffix}").exists()
    for suffix in importlib.machinery.EXTENSION_SUFFIXES
)
if _BUILD_EXT.is_dir() and _BUILT_HERE and str(_BUILD_EXT) not in sys.path:
    sys.path.insert(0, str(_BUILD_EXT))


def pytest_configure(config):
    """Pytest hook to display unified test data status."""
    status = "[OK]" if DATA_AVAILABLE else "[NOT FOUND]"
    print(f"\n{status} Test data root: {DATA_ROOT}")
    if not DATA_AVAILABLE:
        print("  WARNING: Test data directory not found!")
        print("  Set TTTRLIB_DATA environment variable to specify location")
