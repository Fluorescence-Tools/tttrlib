"""
Test selection for tttrlib.

This file lives at test/ rather than test/python/ deliberately: pytest reads
`pytest_addoption` only from *initial* conftest files -- the rootdir's and those
on the path of the arguments given. Running `pytest test/` never makes
test/python/conftest.py initial, so `--modules` defined there is rejected as an
unrecognised argument.
"""
from __future__ import annotations

from pathlib import Path

import pytest


# ---------------------------------------------------------------------------
# Running only the tests that are relevant
#
# The suite is ~11 minutes end to end, which is far too slow to run after every
# change to one subsystem. Every test file already lives in a directory named
# after the subsystem it exercises, so that directory name is turned into a
# marker automatically -- no decorators to add, and no list to keep in step with
# the files.
#
#     pytest test/ -m sim                 # just the simulator
#     pytest test/ --modules sim,pda      # by tttrlib module, incl. dependents
#     pytest test/ -m "not slow"          # skip the long ones
#
# --modules takes MODULE names (as declared in modules/) rather than directory
# names, and expands each to the directories that exercise it. A module's tests
# are not only its own: changing `core` can break anything downstream of it, so
# asking for core runs the lot. The mapping is deliberately generous -- a test
# run that is slightly too wide costs seconds, one that is too narrow costs a
# regression.
# ---------------------------------------------------------------------------

#: tttrlib module -> test directories that exercise it, directly or through a
#: dependency. Keep in step with modules/*/CMakeLists.txt TEST_DIR.
MODULE_TEST_GROUPS = {
    "core":         ["tttr", "misc", "burstfilter", "bva", "twocde", "clsm",
                     "correlator", "decayfit", "pda", "hmm"],
    "hist":         ["tttr", "misc"],
    "opt":          ["decayfit"],
    "imageio":      ["clsm"],
    "sim":          ["simulation", "hmm"],
    "pda":          ["pda"],
    "superres":     ["clsm"],
    "localization": ["clsm"],
    "burst":        ["burstfilter", "bva", "twocde", "tttr"],
    "decay":        ["decayfit", "hmm"],
    "imaging":      ["clsm", "correlator"],
    "nn":           ["hmm"],
    "hmm":          ["hmm"],
}

#: Directories whose tests are not tied to one module.
CROSS_CUTTING = ["misc"]


def pytest_addoption(parser):
    parser.addoption(
        "--modules",
        action="store",
        default=None,
        help="Comma-separated tttrlib module names; runs only the test groups "
             "that exercise them (plus cross-cutting tests). "
             "Example: --modules sim,pda",
    )


def _group_of(item) -> str:
    """The test's group: the directory under test/python/ that holds it."""
    parts = Path(str(item.fspath)).parts
    if "python" in parts:
        i = parts.index("python")
        if i + 2 < len(parts):        # .../python/<group>/test_x.py
            return parts[i + 1]
    return "toplevel"


def pytest_collection_modifyitems(config, items):
    # 1. Every test gets a marker named after its group, so `-m clsm` works.
    for item in items:
        item.add_marker(getattr(pytest.mark, _group_of(item)))

    # 2. --modules narrows the selection to the groups those modules touch.
    requested = config.getoption("--modules")
    if not requested:
        return

    names = [m.strip() for m in requested.split(",") if m.strip()]
    unknown = [m for m in names if m not in MODULE_TEST_GROUPS]
    if unknown:
        raise pytest.UsageError(
            f"unknown module(s): {', '.join(unknown)}. "
            f"Known: {', '.join(sorted(MODULE_TEST_GROUPS))}"
        )

    wanted = set(CROSS_CUTTING) | {"toplevel"}
    for m in names:
        wanted.update(MODULE_TEST_GROUPS[m])

    keep, drop = [], []
    for item in items:
        (keep if _group_of(item) in wanted else drop).append(item)
    if drop:
        # Deselect rather than skip: a skipped test is reported as a test that
        # did not run, which makes a narrowed run look like it has 900 problems.
        config.hook.pytest_deselected(items=drop)
        items[:] = keep
    print(f"\n[modules] {', '.join(names)} -> groups: {', '.join(sorted(wanted))} "
          f"({len(keep)} selected, {len(drop)} deselected)")
