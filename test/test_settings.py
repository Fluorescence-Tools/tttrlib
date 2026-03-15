"""
Centralized test settings loader for tttrlib tests.

Single source of truth for:
- Loading test/settings.json
- Resolving data_root with TTTRLIB_DATA override
- Converting relative data file paths to absolute paths
- Utility helpers for tests (DATA_AVAILABLE, get_data_path, etc.)

Usage in tests:
    from test_settings import settings, DATA_AVAILABLE, get_data_path, DATA_ROOT

This makes settings.json the single management point for file locations
and settings across the whole test suite.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any, Dict, List, Tuple

# Paths
_THIS_DIR = Path(__file__).parent
_REPO_ROOT = _THIS_DIR.parent
_SETTINGS_PATH = _THIS_DIR / "settings.json"

# Load settings.json (do not fail hard if missing; tests can skip)
try:
    _RAW_SETTINGS: Dict[str, Any] = json.load(open(_settings_path := str(_SETTINGS_PATH), "r"))
except Exception:
    _RAW_SETTINGS = {}

# Resolve data root with environment override first
_env_root = os.getenv("TTTRLIB_DATA")
if _env_root:
    _env_root = _env_root.strip().strip('\'"')
    # Use abspath to preserve drive letters; avoid Path.resolve() which may turn V: into UNC on Windows
    DATA_ROOT = Path(os.path.abspath(_env_root))
else:
    _data_root_str = _RAW_SETTINGS.get("data_root", "./tttr-data")
    if os.path.isabs(_data_root_str):
        DATA_ROOT = Path(_data_root_str)
    else:
        # Resolve relative to repository root, preserving drive letters
        DATA_ROOT = Path(os.path.abspath(str(_REPO_ROOT / _data_root_str)))

DATA_AVAILABLE = DATA_ROOT.is_dir()


def get_data_path(rel_path: str) -> str:
    """Return absolute path for a test data file relative to DATA_ROOT.

    Prints a warning if the target file does not exist. Returns the absolute
    path as a string (with drive letters preserved on Windows).
    """
    path_str = os.path.abspath(os.path.join(str(DATA_ROOT), rel_path))
    p = Path(path_str)
    if not p.exists():
        print(f"WARNING: File {p} does not exist")
    return path_str


def _expand_settings_paths(d: Dict[str, Any]) -> Dict[str, Any]:
    """Return a copy of settings where known file entries are converted to absolute paths.

    Rules:
    - For any key ending with '_filename' whose value is a string, treat as a
      path relative to DATA_ROOT and convert to absolute using get_data_path.
    - For key 'test_files' if present and is a list of [path, type], convert the
      paths to absolute using get_data_path.
    - Leave other values untouched.
    """
    out = dict(d)
    # Convert *_filename string values
    for k, v in list(out.items()):
        if isinstance(v, str) and k.endswith("_filename"):
            out[k] = get_data_path(v)
    # Convert test_files entries
    if isinstance(out.get("test_files"), list):
        new_list: List[Tuple[str, Any]] = []
        for item in out["test_files"]:
            try:
                rel, ftype = item
            except (TypeError, ValueError):
                new_list.append(item)
                continue
            new_list.append([get_data_path(rel), ftype])
        out["test_files"] = new_list
    return out


# Public: settings dict with expanded absolute paths for convenience
settings: Dict[str, Any] = _expand_settings_paths(_RAW_SETTINGS)

# Print a single status line so users see what is used during test discovery/execution
_status = "[OK]" if DATA_AVAILABLE else "[NOT FOUND]"
print(f"{_status} Test data root: {DATA_ROOT}")
