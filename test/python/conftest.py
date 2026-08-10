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

import os
import sys
from pathlib import Path
from test_settings import DATA_ROOT, DATA_AVAILABLE  # type: ignore

# A stale or broken tttrlib install in site-packages (e.g. a namespace package
# left over from a partial pip install) can shadow the SWIG-built module in
# build/ext. Put the build output first so every test — including those that
# spawn subprocesses inheriting sys.path — sees the development build.
_REPO = Path(__file__).resolve().parent.parent.parent
_BUILD_EXT = _REPO / "build" / "ext"
if _BUILD_EXT.is_dir():
    _IDX = 0 if str(_BUILD_EXT) not in sys.path else None
    if _IDX is not None:
        sys.path.insert(_IDX, str(_BUILD_EXT))


def pytest_configure(config):
    """Pytest hook to display unified test data status."""
    status = "[OK]" if DATA_AVAILABLE else "[NOT FOUND]"
    print(f"\n{status} Test data root: {DATA_ROOT}")
    if not DATA_AVAILABLE:
        print("  WARNING: Test data directory not found!")
        print("  Set TTTRLIB_DATA environment variable to specify location")


def pytest_sessionfinish(session, exitstatus):
    """On Windows, bypass interpreter shutdown to avoid heap corruption crash.

    tttrlib's C extension destructors trigger a heap corruption exception
    (0xC0000374 / -1073740940) during Python interpreter shutdown on Windows.
    All tests have already completed at this point, so os._exit() is safe:
    it exits with the correct pytest exit code without running any Python
    shutdown machinery (atexit handlers, __del__, extension finalizers).
    """
    if sys.platform == "win32":
        os._exit(int(exitstatus))
