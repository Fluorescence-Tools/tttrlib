"""
Test selection for tttrlib.

Two orthogonal axes, and you almost always want both:

* **what it covers** -- the directory a test lives in becomes a marker, and
  ``--modules`` maps a tttrlib module onto the directories that exercise it.
* **what it costs** -- ``slow`` and ``heavy`` markers, and ``--lane`` to pick
  how much of that you are willing to pay for.

This file lives at test/ rather than test/python/ deliberately: pytest reads
`pytest_addoption` only from *initial* conftest files -- the rootdir's and those
on the path of the arguments given. Running `pytest test/` never makes
test/python/conftest.py initial, so `--modules` defined there is rejected as an
unrecognised argument.
"""
from __future__ import annotations

import collections
import re
from pathlib import Path, PurePosixPath

import pytest


# ---------------------------------------------------------------------------
# Running only the tests that are relevant
#
# The suite is over half an hour end to end, which is far too slow to run after
# every change to one subsystem. Every test file already lives in a directory
# named after the subsystem it exercises, so that directory name is turned into
# a marker automatically -- no decorators to add, and no list to keep in step
# with the files.
#
#     pytest test/ -m clsm                # just the imaging tests
#     pytest test/ --modules sim,pda      # by tttrlib module, incl. dependents
#     pytest test/ --lane fast            # ~2 min, every test file represented
#
# --modules takes MODULE names (as declared in modules/) rather than directory
# names, and expands each to the directories that exercise it. A module's tests
# are not only its own: changing `core` can break anything downstream of it, so
# asking for core runs the lot. The mapping is deliberately generous -- a test
# run that is slightly too wide costs seconds, one that is too narrow costs a
# regression.
# ---------------------------------------------------------------------------

#: Directories whose tests are not tied to one module.
CROSS_CUTTING = ["misc"]

#: Arguments tttrlib_add_module() understands; anything else in the block is a
#: value belonging to the keyword before it.
_MODULE_KEYWORDS = {"NAME", "SOURCES", "HEADERS", "DEPENDS", "EXTERNAL_DEPS",
                    "SWIG_INTERFACES", "TEST_DIR", "INTERFACE", "OPTIONAL"}

_REPO = Path(__file__).resolve().parent.parent


def _parse_modules() -> dict[str, tuple[list[str], str | None]]:
    """name -> (DEPENDS, TEST_DIR), read straight out of the module CMakeLists.

    This used to be a dict maintained by hand next to a note asking for it to be
    kept in step with `modules/`. It was not: it had grown entries for five
    modules that no longer exist (hist, opt, superres, localization, nn) while
    missing the twelve io_* modules entirely, so `--modules io_pq` -- a real
    module, with real tests -- was rejected as unknown. Reading the declarations
    is the only version of this that cannot drift.
    """
    mods = {}
    for cml in sorted(_REPO.glob("modules/**/CMakeLists.txt")):
        text = re.sub(r"#[^\n]*", "", cml.read_text())      # strip comments
        block = re.search(r"tttrlib_add_module\s*\((.*?)\n\s*\)", text, re.S)
        if not block:
            continue
        fields: dict[str, list[str]] = collections.defaultdict(list)
        current = None
        for tok in block.group(1).split():
            if tok in _MODULE_KEYWORDS:
                current = tok
            elif current:
                fields[current].append(tok.strip('"'))
        if fields["NAME"]:
            mods[fields["NAME"][0]] = (
                fields["DEPENDS"],
                fields["TEST_DIR"][0] if fields["TEST_DIR"] else None,
            )
    return mods


def _group_of_dir(test_dir: str) -> str:
    parts = PurePosixPath(test_dir).parts        # test/python/<group>
    return parts[2] if len(parts) > 2 else "toplevel"


def module_test_groups() -> dict[str, set[str]]:
    """module -> the test groups that exercise it, directly or downstream.

    A module's tests are not only its own: changing `core` can break anything
    built on top of it, so asking for core runs everything that depends on core.
    The expansion is deliberately generous -- a run that is slightly too wide
    costs seconds, one that is too narrow costs a regression.
    """
    mods = _parse_modules()
    dependents = {name: {name} for name in mods}
    for _ in range(len(mods)):                   # transitive closure
        for name, (deps, _td) in mods.items():
            for d in deps:
                if d in dependents:
                    dependents[d] |= dependents[name]

    out = {}
    for name in mods:
        groups = {_group_of_dir(mods[d][1]) for d in dependents[name]
                  if mods[d][1]}
        out[name] = groups
    return out


