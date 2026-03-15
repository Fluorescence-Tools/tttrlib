"""Utility helpers for locating example data files.

This mirrors the logic used in `test/test_settings.py` so that gallery examples
resolve data files in the same way as the unit tests:

1. Honour the ``TTTRLIB_DATA`` environment variable when present.
2. Fall back to ``tttr-data`` inside the repository.
3. As a last resort, look in the lightweight test reference data folder.
"""
from __future__ import annotations

import os
import tempfile
from pathlib import Path

# Directories -----------------------------------------------------------------
_EXAMPLES_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _EXAMPLES_DIR.parent

# Candidate data roots in priority order
_DATA_CANDIDATES = (
    lambda: os.getenv("TTTRLIB_DATA"),
    lambda: _REPO_ROOT / "tttr-data",
    lambda: _REPO_ROOT / "test" / "data" / "reference",
)


def _normalize(path: Path | str) -> Path:
    """Return a resolved, expanded absolute path preserving drive letters."""
    return Path(str(path).strip()).expanduser().absolute()


def get_data_root() -> Path:
    """Return the first existing data directory, or the examples directory."""
    for candidate in _DATA_CANDIDATES:
        value = candidate()
        if not value:
            continue
        try:
            path = _normalize(value)
        except OSError:
            continue
        if path.is_dir():
            return path
    return _EXAMPLES_DIR


def get_data_path(relative_path: str) -> Path:
    """Return an absolute path by joining the resolved data root with *relative_path*.

    Raises
    ------
    FileNotFoundError
        If the resolved file does not exist. The error message includes guidance on
        configuring the ``TTTRLIB_DATA`` environment variable so documentation builds
        can access the required data files.
    """
    root = get_data_root()
    path = (root / relative_path).absolute()
    if not path.exists():
        raise FileNotFoundError(
            f"Example data not found at '{path}'. Set the TTTRLIB_DATA environment "
            "variable to point to the TTTR data directory or install the repository "
            "data bundle."
        )
    return path
def get_output_path(filename: str) -> Path:
    """Return a writable path for generated files.

    Files are written into a dedicated directory inside the system temporary
    location so documentation builds do not leave artefacts in the repository.
    """
    target = Path(tempfile.gettempdir()) / "tttrlib_examples"
    target.mkdir(parents=True, exist_ok=True)
    return target / filename
