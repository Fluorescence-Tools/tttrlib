# SPDX-License-Identifier: BSD-3-Clause
"""The Python runner of the cross-language conformance suite.

One parametrised test per case in ``test/conformance/cases/``. Python is where
the expected values were generated, but it is not the oracle: this file asserts
against the committed JSON exactly like the R, Java and JavaScript runners, so a
Python-side regression fails here too.

A failure means the binding is wrong, not that the number needs updating. See
``test/conformance/README.md`` before reaching for ``tools/conformance_update.py``.
"""
from __future__ import annotations

import json
import os
import shutil
import sys
import tempfile

import pytest

_HERE = os.path.dirname(os.path.abspath(__file__))
_CONFORMANCE = os.path.join(os.path.dirname(_HERE), "conformance")
sys.path.insert(0, os.path.join(_CONFORMANCE, "py"))

import cases as C  # noqa: E402
from interpreter import Interpreter, UnsupportedOp  # noqa: E402

from test_settings import DATA_ROOT  # type: ignore  # noqa: E402

ALL_CASES = C.load_all()

# Filled as the tests run and written out at the end when the environment asks
# for it; tools/conformance_matrix.py merges the four languages' reports.
_REPORT = []


def _record(case_id, area, status, reason=""):
    _REPORT.append({"id": case_id, "area": area, "status": status, "reason": reason})


@pytest.fixture(scope="session", autouse=True)
def _write_report_at_the_end():
    """Emit the run's report for tools/conformance_matrix.py.

    A session fixture rather than a `pytest_sessionfinish` hook: pytest reads
    hooks from conftest files and plugins, not from test modules, so the hook
    spelling silently never runs.
    """
    yield
    path = os.environ.get("TTTRLIB_CONFORMANCE_REPORT")
    if not path:
        return
    with open(path, "w", encoding="utf-8") as fh:
        json.dump({"language": "python", "results": _REPORT}, fh, indent=2)


def test_case_files_match_the_schema():
    """A malformed case should fail here, once, not four times in four runners."""
    C.validate_all()


def test_every_case_id_is_unique():
    ids = [c["id"] for _, c in ALL_CASES]
    assert len(ids) == len(set(ids))


def test_every_op_a_case_names_exists():
    """A typo'd op would otherwise read as 'unsupported' in all four runners.

    That is the worst failure this suite can have: the case still appears in the
    matrix, four columns say the binding cannot do it, and nothing is actually
    being tested. Catching it here makes it a hard error in one place.
    """
    import interpreter
    unknown = sorted({step["op"] for _, c in ALL_CASES for step in c["steps"]}
                     - set(interpreter._OPS))
    assert not unknown, f"cases name ops that do not exist: {unknown}"


def test_every_op_is_documented():
    """OPS.md is the contract the other three dispatch tables are written from."""
    import interpreter
    with open(os.path.join(_CONFORMANCE, "OPS.md"), encoding="utf-8") as fh:
        ops_md = fh.read()
    undocumented = sorted(op for op in interpreter._OPS if f"`{op}`" not in ops_md)
    assert not undocumented, (
        f"implemented but not in OPS.md, so no other runner knows to port them: "
        f"{undocumented}")


def test_every_expectation_is_bound_by_a_step():
    """An expectation on a name no step produces would never be checked."""
    problems = []
    for _, case in ALL_CASES:
        bound = {s["as"] for s in case["steps"] if "as" in s}
        for key in case["expect"]:
            if key not in bound:
                problems.append(f"{case['id']}: expects '{key}', which no step binds")
        if not case["expect"]:
            problems.append(f"{case['id']}: has no expectations, so it asserts nothing")
    assert not problems, "\n".join(problems)


def test_no_expectation_needs_more_than_a_double():
    for _, case in ALL_CASES:
        C.check_expectations_are_comparable(case)


@pytest.mark.parametrize("area,case", ALL_CASES, ids=[c["id"] for _, c in ALL_CASES])
def test_conformance(area, case):
    why = case.get("unsupported", {}).get("python")
    if why:
        _record(case["id"], area, "unsupported", why)
        pytest.skip(f"declared unsupported in Python: {why}")

    missing = [d for d in case.get("data", [])
               if not os.path.exists(os.path.join(str(DATA_ROOT), d))]
    if missing:
        _record(case["id"], area, "skip", f"missing {', '.join(missing)}")
        pytest.skip(f"test data not present: {', '.join(missing)}")

    # An expectation no double-backed runner could check is a bug in the case,
    # and it is worth catching in every runner rather than only in the generator.
    C.check_expectations_are_comparable(case)

    tmpdir = tempfile.mkdtemp(prefix="tttrlib-conf-")
    try:
        tmp_paths = [os.path.join(tmpdir, f"scratch{i}")
                     for i in range(case.get("tmp", 0))]
        interp = Interpreter(str(DATA_ROOT), tmp_paths)
        try:
            bindings = interp.run(case)
        except UnsupportedOp as e:
            _record(case["id"], area, "unsupported", f"op '{e}' not implemented")
            pytest.skip(f"op '{e}' is not implemented in the Python runner")

        tolerance = case.get("tolerance", {})
        failures = []
        for key, expected in case["expect"].items():
            if key not in bindings:
                failures.append(f"{key}: the steps never bound it")
                continue
            actual = _comparable(bindings[key])
            ok, why = C.compare(expected, actual, tolerance.get(key, 0))
            if not ok:
                failures.append(f"{key}: {why}")

        if failures:
            _record(case["id"], area, "fail", "; ".join(failures))
            pytest.fail(f"{case['id']}\n  " + "\n  ".join(failures))
        _record(case["id"], area, "pass")
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


def _comparable(value):
    """Collapse NumPy scalars so the comparison sees plain Python numbers."""
    import numpy as np
    if isinstance(value, np.generic):
        return value.item()
    if isinstance(value, np.ndarray):
        return [_comparable(v) for v in value]
    if isinstance(value, (list, tuple)):
        return [_comparable(v) for v in value]
    return value