# ---------------------------------------------------------------------------
# Workload tiers
#
# Cost is wildly unevenly distributed: measured over the whole suite, six tests
# in one file (clsm/test_ism_psf_model.py) are 47% of the runtime, and the top
# 39 tests are 85% of it. Tiering by measured wall time is what makes a fast
# lane possible at all.
#
#   (unmarked)  < 1s     the fast lane
#   slow        1-5s
#   heavy       > 5s
#   smoke       overrides the above -- always in the fast lane
#
# `smoke` exists because cost and coverage are not aligned. Seven files have no
# test under a second at all (they open a large image file in a fixture, and
# every test in the file pays for it), so a purely cost-based fast lane would
# drop them entirely -- including test_clsm.py, the core CLSM file. One test in
# each of those files carries `smoke` so the lane still touches every file.
#
# The tier lives on the test rather than in a generated ledger so that it moves
# with the function when it is renamed, and shows up in the diff when someone
# makes a test 30x more expensive. The audit below is what keeps it honest.
# ---------------------------------------------------------------------------

#: Above this a test must carry `slow`; above HEAVY_BUDGET_S it must carry
#: `heavy`. Measured as setup + call + teardown.
FAST_BUDGET_S = 1.0
HEAVY_BUDGET_S = 5.0

#: The audit only complains once a test is this far the wrong side of a
#: boundary. Timings move with the machine, and an audit that cries wolf on a
#: loaded laptop is an audit everyone learns to ignore.
AUDIT_SLACK = 2.0

#: lane -> does this item belong in it. `smoke` wins over the cost tier.
LANES = {
    "fast":     lambda tier, smoke: smoke or tier is None,
    "standard": lambda tier, smoke: smoke or tier != "heavy",
    "full":     lambda tier, smoke: True,
}

#: nodeid -> (tier, is_smoke), filled at collection so the audit can read it
#: back without touching the items again.
_TIERS: dict[str, tuple[str | None, bool]] = {}

#: nodeid -> measured seconds, summed over setup/call/teardown.
_MEASURED: collections.defaultdict[str, float] = collections.defaultdict(float)


def pytest_addoption(parser):
    parser.addoption(
        "--modules",
        action="store",
        default=None,
        help="Comma-separated tttrlib module names; runs only the test groups "
             "that exercise them (plus cross-cutting tests). "
             "Example: --modules sim,pda",
    )
    parser.addoption(
        "--lane",
        action="store",
        default="full",
        choices=sorted(LANES),
        help="How much runtime to spend. fast: drop `slow` and `heavy` but keep "
             "every file represented. standard: drop `heavy` only. "
             "full (default): everything.",
    )
    parser.addoption(
        "--strict-workload",
        action="store_true",
        default=False,
        help="Fail the run if a test's measured cost does not match its "
             "workload marker.",
    )


def _group_of(item) -> str:
    """The test's group: the directory under test/python/ that holds it."""
    parts = Path(str(item.fspath)).parts
    if "python" in parts:
        i = parts.index("python")
        if i + 2 < len(parts):        # .../python/<group>/test_x.py
            return parts[i + 1]
    return "toplevel"


def _tier_of(item) -> str | None:
    """The cost tier a test declares, or None for the fast majority."""
    for name in ("heavy", "slow"):
        if item.get_closest_marker(name) is not None:
            return name
    return None


def pytest_collection_modifyitems(config, items):
    # 1. Every test gets a marker named after its group, so `-m clsm` works.
    for item in items:
        item.add_marker(getattr(pytest.mark, _group_of(item)))
        _TIERS[item.nodeid] = (_tier_of(item),
                               item.get_closest_marker("smoke") is not None)

    all_files = {str(item.fspath) for item in items}

    # 2. --modules narrows the selection to the groups those modules touch.
    requested = config.getoption("--modules")
    if requested:
        groups_of = module_test_groups()
        if not groups_of:
            raise pytest.UsageError(
                "--modules needs the source tree: no modules/*/CMakeLists.txt "
                f"under {_REPO}"
            )
        names = [m.strip() for m in requested.split(",") if m.strip()]
        unknown = [m for m in names if m not in groups_of]
        if unknown:
            raise pytest.UsageError(
                f"unknown module(s): {', '.join(unknown)}. "
                f"Known: {', '.join(sorted(groups_of))}"
            )

        wanted = set(CROSS_CUTTING) | {"toplevel"}
        for m in names:
            wanted.update(groups_of[m])

        # Groups no module claims via TEST_DIR (bva, twocde, correlator today)
        # would otherwise be unreachable from --modules entirely. Run them
        # rather than never: too wide costs seconds, too narrow costs a
        # regression.
        claimed = {g for gs in groups_of.values() for g in gs}
        unclaimed = {_group_of(i) for i in items} - claimed
        if unclaimed:
            wanted |= unclaimed
            print(f"\n[modules] no module declares a TEST_DIR for "
                  f"{', '.join(sorted(unclaimed))} — running them anyway")

        keep, drop = [], []
        for item in items:
            (keep if _group_of(item) in wanted else drop).append(item)
        if drop:
            # Deselect rather than skip: a skipped test is reported as a test
            # that did not run, which makes a narrowed run look like it has 900
            # problems.
            config.hook.pytest_deselected(items=drop)
            items[:] = keep
        print(f"\n[modules] {', '.join(names)} -> groups: "
              f"{', '.join(sorted(wanted))} "
              f"({len(keep)} selected, {len(drop)} deselected)")

    # 3. --lane drops what the chosen budget will not pay for.
    lane = config.getoption("--lane")
    if lane == "full":
        return
    belongs = LANES[lane]

    keep, drop = [], []
    for item in items:
        tier, smoke = _TIERS[item.nodeid]
        (keep if belongs(tier, smoke) else drop).append(item)
    if drop:
        config.hook.pytest_deselected(items=drop)
        items[:] = keep

    # A lane that silently stops covering a file is the failure mode worth
    # shouting about -- that is a subsystem nobody is testing any more, which
    # looks exactly like a subsystem with no bugs.
    dark = sorted(all_files - {str(item.fspath) for item in keep})
    print(f"\n[lane] {lane}: {len(keep)} selected, {len(drop)} deselected")
    if dark:
        print(f"[lane] {len(dark)} file(s) have no test in this lane; "
              f"mark one of their cheapest tests `smoke`:")
        for f in dark:
            print(f"         {f}")


def pytest_runtest_logreport(report):
    _MEASURED[report.nodeid] += report.duration


def _audit() -> list[str]:
    """Tests whose measured cost contradicts the tier they declare.

    Only tests that actually ran are considered, so any lane audits itself --
    the fast lane catches a test that has crept over a second just as well as a
    full run catches one that has grown past five.
    """
    findings = []
    for nodeid, seconds in _MEASURED.items():
        tier, _ = _TIERS.get(nodeid, (None, False))
        if seconds > HEAVY_BUDGET_S * AUDIT_SLACK and tier != "heavy":
            findings.append(f"{seconds:7.1f}s  mark `heavy`  {nodeid}")
        elif (seconds > FAST_BUDGET_S * AUDIT_SLACK
              and tier is None):
            findings.append(f"{seconds:7.1f}s  mark `slow`   {nodeid}")
        elif tier == "heavy" and seconds < HEAVY_BUDGET_S / AUDIT_SLACK:
            findings.append(f"{seconds:7.1f}s  drop `heavy`  {nodeid}")
        elif tier == "slow" and seconds < FAST_BUDGET_S / AUDIT_SLACK:
            findings.append(f"{seconds:7.1f}s  drop `slow`   {nodeid}")
    return sorted(findings, reverse=True)


def pytest_terminal_summary(terminalreporter, exitstatus, config):
    findings = _audit()
    if not findings:
        return
    w = terminalreporter
    w.write_sep("=", "workload markers out of date", yellow=True)
    for line in findings:
        w.write_line(line)
    w.write_line("")
    w.write_line(f"budgets: fast < {FAST_BUDGET_S}s, slow < {HEAVY_BUDGET_S}s, "
                 f"heavy above; reported at {AUDIT_SLACK}x the boundary.")
    if config.getoption("--strict-workload"):
        w.write_line("--strict-workload: failing the run.", red=True)


# tryfirst: set the status before anything else in sessionfinish reads it
@pytest.hookimpl(tryfirst=True)
def pytest_sessionfinish(session, exitstatus):
    if session.config.getoption("--strict-workload") and _audit():
        session.exitstatus = 1
